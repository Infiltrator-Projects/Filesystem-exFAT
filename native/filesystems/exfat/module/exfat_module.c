/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent exFAT shared-object entry point. Compile the same audited FatFs
 * source into this one format-specific binary; never proxy to Linux exfat.ko.
 */
#if !defined(IFS_FATFS_ONLY) || IFS_FATFS_ONLY != 64
#error "This translation unit is only for the exFAT engine"
#endif
#include "../../fat/portable/fatfs_module.c"

#include "exfat_api.h"

/* File sizes on exFAT are 64-bit. FAT12/16's V3 compatibility interface is
 * insufficient; never truncate the genuine FSIZE_t when exposing metadata. */
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
__attribute__((used,section(".ifsmeta")))
static const char exfat_v5_identity[]="IFS-MODULE:exfat:5";
static const IfsExfatModuleV5 exfat_v5={
    IFS_EXFAT_ABI_VERSION_V5,sizeof(IfsExfatModuleV5),"exfat",
    IFS_EXFAT_CAP_READ64|IFS_EXFAT_CAP_WRITE64|
    IFS_EXFAT_CAP_TRUNCATE64|IFS_EXFAT_CAP_STAT64,
    &module_v4,exfat_read64,exfat_write64,exfat_truncate64,exfat_stat64
};
__attribute__((visibility("default")))
const IfsExfatModuleV5 *infiltrator_filesystem_module_v5(void)
{
    return &exfat_v5;
}
