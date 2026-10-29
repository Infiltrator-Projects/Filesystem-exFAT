/* SPDX-License-Identifier: GPL-3.0-or-later
 * Test only disposable regular files. This is NOT a physical-disk test.
 */
#define _POSIX_C_SOURCE 200809L
#include "exfat_api.h"
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {int fd;uint64_t size;unsigned writes,flushes;} Image;
static int read_at(void *ctx,uint64_t pos,void *buf,uint32_t n) {
    Image*d=ctx;
    if(pos>d->size||n>d->size-pos||pos>INT64_MAX)return -1;
    return pread(d->fd,buf,n,(off_t)pos)==(ssize_t)n?0:-1;
}
static int write_at(void *ctx,uint64_t pos,const void *buf,uint32_t n) {
    Image*d=ctx;
    if(pos>d->size||n>d->size-pos||pos>INT64_MAX)return -1;
    if(pwrite(d->fd,buf,n,(off_t)pos)!=(ssize_t)n)return -1;
    ++d->writes;return 0;
}
static int flush_at(void *ctx) {
    Image*d=ctx;
    if(fsync(d->fd)!=0)return -1;
    ++d->flushes;return 0;
}
static uint32_t timestamp(void *ctx) {
    (void)ctx;
    return ((uint32_t)(2026-1980)<<25)|(10u<<21)|(29u<<16)|(12u<<11);
}
static int fail(const char*s){fprintf(stderr,"EXFAT-SO-FAILED: %s\n",s);return 1;}
static int open_image(Image*d,const char*path) {
    struct stat st;
    memset(d,0,sizeof(*d));
    d->fd=open(path,O_RDWR|O_CLOEXEC|O_NOFOLLOW);
    if(d->fd<0)return -1;
    if(fstat(d->fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<=0){
        close(d->fd);return -1;
    }
    d->size=(uint64_t)st.st_size;return 0;
}
int main(int argc,char**argv) {
    void *library;
    const IfsFatfsModuleV3 *(*resolve3)(void)=NULL;
    const IfsFatfsModuleV4 *(*resolve4)(void)=NULL;
    const IfsExfatModuleV5 *(*resolve5)(void)=NULL;
    const IfsFatfsModuleV3 *api;
    const IfsFatfsModuleV4 *v4;
    const IfsExfatModuleV5 *v5;
    IfsExfatEntry64 meta64;
    IfsFatfsIo io;
    IfsFatfsEntry meta;
    Image d;
    unsigned char data[8192],out[8192];
    if(argc!=4)return fail("arguments: exfat.so exfat-image fat32-image");
    library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!library)return fail(dlerror());
    *(void **)(&resolve3)=dlsym(library,"infiltrator_filesystem_module_v3");
    *(void **)(&resolve4)=dlsym(library,"infiltrator_filesystem_module_v4");
    *(void **)(&resolve5)=dlsym(library,"infiltrator_filesystem_module_v5");
    api=resolve3?resolve3():NULL;
    v4=resolve4?resolve4():NULL;
    v5=resolve5?resolve5():NULL;
    if(!api||!v4||!v5||api->abi_version!=3u||v4->abi_version!=4u||
       v5->abi_version!=5u||v5->structure_bytes!=sizeof(*v5)||
       v5->compat_v4!=v4||strcmp(v5->format_id,"exfat")||
       !v5->read64||!v5->write64||!v5->truncate64||!v5->stat64||
       api->structure_bytes!=sizeof(*api)||v4->structure_bytes!=sizeof(*v4)||
       strcmp(api->format_id,"exfat")||strcmp(v4->format_id,"exfat")||
       v4->v3!=api||!api->create||!api->write||!api->read||!api->truncate||
       !api->rename||!api->mkdir||!api->unlink||!api->directory_entry||
       !v4->format||!v4->space)return fail("incorrect exFAT V3/V4 ABI");
    if(open_image(&d,argv[3]))return fail("opening foreign FAT32 image");
    io=(IfsFatfsIo){&d,d.size,512u,read_at,write_at,flush_at,timestamp};
    if(api->mount(&io)==0||d.writes!=0)return fail("accepted/modified FAT32 volume");
    if(close(d.fd))return fail("close FAT32 image");
    if(open_image(&d,argv[2]))return fail("opening exFAT image");
    io.user=&d;io.partition_bytes=d.size;
    if(api->mount(&io))return fail("cannot mount real mkfs.exfat volume");
    if(api->mkdir("0:/TESTDIR")||api->create("0:/TESTDIR/INPUT.BIN"))
        return fail("directory/file creation");
    for(unsigned i=0;i<sizeof data;++i)data[i]=(unsigned char)(i*31u+7u);
    if(v5->write64("0:/TESTDIR/INPUT.BIN",0,data,sizeof data) ||
       v5->stat64("0:/TESTDIR/INPUT.BIN",&meta64) ||
       meta64.size_bytes!=sizeof data ||
       v5->read64("0:/TESTDIR/INPUT.BIN",UINT64_MAX,out,1u)==0 ||
       api->read("0:/TESTDIR/INPUT.BIN",0,out,sizeof out) ||
       memcmp(data,out,sizeof data))return fail("multi-cluster write/readback");
    if(api->rename("0:/TESTDIR/INPUT.BIN","0:/TESTDIR/OUTPUT.BIN")||
       api->stat("0:/TESTDIR/OUTPUT.BIN",&meta)||meta.bytes!=sizeof data)
        return fail("file rename or size");
    if(api->mkdir("0:/TESTDIR/CHILD") ||
       api->rename("0:/TESTDIR","0:/TESTDIR/CHILD/LOOP")==0)
        return fail("unsafe recursive directory move");
    if(v5->truncate64("0:/TESTDIR/OUTPUT.BIN",4096u)||
       api->unmount()||!d.writes||!d.flushes)return fail("truncate/flush");
    if(api->mount(&io) ||
       api->read("0:/TESTDIR/OUTPUT.BIN",0,out,4096u) ||
       memcmp(out,data,4096u) ||
       api->stat("0:/TESTDIR/OUTPUT.BIN",&meta) || meta.bytes!=4096u ||
       v5->stat64("0:/TESTDIR/OUTPUT.BIN",&meta64) ||
       meta64.size_bytes!=4096u)
        return fail("remount persistence");
    if(api->unlink("0:/TESTDIR/OUTPUT.BIN")||
       api->unlink("0:/TESTDIR/CHILD")||
       api->unlink("0:/TESTDIR")||api->unmount())
        return fail("directory/file deletion");
    /* V4 is a real exFAT formatter, not just an exported symbol. Reformat
     * ONLY this disposable regular-file image, then remount and mutate it. */
    {
        unsigned before=d.writes;
        if(v4->format(&io,512u)!=0 || d.writes<=before ||
           api->mount(&io)!=0 || api->create("0:/FORMATTED.BIN")!=0 ||
           v5->write64("0:/FORMATTED.BIN",0u,data,sizeof data)!=0 ||
           api->unmount()!=0 || !d.flushes)
            return fail("exFAT V4 on-image format and remount");
    }
    if(close(d.fd)!=0)return fail("close exFAT after format");
    if(dlclose(library))return fail("library cleanup");
    puts("EXFAT-SO-MKFS-READ-WRITE-RENAME-TRUNCATE-REMOUNT-PASS");
    return 0;
}
