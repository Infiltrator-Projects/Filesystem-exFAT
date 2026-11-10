/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent exFAT shared-object entry point. Compile the same audited FatFs
 * source into this one format-specific binary; never proxy to Linux exfat.ko.
 */
#if !defined(IFS_FATFS_ONLY) || IFS_FATFS_ONLY != 64
#error "This translation unit is only for the exFAT engine"
#endif
#include "../../../portable/fatfs/fatfs_module.c"

#include "exfat_api.h"

/* exFAT has native 64-bit offsets and file sizes; preserve FSIZE_t. */
static int exfat_read64(const char *path,uint64_t offset,void *out,uint32_t count)
{
    FIL file;UINT read=0;int good;
    if(!selected||!path||(!out&&count)||f_open(&file,path,FA_READ)!=FR_OK)
        return -1;
    good=offset<=(uint64_t)f_size(&file) &&
         count<=(uint64_t)f_size(&file)-offset &&
         f_lseek(&file,(FSIZE_t)offset)==FR_OK &&
         f_tell(&file)==(FSIZE_t)offset &&
         f_read(&file,out,count,&read)==FR_OK && read==count;
    if(f_close(&file)!=FR_OK)good=0;
    return good?0:-1;
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
    if(!selected||!path||!out||f_stat(path,&info)!=FR_OK)return -1;
    out->size_bytes=(uint64_t)info.fsize;
    out->attributes=info.fattrib;
    copy_name(out->name,info.fname);
    return 0;
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
