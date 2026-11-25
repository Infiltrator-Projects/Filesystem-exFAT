/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native exFAT redo log. The standard boot region OEM parameter identifies
 * an explicitly reserved pre-FAT area. Two independent committed headers
 * protect metadata after-images; never overwrite reachable file payloads.
 * Foreign volumes retain ordinary CORE access, but do not acquire this
 * journal by inference from unused space. Replay precedes native mount.
 */
#ifndef IFS_EXFAT_JOURNAL_H
#define IFS_EXFAT_JOURNAL_H
#define EJ_RESERVED 128u
#define EJ_HEAD_A 24u
#define EJ_HEAD_B 25u
#define EJ_FIRST 26u
#define EJ_MAX_PATCH 60u
#define EJ_RECORD 32u
#define EJ_TAG "IFSExfatJournal1"
#define EJ_MAGIC "IFSEXJ01"
typedef struct {
    uint32_t sector,spc,unit,fat,heap,clusters,root,identity;
    uint64_t total;
} EjGeom;
typedef struct { uint32_t sector;const uint8_t *data; } EjPatch;
static void ej_put32(uint8_t *p,uint32_t v)
{for(unsigned i=0u;i<4u;++i)p[i]=(uint8_t)(v>>(8u*i));}
static uint32_t ej_crc(const uint8_t *p,uint32_t n)
{
    uint32_t crc=UINT32_MAX;
    for(uint32_t i=0u;i<n;++i){
        crc^=p[i];
        for(unsigned j=0u;j<8u;++j)
            crc=(crc>>1)^((crc&1u)?UINT32_C(0xedb88320):0u);
    }
    return ~crc;
}
static int ej_read(const IfsFsHostRuntimeV2 *h,const EjGeom *g,uint64_t sector,void *data)
{
    if(sector>=g->total)return IFS_FS_CORRUPT;
    return ifs_fs_host_read_bytes_v2(h,sector*g->sector,data,g->sector);
}
static int ej_geometry(const IfsFsHostRuntimeV2 *h,uint32_t sector,EjGeom *g)
{
    uint8_t boot[512],oem[4096];
    int rc=ifs_fs_host_read_bytes_v2(h,0u,boot,sizeof boot);
    if(rc!=IFS_FS_OK)return rc;
    if(sector<512u || sector>sizeof oem)return IFS_FS_CORRUPT;
    g->sector=sector;g->total=exfat_le64(boot+72);
    g->fat=exfat_le32(boot+80);g->heap=exfat_le32(boot+88);
    g->clusters=exfat_le32(boot+92);g->root=exfat_le32(boot+96);
    if(boot[109]>25u)return IFS_FS_CORRUPT;
    g->spc=1u<<boot[109];
    if((uint64_t)g->spc*sector>UINT32_MAX)return IFS_FS_CORRUPT;
    g->unit=g->spc*sector;
    rc=ej_read(h,g,9u,oem);if(rc!=IFS_FS_OK)return rc;
    if(memcmp(oem,EJ_TAG,16u))return IFS_FS_NOT_FOUND;
    if(g->fat<EJ_RESERVED || g->heap<=g->fat ||
       g->clusters>0x7fffffedu || g->total>UINT32_MAX)
        return IFS_FS_UNSUPPORTED;
    /* Exclude mutable volume flags/percentage from journal identity. */
    boot[106]=boot[107]=boot[112]=0u;
    g->identity=ej_crc(boot,sizeof boot);
    return IFS_FS_OK;
}
static int ej_valid_header(uint8_t *p,const EjGeom *g)
{
    const uint32_t count=exfat_le32(p+12);
    if(memcmp(p,EJ_MAGIC,8u) || exfat_le32(p+8)!=g->identity ||
       exfat_le32(p+16)!=g->sector || exfat_le32(p+24) ||
       exfat_le32(p+28) || count>EJ_MAX_PATCH ||
       EJ_RECORD+count*8u>g->sector)return 0;
    uint32_t old=exfat_le32(p+20);
    ej_put32(p+20,0u);uint32_t actual=ej_crc(p,g->sector);ej_put32(p+20,old);
    return old==actual;
}
static int ej_select(const IfsFsHostRuntimeV2 *h,const EjGeom *g,uint8_t *out)
{
    uint8_t other[4096];
    int rc=ej_read(h,g,EJ_HEAD_A,out);if(rc!=IFS_FS_OK)return rc;
    rc=ej_read(h,g,EJ_HEAD_B,other);if(rc!=IFS_FS_OK)return rc;
    int a=ej_valid_header(out,g),b=ej_valid_header(other,g);
    if(!a&&!b)return IFS_FS_CORRUPT;
    if(a&&b&&exfat_le32(out+12)&&exfat_le32(other+12)&&memcmp(out,other,g->sector))
        return IFS_FS_CORRUPT;
    if(!a || (b&&!exfat_le32(out+12)&&exfat_le32(other+12)))
        memcpy(out,other,g->sector);
    return IFS_FS_OK;
}
#if !defined(IFS_EXFAT_FIRMWARE_BUILD)
static int ej_writable(const IfsFsHostRuntimeV2 *h,const EjGeom *g)
{
    return !(h->capabilities&IFS_FS_HOST_READ_ONLY) &&
        (h->capabilities&IFS_FS_HOST_EXCLUSIVE) &&
        (h->capabilities&IFS_FS_HOST_DURABLE) && h->write_at && h->flush &&
        h->logical_sector_bytes==g->sector && h->physical_sector_bytes==g->sector;
}
static int ej_write(const IfsFsHostRuntimeV2 *h,const EjGeom *g,uint64_t sector,const void *data)
{
    if(sector>=g->total)return IFS_FS_CORRUPT;
    return ifs_fs_host_write_bytes_v2(h,sector*g->sector,data,g->sector);
}
static int ej_flush(const IfsFsHostRuntimeV2 *h)
{return h->flush(h->context)?IFS_FS_IO_ERROR:IFS_FS_OK;}
static int ej_next(const IfsFsHostRuntimeV2 *h,const EjGeom *g,uint32_t c,uint32_t *next)
{
    uint8_t raw[4];
    if(c<2u || c>=g->clusters+2u)return IFS_FS_CORRUPT;
    int rc=ifs_fs_host_read_bytes_v2(h,(uint64_t)g->fat*g->sector+(uint64_t)c*4u,raw,4u);
    if(rc==IFS_FS_OK)*next=exfat_le32(raw)&0x7fffffffu;
    return rc;
}
static uint64_t ej_cluster_sector(const EjGeom *g,uint32_t c)
{return (uint64_t)g->heap+(uint64_t)(c-2u)*g->spc;}
/* Root links never change during relocation. The allocation bitmap is
 * immutable as a stream; only its contents change. Resolve both from the
 * standard metadata independently before applying any logged sector. */
static int ej_bitmap(const IfsFsHostRuntimeV2 *h,const EjGeom *g,uint32_t *base,uint32_t *bytes)
{
    uint8_t sector[4096];
    uint32_t c=g->root;
    for(uint32_t step=0u;step<g->clusters;++step){
        if(c<2u || c>=g->clusters+2u)return IFS_FS_CORRUPT;
        for(uint32_t n=0u;n<g->spc;++n){
            int rc=ej_read(h,g,ej_cluster_sector(g,c)+n,sector);if(rc!=IFS_FS_OK)return rc;
            for(uint32_t at=0u;at<g->sector;at+=32u){
                const uint8_t *e=sector+at;
                if(!e[0])return IFS_FS_CORRUPT;
                if(e[0]!=0x81u)continue;
                uint32_t first=exfat_le32(e+20);
                uint64_t size=exfat_le64(e+24),expected=((uint64_t)g->clusters+7u)/8u;
                if(e[1] || first<2u || first>=g->clusters+2u || size!=expected ||
                   size>UINT32_MAX || (size+g->unit-1u)/g->unit>g->clusters+2u-first)
                    return IFS_FS_CORRUPT;
                *base=(uint32_t)ej_cluster_sector(g,first);*bytes=(uint32_t)size;
                return IFS_FS_OK;
            }
        }
        int rc=ej_next(h,g,c,&c);if(rc!=IFS_FS_OK)return rc;
        if(c>=0x7ffffff8u)break;
    }
    return IFS_FS_CORRUPT;
}
static int ej_root_has(const IfsFsHostRuntimeV2 *h,const EjGeom *g,uint32_t sector)
{
    uint32_t c=g->root;
    for(uint32_t step=0u;step<g->clusters;++step){
        if(c<2u || c>=g->clusters+2u)return 0;
        uint64_t at=ej_cluster_sector(g,c);
        if(sector>=at && sector-at<g->spc)return 1;
        if(ej_next(h,g,c,&c)!=IFS_FS_OK || c>=0x7ffffff8u)return 0;
    }
    return 0;
}
static int ej_targets(const IfsFsHostRuntimeV2 *h,const EjGeom *g,const EjPatch *p,uint32_t n)
{
    uint32_t bitmap=0u,bytes=0u;
    int rc=ej_bitmap(h,g,&bitmap,&bytes);if(rc!=IFS_FS_OK)return rc;
    uint32_t bitmap_sectors=(bytes+g->sector-1u)/g->sector;
    if(!n || n>EJ_MAX_PATCH || EJ_RECORD+n*8u>g->sector)return IFS_FS_CORRUPT;
    unsigned bits=0u,dirs=0u;
    for(uint32_t i=0u;i<n;++i){
        uint32_t at=p[i].sector;
        if(!p[i].data || at<g->heap || at>=g->total)return IFS_FS_CORRUPT;
        for(uint32_t j=0u;j<i;++j)if(p[j].sector==at)return IFS_FS_CORRUPT;
        if(at>=bitmap && at-bitmap<bitmap_sectors){++bits;continue;}
        if(!ej_root_has(h,g,at) || ++dirs>3u)return IFS_FS_CORRUPT;
        /* Never allow the relocation record to change a system entry. */
        uint8_t old[4096];rc=ej_read(h,g,at,old);if(rc!=IFS_FS_OK)return rc;
        for(uint32_t k=0u;k<g->sector;k+=32u)
            if((old[k]==0x81u || old[k]==0x82u || p[i].data[k]==0x81u ||
                p[i].data[k]==0x82u) && memcmp(old+k,p[i].data+k,32u))
                return IFS_FS_CORRUPT;
    }
    return bits&&dirs?IFS_FS_OK:IFS_FS_CORRUPT;
}
static void ej_make(uint8_t *out,const EjGeom *g,const EjPatch *p,uint32_t n)
{
    memset(out,0,g->sector);memcpy(out,EJ_MAGIC,8u);
    ej_put32(out+8,g->identity);ej_put32(out+12,n);ej_put32(out+16,g->sector);
    for(uint32_t i=0u;i<n;++i){
        ej_put32(out+EJ_RECORD+i*8u,p[i].sector);
        ej_put32(out+EJ_RECORD+i*8u+4u,ej_crc(p[i].data,g->sector));
    }
    ej_put32(out+20,ej_crc(out,g->sector));
}
static int ej_publish(const IfsFsHostRuntimeV2 *h,const EjGeom *g,const uint8_t *head)
{
    uint8_t check[4096];
    for(uint32_t n=EJ_HEAD_A;n<=EJ_HEAD_B;++n){
        int rc=ej_write(h,g,n,head);if(rc!=IFS_FS_OK)return rc;
        rc=ej_flush(h);if(rc!=IFS_FS_OK)return rc;
        rc=ej_read(h,g,n,check);if(rc!=IFS_FS_OK)return rc;
        if(memcmp(check,head,g->sector))return IFS_FS_IO_ERROR;
    }
    return IFS_FS_OK;
}
static int ej_clear(const IfsFsHostRuntimeV2 *h,const EjGeom *g)
{
    uint8_t head[4096];ej_make(head,g,NULL,0u);
    return ej_publish(h,g,head);
}
static int ej_checkpoint(const IfsFsHostRuntimeV2 *h,const EjGeom *g,const EjPatch *p,uint32_t n)
{
    for(uint32_t i=0u;i<n;++i){
        int rc=ej_write(h,g,p[i].sector,p[i].data);if(rc!=IFS_FS_OK)return rc;
    }
    int rc=ej_flush(h);if(rc!=IFS_FS_OK)return rc;
    uint8_t check[4096];
    for(uint32_t i=0u;i<n;++i){
        rc=ej_read(h,g,p[i].sector,check);if(rc!=IFS_FS_OK)return rc;
        if(memcmp(check,p[i].data,g->sector))return IFS_FS_IO_ERROR;
    }
    return ej_clear(h,g);
}
static inline int ej_commit(const IfsFsHostRuntimeV2 *h,const EjGeom *g,const EjPatch *p,uint32_t n)
{
    if(!ej_writable(h,g))return IFS_FS_ACCESS;
    int rc=ej_targets(h,g,p,n);if(rc!=IFS_FS_OK)return rc;
    for(uint32_t i=0u;i<n;++i){
        rc=ej_write(h,g,EJ_FIRST+i,p[i].data);if(rc!=IFS_FS_OK)return rc;
    }
    rc=ej_flush(h);if(rc!=IFS_FS_OK)return rc;
    uint8_t check[4096],head[4096];
    for(uint32_t i=0u;i<n;++i){
        rc=ej_read(h,g,EJ_FIRST+i,check);if(rc!=IFS_FS_OK)return rc;
        if(memcmp(check,p[i].data,g->sector))return IFS_FS_IO_ERROR;
    }
    ej_make(head,g,p,n);
    rc=ej_publish(h,g,head);if(rc!=IFS_FS_OK)return rc;
    return ej_checkpoint(h,g,p,n);
}
static int ej_replay(const IfsFsHostRuntimeV2 *h,const EjGeom *g,const uint8_t *head)
{
    if(!ej_writable(h,g))return IFS_FS_ACCESS;
    uint32_t n=exfat_le32(head+12);
    if(!n)return IFS_FS_OK;
    uint8_t *data=h->allocate(h->context,(size_t)n*g->sector);
    if(!data)return IFS_FS_NO_MEMORY;
    EjPatch p[EJ_MAX_PATCH];int rc=IFS_FS_OK;
    for(uint32_t i=0u;i<n;++i){
        uint8_t *b=data+(size_t)i*g->sector;
        rc=ej_read(h,g,EJ_FIRST+i,b);if(rc!=IFS_FS_OK)goto done;
        if(ej_crc(b,g->sector)!=exfat_le32(head+EJ_RECORD+i*8u+4u)){
            rc=IFS_FS_CORRUPT;goto done;
        }
        p[i]=(EjPatch){exfat_le32(head+EJ_RECORD+i*8u),b};
    }
    rc=ej_targets(h,g,p,n);
    if(rc==IFS_FS_OK)rc=ej_checkpoint(h,g,p,n);
done:
    h->release(h->context,data);return rc;
}
#endif
/* Probe never replays. A tagged volume with a pending commit is recognized
 * before native mount; firmware/read-only mounts refuse unreplayed state. */
static int ej_mount(const IfsFsHostRuntimeV2 *h,uint32_t sector,int probe_only,
                    int *present,uint64_t *pending)
{
    *present=0;*pending=0u;
    EjGeom g;int rc=ej_geometry(h,sector,&g);
    if(rc==IFS_FS_NOT_FOUND)return IFS_FS_OK;
    if(rc!=IFS_FS_OK)return rc;
    *present=1;
    uint8_t head[4096];rc=ej_select(h,&g,head);if(rc!=IFS_FS_OK)return rc;
    if(!exfat_le32(head+12))return IFS_FS_OK;
    *pending=1u;if(probe_only)return IFS_FS_OK;
#if defined(IFS_EXFAT_FIRMWARE_BUILD)
    return IFS_FS_ACCESS;
#else
    return ej_replay(h,&g,head);
#endif
}
#endif
