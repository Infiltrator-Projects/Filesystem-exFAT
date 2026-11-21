/* SPDX-License-Identifier: GPL-3.0-or-later
 * Canonical Common V2 over the source-owned exFAT FatFs engine.
 */
#include "exfat_api.h"
#include <infiltratr/filesystem_module_v2.h>
#include <string.h>
static int native_read_failure;
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
/* Native FatFs sectors may be smaller than a host's logical blocks.
 * Every read, write and short transfer must obey Common V2 alignment,
 * maximum transfer and exclusive read-modify-write guarantees. */
static int raw_read(void *p,uint64_t at,void *b,uint32_t n) {
    const IfsFsHostRuntimeV2 *h=p;
    const int result=ifs_fs_host_read_bytes_v2(h,at,b,n);
    if(result!=IFS_FS_OK) {
        if(native_read_failure==IFS_FS_OK)native_read_failure=result;
        return -1;
    }
    return 0;
}
static int raw_write(void *p,uint64_t at,const void *b,uint32_t n){
    const IfsFsHostRuntimeV2 *h=p;
    if((h->capabilities&IFS_FS_HOST_READ_ONLY)||
       !(h->capabilities&IFS_FS_HOST_EXCLUSIVE)||
       !(h->capabilities&IFS_FS_HOST_DURABLE)||
       !h->write_at||!h->flush)return -1;
    return ifs_fs_host_write_bytes_v2(h,at,b,n)==IFS_FS_OK?0:-1;
}
static int raw_flush(void *p){
    const IfsFsHostRuntimeV2 *h=p;
    if((h->capabilities&IFS_FS_HOST_READ_ONLY)||!(h->capabilities&IFS_FS_HOST_EXCLUSIVE))return 0;
    return h->flush?h->flush(h->context):-1;
}
static uint32_t fat_time(void *p){(void)p;return (1u<<21)|(1u<<16);}
/* exFAT checksum of the 11 Main Boot Region sectors, per on-disk format.
 * VolumeFlags (106-107) and PercentInUse (112) are intentionally excluded
 * by the standard; everything else, including boot code, is authenticated
 * by the 32-bit rotate/add checksum. Never skip this check for BOOT.
 * This is a corruption check, not a cryptographic trust signature.
 */
static int boot_checksum(const IfsFsHostRuntimeV2 *host,uint32_t sector)
{
    uint8_t buf[4096];
    uint32_t sum=0;
    if (sector<512u || sector>sizeof(buf) ||
        host->volume_bytes<(uint64_t)sector*24u)
        return IFS_FS_CORRUPT;
    for(uint32_t block=0;block<11u;++block) {
        int status=ifs_fs_host_read_bytes_v2(
            host,(uint64_t)block*sector,buf,sector);
        if(status!=IFS_FS_OK)return status;
        for(uint32_t i=0;i<sector;++i) {
            if(block==0u&&(i==106u||i==107u||i==112u))
                continue;
            sum=(sum>>1)|(sum<<31);
            sum+=(uint32_t)buf[i];
        }
    }
    int status=ifs_fs_host_read_bytes_v2(host,(uint64_t)11u*sector,buf,sector);
    if(status!=IFS_FS_OK)return status;
    for(uint32_t i=0;i<sector;i+=4u) {
        const uint32_t recorded=(uint32_t)buf[i] |
            ((uint32_t)buf[i+1u]<<8) |
            ((uint32_t)buf[i+2u]<<16) |
            ((uint32_t)buf[i+3u]<<24);
        if(recorded!=sum)return IFS_FS_CORRUPT;
    }
    return IFS_FS_OK;
}
/* Bound the native FAT and data-cluster structures using the actual boot
 * sector geometry. An internally consistent checksum alone is insufficient:
 * a corrupt-but-rechecksummed FAT offset must never reach FatFs or BOOT.
 */
static uint32_t exfat_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static uint64_t exfat_le64(const uint8_t *p)
{
    return (uint64_t)exfat_le32(p) |
        ((uint64_t)exfat_le32(p+4)<<32);
}
static int exfat_layout(const uint8_t b[512],
                        const IfsFsHostRuntimeV2 *h,uint32_t sector)
{
    const uint64_t volume=exfat_le64(b+72);
    const uint64_t fat_offset=exfat_le32(b+80);
    const uint64_t fat_sectors=exfat_le32(b+84);
    const uint64_t heap_start=exfat_le32(b+88);
    const uint64_t clusters=exfat_le32(b+92);
    const uint64_t root=exfat_le32(b+96);
    const uint64_t sector_per_cluster=UINT64_C(1)<<b[109];
    const uint32_t rev=(uint32_t)b[104]|((uint32_t)b[105]<<8);
    if(rev!=0x0100u || b[110]!=1u)
        return IFS_FS_UNSUPPORTED; /* TexFAT or unrecognised revision */
    if(!volume || volume>h->volume_bytes/sector ||
       fat_offset<24u || !fat_sectors || !clusters ||
       clusters>0xfffffff5u || root<2u || root>clusters+1u ||
       fat_offset+fat_sectors>heap_start ||
       fat_sectors*(uint64_t)sector<(clusters+2u)*4u ||
       heap_start+clusters*sector_per_cluster>volume ||
       (sector_per_cluster*(uint64_t)sector)>UINT64_C(33554432) ||
       (b[112]>100u&&b[112]!=0xffu))
        return IFS_FS_CORRUPT;
    return IFS_FS_OK;
}
static int geometry(const IfsFsHostRuntimeV2 *h,uint32_t *sector,uint32_t *cluster){
    uint8_t b[512];if(!host_ok(h))return IFS_FS_INVALID;
    int status=ifs_fs_host_read_bytes_v2(h,0u,b,sizeof(b));
    if(status!=IFS_FS_OK)return status;
    if(memcmp(b+3,"EXFAT   ",8))return IFS_FS_NOT_FOUND;
    if(b[108]<9u||b[108]>12u||b[109]>25u||
       ((uint64_t)1u<<(b[108]+b[109]))>UINT32_MAX||
       b[510]!=0x55u||b[511]!=0xAAu)
        return IFS_FS_CORRUPT;
    *sector=1u<<b[108];*cluster=*sector*(1u<<b[109]);
    /* The disk BPB sector size is authoritative for FatFs. A 512-byte
     * exFAT image can be hosted by a 4K BlockIo device when all requests
     * are translated by the Common V2 byte-range bridge above. */
    status=boot_checksum(h,*sector);
    if(status!=IFS_FS_OK)return status;
    return exfat_layout(b,h,*sector);
}
static IfsFatfsIo native(const IfsFsHostRuntimeV2 *h,uint32_t sector){
    IfsFatfsIo io={0};io.user=(void *)h;io.partition_bytes=h->volume_bytes;
    io.sector_bytes=sector;io.read_at=raw_read;io.write_at=raw_write;
    io.flush=raw_flush;io.fat_timestamp=fat_time;return io;
}
static int probe(const IfsFsHostRuntimeV2 *h,uint32_t *variant){
    uint32_t sector,cluster;int r;
    if(!variant)return IFS_FS_INVALID;
    *variant=0u;
    if(!host_ok(h))return IFS_FS_INVALID;
    if(s.mounted)return IFS_FS_BUSY;
    r=geometry(h,&sector,&cluster);if(r)return r;
    IfsFatfsIo io=native(h,sector);
    native_read_failure=IFS_FS_OK;
    r=ifs_exfat_engine_mount(&io);
    if(r)return native_read_failure!=IFS_FS_OK?
        native_read_failure:IFS_FS_CORRUPT;
    r=ifs_exfat_engine_unmount();if(r)return IFS_FS_IO_ERROR;
    *variant=64u;return IFS_FS_OK;
}
static int mount(const IfsFsHostRuntimeV2 *h,void **v,IfsFsVolumeInfoV2 *info){
    uint32_t sector,cluster;uint64_t total=0,free_bytes=0;int r;
    if(!host_ok(h)||!v||!info||info->structure_bytes<sizeof(*info))
        return IFS_FS_INVALID;
    *v=NULL;
#if defined(IFS_EXFAT_FIRMWARE_BUILD)
    /* Never negotiate writable media from a firmware read-only module. */
    if (!(h->capabilities & IFS_FS_HOST_READ_ONLY))
        return IFS_FS_ACCESS;
#endif
    if(s.mounted||s.generation==UINTPTR_MAX)return IFS_FS_BUSY;
    r=geometry(h,&sector,&cluster);if(r)return r;
    /* Keep only the caller-supplied V2 prefix; older hosts do not have the
     * optional entropy field. Clamp the saved size to our own storage. */
    size_t host_bytes=h->structure_bytes<sizeof(s.host)?h->structure_bytes:sizeof(s.host);
    memset(&s.host,0,sizeof(s.host));
    memcpy(&s.host,h,host_bytes);
    s.host.structure_bytes=(uint32_t)host_bytes;
    IfsFatfsIo io=native(&s.host,sector);
    native_read_failure=IFS_FS_OK;
    if(ifs_exfat_engine_mount(&io))return native_read_failure!=IFS_FS_OK?
        native_read_failure:IFS_FS_CORRUPT;
    if(ifs_exfat_engine_space(&total,&free_bytes)) {
        (void)ifs_exfat_engine_unmount();return IFS_FS_IO_ERROR;
    }
#if defined(IFS_EXFAT_FIRMWARE_BUILD)
    s.writable=0;
#else
    s.writable=!(h->capabilities&IFS_FS_HOST_READ_ONLY) &&
        (h->capabilities&IFS_FS_HOST_EXCLUSIVE) && h->write_at && h->flush;
#endif
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
    r=ifs_exfat_engine_stat64(p,&e);
    return r==IFS_FS_OK ? entry(out,&e,0) : r;
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
    r=ifs_exfat_engine_stat64(p,&e);
    return r==IFS_FS_OK ? entry(out,&e,0) : r;
}
static int read_file(void *v,const char *name,uint64_t at,void *out,uint32_t n){
    char p[4096];if(!live(v)||(!out&&n))return IFS_FS_INVALID;
    int r=path(name,p);if(r)return r;
    return ifs_exfat_engine_read64(p,at,out,n);
}
/* Firmware exposes a strictly read-only V2 namespace. Hosted builds retain
 * their independently qualified native write/format functionality. */
#if !defined(IFS_EXFAT_FIRMWARE_BUILD)
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
#endif /* !IFS_EXFAT_FIRMWARE_BUILD */
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
#if !defined(IFS_EXFAT_FIRMWARE_BUILD)
static int v2_sync(void *v) {
    if(!live(v))return IFS_FS_INVALID;
    if(!s.host.flush)return IFS_FS_OK;
    return s.host.flush(s.host.context) ? IFS_FS_IO_ERROR : IFS_FS_OK;
}
#endif /* !IFS_EXFAT_FIRMWARE_BUILD */
static const IfsFsVolumeOpsV2 group_volume={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsVolumeOpsV2),
             IFS_FS_GROUP_VOLUME,0u,
#if defined(IFS_EXFAT_FIRMWARE_BUILD)
             IFS_FS_VOLUME_QUERY},
    .query=v2_query
#else
             IFS_FS_VOLUME_QUERY|IFS_FS_VOLUME_SYNC|IFS_FS_VOLUME_FORMAT},
    .query=v2_query, .sync=v2_sync, .format=format
#endif
};
/* Native CORE and VOLUME callbacks use the same mounted exFAT FatFs state. */
static const IfsFsCoreOpsV2 group_core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),
             IFS_FS_GROUP_CORE,0u,
#if defined(IFS_EXFAT_FIRMWARE_BUILD)
             IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|IFS_FS_CORE_STAT|
             IFS_FS_CORE_LIST|IFS_FS_CORE_READ},
    .probe=probe, .mount=mount, .unmount=unmount,
    .stat=stat_path, .directory_entry=nth, .read=read_file
#else
             IFS_FS_CORE_ALL},
    .probe=probe, .mount=mount, .unmount=unmount,
    .stat=stat_path, .directory_entry=nth,
    .read=read_file, .write=write_file, .create=create,
    .remove=remove_path, .rename=rename_path, .truncate=truncate_path
#endif
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
    #if defined(IFS_EXFAT_FIRMWARE_BUILD)
    .runtime_profiles=IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE,
#else
    .runtime_profiles=IFS_FS_PROFILE_HOSTED,
#endif
    .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|
                IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME),
    .query_group=query_v2
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void){
    return &descriptor_v2;
}
