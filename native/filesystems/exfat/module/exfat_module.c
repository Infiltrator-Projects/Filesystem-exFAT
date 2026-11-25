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
/* Ownership reconciliation for the native exFAT allocation domain.
 * Every file/directory stream is opened by FatFs, which validates its
 * entry-set checksum and interprets NoFatChain. Independently claim the
 * two system streams in the root (bitmap and upcase). A final bitmap
 * scan catches unowned allocations and references to free clusters.
 * This is a read-only check, not a destructive repair. */
typedef struct {
    const IfsFsHostRuntimeV2 *host;
    uint8_t *owners;
    uint64_t unit;
    unsigned bitmap_seen,upcase_seen;
} ExfatCheck;

static int exfat_check_cancel(ExfatCheck *c)
{return c->host->cancelled && c->host->cancelled(c->host->context);}

static int exfat_check_claim(ExfatCheck *c,FFOBJID *obj,int root)
{
    const uint64_t size=(uint64_t)obj->objsize;
    const uint64_t wanted=size/c->unit+(size%c->unit!=0u);
    DWORD cluster=root?(DWORD)volume.dirbase:obj->sclust;
    if(!root && !wanted)return cluster?IFS_FS_CORRUPT:IFS_FS_OK;
    if(!root && wanted>volume.n_fatent-2u)return IFS_FS_CORRUPT;
    if(root){obj->stat=0u;obj->sclust=0u;}
    for(uint64_t i=0u;i<volume.n_fatent-2u;++i) {
        if(exfat_check_cancel(c))return IFS_FS_BUSY;
        if(cluster<2u || cluster>=volume.n_fatent || cluster>=0x7ffffff0u ||
           (!root && i>=wanted))return IFS_FS_CORRUPT;
        const uint8_t mask=(uint8_t)(1u<<(cluster%8u));
        if(c->owners[cluster/8u]&mask)return IFS_FS_CORRUPT;
        c->owners[cluster/8u]|=mask;
        DWORD next=ifs_exfat_native_next_cluster(obj,cluster);
        if(next==UINT32_MAX)return IFS_FS_IO_ERROR;
        if(next>=0x7ffffff8u && next<=0x7fffffffu)
            return root || i+1u==wanted?IFS_FS_OK:IFS_FS_CORRUPT;
        if(next<2u || next>=volume.n_fatent || next>=0x7ffffff0u)
            return IFS_FS_CORRUPT;
        cluster=next;
    }
    return IFS_FS_CORRUPT;
}

/* System streams have no file-directory entry sets. Their first-cluster
 * and length fields are defined directly in the 0x81/0x82 records.
 * FatFs's mount already requires a contiguous allocation bitmap. */
static int exfat_check_system(ExfatCheck *c)
{
    FFOBJID root={0};root.fs=&volume;
    DWORD cluster=(DWORD)volume.dirbase;
    uint8_t sector[4096];
    for(uint32_t step=0u;step<volume.n_fatent-2u;++step) {
        if(exfat_check_cancel(c))return IFS_FS_BUSY;
        if(cluster<2u || cluster>=volume.n_fatent || cluster>=0x7ffffff0u)
            return IFS_FS_CORRUPT;
        const uint64_t base=(uint64_t)volume.database+
            (uint64_t)(cluster-2u)*volume.csize;
        for(uint32_t n=0u;n<volume.csize;++n) {
            if(disk_read(0,sector,(LBA_t)(base+n),1u)!=RES_OK)
                return IFS_FS_IO_ERROR;
            for(uint32_t at=0u;at<device.sector_bytes;at+=32u) {
                const uint8_t *e=sector+at;
                if(!e[0])goto complete;
                if(!(e[0]&0x80u))continue;
                if(e[0]==0x81u || e[0]==0x82u) {
                    FFOBJID obj={0};obj.fs=&volume;
                    obj.sclust=exfat_le32(e+20);
                    obj.objsize=exfat_le64(e+24);
                    if(!obj.objsize || obj.sclust<2u ||
                       obj.sclust>=volume.n_fatent)return IFS_FS_CORRUPT;
                    if(e[0]==0x81u) {
                        const uint64_t expected=((uint64_t)volume.n_fatent-2u+7u)/8u;
                        if(++c->bitmap_seen!=1u || e[1] ||
                           obj.objsize!=expected ||
                           (uint64_t)volume.database+
                            (uint64_t)(obj.sclust-2u)*volume.csize!=volume.bitbase)
                            return IFS_FS_CORRUPT;
                    }else if(++c->upcase_seen!=1u || obj.objsize>131072u ||
                             (obj.objsize&1u))return IFS_FS_CORRUPT;
                    int rc=exfat_check_claim(c,&obj,0);
                    if(rc!=IFS_FS_OK)return rc;
                }else if(e[0]!=0x83u && e[0]!=0x85u && e[0]!=0xc0u &&
                         e[0]!=0xc1u && e[0]!=0xa0u) {
                    /* Do not certify an unknown allocation-bearing extension. */
                    return IFS_FS_UNSUPPORTED;
                }
            }
        }
        DWORD next=ifs_exfat_native_next_cluster(&root,cluster);
        if(next==UINT32_MAX)return IFS_FS_IO_ERROR;
        if(next>=0x7ffffff8u && next<=0x7fffffffu)goto complete;
        cluster=next;
    }
    return IFS_FS_CORRUPT;
complete:
    return c->bitmap_seen==1u && c->upcase_seen==1u?IFS_FS_OK:IFS_FS_CORRUPT;
}

static int exfat_check_tree(ExfatCheck *c,char path[4096],size_t length,unsigned depth)
{
    if(depth>64u)return IFS_FS_UNSUPPORTED;
    DIR dir={0};
    FRESULT native_rc=f_opendir(&dir,path);
    if(native_rc!=FR_OK)return exfat_common_failure(native_rc);
    int rc=exfat_check_claim(c,&dir.obj,depth==0u);
    if(rc!=IFS_FS_OK)goto done;
    if(!depth) {
        rc=exfat_check_system(c);
        if(rc!=IFS_FS_OK)goto done;
    }
    for(;;) {
        if(exfat_check_cancel(c)){rc=IFS_FS_BUSY;goto done;}
        FILINFO entry;
        native_rc=f_readdir(&dir,&entry);
        if(native_rc!=FR_OK){rc=exfat_common_failure(native_rc);goto done;}
        if(!entry.fname[0])break;
        const size_t size=strlen(entry.fname);
        if(!size || size>=sizeof(entry.fname) ||
           length+1u+size>=4096u || strchr(entry.fname,'/') ||
           strchr(entry.fname,'\\')){rc=IFS_FS_CORRUPT;goto done;}
        size_t start=length;
        if(path[start-1u]!='/')path[start++]='/';
        memcpy(path+start,entry.fname,size+1u);
        if(entry.fattrib&AM_DIR)rc=exfat_check_tree(c,path,start+size,depth+1u);
        else {
            FIL file={0};
            native_rc=f_open(&file,path,FA_READ);
            if(native_rc!=FR_OK)rc=exfat_common_failure(native_rc);
            else {
                rc=exfat_check_claim(c,&file.obj,0);
                native_rc=f_close(&file);
                if(rc==IFS_FS_OK && native_rc!=FR_OK)rc=exfat_common_failure(native_rc);
            }
        }
        path[length]='\0';
        if(rc!=IFS_FS_OK)goto done;
    }
done:
    native_rc=f_closedir(&dir);
    if(rc==IFS_FS_OK && native_rc!=FR_OK)rc=exfat_common_failure(native_rc);
    return rc;
}

int ifs_exfat_engine_check(const IfsFsHostRuntimeV2 *host,uint64_t *errors)
{
    if(!errors || !host || !selected)return IFS_FS_INVALID;
    *errors=0u;
    if(volume.fs_type!=FS_EXFAT || volume.n_fatent<3u ||
       !volume.csize || !device.sector_bytes ||
       device.sector_bytes>4096u || volume.wflag)return IFS_FS_CORRUPT;
    ExfatCheck c={.host=host,.unit=(uint64_t)volume.csize*device.sector_bytes};
    const size_t size=(size_t)(((uint64_t)volume.n_fatent+7u)/8u);
    c.owners=host->allocate(host->context,size);
    if(!c.owners)return IFS_FS_NO_MEMORY;
    memset(c.owners,0,size);
    char path[4096]="0:/";
    int rc=exfat_check_tree(&c,path,3u,0u);
    if(rc==IFS_FS_CORRUPT || rc==IFS_FS_NOT_FOUND) {
        *errors=1u;rc=IFS_FS_OK;goto done;
    }
    if(rc!=IFS_FS_OK)goto done;
    /* Allocation bits, not unused FAT entries, are the authority for
     * contiguous streams. A bad-cluster marker is the sole non-owner
     * allocation admitted by this supported native layout. */
    FFOBJID table={0};table.fs=&volume;
    uint8_t bitmap[4096];
    uint64_t cached=UINT64_MAX;
    for(uint32_t cluster=2u;cluster<volume.n_fatent;++cluster) {
        if(exfat_check_cancel(&c)){rc=IFS_FS_BUSY;goto done;}
        const uint64_t bit=cluster-2u;
        const uint64_t sector=(uint64_t)volume.bitbase+bit/8u/device.sector_bytes;
        if(sector!=cached) {
            if(disk_read(0,bitmap,(LBA_t)sector,1u)!=RES_OK){
                rc=IFS_FS_IO_ERROR;goto done;
            }
            cached=sector;
        }
        const int allocated=(bitmap[(bit/8u)%device.sector_bytes]&(1u<<(bit%8u)))!=0u;
        const int claimed=(c.owners[cluster/8u]&(1u<<(cluster%8u)))!=0u;
        if(allocated==claimed)continue;
        if(allocated) {
            DWORD next=ifs_exfat_native_next_cluster(&table,cluster);
            if(next==UINT32_MAX){rc=IFS_FS_IO_ERROR;goto done;}
            if(next==0x7ffffff7u)continue;
        }
        ++*errors;
    }
done:
    host->release(host->context,c.owners);
    if(rc!=IFS_FS_OK)*errors=0u;
    return rc;
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
