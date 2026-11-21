/* SPDX-License-Identifier: GPL-3.0-or-later
 * Execute the real production BOOT ELF loader against a disposable exFAT.
 * The fixture is written by the independent hosted Common V2 image test.
 * This tests the freestanding code, not dlopen() or a private FatFs hook.
 */
#define _GNU_SOURCE
#include "so_elf.h"
#include <infiltratr/filesystem_module_v2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define ENSURE(condition) do { if (!(condition)) { \
    fprintf(stderr, "EXFAT-BOOT-V2 FAIL line %d: %s\n", __LINE__, #condition); \
    return 1; \
} } while (0)
typedef struct {
    FILE *file;
    uint64_t bytes;
    uint32_t sector;
    unsigned reads;
    int fault_reads;
} Disk;
static void *elf_allocate(void *unused, size_t bytes)
{
    (void)unused;
    void *p=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    return p==MAP_FAILED?NULL:p;
}
static void elf_release(void *unused, void *ptr, size_t bytes)
{
    (void)unused;
    (void)munmap(ptr,bytes);
}
static int elf_protect(void *unused, void *ptr, size_t bytes, uint32_t flags)
{
    (void)unused;
    return mprotect(ptr,bytes,((flags&4u)?PROT_READ:0) |
                              ((flags&2u)?PROT_WRITE:0) |
                              ((flags&1u)?PROT_EXEC:0));
}
static int read_sectors(void *ctx, uint64_t pos, void *dst, uint32_t count)
{
    Disk *disk=ctx;
    if(!dst || !count || !disk->sector || pos%disk->sector ||
       count%disk->sector || pos>disk->bytes ||
       count>disk->bytes-pos || pos>INT64_MAX)
        return -1;
    ++disk->reads;
    if(disk->fault_reads)return -1;
    return fseeko(disk->file,(off_t)pos,SEEK_SET)==0 &&
           fread(dst,1,count,disk->file)==count?0:-1;
}
static void *host_alloc(void *ctx,size_t bytes)
{
    (void)ctx;
    return malloc(bytes);
}
static void host_free(void *ctx,void *ptr)
{
    (void)ctx;
    free(ptr);
}
int main(int argc,char **argv)
{
    ENSURE(argc==3 || (argc==4 && strcmp(argv[3],"--reject-corrupt")==0));
    FILE *binary=fopen(argv[1],"rb");
    ENSURE(binary!=NULL);
    struct stat st;
    ENSURE(fstat(fileno(binary),&st)==0 && S_ISREG(st.st_mode) &&
           st.st_size>0 && st.st_size<=BOOT_SO_MAX_FILE);
    size_t binary_bytes=(size_t)st.st_size;
    void *payload=malloc(binary_bytes);
    ENSURE(payload!=NULL && fread(payload,1,binary_bytes,binary)==binary_bytes);
    ENSURE(fclose(binary)==0);
    ENSURE(BootSoValidate(payload,binary_bytes)==0);
    BootSoMemory memory={0,elf_allocate,elf_release,elf_protect};
    BootSoImage image={0};
    ENSURE(BootSoLoad(payload,binary_bytes,&memory,&image)==0);
    free(payload);
    ENSURE(strcmp(image.format_id,"exfat")==0 && image.core!=NULL);
    const IfsFsCoreOpsV2 *core=image.core;
    const uint64_t required=IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|
       IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ;
    ENSURE(core->header.capabilities==required &&
           core->probe && core->mount && core->unmount &&
           core->stat && core->directory_entry && core->read &&
           !core->write && !core->create && !core->remove &&
           !core->rename && !core->truncate);
    Disk disk={fopen(argv[2],"rb"),0,4096u,0};
    ENSURE(disk.file!=NULL);
    ENSURE(fstat(fileno(disk.file),&st)==0 && S_ISREG(st.st_mode) &&
           st.st_size>=8192);
    disk.bytes=(uint64_t)st.st_size;
    IfsFsHostRuntimeV2 host={0};
    host.abi_version=IFS_FS_HOST_ABI_V2;
    host.structure_bytes=sizeof(host);
    host.capabilities=IFS_FS_HOST_READ_ONLY;
    host.volume_bytes=disk.bytes;
    host.logical_sector_bytes=host.physical_sector_bytes=disk.sector;
    host.maximum_transfer_bytes=65536u;
    host.context=&disk;
    host.read_at=read_sectors;
    host.allocate=host_alloc;
    host.release=host_free;
    ENSURE(ifs_fs_host_v2_valid(&host));
    uint32_t variant=0;
    const int probed=core->probe(&host,&variant);
    if(argc==4) {
        ENSURE(probed==IFS_FS_CORRUPT);
        IfsFsVolumeInfoV2 bad_info={0};
        bad_info.structure_bytes=sizeof(bad_info);
        void *bad_volume=NULL;
        ENSURE(core->mount(&host,&bad_volume,&bad_info)==IFS_FS_CORRUPT &&
               bad_volume==NULL);
        ENSURE(fclose(disk.file)==0);
        BootSoUnload(&image);
        puts("EXFAT-BOOT-COMMON-V2-CORRUPT-CHECKSUM-REFUSED");
        return 0;
    }
    ENSURE(probed==IFS_FS_OK && variant==64);
    IfsFsVolumeInfoV2 volume_info={0};
    volume_info.structure_bytes=sizeof(volume_info);
    void *volume=NULL;
    ENSURE(core->mount(&host,&volume,&volume_info)==IFS_FS_OK &&
           volume!=NULL && volume_info.variant==64);
    char name[768]={0};
    IfsFsEntryV2 entry={0};
    entry.structure_bytes=sizeof(entry);
    entry.name=name;
    entry.name_capacity=sizeof(name);
    ENSURE(core->stat(volume,"/",&entry)==IFS_FS_OK &&
           entry.type==IFS_FS_ENTRY_DIRECTORY);
    ENSURE(core->stat(volume,"/MISSING.BIN",&entry)==IFS_FS_NOT_FOUND);
    ENSURE(core->stat(volume,"/FOURK.BIN",&entry)==IFS_FS_OK &&
           entry.type==IFS_FS_ENTRY_FILE && entry.size_bytes==8192u);
    int found=0;
    for(uint32_t i=0;i<256u;++i) {
        int result=core->directory_entry(volume,"/",i,&entry);
        if(result==IFS_FS_END)break;
        ENSURE(result==IFS_FS_OK);
        if(strcmp(entry.name,"FOURK.BIN")==0)found=1;
    }
    ENSURE(found);
    uint8_t data[8192]={0};
    /* The same loaded freestanding reader must preserve distinct error
     * classes for absent files and invalid reads, not report generic I/O. */
    ENSURE(core->read(volume,"/MISSING.BIN",0,data,1u)==IFS_FS_NOT_FOUND);
    ENSURE(core->read(volume,"/FOURK.BIN",8191u,data,2u)==IFS_FS_INVALID);
    ENSURE(core->read(volume,"/FOURK.BIN",0,data,sizeof(data))==IFS_FS_OK);
    for(unsigned i=0;i<sizeof(data);++i)
        ENSURE(data[i]==(uint8_t)(i*31u+7u));
    memset(data,0,sizeof(data));
    ENSURE(core->read(volume,"/FOURK.BIN",17,data,177)==IFS_FS_OK);
    for(unsigned i=0;i<177;++i)
        ENSURE(data[i]==(uint8_t)((i+17u)*31u+7u));
    ENSURE(core->read(volume,"/FOURK.BIN",UINT64_MAX,data,1u)!=IFS_FS_OK);
    disk.fault_reads=1;
    int io_status=core->read(volume,"/FOURK.BIN",4096u,data,4096u);
    disk.fault_reads=0;
    ENSURE(io_status==IFS_FS_IO_ERROR);
    ENSURE(core->unmount(volume)==IFS_FS_OK);
    ENSURE(disk.reads>0 && fclose(disk.file)==0);
    BootSoUnload(&image);
    puts("EXFAT-BOOT-COMMON-V2-REAL-DIRECTORY-FILE-READ-PASS");
    return 0;
}
