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
static uint64_t fixture_clock(void *ctx)
{ (void)ctx; return UINT64_C(1709212456000000000); }
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|
        ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
/* Read exFAT's root directory entry set directly, without using the
 * provider's metadata parser to validate its own output. */
static int check_clock_entry(Media *m)
{
    unsigned char boot[512],root[4096];
    if(pread(m->fd,boot,sizeof boot,0)!=(ssize_t)sizeof boot ||
       boot[108]<9u || boot[108]>12u || boot[109]>25u-boot[108]) return -1;
    uint32_t sector=1u<<boot[108], cluster=le32(boot+96);
    if(cluster<2u) return -1;
    uint64_t at=((uint64_t)le32(boot+88)+
        ((uint64_t)(cluster-2u)<<boot[109]))*sector;
    if(at>m->bytes || sector>m->bytes-at ||
       pread(m->fd,root,sector,(off_t)at)!=(ssize_t)sector) return -1;
    for(uint32_t i=0;i+96u<=sector;i+=32u) {
        if(!root[i]) break;
        if(root[i]!=0x85u || root[i+32u]!=0xc0u || root[i+64u]!=0xc1u)
            continue;
        const unsigned char *name=root+i+66u;
        const char expected[]="CLOCK.TXT";
        unsigned k=0;
        for(;k<sizeof(expected)-1u;++k)
            if(name[2u*k]!=(unsigned char)expected[k] || name[2u*k+1u]) break;
        if(k!=sizeof(expected)-1u || root[i+35u]!=k) continue;
        const uint32_t stamp=((2024u-1980u)<<25)|(2u<<21)|(29u<<16)|
                             (13u<<11)|(14u<<5)|8u;
        return le32(root+i+12u)==stamp?0:-1;
    }
    return -1;
}
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
  if(fclose(f))return -1;
  return ok?0:-1;
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
  h.capabilities=IFS_FS_HOST_EXCLUSIVE|IFS_FS_HOST_DURABLE|IFS_FS_HOST_CLOCK;h.volume_bytes=m.bytes;
  h.monotonic_ns=fixture_clock;h.realtime_ns=fixture_clock;
  h.logical_sector_bytes=h.physical_sector_bytes=512;
  h.maximum_transfer_bytes=65536;h.context=&m;h.read_at=read_disk;h.write_at=write_disk;
  h.flush=flush_disk;h.allocate=alloc;h.release=dealloc;
  MUST(ifs_fs_host_v2_valid(&h));
  uint32_t variant=0;MUST(c->probe(&h,&variant)==IFS_FS_OK&&variant==64);
  IfsFsVolumeInfoV2 info={0};info.structure_bytes=sizeof(info);void *vol=NULL;
  MUST(c->mount(&h,&vol,&info)==IFS_FS_OK&&vol);
  char original[64],label[64],tiny[2]={'X','X'};
  uint32_t label_bytes=UINT32_MAX;
  MUST(ops->label && ops->set_label);
  MUST(ops->label(vol,original,sizeof(original),&label_bytes)==IFS_FS_OK);
  MUST(ops->set_label(vol,"Café 2026")==IFS_FS_OK);
  MUST(ops->label(vol,label,sizeof(label),&label_bytes)==IFS_FS_OK &&
       !strcmp(label,"Café 2026") && label_bytes==strlen(label));
  MUST(ops->label(vol,tiny,sizeof(tiny),&label_bytes)==IFS_FS_NO_SPACE &&
       !label_bytes && tiny[0]=='X' && tiny[1]=='X');
  unsigned writes=m.writes;
  MUST(ops->set_label(vol,"bad:label")==IFS_FS_INVALID && m.writes==writes);
  MUST(ops->set_label(vol,"0123456789AB")==IFS_FS_INVALID);
  MUST(c->unmount(vol)==IFS_FS_OK);
  IfsFsHostRuntimeV2 ro=h;
  ro.capabilities=IFS_FS_HOST_READ_ONLY;
  ro.write_at=NULL;ro.flush=NULL;
  MUST(c->mount(&ro,&vol,&info)==IFS_FS_OK);
  MUST(ops->label(vol,label,sizeof(label),&label_bytes)==IFS_FS_OK &&
       !strcmp(label,"Café 2026"));
  MUST(ops->set_label(vol,"DENIED")==IFS_FS_ACCESS);
  MUST(c->unmount(vol)==IFS_FS_OK);
  MUST(c->mount(&h,&vol,&info)==IFS_FS_OK);
  MUST(ops->set_label(vol,original)==IFS_FS_OK);
  MUST(c->create(vol,"/CLOCK.TXT",IFS_FS_ENTRY_FILE)==IFS_FS_OK);
  MUST(c->write(vol,"/CLOCK.TXT",0u,"x",1u)==IFS_FS_OK);
  MUST(ops->sync(vol)==IFS_FS_OK && check_clock_entry(&m)==0);
  MUST(c->remove(vol,"/CLOCK.TXT")==IFS_FS_OK);
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
