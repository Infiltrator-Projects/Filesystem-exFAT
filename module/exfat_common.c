/* SPDX-License-Identifier: GPL-3.0-or-later
 * Canonical Common V2 over the source-owned exFAT FatFs engine.
 */
#include "exfat_api.h"
#include <infiltratr/filesystem_module_v2.h>
#include <string.h>
static struct {
    IfsFsHostRuntimeV2 host;
    uintptr_t generation, handle;
    int mounted, writable;
    uint32_t cluster_bytes;
} s;
static int host_ok(const IfsFsHostRuntimeV2 *h) {
    return ifs_fs_host_v2_valid(h) && h->volume_bytes>=512u;
}
static int live(void *v){return s.mounted && s.handle && (uintptr_t)v==s.handle;}
static int raw_read(void *p,uint64_t at,void *b,uint32_t n) {
    const IfsFsHostRuntimeV2 *h=p;return h->read_at(h->context,at,b,n);
}
static int raw_write(void *p,uint64_t at,const void *b,uint32_t n){
    const IfsFsHostRuntimeV2 *h=p;
    if((h->capabilities&IFS_FS_HOST_READ_ONLY)||!(h->capabilities&IFS_FS_HOST_EXCLUSIVE)||
       !h->write_at)return -1;
    return h->write_at(h->context,at,b,n);
}
static int raw_flush(void *p){
    const IfsFsHostRuntimeV2 *h=p;
    if((h->capabilities&IFS_FS_HOST_READ_ONLY)||!(h->capabilities&IFS_FS_HOST_EXCLUSIVE))return 0;
    return h->flush?h->flush(h->context):-1;
}
static uint32_t fat_time(void *p){(void)p;return (1u<<21)|(1u<<16);}
static int geometry(const IfsFsHostRuntimeV2 *h,uint32_t *sector,uint32_t *cluster){
    uint8_t b[512];if(!host_ok(h))return IFS_FS_INVALID;
    int status=ifs_fs_host_read_bytes_v2(h,0u,b,sizeof(b));
    if(status!=IFS_FS_OK)return status;
    if(memcmp(b+3,"EXFAT   ",8)||b[108]<9u||b[108]>12u||b[109]>25u||
       ((uint64_t)1u<<(b[108]+b[109]))>UINT32_MAX)return IFS_FS_NOT_FOUND;
    *sector=1u<<b[108];*cluster=*sector*(1u<<b[109]);
    if (*sector != h->logical_sector_bytes) return IFS_FS_CORRUPT;
    return IFS_FS_OK;
}
static IfsFatfsIo native(const IfsFsHostRuntimeV2 *h,uint32_t sector){
    IfsFatfsIo io={0};io.user=(void *)h;io.partition_bytes=h->volume_bytes;
    io.sector_bytes=sector;io.read_at=raw_read;io.write_at=raw_write;
    io.flush=raw_flush;io.fat_timestamp=fat_time;return io;
}
static int probe(const IfsFsHostRuntimeV2 *h,uint32_t *variant){
    uint32_t sector,cluster;int r;
    if(!host_ok(h)||!variant)return IFS_FS_INVALID;
    if(s.mounted)return IFS_FS_BUSY;
    r=geometry(h,&sector,&cluster);if(r)return r;
    IfsFatfsIo io=native(h,sector);
    r=ifs_exfat_engine_mount(&io);if(r)return IFS_FS_CORRUPT;
    r=ifs_exfat_engine_unmount();if(r)return IFS_FS_IO_ERROR;
    *variant=64u;return IFS_FS_OK;
}
static int mount(const IfsFsHostRuntimeV2 *h,void **v,IfsFsVolumeInfoV2 *info){
    uint32_t sector,cluster;uint64_t total=0,free_bytes=0;int r;
    if(!host_ok(h)||!v||!info||info->structure_bytes<sizeof(*info))
        return IFS_FS_INVALID;
    *v=NULL;if(s.mounted||s.generation==UINTPTR_MAX)return IFS_FS_BUSY;
    r=geometry(h,&sector,&cluster);if(r)return r;
    /* Keep only the caller-supplied V2 prefix; older hosts do not have the
     * optional entropy field. Clamp the saved size to our own storage. */
    size_t host_bytes=h->structure_bytes<sizeof(s.host)?h->structure_bytes:sizeof(s.host);
    memset(&s.host,0,sizeof(s.host));
    memcpy(&s.host,h,host_bytes);
    s.host.structure_bytes=(uint32_t)host_bytes;
    IfsFatfsIo io=native(&s.host,sector);
    if(ifs_exfat_engine_mount(&io))return IFS_FS_CORRUPT;
    if(ifs_exfat_engine_space(&total,&free_bytes)) {
        (void)ifs_exfat_engine_unmount();return IFS_FS_IO_ERROR;
    }
    s.writable=!(h->capabilities&IFS_FS_HOST_READ_ONLY) &&
        (h->capabilities&IFS_FS_HOST_EXCLUSIVE) && h->write_at && h->flush;
    s.mounted=1;s.cluster_bytes=cluster;s.handle=++s.generation;
    info->variant=64u;info->max_name_bytes=765u;info->block_bytes=cluster;
    info->total_bytes=total;info->free_bytes=free_bytes;*v=(void *)s.handle;
    return IFS_FS_OK;
}
static int unmount(void *v){
    if(!live(v))return IFS_FS_INVALID;
    if(ifs_exfat_engine_unmount())return IFS_FS_IO_ERROR;
    s.mounted=0;s.writable=0;s.handle=0;s.cluster_bytes=0u;return IFS_FS_OK;
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
static int entry(IfsFsEntryV2 *out,const IfsExfatEntry64 *e,int root){
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
static int stat_path(void *v,const char *name,IfsFsEntryV2 *out){
    char p[4096];IfsExfatEntry64 e={0};
    if(!live(v)||!out)return IFS_FS_INVALID;
    int r=path(name,p);if(r)return r;
    if(p[2]=='/'&&!p[3])return entry(out,&e,1);
    return ifs_exfat_engine_stat64(p,&e)?
        IFS_FS_NOT_FOUND:entry(out,&e,0);
}
static int nth(void *v,const char *name,uint32_t index,IfsFsEntryV2 *out){
    char p[4096];IfsFatfsEntry small={0};IfsExfatEntry64 e={0};
    if(!live(v)||!out)return IFS_FS_INVALID;
    int r=path(name,p);if(r)return r;
    r=ifs_exfat_engine_directory_entry(p,index,&small);
    if(r)return r==1?IFS_FS_END:IFS_FS_IO_ERROR;
    size_t n=strlen(p),m=strlen(small.name);
    if(!m||n+m+2u>=sizeof(p))return IFS_FS_INVALID;
    if(p[n-1]!='/')p[n++]='/';
    memcpy(p+n,small.name,m+1u);
    if(ifs_exfat_engine_stat64(p,&e))return IFS_FS_IO_ERROR;
    return entry(out,&e,0);
}
static int read_file(void *v,const char *name,uint64_t at,void *out,uint32_t n){
    char p[4096];if(!live(v)||(!out&&n))return IFS_FS_INVALID;
    int r=path(name,p);if(r)return r;
    return ifs_exfat_engine_read64(p,at,out,n)?
        IFS_FS_IO_ERROR:IFS_FS_OK;
}
static int write_file(void *v,const char *name,uint64_t at,const void *data,uint32_t n){
    char p[4096];if(!live(v)||(!data&&n))return IFS_FS_INVALID;
    if(!s.writable)return IFS_FS_ACCESS;
    int r=path(name,p);if(r)return r;
    return ifs_exfat_engine_write64(p,at,data,n)?
        IFS_FS_IO_ERROR:IFS_FS_OK;
}
static int create(void *v,const char *name,uint32_t type){
    char p[4096];if(!live(v))return IFS_FS_INVALID;
    if(!s.writable)return IFS_FS_ACCESS;
    int r=path(name,p);if(r)return r;
    if(!p[3])return IFS_FS_INVALID;
    if(type==IFS_FS_ENTRY_FILE)r=ifs_exfat_engine_create(p);
    else if(type==IFS_FS_ENTRY_DIRECTORY)r=ifs_exfat_engine_mkdir(p);
    else return IFS_FS_UNSUPPORTED;
    return r?IFS_FS_IO_ERROR:IFS_FS_OK;
}
static int remove_path(void *v,const char *name){
    char p[4096];if(!live(v))return IFS_FS_INVALID;
    if(!s.writable)return IFS_FS_ACCESS;
    int r=path(name,p);if(r)return r;
    if(!p[3])return IFS_FS_INVALID;
    return ifs_exfat_engine_remove(p)?IFS_FS_IO_ERROR:IFS_FS_OK;
}
static int rename_path(void *v,const char *a,const char *b){
    char x[4096],y[4096];if(!live(v))return IFS_FS_INVALID;
    if(!s.writable)return IFS_FS_ACCESS;
    int r=path(a,x);if(r)return r;
    r=path(b,y);if(r)return r;
    if(!x[3]||!y[3])return IFS_FS_INVALID;
    return ifs_exfat_engine_rename(x,y)?IFS_FS_IO_ERROR:IFS_FS_OK;
}
static int truncate_path(void *v,const char *name,uint64_t size){
    char p[4096];if(!live(v))return IFS_FS_INVALID;
    if(!s.writable)return IFS_FS_ACCESS;
    int r=path(name,p);if(r)return r;
    return ifs_exfat_engine_truncate64(p,size)?
        IFS_FS_IO_ERROR:IFS_FS_OK;
}
static int format(const IfsFsHostRuntimeV2 *h,uint32_t variant,
                  const char *label,uint32_t flags) {
    if (!h || !ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    if ((h->capabilities & IFS_FS_HOST_READ_ONLY) ||
        !(h->capabilities & IFS_FS_HOST_EXCLUSIVE) ||
        !(h->capabilities & IFS_FS_HOST_DURABLE))
        return IFS_FS_ACCESS;
    if (s.mounted) return IFS_FS_BUSY;
    if (flags) return IFS_FS_UNSUPPORTED;
    if (variant != 64u || (label && *label))
        return IFS_FS_UNSUPPORTED;
    if (h->logical_sector_bytes < 512u || h->logical_sector_bytes > 4096u)
        return IFS_FS_INVALID;
    IfsFatfsIo io=native(h,h->logical_sector_bytes);
    return ifs_exfat_engine_format(&io,0u) ?
        IFS_FS_IO_ERROR:IFS_FS_OK;
}
/* V2 volume operations share the same canonical source-owned FatFs state.
 * Return the mounted engine's authoritative allocation information.
 * This module knows its mounted cluster size and never reparses on-disk FAT. */
static int v2_query(void *v, IfsFsVolumeInfoV2 *info) {
    uint64_t total=0, free_bytes=0;
    if(!live(v)||!info||info->structure_bytes<sizeof(*info))
        return IFS_FS_INVALID;
    if(ifs_exfat_engine_space(&total,&free_bytes)!=0)
        return IFS_FS_IO_ERROR;
    info->variant=64u;
    info->block_bytes=s.cluster_bytes;
    info->max_name_bytes=765u;
    info->total_bytes=total;
    info->free_bytes=free_bytes;
    return IFS_FS_OK;
}
static int v2_sync(void *v) {
    if(!live(v))return IFS_FS_INVALID;
    if(!s.host.flush)return IFS_FS_OK;
    return s.host.flush(s.host.context) ? IFS_FS_IO_ERROR : IFS_FS_OK;
}
static const IfsFsVolumeOpsV2 group_volume={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsVolumeOpsV2),
             IFS_FS_GROUP_VOLUME,0u,
             IFS_FS_VOLUME_QUERY|IFS_FS_VOLUME_SYNC|IFS_FS_VOLUME_FORMAT},
    .query=v2_query,
    .sync=v2_sync,
    .format=format
};
/* Native CORE and VOLUME callbacks use the same mounted exFAT FatFs state. */
static const IfsFsCoreOpsV2 group_core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),
             IFS_FS_GROUP_CORE,0u,IFS_FS_CORE_ALL},
    .probe=probe, .mount=mount, .unmount=unmount,
    .stat=stat_path, .directory_entry=nth,
    .read=read_file, .write=write_file, .create=create,
    .remove=remove_path, .rename=rename_path, .truncate=truncate_path
};
static const IfsFsGroupHeaderV2 *query_v2(
    uint32_t id,uint32_t min,uint32_t max){
    if (min>IFS_FS_GROUP_ABI_V2 || max<IFS_FS_GROUP_ABI_V2) return NULL;
    if (id==IFS_FS_GROUP_CORE) return &group_core.header;
    if (id==IFS_FS_GROUP_VOLUME) return &group_volume.header;
    return NULL;
}
__attribute__((used,section(".ifsmeta")))
static const char common_v2_identity[]="IFS-MODULE:exfat:common2";
static const IfsFsModuleApiV2 descriptor_v2={
    .abi_version=IFS_FS_COMMON_ABI_V2,
    .structure_bytes=sizeof(IfsFsModuleApiV2),
    .format_id="exfat",
    .runtime_profiles=IFS_FS_PROFILE_HOSTED,
    .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|
                IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME),
    .query_group=query_v2
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void){
    return &descriptor_v2;
}
