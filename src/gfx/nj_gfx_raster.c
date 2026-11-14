/* Pure-C reference for exact-coverage 3D fragment packets. */
#include "nj2me/nj_gfx_raster.h"
#include <stdint.h>
#include <stddef.h>

typedef struct raster_range { uintptr_t lo, hi; } raster_range;
static int range_make(const void *p, size_t rows, size_t stride,
                      size_t tail, raster_range *out)
{
    size_t bytes;
    if (!p || !rows || !tail || (rows-1u && stride > (SIZE_MAX-tail)/(rows-1u))) return 0;
    bytes = (rows-1u)*stride + tail;
    out->lo = (uintptr_t)p;
    if (bytes > UINTPTR_MAX-out->lo) return 0;
    out->hi = out->lo+bytes;
    return 1;
}
static int overlap(raster_range a, raster_range b)
{
    return a.lo < b.hi && b.lo < a.hi;
}
static int ranges(const nj_gfx_raster *r, raster_range *pixels,
                  raster_range *keys, raster_range *depth)
{
    const nj_surface *s;
    int bpp;
    raster_range packet, surface;
    if (!r || r->version != NJ_GFX_RASTER_VERSION || !(s=r->surface) ||
        !s->pixels || s->width<=0 || s->height<=0 || s->stride<=0 ||
        r->width<=0 || r->height<=0 || r->x<0 || r->y<0 ||
        r->x>s->width || r->width>s->width-r->x ||
        r->y>s->height || r->height>s->height-r->y ||
        !r->keys || r->key_stride<r->width ||
        (r->depth_test!=0 && r->depth_test!=1) ||
        (r->depth_write!=0 && r->depth_write!=1) || (r->color>>24)!=255u)
        return 0;
    bpp = s->format==NJ_PIXFMT_RGB565 ? 2 :
          s->format==NJ_PIXFMT_RGB888 ? 3 : s->format==NJ_PIXFMT_ARGB8888 ? 4 : 0;
    if (!bpp || (size_t)s->width > (size_t)s->stride/(size_t)bpp ||
        (size_t)r->key_stride > SIZE_MAX/sizeof(nj_s4) ||
        !range_make(s->pixels,(size_t)s->height,(size_t)s->stride,
                    (size_t)s->width*(size_t)bpp,pixels) ||
        !range_make(r->keys,(size_t)r->height,(size_t)r->key_stride*sizeof(nj_s4),
                    (size_t)r->width*sizeof(nj_s4),keys) ||
        !range_make(r,1,0,sizeof *r,&packet) ||
        !range_make(s,1,0,sizeof *s,&surface)) return 0;
    depth->lo=depth->hi=0;
    if ((r->depth_test || r->depth_write) && !r->depth) return 0;
    if (r->depth) {
        if (r->depth_stride<s->width ||
            (size_t)r->depth_stride>SIZE_MAX/sizeof(nj_s4) ||
            !range_make(r->depth,(size_t)s->height,(size_t)r->depth_stride*sizeof(nj_s4),
                        (size_t)s->width*sizeof(nj_s4),depth)) return 0;
    }
    if (overlap(*pixels,*keys) || overlap(*pixels,*depth) || overlap(*keys,*depth) ||
        overlap(*pixels,packet) || overlap(*depth,packet) ||
        overlap(*keys,packet) || overlap(*keys,surface) || overlap(packet,surface) ||
        overlap(*pixels,surface) || overlap(*depth,surface)) return 0;
    return 1;
}
int nj_gfx_raster_validate(const nj_gfx_raster *r)
{
    raster_range pixels, keys, depth;
    int x,y;
    if (!ranges(r,&pixels,&keys,&depth)) return 0;
    for(y=0;y<r->height;++y) {
        const nj_s4 *k=r->keys+(size_t)y*(size_t)r->key_stride;
        const nj_s4 *z=r->depth ? r->depth+(size_t)(y+r->y)*(size_t)r->depth_stride+r->x : NULL;
        for(x=0;x<r->width;++x) {
            if(k[x]<0 || k[x]>NJ_GFX_RASTER_MAX_KEY) return 0;
            if(z && (r->depth_test || r->depth_write) &&
               (z[x]<0 || z[x]>NJ_GFX_RASTER_MAX_KEY)) return 0;
        }
    }
    return 1;
}
int nj_gfx_raster_software(const nj_gfx_raster *r, nj_u4 *drawn)
{
    raster_range pixels, keys, depth, result, packet, surface;
    nj_u4 touched=0;
    int x,y;
    if(!drawn || !ranges(r,&pixels,&keys,&depth) ||
       !range_make(drawn,1,0,sizeof *drawn,&result) ||
       !range_make(r,1,0,sizeof *r,&packet) ||
       !range_make(r->surface,1,0,sizeof *r->surface,&surface) ||
       overlap(result,pixels) || overlap(result,keys) || overlap(result,depth) ||
       overlap(result,packet) || overlap(result,surface) || !nj_gfx_raster_validate(r))
        return NJ_GFX_ACCEL_DECLINE;
    for(y=0;y<r->height;++y) {
        const nj_s4 *k=r->keys+(size_t)y*(size_t)r->key_stride;
        nj_s4 *z=r->depth ? r->depth+(size_t)(y+r->y)*(size_t)r->depth_stride+r->x : NULL;
        for(x=0;x<r->width;++x) if(k[x] && (!r->depth_test || k[x]>=z[x])) {
            nj_surface_set(r->surface,r->x+x,r->y+y,r->color);
            if(r->depth_write) z[x]=k[x];
            touched=1;
        }
    }
    *drawn=touched;
    return NJ_GFX_ACCEL_DONE;
}
