/* SPDX-License-Identifier: GPL-3.0-or-later
 * Write and read back real EFI payloads through hosted Common V2 on exFAT.
 */
#define _POSIX_C_SOURCE 200809L
#include <infiltratr/filesystem_module_v2.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define MUST(x) do {if(!(x)){fprintf(stderr,"V2 exFAT writer:%d %s\n",__LINE__,#x);return 1;}}while(0)
typedef struct {int fd;uint64_t bytes;unsigned reads,writes;} Media;
static int read_disk(void *p,uint64_t at,void *dst,uint32_t n) {
  Media *m=p;if(!dst||!n||at%512||n%512||at>m->bytes||n>m->bytes-at||at>INT64_MAX)return -1;
  ++m->reads;return pread(m->fd,dst,n,(off_t)at)==(ssize_t)n?0:-1;
}
static int write_disk(void *p,uint64_t at,const void *src,uint32_t n) {
  Media *m=p;if(!src||!n||at%512||n%512||at>m->bytes||n>m->bytes-at||at>INT64_MAX)return -1;
  ++m->writes;return pwrite(m->fd,src,n,(off_t)at)==(ssize_t)n?0:-1;
}
static int flush_disk(void *p){return fsync(((Media *)p)->fd);}
static void *alloc(void *p,size_t n){(void)p;return malloc(n);}
static void dealloc(void *p,void *b){(void)p;free(b);}
static int copy_and_verify(const IfsFsCoreOpsV2 *c,void *v,const char *name,const char *source){
  FILE *f=fopen(source,"rb");if(!f)return -1;
  struct stat s;if(fstat(fileno(f),&s)||!S_ISREG(s.st_mode)||s.st_size<=0||
     c->create(v,name,IFS_FS_ENTRY_FILE)!=IFS_FS_OK){fclose(f);return -1;}
  unsigned char buf[65536],check[65536];uint64_t pos=0;
  while(pos<(uint64_t)s.st_size){
    uint32_t n=(uint64_t)s.st_size-pos<sizeof(buf)?
      (uint32_t)((uint64_t)s.st_size-pos):(uint32_t)sizeof(buf);
    if(fread(buf,1,n,f)!=n||c->write(v,name,pos,buf,n)!=IFS_FS_OK||
       c->read(v,name,pos,check,n)!=IFS_FS_OK||memcmp(buf,check,n)){
      fclose(f);return -1;
    }
    pos+=n;
  }
  char label[768];IfsFsEntryV2 e={0};
  e.structure_bytes=sizeof(e);e.name=label;e.name_capacity=sizeof(label);
  int ok=c->stat(v,name,&e)==IFS_FS_OK && e.size_bytes==(uint64_t)s.st_size;
  if(fclose(f))return -1;return ok?0:-1;
}
int main(int argc,char **argv) {
  MUST(argc==5);
  void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);MUST(lib);
  void *symbol=dlsym(lib,IFS_FS_COMMON_EXPORT_V2);
  IfsFsModuleApiEntryV2 fn=NULL;MUST(symbol&&sizeof(fn)==sizeof(symbol));
  memcpy(&fn,&symbol,sizeof(fn));
  const IfsFsModuleApiV2 *api=fn();MUST(ifs_fs_api_v2_valid(api,"exfat"));
  const IfsFsCoreOpsV2 *c=(const IfsFsCoreOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
  const IfsFsVolumeOpsV2 *ops=(const IfsFsVolumeOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_VOLUME);
  MUST(c&&c->probe&&c->mount&&c->unmount&&c->create&&c->write&&c->read&&c->stat&&ops&&ops->sync);
  Media m={0};m.fd=open(argv[2],O_RDWR|O_CLOEXEC|O_NOFOLLOW);MUST(m.fd>=0);
  struct stat st;MUST(!fstat(m.fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=128*1024*1024);
  m.bytes=(uint64_t)st.st_size;
  IfsFsHostRuntimeV2 h={0};h.abi_version=IFS_FS_HOST_ABI_V2;h.structure_bytes=sizeof(h);
  h.capabilities=IFS_FS_HOST_EXCLUSIVE|IFS_FS_HOST_DURABLE;h.volume_bytes=m.bytes;
  h.logical_sector_bytes=h.physical_sector_bytes=512;
  h.maximum_transfer_bytes=65536;h.context=&m;h.read_at=read_disk;h.write_at=write_disk;
  h.flush=flush_disk;h.allocate=alloc;h.release=dealloc;
  MUST(ifs_fs_host_v2_valid(&h));
  uint32_t variant=0;MUST(c->probe(&h,&variant)==IFS_FS_OK&&variant==64);
  IfsFsVolumeInfoV2 info={0};info.structure_bytes=sizeof(info);void *vol=NULL;
  MUST(c->mount(&h,&vol,&info)==IFS_FS_OK&&vol);
  MUST(c->create(vol,"/System",IFS_FS_ENTRY_DIRECTORY)==IFS_FS_OK);
  MUST(c->create(vol,"/System/Kernel",IFS_FS_ENTRY_DIRECTORY)==IFS_FS_OK);
  MUST(c->create(vol,"/System/Kernel/Images",IFS_FS_ENTRY_DIRECTORY)==IFS_FS_OK);
  MUST(copy_and_verify(c,vol,"/System/Kernel/Images/native.img",argv[3])==0);
  MUST(copy_and_verify(c,vol,"/System/Kernel/Images/native.initrd",argv[4])==0);
  MUST(ops->sync(vol)==IFS_FS_OK && c->unmount(vol)==IFS_FS_OK);
  MUST(m.reads>0&&m.writes>0&&close(m.fd)==0&&dlclose(lib)==0);
  puts("EXFAT-COMMON-V2-KERNEL-INITRD-WRITE-READ-PASS");
  return 0;
}
