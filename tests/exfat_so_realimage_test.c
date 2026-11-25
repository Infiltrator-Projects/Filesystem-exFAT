/* SPDX-License-Identifier: GPL-3.0-or-later
 * Strict Common V2 integration over independently formatted disposable files.
 * Never opens or formats physical block devices.
 */
#define _POSIX_C_SOURCE 200809L
#include <infiltratr/filesystem_module_v2.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct { int fd; uint64_t size; uint32_t sector; unsigned writes,flushes,reads,fault_after_reads; int fault_reads; } Image;
static int read_at(void *ctx,uint64_t at,void *p,uint32_t n) {
    Image *d=ctx;
    if(d->fault_reads || (d->fault_after_reads &&
        ++d->reads>d->fault_after_reads))return -1;
    return p && d->sector && !(at % d->sector) && !(n % d->sector) &&
        at<=d->size && n<=d->size-at && at<=INT64_MAX &&
        pread(d->fd,p,n,(off_t)at)==(ssize_t)n ? 0 : -1;
}
static int write_at(void *ctx,uint64_t at,const void *p,uint32_t n) {
    Image *d=ctx;
    if (!p || !d->sector || (at % d->sector) || (n % d->sector) ||
        at>d->size || n>d->size-at || at>INT64_MAX ||
        pwrite(d->fd,p,n,(off_t)at)!=(ssize_t)n) return -1;
    ++d->writes;
    return 0;
}
static int flush_at(void *ctx) {
    Image *d=ctx;
    if(fsync(d->fd))return -1;
    ++d->flushes;return 0;
}
static void *allocate_at(void *ctx,size_t n) {(void)ctx;return malloc(n);}
static void release_at(void *ctx,void *p) {(void)ctx;free(p);}
static int fail(const char *s) {
    fprintf(stderr,"EXFAT-COMMON-V2-FAIL: %s\n",s);
    return 1;
}
static int open_image(Image *d,const char *path) {
    struct stat st;
    memset(d,0,sizeof(*d));
    d->fd=open(path,O_RDWR|O_CLOEXEC|O_NOFOLLOW);
    if(d->fd<0)return -1;
    if(fstat(d->fd,&st) || !S_ISREG(st.st_mode) || st.st_size<=0) {
        (void)close(d->fd);return -1;
    }
    d->size=(uint64_t)st.st_size;
    d->sector=512u;
    return 0;
}
static IfsFsHostRuntimeV2 host_for(Image *d,int writable) {
    IfsFsHostRuntimeV2 h={0};
    h.abi_version=IFS_FS_HOST_ABI_V2;
    h.structure_bytes=sizeof(h);
    h.capabilities=writable?
        IFS_FS_HOST_EXCLUSIVE|IFS_FS_HOST_DURABLE:IFS_FS_HOST_READ_ONLY;
    h.volume_bytes=d->size;
    h.logical_sector_bytes=512u;
    h.physical_sector_bytes=512u;
    h.maximum_transfer_bytes=65536u;
    h.context=d;
    h.read_at=read_at;
    h.write_at=writable?write_at:NULL;
    h.flush=writable?flush_at:NULL;
    h.allocate=allocate_at;
    h.release=release_at;
    return h;
}
int main(int argc,char **argv) {
    if(argc!=4)return fail("usage: exFAT.so disposable-exfat.img disposable-fat32.img");
    void *library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!library)return fail(dlerror());
    void *symbol=dlsym(library,IFS_FS_COMMON_EXPORT_V2);
    IfsFsModuleApiEntryV2 resolve=NULL;
    if(sizeof(resolve)!=sizeof(symbol))return fail("module function pointer representation");
    memcpy(&resolve,&symbol,sizeof(resolve));
    const IfsFsModuleApiV2 *api=resolve?resolve():NULL;
    if(!ifs_fs_api_v2_valid(api,"exfat") ||
       api->group_mask !=
           (IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|
            IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME)|
            IFS_FS_GROUP_BIT(IFS_FS_GROUP_DEFRAG)) ||
       (api->runtime_profiles & (IFS_FS_PROFILE_KERNEL|IFS_FS_PROFILE_FIRMWARE)))
        return fail("strict V2 descriptor not qualified");
    const IfsFsCoreOpsV2 *core =
        (const IfsFsCoreOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
    const IfsFsVolumeOpsV2 *vol =
        (const IfsFsVolumeOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_VOLUME);
    const IfsFsDefragOpsV2 *map=
        (const IfsFsDefragOpsV2 *)ifs_fs_group_v2(api,IFS_FS_GROUP_DEFRAG);
    if(!map || !map->map || map->header.capabilities!=IFS_FS_DEFRAG_MAP ||
       !core || !vol || core->header.capabilities!=IFS_FS_CORE_ALL ||
       vol->header.capabilities!=
         (IFS_FS_VOLUME_QUERY|IFS_FS_VOLUME_SYNC|IFS_FS_VOLUME_FORMAT|
          IFS_FS_VOLUME_LABEL_GET|IFS_FS_VOLUME_LABEL_SET) || !vol->label || !vol->set_label)
        return fail("native CORE or VOLUME capabilities incorrect");

    Image d;
    if(open_image(&d,argv[3]))return fail("cannot open foreign FAT32 fixture");
    IfsFsHostRuntimeV2 host=host_for(&d,0);
    uint32_t variant=123u;
    if(!ifs_fs_host_v2_valid(&host) ||
       core->probe(&host,&variant)!=IFS_FS_NOT_FOUND || variant!=0u || d.writes)
        return fail("wrong filesystem accepted or foreign data modified");
    if(close(d.fd))return fail("foreign media close failed");

    if(open_image(&d,argv[2]))return fail("cannot open external exFAT fixture");
    host=host_for(&d,1);
    variant=123u;
    d.fault_reads=1;
    if(core->probe(&host,&variant)!=IFS_FS_IO_ERROR || variant!=0u)
        return fail("V2 probe retained stale variant after device I/O failure");
    d.fault_reads=0;
    if(!ifs_fs_host_v2_valid(&host) ||
       core->probe(&host,&variant)!=IFS_FS_OK || variant!=64u)
        return fail("exFAT probe rejected independent mkfs media");
    IfsFsVolumeInfoV2 info={0};
    info.structure_bytes=sizeof(info);
    void *mounted=NULL;
    /* The geometry and checksum consume 13 reads before the native FatFs
     * attach. Fail only that native read and preserve I/O_ERROR. */
    d.reads=0u;d.fault_after_reads=13u;
    if(core->mount(&host,&mounted,&info)!=IFS_FS_IO_ERROR || mounted)
        return fail("V2 mount hid a native device read error as corruption");
    d.fault_after_reads=0u;d.reads=0u;
    /* The prefix allocation has no room for newer optional host members. */
    volatile size_t short_bytes=IFS_FS_HOST_V2_BASE_BYTES;
    IfsFsHostRuntimeV2 *short_host=malloc(short_bytes);
    if(!short_host)return fail("short V2 host allocation");
    memcpy(short_host,&host,IFS_FS_HOST_V2_BASE_BYTES);
    short_host->structure_bytes=IFS_FS_HOST_V2_BASE_BYTES;
    if(core->mount(short_host,&mounted,&info)!=IFS_FS_OK || !mounted ||
       core->unmount(mounted)!=IFS_FS_OK)
        return fail("mount with original short V2 host");
    free(short_host);
    mounted=NULL;info.structure_bytes=sizeof(info);
    if(core->mount(&host,&mounted,&info)!=IFS_FS_OK || !mounted ||
       info.variant!=64u || !info.block_bytes || info.free_bytes>info.total_bytes)
        return fail("V2 exFAT mount");
    IfsFsVolumeInfoV2 live_info={0};
    live_info.structure_bytes=sizeof(live_info);
    if(vol->query(mounted,&live_info)!=IFS_FS_OK ||
       live_info.total_bytes!=info.total_bytes ||
       live_info.free_bytes!=info.free_bytes)
        return fail("V2 mounted allocation report");

    unsigned char data[8192],back[8192];
    for(unsigned i=0;i<sizeof(data);++i)data[i]=(unsigned char)(i*31u+7u);
    if(core->create(mounted,"/TESTDIR",IFS_FS_ENTRY_DIRECTORY)!=IFS_FS_OK ||
       core->create(mounted,"/TESTDIR/INPUT.BIN",IFS_FS_ENTRY_FILE)!=IFS_FS_OK ||
       core->write(mounted,"/TESTDIR/INPUT.BIN",0u,data,sizeof(data))!=IFS_FS_OK ||
       core->read(mounted,"/TESTDIR/INPUT.BIN",0u,back,sizeof(back))!=IFS_FS_OK ||
       memcmp(back,data,sizeof(data)))
        return fail("V2 create, write or multicluster read");
    char name[768]={0};
    IfsFsEntryV2 entry={0};
    entry.structure_bytes=sizeof(entry);
    entry.name=name;entry.name_capacity=sizeof(name);
    if(core->rename(mounted,"/TESTDIR/INPUT.BIN","/TESTDIR/OUTPUT.BIN")!=IFS_FS_OK ||
       core->stat(mounted,"/TESTDIR/OUTPUT.BIN",&entry)!=IFS_FS_OK ||
       entry.size_bytes!=sizeof(data) ||
       core->stat(mounted,"/TESTDIR/NO-SUCH-FILE",&entry)!=IFS_FS_NOT_FOUND ||
       core->read(mounted,"/TESTDIR/NO-SUCH-FILE",0u,back,1u)!=IFS_FS_NOT_FOUND ||
       core->read(mounted,"/TESTDIR/OUTPUT.BIN",8191u,back,2u)!=IFS_FS_INVALID ||
       core->read(mounted,"/TESTDIR/OUTPUT.BIN",UINT64_MAX,back,1u)==IFS_FS_OK ||
       core->create(mounted,"/TESTDIR/CHILD",IFS_FS_ENTRY_DIRECTORY)!=IFS_FS_OK ||
       core->rename(mounted,"/TESTDIR","/TESTDIR/CHILD/LOOP")==IFS_FS_OK)
        return fail("V2 rename, missing paths, bounds or recursive directory safety");
    /* The native provider's physical extents must account for exactly the
     * file length, with the actual payload at the reported physical offsets.
     * Exercise a one-extent page and an explicit END, without any writes. */
    {
        uint64_t logical=0u,first=0u;
        unsigned writes=d.writes;
        for(;;) {
            IfsFsExtentV2 e={0};
            uint32_t got=123u;
            int status=map->map(mounted,"/TESTDIR/OUTPUT.BIN",first,&e,1u,&got);
            if(status==IFS_FS_END) {
                if(got || logical!=sizeof(data))return fail("exFAT extent EOF");
                break;
            }
            if(status!=IFS_FS_OK || got!=1u || e.logical_offset!=logical ||
               !e.length || e.length>sizeof(data)-logical || e.flags || e.reserved ||
               e.physical_offset>d.size || e.length>d.size-e.physical_offset ||
               pread(d.fd,back,(size_t)e.length,(off_t)e.physical_offset)!=(ssize_t)e.length ||
               memcmp(back,data+logical,(size_t)e.length))
                return fail("exFAT provider extent does not identify native payload");
            logical+=e.length;++first;
        }
        if(d.writes!=writes)return fail("exFAT read-only extent mapping wrote media");
    }
    /* Inject an actual host I/O failure after a successful mount. Missing
     * files and hardware errors must not be reported interchangeably. */
    d.fault_reads=1;
    int fault_result=core->read(mounted,"/TESTDIR/OUTPUT.BIN",4096u,back,4096u);
    d.fault_reads=0;
    if(fault_result!=IFS_FS_IO_ERROR)
        return fail("V2 read failed to preserve an injected device I/O error");
    if(core->read(mounted,"/TESTDIR/ABSENT.BIN",0u,back,1u)!=IFS_FS_NOT_FOUND)
        return fail("V2 stale device fault contaminated a subsequent missing file");
    if(core->truncate(mounted,"/TESTDIR/OUTPUT.BIN",4096u)!=IFS_FS_OK ||
       vol->sync(mounted)!=IFS_FS_OK ||
       core->unmount(mounted)!=IFS_FS_OK || !d.writes || !d.flushes)
        return fail("V2 truncate, flush or unmount");
    /* Mount the SAME 512-byte-sector image over a strict 4 KiB host:
     * the block layer refuses all unaligned reads, and no writes are
     * permitted. This is an actual namespace read, not probe alone. */
    Image strict_4k=d;
    strict_4k.sector=4096u;
    IfsFsHostRuntimeV2 boot_host=host_for(&strict_4k,0);
    boot_host.logical_sector_bytes=4096u;
    boot_host.physical_sector_bytes=4096u;
    mounted=NULL;info.structure_bytes=sizeof(info);
    if(core->probe(&boot_host,&variant)!=IFS_FS_OK || variant!=64u ||
       core->mount(&boot_host,&mounted,&info)!=IFS_FS_OK || !mounted ||
       core->read(mounted,"/TESTDIR/OUTPUT.BIN",0u,back,4096u)!=IFS_FS_OK ||
       memcmp(back,data,4096u) ||
       core->unmount(mounted)!=IFS_FS_OK ||
       strict_4k.writes!=d.writes)
        return fail("512-byte exFAT image over a 4 KiB read-only BOOT host");
    mounted=NULL; info.structure_bytes=sizeof(info);
    if(core->mount(&host,&mounted,&info)!=IFS_FS_OK ||
       core->read(mounted,"/TESTDIR/OUTPUT.BIN",0u,back,4096u)!=IFS_FS_OK ||
       memcmp(back,data,4096u) ||
       core->stat(mounted,"/TESTDIR/OUTPUT.BIN",&entry)!=IFS_FS_OK ||
       entry.size_bytes!=4096u)
        return fail("V2 remount persistence");
    if(core->remove(mounted,"/TESTDIR/OUTPUT.BIN")!=IFS_FS_OK ||
       core->remove(mounted,"/TESTDIR/CHILD")!=IFS_FS_OK ||
       core->remove(mounted,"/TESTDIR")!=IFS_FS_OK ||
       core->unmount(mounted)!=IFS_FS_OK)
        return fail("V2 deletion and final unmount");
    /* V2 format is permitted only after unmount, on this disposable image. */
    unsigned before=d.writes;
    if(vol->format(&host,32u,NULL,0u)!=IFS_FS_UNSUPPORTED ||
       d.writes!=before ||
       vol->format(&host,64u,NULL,1u)!=IFS_FS_UNSUPPORTED ||
       d.writes!=before ||
       vol->format(&host,64u,NULL,0u)!=IFS_FS_OK ||
       d.writes<=before)
        return fail("exclusive V2 formatting contract");
    mounted=NULL;info.structure_bytes=sizeof(info);
    if(core->mount(&host,&mounted,&info)!=IFS_FS_OK ||
       core->create(mounted,"/FORMATTED.BIN",IFS_FS_ENTRY_FILE)!=IFS_FS_OK ||
       core->write(mounted,"/FORMATTED.BIN",0u,data,sizeof(data))!=IFS_FS_OK ||
       core->unmount(mounted)!=IFS_FS_OK)
        return fail("V2 formatted media cannot be remounted and written");

    host=host_for(&d,0);
    if(vol->format(&host,64u,NULL,0u)!=IFS_FS_ACCESS)
        return fail("read-only format incorrectly permitted");
    /* Reformat the disposable image with a strict 4 KiB-sector host. Both
     * callback alignment and real mount/write are checked, not just a probe. */
    d.sector=4096u;
    host=host_for(&d,1);
    host.logical_sector_bytes=host.physical_sector_bytes=d.sector;
    if(vol->format(&host,64u,NULL,0u)!=IFS_FS_OK ||
       core->probe(&host,&variant)!=IFS_FS_OK || variant!=64u)
        return fail("4 KiB-sector V2 format or probe");
    mounted=NULL;info.structure_bytes=sizeof(info);
    if(core->mount(&host,&mounted,&info)!=IFS_FS_OK ||
       core->create(mounted,"/FOURK.BIN",IFS_FS_ENTRY_FILE)!=IFS_FS_OK ||
       core->write(mounted,"/FOURK.BIN",0u,data,sizeof(data))!=IFS_FS_OK ||
       core->unmount(mounted)!=IFS_FS_OK)
        return fail("4 KiB-sector V2 mount or write");
    if(close(d.fd)!=0 || dlclose(library)!=0)
        return fail("resource cleanup");
    puts("EXFAT-COMMON-V2-REALIMAGE-ROUNDTRIP-PASS");
    return 0;
}
