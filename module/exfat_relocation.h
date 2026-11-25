/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native exFAT whole-stream relocation with allocation-bitmap and entry-set
 * redo. The source remains allocated until its complete replacement payload
 * has been written, flushed and read back. NoFatChain makes a new run
 * directly interoperable; stale FAT values in free/NoFatChain allocations
 * are not authoritative. No external process or legacy provider is used.
 *
 * This bounded first writer accepts journal-enabled flat namespaces and
 * enough disjoint free space to copy a file before retiring its source.
 * Directory trees, unknown extensions, overlarge redo sets and unavailable
 * contiguous destinations are rejected during read-only planning.
 */
#ifndef IFS_EXFAT_RELOCATION_H
#define IFS_EXFAT_RELOCATION_H
#define ER_ROOT_MAX (16u*1024u*1024u)
#define ER_BITMAP_MAX (32u*1024u*1024u)
#define ER_UNIT_MAX (1024u*1024u)
typedef struct {
    uint64_t offset,size,hash;
    uint32_t entries,count,target,reserve;
    uint32_t *chain;
} ErFile;
typedef struct {
    EjGeom g;
    uint32_t bitmap_base,bitmap_bytes,bitmap_size,root_count,root_size;
    uint8_t *bitmap,*root;
    uint32_t *sectors;
    ErFile *files;
    uint32_t file_count,capacity,policy;
    int executed;
} ErPlan;
static ErPlan *er_active;
static uintptr_t er_generation,er_token;
static int er_has_plan(void){return er_active!=NULL;}
static void er_destroy(ErPlan *p)
{
    const IfsFsHostRuntimeV2 *h=&s.host;
    if(!p)return;
    if(p->files){
        for(uint32_t i=0u;i<p->file_count;++i)
            if(p->files[i].chain)h->release(h->context,p->files[i].chain);
        h->release(h->context,p->files);
    }
    if(p->bitmap)h->release(h->context,p->bitmap);
    if(p->root)h->release(h->context,p->root);
    if(p->sectors)h->release(h->context,p->sectors);
    h->release(h->context,p);
}
static void er_discard(void)
{er_destroy(er_active);er_active=NULL;er_token=0u;}
static int er_cancel(const IfsFsHostRuntimeV2 *h)
{return h->cancelled && h->cancelled(h->context);}
static int er_bit(const uint8_t *b,uint32_t cluster)
{uint32_t k=cluster-2u;return (b[k/8u]&(1u<<(k%8u)))!=0u;}
static void er_set(uint8_t *b,uint32_t cluster,int on)
{
    uint32_t k=cluster-2u;uint8_t mask=(uint8_t)(1u<<(k%8u));
    if(on)b[k/8u]|=mask;else b[k/8u]&=(uint8_t)~mask;
}
static uint64_t er_hash(uint64_t hash,const uint8_t *b,uint32_t n)
{for(uint32_t i=0u;i<n;++i)hash=(hash^b[i])*UINT64_C(1099511628211);return hash;}
#define ER_HASH_INITIAL UINT64_C(14695981039346656037)
static uint16_t er_checksum(const uint8_t *entry,uint32_t n)
{
    uint16_t sum=0u;
    for(uint32_t i=0u;i<n;++i)
        if(i!=2u && i!=3u)sum=(uint16_t)(((sum>>1)|(sum<<15))+entry[i]);
    return sum;
}
static int er_root(ErPlan *p)
{
    const IfsFsHostRuntimeV2 *h=&s.host;const EjGeom *g=&p->g;
    uint32_t c=g->root,count=0u;
    for(;;){
        if(er_cancel(h))return IFS_FS_BUSY;
        if(c<2u || c>=g->clusters+2u || count>=g->clusters)
            return IFS_FS_CORRUPT;
        ++count;
        if((uint64_t)count*g->unit>ER_ROOT_MAX)return IFS_FS_UNSUPPORTED;
        int rc=ej_next(h,g,c,&c);if(rc!=IFS_FS_OK)return rc;
        if(c>=0x7ffffff8u)break;
    }
    p->root_size=count*g->unit;p->root_count=count*g->spc;
    p->root=h->allocate(h->context,p->root_size);
    p->sectors=h->allocate(h->context,(size_t)p->root_count*sizeof(*p->sectors));
    if(!p->root || !p->sectors)return IFS_FS_NO_MEMORY;
    c=g->root;
    for(uint32_t i=0u;i<count;++i){
        uint64_t start=ej_cluster_sector(g,c);
        for(uint32_t j=0u;j<g->spc;++j)p->sectors[i*g->spc+j]=(uint32_t)(start+j);
        int rc=ifs_fs_host_read_bytes_v2(h,start*g->sector,
                    p->root+(size_t)i*g->unit,g->unit);
        if(rc!=IFS_FS_OK)return rc;
        rc=ej_next(h,g,c,&c);if(rc!=IFS_FS_OK)return rc;
    }
    return IFS_FS_OK;
}
static int er_file_chain(ErPlan *p,ErFile *f,const uint8_t *stream,uint8_t *data)
{
    const IfsFsHostRuntimeV2 *h=&s.host;const EjGeom *g=&p->g;
    uint64_t length=exfat_le64(stream+24),valid=exfat_le64(stream+8);
    uint64_t blocks=length/g->unit+(length%g->unit!=0u);
    uint32_t cluster=exfat_le32(stream+20);
    if(valid>length || blocks>g->clusters || !(stream[1]&1u) || (stream[1]&~3u))
        return IFS_FS_CORRUPT;
    f->size=length;f->count=(uint32_t)blocks;
    if(!blocks)return cluster?IFS_FS_CORRUPT:IFS_FS_OK;
    if(blocks>SIZE_MAX/sizeof(*f->chain))return IFS_FS_UNSUPPORTED;
    f->chain=h->allocate(h->context,(size_t)blocks*sizeof(*f->chain));
    if(!f->chain)return IFS_FS_NO_MEMORY;
    f->hash=ER_HASH_INITIAL;
    for(uint32_t i=0u;i<f->count;++i){
        if(er_cancel(h))return IFS_FS_BUSY;
        if(cluster<2u || cluster>=g->clusters+2u || !er_bit(p->bitmap,cluster))
            return IFS_FS_CORRUPT;
        f->chain[i]=cluster;
        int rc=ifs_fs_host_read_bytes_v2(h,ej_cluster_sector(g,cluster)*g->sector,data,g->unit);
        if(rc!=IFS_FS_OK)return rc;
        f->hash=er_hash(f->hash,data,g->unit);
        if(stream[1]&2u){++cluster;continue;}
        rc=ej_next(h,g,cluster,&cluster);if(rc!=IFS_FS_OK)return rc;
        if((i+1u==f->count && cluster<0x7ffffff8u) ||
           (i+1u<f->count && (cluster<2u || cluster>=g->clusters+2u)))
            return IFS_FS_CORRUPT;
    }
    return IFS_FS_OK;
}
static int er_unique(uint32_t *items,uint32_t *n,uint32_t value)
{
    for(uint32_t i=0u;i<*n;++i)if(items[i]==value)return 1;
    if(*n==EJ_MAX_PATCH)return 0;
    items[(*n)++]=value;return 1;
}
/* Conservative redo bound: count every bitmap sector that may change and
 * every directory sector occupied by this complete entry set. */
static int er_fits(const ErPlan *p,const ErFile *f)
{
    const EjGeom *g=&p->g;
    uint32_t items[EJ_MAX_PATCH],n=0u;
    for(uint32_t i=0u;i<f->count;++i){
        uint32_t a=p->bitmap_base+(f->chain[i]-2u)/8u/g->sector;
        uint32_t b=p->bitmap_base+(f->target+i-2u)/8u/g->sector;
        if(!er_unique(items,&n,a)||!er_unique(items,&n,b))return 0;
    }
    for(uint64_t k=f->offset/g->sector;k<=(f->offset+f->entries*32u-1u)/g->sector;++k)
        if(!er_unique(items,&n,p->sectors[k]))return 0;
    return EJ_RECORD+n*8u<=g->sector;
}
static int er_scan(ErPlan *p,uint8_t *scratch)
{
    const IfsFsHostRuntimeV2 *h=&s.host;const EjGeom *g=&p->g;
    p->capacity=p->root_size/96u+1u;
    p->files=h->allocate(h->context,(size_t)p->capacity*sizeof(*p->files));
    if(!p->files)return IFS_FS_NO_MEMORY;
    memset(p->files,0,(size_t)p->capacity*sizeof(*p->files));
    for(uint32_t at=0u;at<p->root_size;){
        if(er_cancel(h))return IFS_FS_BUSY;
        uint8_t *entry=p->root+at;
        if(!entry[0])break;
        if(!(entry[0]&0x80u) || entry[0]==0x81u || entry[0]==0x82u ||
           entry[0]==0x83u || entry[0]==0xa0u){at+=32u;continue;}
        if(entry[0]!=0x85u || (entry[4]&0x10u))return IFS_FS_UNSUPPORTED;
        uint32_t n=(uint32_t)entry[1]+1u;
        if(n<3u || n>19u || n*32u>p->root_size-at || entry[32]!=0xc0u ||
           er_checksum(entry,n*32u)!=((uint16_t)entry[2]|((uint16_t)entry[3]<<8)))
            return IFS_FS_CORRUPT;
        const uint32_t names=entry[35];
        if(!names || n!=2u+(names+14u)/15u)return IFS_FS_CORRUPT;
        for(uint32_t i=2u;i<n;++i)if(entry[i*32u]!=0xc1u)return IFS_FS_UNSUPPORTED;
        if(p->file_count>=p->capacity)return IFS_FS_CORRUPT;
        ErFile *f=&p->files[p->file_count++];
        f->offset=at;f->entries=n;
        int rc=er_file_chain(p,f,entry+32,scratch);if(rc!=IFS_FS_OK)return rc;
        f->reserve=p->policy?(f->count+9u)/10u:0u;
        at+=n*32u;
    }
    /* Simulate exact future allocation order. A destination must be
     * unallocated before the associated file's commit. Source clusters
     * become available only after that move; growth gaps remain excluded. */
    uint8_t *sim=h->allocate(h->context,p->bitmap_size);
    if(!sim)return IFS_FS_NO_MEMORY;
    memcpy(sim,p->bitmap,p->bitmap_size);
    int rc=IFS_FS_OK;
    for(uint32_t i=0u;i<p->file_count;++i){
        ErFile *f=&p->files[i];if(!f->count)continue;
        uint32_t need=f->count+f->reserve;
        if(need>g->clusters){rc=IFS_FS_NO_SPACE;break;}
        int keep=f->chain[0]<=g->clusters+2u-need;
        for(uint32_t j=1u;j<f->count && keep;++j)
            if(f->chain[j]!=f->chain[0]+j)keep=0;
        for(uint32_t j=0u;j<f->reserve && keep;++j)
            if(er_bit(sim,f->chain[0]+f->count+j))keep=0;
        if(keep)f->target=f->chain[0];
        else {
            uint32_t run=0u;
            for(uint32_t k=2u;k<g->clusters+2u;++k){
                if(er_cancel(h)){rc=IFS_FS_BUSY;goto done;}
                run=er_bit(sim,k)?0u:run+1u;
                if(run==need){f->target=k+1u-need;break;}
            }
            if(!f->target){rc=IFS_FS_NO_SPACE;break;}
            if(!er_fits(p,f)){rc=IFS_FS_UNSUPPORTED;break;}
            for(uint32_t j=0u;j<f->count;++j)er_set(sim,f->chain[j],0);
        }
        for(uint32_t j=0u;j<need;++j)er_set(sim,f->target+j,1);
    }
done:
    h->release(h->context,sim);return rc;
}
static int er_plan(void *v,const char *path,uint32_t policy,void **out)
{
    if(!out)return IFS_FS_INVALID;
    *out=NULL;
    if(!live(v)||!path)return IFS_FS_INVALID;
    if(strcmp(path,"/") || (policy!=0u && policy!=10u) || !s.journal_present)
        return IFS_FS_UNSUPPORTED;
    if(er_active || er_generation==UINTPTR_MAX)return IFS_FS_BUSY;
    const IfsFsHostRuntimeV2 *h=&s.host;
    uint32_t sector=0u,cluster=0u;
    int rc=geometry(h,&sector,&cluster);if(rc!=IFS_FS_OK)return rc;
    EjGeom g;rc=ej_geometry(h,sector,&g);if(rc!=IFS_FS_OK)return rc;
    if(g.unit>ER_UNIT_MAX || sector!=h->logical_sector_bytes || sector!=h->physical_sector_bytes)
        return IFS_FS_UNSUPPORTED;
    uint8_t header[4096];rc=ej_select(h,&g,header);if(rc!=IFS_FS_OK)return rc;
    if(exfat_le32(header+12))return IFS_FS_CORRUPT;
    uint64_t errors=0u;rc=ifs_exfat_engine_check(h,&errors);
    if(rc!=IFS_FS_OK || errors)return rc!=IFS_FS_OK?rc:IFS_FS_CORRUPT;
    ErPlan *p=h->allocate(h->context,sizeof(*p));
    if(!p)return IFS_FS_NO_MEMORY;
    memset(p,0,sizeof(*p));p->g=g;p->policy=policy;
    uint8_t *data=NULL;
    rc=ej_bitmap(h,&g,&p->bitmap_base,&p->bitmap_bytes);if(rc!=IFS_FS_OK)goto done;
    if(!p->bitmap_bytes || p->bitmap_bytes>ER_BITMAP_MAX){rc=IFS_FS_UNSUPPORTED;goto done;}
    p->bitmap_size=(p->bitmap_bytes+sector-1u)/sector*sector;
    p->bitmap=h->allocate(h->context,p->bitmap_size);data=h->allocate(h->context,g.unit);
    if(!p->bitmap || !data){rc=IFS_FS_NO_MEMORY;goto done;}
    rc=ifs_fs_host_read_bytes_v2(h,(uint64_t)p->bitmap_base*sector,p->bitmap,p->bitmap_size);
    if(rc!=IFS_FS_OK)goto done;
    rc=er_root(p);if(rc!=IFS_FS_OK)goto done;
    rc=er_scan(p,data);if(rc!=IFS_FS_OK)goto done;
    er_active=p;er_token=++er_generation;*out=(void *)er_token;p=NULL;
done:
    if(data)h->release(h->context,data);
    er_destroy(p);return rc;
}
static ErPlan *er_lookup(void *v,void *plan)
{return live(v) && plan && (uintptr_t)plan==er_token?er_active:NULL;}
static int er_unchanged(const ErPlan *p)
{
    const IfsFsHostRuntimeV2 *h=&s.host;const EjGeom *g=&p->g;
    uint8_t raw[4096];
    for(uint32_t n=0u;n<p->bitmap_size/g->sector;++n){
        int rc=ej_read(h,g,p->bitmap_base+n,raw);if(rc!=IFS_FS_OK)return rc;
        if(memcmp(raw,p->bitmap+(size_t)n*g->sector,g->sector))return IFS_FS_CORRUPT;
    }
    for(uint32_t n=0u;n<p->root_count;++n){
        int rc=ej_read(h,g,p->sectors[n],raw);if(rc!=IFS_FS_OK)return rc;
        if(memcmp(raw,p->root+(size_t)n*g->sector,g->sector))return IFS_FS_CORRUPT;
    }
    return IFS_FS_OK;
}
static int er_move(ErPlan *p,ErFile *f,uint8_t *data,uint8_t *after,uint64_t *moved)
{
    const IfsFsHostRuntimeV2 *h=&s.host;const EjGeom *g=&p->g;
    int unchanged=1;
    for(uint32_t j=0u;j<f->count;++j)
        if(f->chain[j]!=f->target+j){unchanged=0;break;}
    if(unchanged)return IFS_FS_OK;
    uint64_t hash=ER_HASH_INITIAL;
    for(uint32_t j=0u;j<f->count;++j){
        if(er_cancel(h))return IFS_FS_BUSY;
        uint32_t dst=f->target+j,src=f->chain[j];
        if(er_bit(p->bitmap,dst) || !er_bit(p->bitmap,src))return IFS_FS_CORRUPT;
        int rc=ifs_fs_host_read_bytes_v2(h,ej_cluster_sector(g,src)*g->sector,data,g->unit);
        if(rc!=IFS_FS_OK)return rc;
        hash=er_hash(hash,data,g->unit);
        rc=ifs_fs_host_write_bytes_v2(h,ej_cluster_sector(g,dst)*g->sector,data,g->unit);
        if(rc!=IFS_FS_OK)return rc;
    }
    if(hash!=f->hash)return IFS_FS_CORRUPT;
    int rc=ej_flush(h);if(rc!=IFS_FS_OK)return rc;
    hash=ER_HASH_INITIAL;
    for(uint32_t j=0u;j<f->count;++j){
        if(er_cancel(h))return IFS_FS_BUSY;
        rc=ifs_fs_host_read_bytes_v2(h,ej_cluster_sector(g,f->target+j)*g->sector,data,g->unit);
        if(rc!=IFS_FS_OK)return rc;
        hash=er_hash(hash,data,g->unit);
    }
    if(hash!=f->hash)return IFS_FS_IO_ERROR;
    memcpy(after,p->bitmap,p->bitmap_size);
    for(uint32_t j=0u;j<f->count;++j){
        er_set(after,f->chain[j],0);er_set(after,f->target+j,1);
    }
    uint8_t *entry=p->root+f->offset;
    entry[33]|=2u;ej_put32(entry+52,f->target);
    uint16_t sum=er_checksum(entry,f->entries*32u);
    entry[2]=(uint8_t)sum;entry[3]=(uint8_t)(sum>>8);
    EjPatch patches[EJ_MAX_PATCH];uint32_t n=0u;
    for(uint32_t j=0u;j<p->bitmap_size/g->sector;++j){
        size_t at=(size_t)j*g->sector;
        if(!memcmp(p->bitmap+at,after+at,g->sector))continue;
        if(n==EJ_MAX_PATCH)return IFS_FS_UNSUPPORTED;
        patches[n++]=(EjPatch){p->bitmap_base+j,after+at};
    }
    for(uint64_t j=f->offset/g->sector;j<=(f->offset+f->entries*32u-1u)/g->sector;++j){
        if(n==EJ_MAX_PATCH)return IFS_FS_UNSUPPORTED;
        patches[n++]=(EjPatch){p->sectors[j],p->root+j*g->sector};
    }
    rc=ej_commit(h,g,patches,n);if(rc!=IFS_FS_OK)return rc;
    memcpy(p->bitmap,after,p->bitmap_size);
    *moved+=f->size;return IFS_FS_OK;
}
static int er_execute(void *v,void *plan,uint64_t *moved)
{
    if(!moved)return IFS_FS_INVALID;
    *moved=0u;ErPlan *p=er_lookup(v,plan);if(!p)return IFS_FS_INVALID;
    const IfsFsHostRuntimeV2 *h=&s.host;const EjGeom *g=&p->g;
    if(!ej_writable(h,g))return IFS_FS_ACCESS;
    if(p->executed)return IFS_FS_BUSY;
    int rc=er_unchanged(p);if(rc!=IFS_FS_OK)return rc;
    uint8_t *data=h->allocate(h->context,g->unit),*after=h->allocate(h->context,p->bitmap_size);
    if(!data || !after){rc=IFS_FS_NO_MEMORY;goto done;}
    /* Raw on-disk transactions must never race a stale native cache. */
    if(ifs_exfat_engine_unmount()){rc=IFS_FS_IO_ERROR;goto done;}
    s.engine_attached=0;p->executed=-1;
    for(uint32_t i=0u;i<p->file_count;++i){
        if(!p->files[i].count)continue;
        rc=er_move(p,&p->files[i],data,after,moved);
        if(rc!=IFS_FS_OK)goto fault;
    }
    {
        IfsFatfsIo io=native(h,g->sector);
        if(ifs_exfat_engine_mount(&io)){rc=IFS_FS_IO_ERROR;goto fault;}
        s.engine_attached=1;
    }
    p->executed=1;rc=IFS_FS_OK;goto done;
fault:
    /* A committed redo record may survive an ambiguous I/O failure. No
     * further namespace access on this handle; cold writable mount replays. */
    s.fault=1;
done:
    if(data)h->release(h->context,data);
    if(after)h->release(h->context,after);
    return rc;
}
static int er_verify(void *v,void *plan)
{
    ErPlan *p=er_lookup(v,plan);if(!p || p->executed!=1)return IFS_FS_INVALID;
    const IfsFsHostRuntimeV2 *h=&s.host;const EjGeom *g=&p->g;
    int rc=er_unchanged(p);if(rc!=IFS_FS_OK)return rc;
    uint64_t errors=0u;rc=ifs_exfat_engine_check(h,&errors);
    if(rc!=IFS_FS_OK || errors)return rc!=IFS_FS_OK?rc:IFS_FS_CORRUPT;
    uint8_t *data=h->allocate(h->context,g->unit);
    if(!data)return IFS_FS_NO_MEMORY;
    for(uint32_t i=0u;i<p->file_count;++i){
        ErFile *f=&p->files[i];if(!f->count)continue;
        uint64_t hash=ER_HASH_INITIAL;
        for(uint32_t j=0u;j<f->count;++j){
            if(er_cancel(h)){rc=IFS_FS_BUSY;goto done;}
            if(!er_bit(p->bitmap,f->target+j)){rc=IFS_FS_CORRUPT;goto done;}
            rc=ifs_fs_host_read_bytes_v2(h,ej_cluster_sector(g,f->target+j)*g->sector,data,g->unit);
            if(rc!=IFS_FS_OK)goto done;
            hash=er_hash(hash,data,g->unit);
        }
        if(hash!=f->hash){rc=IFS_FS_CORRUPT;goto done;}
        for(uint32_t j=0u;j<f->reserve;++j)
            if(er_bit(p->bitmap,f->target+f->count+j)){rc=IFS_FS_CORRUPT;goto done;}
    }
    rc=IFS_FS_OK;
done:
    h->release(h->context,data);return rc;
}
static int er_recover(void *v,uint32_t flags,uint64_t *repaired)
{
    if(!repaired)return IFS_FS_INVALID;
    *repaired=0u;
    if(!live(v) || flags)return IFS_FS_INVALID;
    if(!s.writable)return IFS_FS_ACCESS;
    if(!s.journal_present)return IFS_FS_UNSUPPORTED;
    if(er_active)return IFS_FS_BUSY;
    uint64_t errors=0u;int rc=ifs_exfat_engine_check(&s.host,&errors);
    if(rc!=IFS_FS_OK || errors)return rc!=IFS_FS_OK?rc:IFS_FS_CORRUPT;
    *repaired=s.recovered;s.recovered=0u;return IFS_FS_OK;
}
static int er_guard_plan(void *v,const char *path,uint32_t policy,void **out)
{
    if(out)*out=NULL;
    if(!v2_enter_operation())return IFS_FS_BUSY;
    return v2_leave_operation(er_plan(v,path,policy,out));
}
static int er_guard_execute(void *v,void *p,uint64_t *moved)
{
    if(moved)*moved=0u;
    if(!v2_enter_operation())return IFS_FS_BUSY;
    return v2_leave_operation(er_execute(v,p,moved));
}
static int er_guard_verify(void *v,void *p)
{
    if(!v2_enter_operation())return IFS_FS_BUSY;
    return v2_leave_operation(er_verify(v,p));
}
static int er_guard_recover(void *v,uint32_t flags,uint64_t *repaired)
{
    if(repaired)*repaired=0u;
    if(!v2_enter_operation())return IFS_FS_BUSY;
    return v2_leave_operation(er_recover(v,flags,repaired));
}
static void er_guard_release(void *v,void *p)
{
    if(!v2_enter_operation())return;
    if(s.mounted && v && (uintptr_t)v==s.handle && p && (uintptr_t)p==er_token)er_discard();
    (void)v2_leave_operation(IFS_FS_OK);
}
#endif
