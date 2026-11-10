/* SPDX-License-Identifier: GPL-3.0-or-later
 * Common V1 on the existing exFAT V3/V4/V5 engine, with true 64-bit files.
 */
#include "exfat_api.h"
#include <infiltratr/filesystem_module.h>
#include <string.h>
static struct {
    IfsFsHostIoV1 host;
    uintptr_t generation, handle;
    int mounted, writable;
} s;
static int host_ok(const IfsFsHostIoV1 *h) {
    return h && h->structure_bytes>=sizeof(*h) && h->read_at &&
           h->volume_bytes>=512u &&
           !(h->flags & ~(IFS_FS_IO_READ_ONLY|IFS_FS_IO_EXCLUSIVE));
}
static int live(void *v){return s.mounted && s.handle && (uintptr_t)v==s.handle;}
static int raw_read(void *p,uint64_t at,void *b,uint32_t n) {
    const IfsFsHostIoV1 *h=p;return h->read_at(h->context,at,b,n);
}
static int raw_write(void *p,uint64_t at,const void *b,uint32_t n){
    const IfsFsHostIoV1 *h=p;
    if((h->flags&IFS_FS_IO_READ_ONLY)||!(h->flags&IFS_FS_IO_EXCLUSIVE)||
       !h->write_at)return -1;
    return h->write_at(h->context,at,b,n);
}
static int raw_flush(void *p){
    const IfsFsHostIoV1 *h=p;
    if((h->flags&IFS_FS_IO_READ_ONLY)||!(h->flags&IFS_FS_IO_EXCLUSIVE))return 0;
    return h->flush?h->flush(h->context):-1;
}
static uint32_t fat_time(void *p){(void)p;return (1u<<21)|(1u<<16);}
static int geometry(const IfsFsHostIoV1 *h,uint32_t *sector,uint32_t *cluster){
    uint8_t b[512];if(!host_ok(h))return IFS_FS_INVALID;
    if(h->read_at(h->context,0,b,sizeof(b)))return IFS_FS_IO_ERROR;
    if(memcmp(b+3,"EXFAT   ",8)||b[108]<9u||b[108]>12u||b[109]>25u||
       ((uint64_t)1u<<(b[108]+b[109]))>UINT32_MAX)return IFS_FS_NOT_FOUND;
    *sector=1u<<b[108];*cluster=*sector*(1u<<b[109]);
    return IFS_FS_OK;
}
static IfsFatfsIo native(const IfsFsHostIoV1 *h,uint32_t sector){
    IfsFatfsIo io={0};io.user=(void *)h;io.partition_bytes=h->volume_bytes;
    io.sector_bytes=sector;io.read_at=raw_read;io.write_at=raw_write;
    io.flush=raw_flush;io.fat_timestamp=fat_time;return io;
}
static int probe(const IfsFsHostIoV1 *h,uint32_t *variant){
    uint32_t sector,cluster;int r;
    if(!host_ok(h)||!variant)return IFS_FS_INVALID;
    if(s.mounted)return IFS_FS_BUSY;
    r=geometry(h,&sector,&cluster);if(r)return r;
    IfsFatfsIo io=native(h,sector);
    const IfsFatfsModuleV3 *m=infiltrator_filesystem_module_v3();
    r=m->mount(&io);if(r)return IFS_FS_CORRUPT;
    r=m->unmount();if(r)return IFS_FS_IO_ERROR;
    *variant=64u;return IFS_FS_OK;
}
static int mount(const IfsFsHostIoV1 *h,void **v,IfsFsVolumeInfoV1 *info){
    uint32_t sector,cluster;uint64_t total=0,free_bytes=0;int r;
    if(!host_ok(h)||!v||!info||info->structure_bytes<sizeof(*info))
        return IFS_FS_INVALID;
    *v=NULL;if(s.mounted||s.generation==UINTPTR_MAX)return IFS_FS_BUSY;
    r=geometry(h,&sector,&cluster);if(r)return r;
    s.host=*h;IfsFatfsIo io=native(&s.host,sector);
    const IfsFatfsModuleV4 *m=infiltrator_filesystem_module_v4();
    if(m->v3->mount(&io))return IFS_FS_CORRUPT;
    if(m->space(&total,&free_bytes)) {
        (void)m->v3->unmount();return IFS_FS_IO_ERROR;
    }
    s.writable=!(h->flags&IFS_FS_IO_READ_ONLY) &&
        (h->flags&IFS_FS_IO_EXCLUSIVE) && h->write_at && h->flush;
    s.mounted=1;s.handle=++s.generation;
    info->variant=64u;info->max_name_bytes=765u;info->block_bytes=cluster;
    info->total_bytes=total;info->free_bytes=free_bytes;*v=(void *)s.handle;
    return IFS_FS_OK;
}
static int unmount(void *v){
    if(!live(v))return IFS_FS_INVALID;
    if(infiltrator_filesystem_module_v3()->unmount())return IFS_FS_IO_ERROR;
    s.mounted=0;s.writable=0;s.handle=0;return IFS_FS_OK;
}
static int path(const char *src,char dst[4096]){
    unsigned n=0;if(!src||src[0]!='/')return IFS_FS_INVALID;
    dst[0]='0';dst[1]=':';
    while(n<4093u&&src[n]){
        unsigned char c=(unsigned char)src[n];
        if(c<32u||c==127u||c==':'||c=='\\')return IFS_FS_INVALID;
        if(c=='/'&&n&&src[n-1]=='/')return IFS_FS_INVALID;
        if(c=='.'&&(n==1u||src[n-1]=='/')&&
           (src[n+1]=='/'||!src[n+1]||
           (src[n+1]=='.'&&(src[n+2]=='/'||!src[n+2]))))
            return IFS_FS_INVALID;
        dst[n+2u]=src[n];++n;
    }
    if(src[n])return IFS_FS_INVALID;
    dst[n+2u]=0;return IFS_FS_OK;
}
static int entry(IfsFsEntryV1 *out,const IfsExfatEntry64 *e,int root){
    const char *name=root?"/":e->name;uint32_t n=0;
    if(!out||out->structure_bytes<sizeof(*out)||!out->name||
       !out->name_capacity)return IFS_FS_INVALID;
    while(n<768u&&name[n])++n;
    if(n==768u||n>=out->name_capacity)return IFS_FS_NO_SPACE;
    memcpy(out->name,name,n+1u);
    out->name_bytes=n;out->attributes=root?0u:e->attributes;
    out->size_bytes=root?0u:e->size_bytes;
    out->type=root||(e->attributes&0x10u)?
        IFS_FS_ENTRY_DIRECTORY:IFS_FS_ENTRY_FILE;
    return IFS_FS_OK;
}
static int stat_path(void *v,const char *name,IfsFsEntryV1 *out){
    char p[4096];IfsExfatEntry64 e={0};
    if(!live(v)||!out)return IFS_FS_INVALID;
    int r=path(name,p);if(r)return r;
    if(p[2]=='/'&&!p[3])return entry(out,&e,1);
    return infiltrator_filesystem_module_v5()->stat64(p,&e)?
        IFS_FS_NOT_FOUND:entry(out,&e,0);
}
static int nth(void *v,const char *name,uint32_t index,IfsFsEntryV1 *out){
    char p[4096];IfsFatfsEntry small={0};IfsExfatEntry64 e={0};
    if(!live(v)||!out)return IFS_FS_INVALID;
    int r=path(name,p);if(r)return r;
    r=infiltrator_filesystem_module_v3()->directory_entry(p,index,&small);
    if(r)return r==1?IFS_FS_END:IFS_FS_IO_ERROR;
    size_t n=strlen(p),m=strlen(small.name);
    if(!m||n+m+2u>=sizeof(p))return IFS_FS_INVALID;
    if(p[n-1]!='/')p[n++]='/';
    memcpy(p+n,small.name,m+1u);
    if(infiltrator_filesystem_module_v5()->stat64(p,&e))return IFS_FS_IO_ERROR;
    return entry(out,&e,0);
}
static int read_file(void *v,const char *name,uint64_t at,void *out,uint32_t n){
    char p[4096];if(!live(v)||(!out&&n))return IFS_FS_INVALID;
    int r=path(name,p);if(r)return r;
    return infiltrator_filesystem_module_v5()->read64(p,at,out,n)?
        IFS_FS_IO_ERROR:IFS_FS_OK;
}
static int write_file(void *v,const char *name,uint64_t at,const void *data,uint32_t n){
    char p[4096];if(!live(v)||(!data&&n))return IFS_FS_INVALID;
    if(!s.writable)return IFS_FS_ACCESS;
    int r=path(name,p);if(r)return r;
    return infiltrator_filesystem_module_v5()->write64(p,at,data,n)?
        IFS_FS_IO_ERROR:IFS_FS_OK;
}
static int create(void *v,const char *name,uint32_t type){
    char p[4096];if(!live(v))return IFS_FS_INVALID;
    if(!s.writable)return IFS_FS_ACCESS;
    int r=path(name,p);if(r)return r;
    if(!p[3])return IFS_FS_INVALID;
    const IfsFatfsModuleV3 *m=infiltrator_filesystem_module_v3();
    if(type==IFS_FS_ENTRY_FILE)r=m->create(p);
    else if(type==IFS_FS_ENTRY_DIRECTORY)r=m->mkdir(p);
    else return IFS_FS_UNSUPPORTED;
    return r?IFS_FS_IO_ERROR:IFS_FS_OK;
}
static int remove_path(void *v,const char *name){
    char p[4096];if(!live(v))return IFS_FS_INVALID;
    if(!s.writable)return IFS_FS_ACCESS;
    int r=path(name,p);if(r)return r;
    if(!p[3])return IFS_FS_INVALID;
    return infiltrator_filesystem_module_v3()->unlink(p)?IFS_FS_IO_ERROR:IFS_FS_OK;
}
static int rename_path(void *v,const char *a,const char *b){
    char x[4096],y[4096];if(!live(v))return IFS_FS_INVALID;
    if(!s.writable)return IFS_FS_ACCESS;
    int r=path(a,x);if(r)return r;
    r=path(b,y);if(r)return r;
    if(!x[3]||!y[3])return IFS_FS_INVALID;
    return infiltrator_filesystem_module_v3()->rename(x,y)?IFS_FS_IO_ERROR:IFS_FS_OK;
}
static int truncate_path(void *v,const char *name,uint64_t size){
    char p[4096];if(!live(v))return IFS_FS_INVALID;
    if(!s.writable)return IFS_FS_ACCESS;
    int r=path(name,p);if(r)return r;
    return infiltrator_filesystem_module_v5()->truncate64(p,size)?
        IFS_FS_IO_ERROR:IFS_FS_OK;
}
static int format(const IfsFsHostIoV1 *h,uint32_t variant,const char *label){
    if(!host_ok(h)||variant!=64u)return IFS_FS_INVALID;
    if((h->flags&IFS_FS_IO_READ_ONLY)||!(h->flags&IFS_FS_IO_EXCLUSIVE)||
        !h->write_at||!h->flush)return IFS_FS_ACCESS;
    if(s.mounted)return IFS_FS_BUSY;
    if(label&&*label)return IFS_FS_UNSUPPORTED;
    if(h->structure_bytes<sizeof(IfsFsHostFormatIoV1))
        return IFS_FS_UNSUPPORTED;
    const IfsFsHostFormatIoV1 *ext=(const void *)h;
    if(ext->reserved||ext->logical_sector_bytes<512u||
       ext->logical_sector_bytes>4096u||
       (ext->logical_sector_bytes&(ext->logical_sector_bytes-1u)))
        return IFS_FS_INVALID;
    IfsFatfsIo io=native(h,ext->logical_sector_bytes);
    return infiltrator_filesystem_module_v4()->format(&io,0u)?
        IFS_FS_IO_ERROR:IFS_FS_OK;
}
static const IfsFsModuleApiV1 api={
    IFS_FS_COMMON_ABI_V1,sizeof(IfsFsModuleApiV1),"exfat",
    IFS_FS_CAP_PROBE|IFS_FS_CAP_MOUNT|IFS_FS_CAP_LIST|IFS_FS_CAP_READ|
    IFS_FS_CAP_WRITE|IFS_FS_CAP_CREATE|IFS_FS_CAP_REMOVE|IFS_FS_CAP_RENAME|
    IFS_FS_CAP_TRUNCATE|IFS_FS_CAP_FORMAT,
    probe,mount,unmount,stat_path,nth,read_file,write_file,create,
    remove_path,rename_path,truncate_path,format,NULL,NULL
};
__attribute__((visibility("default")))
const IfsFsModuleApiV1 *infiltrator_filesystem_api_v1(void){return &api;}
