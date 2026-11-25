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

/* Test-only rollback saves original bytes of each actual host write.
 * This retains a real regular-file image without recopying gigabytes of
 * untouched media for each interruption boundary. Recovery writes are
 * captured as well; rollback is never an operation of the provider. */
typedef struct Undo { struct Undo *prev;uint64_t at;uint32_t n;uint8_t bytes[]; } Undo;
typedef struct {
    int fd;uint64_t size;uint32_t sector;
    unsigned writes,flushes,reads,fault_after_reads;
    int fault_reads,capture;
    unsigned ops,cut,mode;
    Undo *undo;
} Image;
static int save_original(Image *d,uint64_t at,uint32_t n)
{
    if(!d->capture)return 0;
    Undo *u=malloc(sizeof(*u)+n);
    if(!u)return -1;
    if(pread(d->fd,u->bytes,n,(off_t)at)!=(ssize_t)n){free(u);return -1;}
    u->at=at;u->n=n;u->prev=d->undo;d->undo=u;return 0;
}
static int rollback_image(Image *d)
{
    d->capture=0;d->cut=0;d->ops=0;
    while(d->undo){
        Undo *u=d->undo;d->undo=u->prev;
        int bad=pwrite(d->fd,u->bytes,u->n,(off_t)u->at)!=(ssize_t)u->n;
        free(u);if(bad)return -1;
    }
    return fsync(d->fd);
}
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
        at>d->size || n>d->size-at || at>INT64_MAX) return -1;
    const int cut=d->capture && ++d->ops==d->cut;
    if(cut && d->mode==0u)return -1;
    if(cut && d->mode==3u)return 0; /* device acknowledges a dropped write */
    uint32_t actual=cut && d->mode==1u?n/2u:n;
    if(save_original(d,at,actual) ||
       pwrite(d->fd,p,actual,(off_t)at)!=(ssize_t)actual)return -1;
    ++d->writes;
    return cut?-1:0;
}
static int flush_at(void *ctx) {
    Image *d=ctx;
    const int cut=d->capture && ++d->ops==d->cut;
    if(cut && d->mode==0u)return -1;
    if(fsync(d->fd))return -1;
    if(cut && d->mode!=3u)return -1;
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
    if(!map || !map->map || map->header.capabilities!=(IFS_FS_DEFRAG_MAP|IFS_FS_DEFRAG_PLAN|IFS_FS_DEFRAG_EXECUTE|IFS_FS_DEFRAG_VERIFY|IFS_FS_DEFRAG_RECOVER) || !map->plan || !map->execute || !map->verify || !map->recover || !map->release_plan ||
       !core || !vol || core->header.capabilities!=IFS_FS_CORE_ALL ||
       vol->header.capabilities!=
         (IFS_FS_VOLUME_QUERY|IFS_FS_VOLUME_SYNC|IFS_FS_VOLUME_FORMAT|
          IFS_FS_VOLUME_LABEL_GET|IFS_FS_VOLUME_LABEL_SET|IFS_FS_VOLUME_CHECK) || !vol->label || !vol->set_label || !vol->check || vol->repair)
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
        const char *dirs[]={"/","/TESTDIR","/TESTDIR/CHILD"};
        for(unsigned j=0u;j<sizeof dirs/sizeof dirs[0];++j) {
            IfsFsExtentV2 e={0};
            uint32_t got=0u;
            if(map->map(mounted,dirs[j],0u,&e,1u,&got)!=IFS_FS_OK ||
               got!=1u || e.logical_offset || e.length!=info.block_bytes ||
               e.physical_offset>d.size || e.length>d.size-e.physical_offset ||
               map->map(mounted,dirs[j],1u,&e,1u,&got)!=IFS_FS_END || got)
                return fail("exFAT physical directory allocation and terminal page");
        }
        if(d.writes!=writes)return fail("exFAT read-only extent mapping wrote media");
    }
    /* Native ownership check covers the actual external mkfs image,
     * nested directories and system streams without writing media. */
    uint64_t errors=UINT64_MAX;
    unsigned writes_before_check=d.writes;
    if(vol->check(mounted,0u,&errors)!=IFS_FS_OK || errors ||
       d.writes!=writes_before_check)
        return fail("clean native exFAT ownership check");
    /* Independently alter the allocation bitmap on the disposable disk.
     * Cold-mount each corruption, require CHECK to detect it without any
     * media mutation, and restore the original bitmap before continuing. */
    {
        uint8_t boot[512],system[32],original;
        IfsFsExtentV2 root={0},file={0}; uint32_t count=0u;
        if(pread(d.fd,boot,sizeof boot,0)!=(ssize_t)sizeof boot ||
           map->map(mounted,"/",0u,&root,1u,&count)!=IFS_FS_OK || count!=1u ||
           map->map(mounted,"/TESTDIR/OUTPUT.BIN",0u,&file,1u,&count)!=IFS_FS_OK || count!=1u)
            return fail("bitmap corruption fixture geometry");
        uint64_t heap=0u,clusters=0u;
        for(unsigned k=0u;k<4u;++k) {
            heap|=(uint64_t)boot[88u+k]<<(8u*k);
            clusters|=(uint64_t)boot[92u+k]<<(8u*k);
        }
        heap*=UINT64_C(1)<<boot[108];
        uint64_t bitmap=UINT64_MAX,bitmap_bytes=0u;
        for(uint64_t off=0u;off+32u<=root.length;off+=32u) {
            if(pread(d.fd,system,32u,(off_t)(root.physical_offset+off))!=32)
                return fail("read independent bitmap directory record");
            if(system[0]==0x81u) {
                uint32_t first=0u;
                for(unsigned k=0u;k<4u;++k)first|=(uint32_t)system[20u+k]<<(8u*k);
                for(unsigned k=0u;k<8u;++k)bitmap_bytes|=(uint64_t)system[24u+k]<<(8u*k);
                if(first<2u)return fail("invalid bitmap cluster");
                bitmap=heap+(uint64_t)(first-2u)*info.block_bytes;
                break;
            }
        }
        if(bitmap==UINT64_MAX || bitmap_bytes!=(clusters+7u)/8u ||
           bitmap>d.size || bitmap_bytes>d.size-bitmap ||
           file.physical_offset<heap || (file.physical_offset-heap)%info.block_bytes)
            return fail("invalid independent allocation bitmap bounds");
        uint64_t referenced=(file.physical_offset-heap)/info.block_bytes;
        uint64_t orphan=UINT64_MAX;
        for(uint64_t k=0u;k<clusters;++k) {
            if(pread(d.fd,&original,1u,(off_t)(bitmap+k/8u))!=1)
                return fail("read allocation bitmap");
            if(!(original&(1u<<(k%8u)))){orphan=k;break;}
        }
        if(orphan==UINT64_MAX || referenced>=clusters ||
           core->unmount(mounted)!=IFS_FS_OK)
            return fail("no free cluster for independent orphan fixture");
        uint64_t bits[2]={orphan,referenced};
        for(unsigned k=0u;k<2u;++k) {
            uint64_t off=bitmap+bits[k]/8u;
            uint8_t mask=(uint8_t)(1u<<(bits[k]%8u));
            if(pread(d.fd,&original,1u,(off_t)off)!=1 ||
               (!!(original&mask))!=(k!=0u))
                return fail("bitmap fixture bit has wrong original state");
            uint8_t damaged=(uint8_t)(original^mask);
            if(pwrite(d.fd,&damaged,1u,(off_t)off)!=1 || fsync(d.fd))
                return fail("inject bitmap corruption");
            mounted=NULL;info.structure_bytes=sizeof(info);
            if(core->mount(&host,&mounted,&info)!=IFS_FS_OK)
                return fail("mount deliberately inconsistent allocation bitmap");
            unsigned writes=d.writes;errors=0u;
            if(vol->check(mounted,0u,&errors)!=IFS_FS_OK || errors!=1u ||
               d.writes!=writes)
                return fail("native check missed an orphan or falsely free live cluster");
            if(core->unmount(mounted)!=IFS_FS_OK ||
               pwrite(d.fd,&original,1u,(off_t)off)!=1 || fsync(d.fd))
                return fail("restore bitmap corruption fixture");
        }
        mounted=NULL;info.structure_bytes=sizeof(info);
        if(core->mount(&host,&mounted,&info)!=IFS_FS_OK ||
           vol->check(mounted,0u,&errors)!=IFS_FS_OK || errors ||
           core->read(mounted,"/TESTDIR/OUTPUT.BIN",0u,back,sizeof(back))!=IFS_FS_OK ||
           memcmp(back,data,sizeof(data)))
            return fail("restored allocation and full original payload");
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

    /* The real writer must physically relocate fragmented streams on
     * separately created disposable native images for both policies. */
    for(uint32_t policy=0u;policy<=10u;policy+=10u){
        if(vol->format(&host,64u,NULL,0u)!=IFS_FS_OK)return fail("native relocation format");
        mounted=NULL;info.structure_bytes=sizeof(info);
        if(core->mount(&host,&mounted,&info)!=IFS_FS_OK ||
           info.block_bytes<512u || info.block_bytes>65536u)
            return fail("native relocation fixture mount");
        uint32_t unit=info.block_bytes;
        uint8_t *expected=malloc(4u*unit),*readback=malloc(4u*unit);
        if(!expected||!readback)return fail("relocation payload allocation");
        for(uint32_t j=0u;j<4u*unit;++j)expected[j]=(uint8_t)(j*19u+j/256u+policy);
        const char *names[]={"/A.BIN","/B.BIN"};
        for(unsigned i=0u;i<2u;++i)
            if(core->create(mounted,names[i],IFS_FS_ENTRY_FILE)!=IFS_FS_OK)
                return fail("relocation fixture create");
        for(unsigned j=0u;j<4u;++j)for(unsigned i=0u;i<2u;++i)
            if(core->write(mounted,names[i],(uint64_t)j*unit,expected+j*unit,unit)!=IFS_FS_OK)
                return fail("fragmented alternating native writes");
        for(unsigned i=0u;i<2u;++i){
            IfsFsExtentV2 e[8];uint32_t n=0u;
            if(map->map(mounted,names[i],0u,e,8u,&n)!=IFS_FS_OK || n<2u)
                return fail("relocation fixture is not physically fragmented");
        }
        /* Baseline is the genuinely fragmented image, not a reconstructed
         * successful layout. Measure the actual execution write/flush count,
         * then fail each operation independently before a fresh mount. */
        if(core->unmount(mounted)!=IFS_FS_OK)return fail("cold fault baseline");
        mounted=NULL;info.structure_bytes=sizeof(info);
        if(core->mount(&host,&mounted,&info)!=IFS_FS_OK)return fail("fault baseline remount");
        void *probe_plan=NULL;uint64_t probe_moved=0u;
        if(map->plan(mounted,"/",policy,&probe_plan)!=IFS_FS_OK || !probe_plan)
            return fail("fault baseline plan");
        d.capture=1;d.ops=d.cut=0;
        if(map->execute(mounted,probe_plan,&probe_moved)!=IFS_FS_OK ||
           map->verify(mounted,probe_plan)!=IFS_FS_OK)
            return fail("fault baseline execute");
        unsigned cuts=d.ops;d.capture=0;
        map->release_plan(mounted,probe_plan);
        if(!cuts || core->unmount(mounted)!=IFS_FS_OK || rollback_image(&d))
            return fail("restore fragmented fault baseline");
        unsigned recovered_transactions=0u;
        for(unsigned mode=0u;mode<3u;++mode)for(unsigned cut=1u;cut<=cuts;++cut){
            mounted=NULL;info.structure_bytes=sizeof(info);
            if(core->mount(&host,&mounted,&info)!=IFS_FS_OK)return fail("interruption fixture mount");
            void *fault_plan=NULL;uint64_t partial=0u;
            if(map->plan(mounted,"/",policy,&fault_plan)!=IFS_FS_OK || !fault_plan)
                return fail("interruption fixture plan");
            d.capture=1;d.ops=0;d.cut=cut;d.mode=mode;
            int status=map->execute(mounted,fault_plan,&partial);
            d.cut=0;
            if(status==IFS_FS_OK)return fail("injected host write/flush failure was ignored");
            map->release_plan(mounted,fault_plan);
            if(core->unmount(mounted)!=IFS_FS_OK)return fail("faulted session teardown");
            mounted=NULL;info.structure_bytes=sizeof(info);
            if(core->mount(&host,&mounted,&info)!=IFS_FS_OK)
                return fail("cold redo recovery failed");
            uint64_t replayed=0u;
            if(map->recover(mounted,0u,&replayed)!=IFS_FS_OK || replayed>1u)
                return fail("recovery result");
            recovered_transactions+=(unsigned)replayed;
            if(map->recover(mounted,0u,&replayed)!=IFS_FS_OK || replayed ||
               vol->check(mounted,0u,&errors)!=IFS_FS_OK || errors)
                return fail("recovery is not idempotent or ownership is corrupt");
            for(unsigned i=0u;i<2u;++i)
                if(core->read(mounted,names[i],0u,readback,4u*unit)!=IFS_FS_OK ||
                   memcmp(readback,expected,4u*unit))
                    return fail("interruption lost live payload");
            if(core->unmount(mounted)!=IFS_FS_OK || rollback_image(&d))
                return fail("restore interruption baseline");
        }
        if(!recovered_transactions)return fail("interruption matrix never exercised real redo");
        printf("EXFAT-RELOCATION-RECOVERY-PASS policy=%u cuts=%u recovered=%u\n",
               policy,cuts*3u,recovered_transactions);
        mounted=NULL;info.structure_bytes=sizeof(info);
        if(core->mount(&host,&mounted,&info)!=IFS_FS_OK)
            return fail("restore original fragmented fixture");
        void *plan=NULL;
        unsigned writes=d.writes;uint64_t moved=0u,repaired=0u;
        if(map->plan(mounted,"/",policy,&plan)!=IFS_FS_OK || !plan || writes!=d.writes ||
           core->write(mounted,names[0],0u,expected,1u)!=IFS_FS_BUSY ||
           map->execute(mounted,plan,&moved)!=IFS_FS_OK || moved<8u*unit ||
           map->verify(mounted,plan)!=IFS_FS_OK)
            return fail("native exFAT planned relocation execution");
        map->release_plan(mounted,plan);
        if(map->recover(mounted,0u,&repaired)!=IFS_FS_OK || repaired ||
           core->unmount(mounted)!=IFS_FS_OK)
            return fail("clean relocation journal recovery");
        mounted=NULL;info.structure_bytes=sizeof(info);
        if(core->mount(&host,&mounted,&info)!=IFS_FS_OK)
            return fail("cold relocation remount");
        const IfsFsAllocationOpsV2 *allocation=(const void*)ifs_fs_group_v2(api,IFS_FS_GROUP_ALLOCATION);
        if(!allocation || !allocation->map)return fail("missing native allocation map");
        uint64_t cursor=0u,unknown=0u,free_bytes=0u;
        for(unsigned page=0u;;++page){
            IfsFsAllocationRangeV2 ranges[8];uint32_t count=0u;uint64_t next=0u;
            if(page>=65536u)return fail("allocation paging did not terminate");
            int status=allocation->map(mounted,cursor,ranges,8u,&count);
            if(!ifs_fs_allocation_page_v2_valid(host.volume_bytes,cursor,status,ranges,8u,count,&next,&unknown))
                return fail("malformed native allocation map");
            for(uint32_t k=0u;k<count;++k)if(ranges[k].state==IFS_FS_SPACE_FREE)free_bytes+=ranges[k].length;
            cursor=next;
            if(status==IFS_FS_END)break;
        }
        if(cursor!=host.volume_bytes || unknown || free_bytes!=info.free_bytes)
            return fail("native allocation map does not reconcile with volume accounting");
        for(unsigned i=0u;i<2u;++i){
            IfsFsExtentV2 e={0};uint32_t n=0u;
            if(map->map(mounted,names[i],0u,&e,1u,&n)!=IFS_FS_OK ||
               n!=1u || e.length!=4u*unit ||
               map->map(mounted,names[i],1u,&e,1u,&n)!=IFS_FS_END || n ||
               core->read(mounted,names[i],0u,readback,4u*unit)!=IFS_FS_OK ||
               memcmp(readback,expected,4u*unit))
                return fail("cold native relocation physical map or payload mismatch");
        }
        if(vol->check(mounted,0u,&errors)!=IFS_FS_OK || errors ||
           core->unmount(mounted)!=IFS_FS_OK)
            return fail("cold relocated allocation ownership");
        free(expected);free(readback);
    }
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
