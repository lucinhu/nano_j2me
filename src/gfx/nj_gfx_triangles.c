/* Pure-C validation for synchronous native triangle transactions. */
#include "nj2me/nj_gfx_triangles.h"
#include <stdint.h>
#include <stddef.h>
#include <math.h>

typedef struct tri_range { uintptr_t lo, hi; } tri_range;
static int make_range(const void *p, size_t rows, size_t stride, size_t tail,
                      tri_range *out)
{
    size_t bytes;
    if (!p || !rows || !tail || (rows > 1 && stride > (SIZE_MAX-tail)/(rows-1)))
        return 0;
    bytes = (rows-1)*stride + tail;
    out->lo = (uintptr_t)p;
    if (bytes > UINTPTR_MAX-out->lo) return 0;
    out->hi = out->lo+bytes;
    return 1;
}
static int overlaps(tri_range a, tri_range b)
{
    return a.lo < b.hi && b.lo < a.hi;
}
static int boolean_value(int x) { return x == 0 || x == 1; }
int nj_gfx_triangles_validate(const nj_gfx_triangles *r, const nj_u4 *submitted)
{
    const nj_surface *s;
    tri_range pixels, depth = {0,0}, triangles, packet, surface, result;
    tri_range writes[3], inputs[3];
    nj_u4 i;
    int bpp, a, b, needs_depth = 0, x, y;
    if (!r || r->version != NJ_GFX_TRIANGLES_VERSION || !(s=r->surface) ||
        !s->pixels || s->width<=0 || s->height<=0 || s->stride<=0 ||
        r->x<0 || r->y<0 || r->width<=0 || r->height<=0 ||
        r->x>s->width || r->width>s->width-r->x ||
        r->y>s->height || r->height>s->height-r->y ||
        !r->triangles || !r->count || r->count>NJ_GFX_TRIANGLES_MAX_COUNT ||
        !submitted) return 0;
    bpp = s->format==NJ_PIXFMT_RGB565 ? 2 : s->format==NJ_PIXFMT_RGB888 ? 3 :
          s->format==NJ_PIXFMT_ARGB8888 ? 4 : 0;
    if (!bpp || (size_t)s->width>(size_t)s->stride/(size_t)bpp ||
        !make_range(s->pixels,(size_t)s->height,(size_t)s->stride,
                    (size_t)s->width*(size_t)bpp,&pixels) ||
        !make_range(r->triangles,(size_t)r->count,sizeof *r->triangles,
                    sizeof *r->triangles,&triangles) ||
        !make_range(r,1,0,sizeof *r,&packet) ||
        !make_range(s,1,0,sizeof *s,&surface) ||
        !make_range(submitted,1,0,sizeof *submitted,&result)) return 0;
    if (r->depth) {
        if ((uintptr_t)r->depth % sizeof(nj_s4) || r->depth_stride<s->width ||
            (size_t)r->depth_stride>SIZE_MAX/sizeof(nj_s4) ||
            !make_range(r->depth,(size_t)s->height,
                (size_t)r->depth_stride*sizeof(nj_s4),
                (size_t)s->width*sizeof(nj_s4),&depth)) return 0;
    }
    writes[0]=pixels; writes[1]=depth; writes[2]=result;
    inputs[0]=triangles; inputs[1]=packet; inputs[2]=surface;
    for (a=0;a<3;++a) {
        for (b=a+1;b<3;++b) if (overlaps(writes[a],writes[b])) return 0;
        for (b=0;b<3;++b) if (overlaps(writes[a],inputs[b])) return 0;
    }
    /* Descriptor/data overlap is forbidden as well: a port must never mutate
     * the shape of the borrowed packet while reading its triangle storage. */
    for (a=0;a<3;++a)
        for (b=a+1;b<3;++b) if (overlaps(inputs[a],inputs[b])) return 0;
    for (i=0;i<r->count;++i) {
        const nj_gfx_triangle_cmd *t=r->triangles+i;
        int j;
        if (!boolean_value(t->alpha_write) || !boolean_value(t->depth_test) ||
            !boolean_value(t->depth_write) || !boolean_value(t->texture_no_alpha) ||
            t->blend<64 || t->blend>68 ||
            t->alpha_threshold<0 || t->alpha_threshold>255 ||
            (t->fog!=0 && t->fog!=80 && t->fog!=81) ||
            (t->fog && (!isfinite(t->fog_near) || !isfinite(t->fog_far) ||
                         !isfinite(t->fog_density) || t->fog_density<0))) return 0;
        needs_depth |= t->depth_test || t->depth_write;
        for (j=0;j<3;++j) {
            const nj_gfx_triangle_vertex *v=t->vertices+j;
            if (!isfinite(v->x) || !isfinite(v->y) || !isfinite(v->depth) ||
                !isfinite(v->inv_w) || v->inv_w<=0 ||
                !isfinite(v->u) || !isfinite(v->v) ||
                !isfinite(v->u*v->inv_w) || !isfinite(v->v*v->inv_w)) return 0;
        }
        if (t->texture) {
            tri_range texture;
            if (t->texture_width<=0 || t->texture_height<=0 ||
                (t->texture_bpp!=3 && t->texture_bpp!=4) ||
                (t->wrap_s!=240 && t->wrap_s!=241) ||
                (t->wrap_t!=240 && t->wrap_t!=241) ||
                (t->filter!=208 && t->filter!=209) ||
                (t->texture_function!=226 && t->texture_function!=227 &&
                 t->texture_function!=228) ||
                (size_t)t->texture_width>SIZE_MAX/(size_t)t->texture_bpp ||
                !make_range(t->texture,(size_t)t->texture_height,
                    (size_t)t->texture_width*(size_t)t->texture_bpp,
                    (size_t)t->texture_width*(size_t)t->texture_bpp,&texture)) return 0;
            for (a=0;a<3;++a)
                if (overlaps(texture,writes[a]) || overlaps(texture,inputs[a])) return 0;
        }
    }
    if (needs_depth && !r->depth) return 0;
    if (needs_depth) {
        for (y=r->y;y<r->y+r->height;++y) {
            const nj_s4 *z=r->depth+(size_t)y*(size_t)r->depth_stride+r->x;
            for (x=0;x<r->width;++x)
                if (z[x]<0 || z[x]>16777216) return 0;
        }
    }
    return 1;
}

int nj_gfx_triangles_list_validate(const nj_gfx_triangles_list *l,
                                   const nj_u4 *submitted)
{
    tri_range result, list_range, array;
    nj_u4 i, j, total = 0;
    if (!l || l->version != NJ_GFX_TRIANGLES_LIST_VERSION || !l->surface ||
        !l->packets || !l->count ||
        l->count > NJ_GFX_TRIANGLES_LIST_MAX_PACKETS || !submitted) return 0;
    if (!make_range(l,1,0,sizeof *l,&list_range) ||
        !make_range(l->packets,(size_t)l->count,sizeof *l->packets,
                    sizeof *l->packets,&array) ||
        !make_range(submitted,1,0,sizeof *submitted,&result)) return 0;
    for (i=0;i<l->count;++i) {
        const nj_gfx_triangles *p=l->packets[i];
        tri_range desc, tris;
        nj_u4 scratch = 0;
        if (!p || p->version != NJ_GFX_TRIANGLES_VERSION ||
            p->surface != l->surface || p->depth != l->depth ||
            p->depth_stride != l->depth_stride ||
            !nj_gfx_triangles_validate(p,&scratch)) return 0;
        if (p->count > NJ_GFX_TRIANGLES_LIST_MAX_TOTAL-total) return 0;
        total += p->count;
        if (!make_range(p,1,0,sizeof *p,&desc) ||
            !make_range(p->triangles,(size_t)p->count,sizeof *p->triangles,
                        sizeof *p->triangles,&tris)) return 0;
        if (overlaps(desc,result) || overlaps(desc,list_range) ||
            overlaps(desc,array) || overlaps(tris,result) ||
            overlaps(tris,list_range) || overlaps(tris,array)) return 0;
        /* Descriptors and triangle arrays stay disjoint from each other: a
         * port must never mutate one packet while reading another. */
        for (j=0;j<i;++j) {
            const nj_gfx_triangles *q=l->packets[j];
            tri_range qdesc, qtris;
            if (!make_range(q,1,0,sizeof *q,&qdesc) ||
                !make_range(q->triangles,(size_t)q->count,sizeof *q->triangles,
                            sizeof *q->triangles,&qtris)) return 0;
            if (overlaps(desc,qdesc) || overlaps(desc,qtris) ||
                overlaps(tris,qdesc) || overlaps(tris,qtris)) return 0;
        }
    }
    return 1;
}
