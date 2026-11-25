/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent exFAT shared-object entry point. Compile the same audited FatFs
 * source into this one format-specific binary; never proxy to Linux exfat.ko.
 */
#if !defined(IFS_FATFS_ONLY) || IFS_FATFS_ONLY != 64
#error "This translation unit is only for the exFAT engine"
#endif
#include "../../../portable/fatfs/fatfs_module.c"

#include "exfat_api.h"
#include <infiltratr/filesystem_module_v2.h>

/* Preserve the native FatFs failure category at the Common V2 boundary.
 * Missing paths must not mask failed or corrupt on-media reads. */
static int exfat_common_failure(FRESULT result)
{
    switch (result) {
    case FR_OK: return IFS_FS_OK;
    case FR_NO_FILE:
    case FR_NO_PATH: return IFS_FS_NOT_FOUND;
    case FR_DISK_ERR:
    case FR_NOT_READY: return IFS_FS_IO_ERROR;
    case FR_INT_ERR:
    case FR_INVALID_OBJECT:
    case FR_NO_FILESYSTEM: return IFS_FS_CORRUPT;
    case FR_INVALID_NAME:
    case FR_INVALID_DRIVE:
    case FR_INVALID_PARAMETER:
    case FR_NOT_ENABLED: return IFS_FS_INVALID;
    case FR_DENIED:
    case FR_WRITE_PROTECTED: return IFS_FS_ACCESS;
    case FR_EXIST: return IFS_FS_EXISTS;
    case FR_TIMEOUT:
    case FR_LOCKED:
    case FR_TOO_MANY_OPEN_FILES: return IFS_FS_BUSY;
    case FR_NOT_ENOUGH_CORE: return IFS_FS_NO_MEMORY;
    default: return IFS_FS_UNSUPPORTED;
    }
}

/* exFAT has native 64-bit offsets and file sizes; preserve FSIZE_t. */
static int exfat_read64(const char *path,uint64_t offset,void *out,uint32_t count)
{
    FIL file;
    UINT read=0;
    FRESULT result;
    int status=IFS_FS_OK;
    if(!selected||!path||(!out&&count))
        return IFS_FS_INVALID;
    result=f_open(&file,path,FA_READ);
    if(result!=FR_OK)
        return exfat_common_failure(result);
    if(offset>(uint64_t)f_size(&file) ||
       count>(uint64_t)f_size(&file)-offset) {
        status=IFS_FS_INVALID;
    } else {
        result=f_lseek(&file,(FSIZE_t)offset);
        if(result!=FR_OK)
            status=exfat_common_failure(result);
        else if(f_tell(&file)!=(FSIZE_t)offset)
            status=IFS_FS_IO_ERROR;
        else {
            result=f_read(&file,out,count,&read);
            status=result!=FR_OK ? exfat_common_failure(result) :
                read==count ? IFS_FS_OK : IFS_FS_CORRUPT;
        }
    }
    result=f_close(&file);
    if(result!=FR_OK && status==IFS_FS_OK)
        status=exfat_common_failure(result);
    return status;
}
static int exfat_write64(const char *path,uint64_t offset,const void *data,uint32_t count)
{
    FIL file;UINT wrote=0;int good;
    if(!selected||!path||(!data&&count)||count>UINT64_MAX-offset||
       f_open(&file,path,FA_WRITE|FA_OPEN_EXISTING)!=FR_OK)return -1;
    good=f_lseek(&file,(FSIZE_t)offset)==FR_OK &&
         f_tell(&file)==(FSIZE_t)offset &&
         f_write(&file,data,count,&wrote)==FR_OK && wrote==count &&
         f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK)good=0;
    return good?0:-1;
}
static int exfat_truncate64(const char *path,uint64_t size)
{
    FIL file;int good;
    if(!selected||!path||f_open(&file,path,FA_WRITE|FA_OPEN_EXISTING)!=FR_OK)
        return -1;
    good=f_lseek(&file,(FSIZE_t)size)==FR_OK &&
         f_tell(&file)==(FSIZE_t)size &&
         f_truncate(&file)==FR_OK && f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK)good=0;
    return good?0:-1;
}
static int exfat_stat64(const char *path,IfsExfatEntry64 *out)
{
    FILINFO info;
    FRESULT result;
    if(!selected||!path||!out)
        return IFS_FS_INVALID;
    result=f_stat(path,&info);
    if(result!=FR_OK)
        return exfat_common_failure(result);
    out->size_bytes=(uint64_t)info.fsize;
    out->attributes=info.fattrib;
    copy_name(out->name,info.fname);
    return IFS_FS_OK;
}
/* Private C entry points, linked directly into exFAT.so. Not versioned ABI
 * exports; the only externally discoverable filesystem API is Common V2. */
int ifs_exfat_engine_mount(const IfsFatfsIo *io) {return attach(io);}
int ifs_exfat_engine_unmount(void) {return detach();}
int ifs_exfat_engine_create(const char *p) {return create_file(p);}
int ifs_exfat_engine_mkdir(const char *p) {return make_dir(p);}
int ifs_exfat_engine_remove(const char *p) {return remove_path(p);}
int ifs_exfat_engine_rename(const char *a,const char *b) {return rename_path(a,b);}
int ifs_exfat_engine_directory_entry(const char *p,uint32_t nth,IfsFatfsEntry *out) {
    return nth_dir_entry(p,nth,out);
}
int ifs_exfat_engine_read64(const char *p,uint64_t at,void *out,uint32_t n) {
    return exfat_read64(p,at,out,n);
}
int ifs_exfat_engine_write64(const char *p,uint64_t at,const void *data,uint32_t n) {
    return exfat_write64(p,at,data,n);
}
int ifs_exfat_engine_truncate64(const char *p,uint64_t n) {
    return exfat_truncate64(p,n);
}
int ifs_exfat_engine_stat64(const char *p,IfsExfatEntry64 *out) {
    return exfat_stat64(p,out);
}
int ifs_exfat_engine_space(uint64_t *total,uint64_t *free_bytes) {
    return volume_space(total,free_bytes);
}
int ifs_exfat_engine_format(const IfsFatfsIo *io,uint32_t cluster_bytes) {
    return format_volume(io,cluster_bytes);
}

/* exFAT stores its volume label in a root-directory entry. The native
 * UTF-16/UTF-8 converter and directory updater own the on-disk format. */
int ifs_exfat_engine_label(char *out,uint32_t capacity,uint32_t *bytes)
{
    if (!selected || !out || !capacity || !bytes) return IFS_FS_INVALID;
    *bytes=0u;
    char label[64]={0};
    FRESULT rc=f_getlabel("0:",label,NULL);
    if(rc!=FR_OK)return exfat_common_failure(rc);
    uint32_t n=0u;
    while(n<sizeof(label) && label[n]) ++n;
    if(n==sizeof(label))return IFS_FS_CORRUPT;
    if(n>=capacity)return IFS_FS_NO_SPACE;
    for(uint32_t i=0;i<=n;++i)out[i]=label[i];
    *bytes=n;
    return IFS_FS_OK;
}
#if !defined(IFS_EXFAT_FIRMWARE_BUILD)
/* The provider validates the whole native stream, including every
 * allocation bit and the terminal FAT link, before returning any page.
 * NoFatChain streams are interpreted by FatFs itself, not guessed from
 * the directory entry or from neighbouring free-space runs.
 * For a directory, DataLength is the allocation size; the root has an
 * explicit FAT chain without a stream length. */
extern DWORD ifs_exfat_native_next_cluster(FFOBJID *,DWORD);
static int exfat_map_chain(FFOBJID *object,int root,uint64_t first,
                           IfsFsExtentV2 *out,uint32_t capacity,uint32_t *written)
{
    FATFS *fs=object->fs;
    if(fs!=&volume || fs->fs_type!=FS_EXFAT || fs->n_fatent<3u ||
       !fs->csize || !device.sector_bytes || device.sector_bytes>4096u ||
       fs->wflag)return IFS_FS_CORRUPT;
    const uint64_t unit=(uint64_t)fs->csize*device.sector_bytes;
    const uint64_t base=(uint64_t)fs->database*device.sector_bytes;
    const uint64_t size=(uint64_t)object->objsize;
    const uint64_t total=size/unit+(size%unit!=0u);
    DWORD cluster=root?(DWORD)fs->dirbase:object->sclust;
    if(!root && !total)return cluster?IFS_FS_CORRUPT:IFS_FS_END;
    if(!root && (total>fs->n_fatent-2u || size>device.partition_bytes))
        return IFS_FS_CORRUPT;
    if(root){object->stat=0u;object->sclust=0u;}
    IfsFsExtentV2 page[128]={{0}};
    uint8_t bitmap[4096];
    uint64_t cached=UINT64_MAX,seen=0u;
    uint32_t count=0u;
    DWORD previous=0u;
    for(uint64_t i=0u;i<fs->n_fatent-2u;++i) {
        if(cluster<2u || cluster>=fs->n_fatent || cluster>=0x7ffffff0u ||
           (!root && i>=total))return IFS_FS_CORRUPT;
        const uint64_t bit=cluster-2u;
        const uint64_t sector=(uint64_t)fs->bitbase+
            bit/8u/device.sector_bytes;
        if(sector!=cached) {
            if(disk_read(0,bitmap,(LBA_t)sector,1u)!=RES_OK)
                return IFS_FS_IO_ERROR;
            cached=sector;
        }
        if(!(bitmap[(bit/8u)%device.sector_bytes]&(1u<<(bit%8u))))
            return IFS_FS_CORRUPT;
        if(base>device.partition_bytes ||
           bit>(device.partition_bytes-base)/unit)return IFS_FS_CORRUPT;
        const uint64_t physical=base+bit*unit,logical=i*unit;
        if(unit>device.partition_bytes-physical)return IFS_FS_CORRUPT;
        const uint64_t length=root?unit:(size-logical<unit?size-logical:unit);
        if(!i || cluster!=previous+1u) {
            if(seen>=first && count<capacity)
                page[count++]=(IfsFsExtentV2){.logical_offset=logical,
                    .physical_offset=physical,.length=length};
            ++seen;
        }else if(seen>first && seen-first<=count)page[count-1u].length+=length;
        previous=cluster;
        const DWORD next=ifs_exfat_native_next_cluster(object,cluster);
        if(next==UINT32_MAX)return IFS_FS_IO_ERROR;
        if(next>=0x7ffffff8u && next<=0x7fffffffu) {
            if(!root && i+1u!=total)return IFS_FS_CORRUPT;
            for(uint32_t j=0u;j<count;++j)out[j]=page[j];
            *written=count;
            return count?IFS_FS_OK:IFS_FS_END;
        }
        if(next<2u || next>=fs->n_fatent || next>=0x7ffffff0u)
            return IFS_FS_CORRUPT;
        cluster=next;
    }
    return IFS_FS_CORRUPT;
}

int ifs_exfat_engine_map(const char *path,uint64_t first,IfsFsExtentV2 *out,
                         uint32_t capacity,uint32_t *written)
{
    if(!written)return IFS_FS_INVALID;
    *written=0u;
    if(!selected || !path || !out || !capacity || capacity>128u)
        return IFS_FS_INVALID;
    FIL file={0};
    FRESULT rc=f_open(&file,path,FA_READ);
    int result;
    if(rc==FR_OK) {
        result=exfat_map_chain(&file.obj,0,first,out,capacity,written);
        rc=f_close(&file);
    } else if(rc==FR_NO_FILE || rc==FR_NO_PATH || rc==FR_DENIED ||
              rc==FR_INVALID_NAME) {
        DIR dir={0};
        rc=f_opendir(&dir,path);
        if(rc!=FR_OK)return exfat_common_failure(rc);
        result=exfat_map_chain(&dir.obj,!dir.obj.sclust,first,out,capacity,written);
        rc=f_closedir(&dir);
    } else return exfat_common_failure(rc);
    if(rc!=FR_OK && (result==IFS_FS_OK || result==IFS_FS_END)) {
        *written=0u;
        return exfat_common_failure(rc);
    }
    return result;
}
int ifs_exfat_engine_set_label(const char *label)
{
    if(!selected||!label)return IFS_FS_INVALID;
    /* Reject control characters before FatFs can interpret one as a
     * terminator. FatFs validates Unicode and the 11 UTF-16-unit limit. */
    uint32_t n=0u;
    while(n<=44u && label[n]){
        unsigned char c=(unsigned char)label[n++];
        if(c<32u || c==127u || c==':')return IFS_FS_INVALID;
    }
    if(n>44u)return IFS_FS_INVALID;
    return exfat_common_failure(f_setlabel(label));
}
#endif
