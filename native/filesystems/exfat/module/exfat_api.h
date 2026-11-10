/* SPDX-License-Identifier: GPL-3.0-or-later
 * Source-private exFAT engine entry points, never a loadable ABI.
 * The one public filesystem descriptor is Common V2.
 */
#ifndef IFS_EXFAT_ENGINE_INTERNAL_H
#define IFS_EXFAT_ENGINE_INTERNAL_H
#include <stdint.h>
#include "../../../portable/fatfs/fatfs_module.h"
typedef struct {
    uint64_t size_bytes;
    uint8_t attributes;
    char name[768];
} IfsExfatEntry64;
int ifs_exfat_engine_mount(const IfsFatfsIo *);
int ifs_exfat_engine_unmount(void);
int ifs_exfat_engine_create(const char *);
int ifs_exfat_engine_mkdir(const char *);
int ifs_exfat_engine_remove(const char *);
int ifs_exfat_engine_rename(const char *,const char *);
int ifs_exfat_engine_directory_entry(const char *,uint32_t,IfsFatfsEntry *);
int ifs_exfat_engine_read64(const char *,uint64_t,void *,uint32_t);
int ifs_exfat_engine_write64(const char *,uint64_t,const void *,uint32_t);
int ifs_exfat_engine_truncate64(const char *,uint64_t);
int ifs_exfat_engine_stat64(const char *,IfsExfatEntry64 *);
int ifs_exfat_engine_space(uint64_t *,uint64_t *);
int ifs_exfat_engine_format(const IfsFatfsIo *,uint32_t);
#endif
