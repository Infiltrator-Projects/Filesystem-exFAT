/* SPDX-License-Identifier: GPL-3.0-or-later
 * Private, source-owned FatFs I/O structures. This is NOT a public ABI.
 * Filesystem operations are exposed only through Common V2.
 */
#ifndef IFS_EXFAT_FATFS_ENGINE_TYPES_H
#define IFS_EXFAT_FATFS_ENGINE_TYPES_H
#include <stdint.h>
typedef struct {
    void *user;
    uint64_t partition_bytes;
    uint32_t sector_bytes;
    int (*read_at)(void *,uint64_t,void *,uint32_t);
    int (*write_at)(void *,uint64_t,const void *,uint32_t);
    int (*flush)(void *);
    uint32_t (*fat_timestamp)(void *);
} IfsFatfsIo;
typedef struct {
    char name[768];
    uint32_t bytes;
    uint8_t attributes;
} IfsFatfsEntry;
#endif
