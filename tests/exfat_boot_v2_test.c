/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exercise the exact freestanding ELF from an exFAT release candidate through
 * Common V2 on an isolated host. The consumer's validator and OVMF run in CI.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <infiltratr/filesystem_module_v2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ENSURE(condition) do { if (!(condition)) { \
    fprintf(stderr, "EXFAT-FREESTANDING-V2 FAIL line %d: %s\n", __LINE__, #condition); \
    return 1; \
} } while (0)
typedef struct {
    FILE *file;
    uint64_t bytes;
    uint32_t sector;
    unsigned reads;
    int fault_reads;
} Disk;
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
static int forbidden_write(void *ctx,uint64_t pos,const void *src,uint32_t bytes)
{
    (void)ctx;(void)pos;(void)src;(void)bytes;
    return -1;
}
static int forbidden_flush(void *ctx)
{
    (void)ctx;
    return -1;
}
int main(int argc,char **argv)
{
    ENSURE(argc==3 || (argc==4 && strcmp(argv[3],"--reject-corrupt")==0));
    void *image = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!image) fputs(dlerror(), stderr);
    ENSURE(image);
    void *symbol = dlsym(image, IFS_FS_COMMON_EXPORT_V2);
    IfsFsModuleApiEntryV2 get_api;
    ENSURE(symbol && sizeof(symbol) == sizeof(get_api));
    memcpy(&get_api, &symbol, sizeof(get_api));
    const IfsFsModuleApiV2 *api = get_api();
    ENSURE(ifs_fs_api_v2_valid(api, "exfat") &&
           (api->runtime_profiles & IFS_FS_PROFILE_FIRMWARE) &&
           (api->runtime_profiles & IFS_FS_PROFILE_FREESTANDING));
    const IfsFsCoreOpsV2 *core = (const void *)ifs_fs_group_v2(api, IFS_FS_GROUP_CORE);
    struct stat st;
    const uint64_t required=IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|
       IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ;
    ENSURE(core->header.capabilities==required &&
           core->probe && core->mount && core->unmount &&
           core->stat && core->directory_entry && core->read &&
           !core->write && !core->create && !core->remove &&
           !core->rename && !core->truncate);
    Disk disk = {.file = fopen(argv[2], "rb"), .sector = 4096u};
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
    /* A writable host must be refused before the firmware reads media. */
    IfsFsHostRuntimeV2 writable=host;
    writable.capabilities=IFS_FS_HOST_EXCLUSIVE|IFS_FS_HOST_DURABLE;
    writable.write_at=forbidden_write;
    writable.flush=forbidden_flush;
    ENSURE(ifs_fs_host_v2_valid(&writable));
    IfsFsVolumeInfoV2 denied_info={.structure_bytes=sizeof(denied_info)};
    void *denied=(void *)(uintptr_t)1u;
    unsigned before=disk.reads;
    ENSURE(core->mount(&writable,&denied,&denied_info)==IFS_FS_ACCESS &&
           denied==NULL && disk.reads==before);
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
        ENSURE(dlclose(image) == 0);
        puts("EXFAT-FREESTANDING-COMMON-V2-CORRUPT-CHECKSUM-REFUSED");
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
    ENSURE(dlclose(image) == 0);
    puts("EXFAT-FREESTANDING-COMMON-V2-REAL-DIRECTORY-FILE-READ-PASS");
    return 0;
}
