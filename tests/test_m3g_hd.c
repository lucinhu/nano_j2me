/* Render through the public M3G API at 64x64, with an independent 128x128
 * binding as a raster reference. Compare both products of each HD draw. */
#define main m3g_depth_fixture_main
#include "test_m3g_depth.c"
#undef main
#include "../src/midp/nj_midp.h"
#if NJ_ENABLE_VULKAN
#include "nj2me/nj_gfx_vulkan.h"
#endif

#if NJ_ENABLE_M3G
static const nj_u4 foreground = 0xffff2020u;

static void triangle(nj_vm *vm, canvas *c, const nj_s2 xyz[9])
{
    nj_value a[5];
    memcpy(nj_array_data(c->data), xyz, 9 * sizeof(*xyz));
    a[0].i = (nj_s4)foreground;
    call(vm,c->vb,C("VertexBuffer"),"setDefaultColor","(I)V",a,NULL);
    a[0].r=c->vb; a[1].r=c->ib; a[2].r=c->app; a[3].r=NULL; a[4].i=0;
    call(vm,c->g3,C("Graphics3D"),"render","(L" C("VertexBuffer") ";L" C("IndexBuffer") ";L" C("Appearance") ";L" C("Transform") ";I)V",a,NULL);
}

static void diagonal(nj_vm *vm, canvas *c)
{
    const nj_s2 xyz[9] = {-2,-2,-4, 3,-1,-4, -1,3,-4};
    triangle(vm,c,xyz);
}

static nj_surface *companion(nj_vm *vm, canvas *c)
{
    nj_surface logical;
    nj_render_mirror *m = nj_midp_hd_target(vm, nj_midp_surface(vm,c->image,&logical));
    if (!m || !m->target) { fputs("missing HD target\n", stderr); exit(2); }
    return m->target;
}

static void check_geometry(nj_vm *vm, canvas *c, int subdivide,
                           const nj_u4 *baseline, const nj_s4 *logical_z, const canvas *reference)
{
    int mixed = 0, differing = 0;
    nj_surface *hd;
    vm->render_quality.subdivide = subdivide;
    default_clear(vm,c);
    diagonal(vm,c);
    hd = companion(vm,c);
    check(hd->width == 128 && hd->height == 128,"2x allocates the expected target");
    const nj_u4 *ref = nj_array_data(reference->pixels);
    for (int y=0; y<hd->height; ++y)
        for (int x=0; x<hd->width; ++x)
            differing += nj_surface_get(hd,x,y) != ref[y*hd->width+x];
    for (int y=0; y<64; ++y) for (int x=0; x<64; ++x) {
        nj_u4 a = nj_surface_get(hd,2*x,2*y);
        mixed += a != nj_surface_get(hd,2*x+1,2*y) || a != nj_surface_get(hd,2*x,2*y+1) ||
                 a != nj_surface_get(hd,2*x+1,2*y+1);
    }
    printf("subdivision=%d: %d partially covered logical pixels, %d HD differences from independent 128x128\n",subdivide,mixed,differing);
    check(mixed >= 20,"slanted edges cover distinct samples within logical pixels");
    check(differing == 0,"HD coverage matches independently projected geometry");
    check(memcmp(baseline,nj_array_data(c->pixels),64*64*4)==0,"Java image unchanged by HD pass");
    check(memcmp(logical_z,nj_array_data(zbuf(vm,c)),64*64*4)==0,"logical depth unchanged by HD pass");
}

#if NJ_ENABLE_GFX_ACCEL
static unsigned filtered_faces;
static int expected_filter = 209, expected_width = 8;
static int trace_filtered_texture(void *ud, const nj_gfx_triangles *packet, nj_u4 *submitted)
{
    const nj_port *driver = ud;
    for (nj_u4 i = 0; i < packet->count; ++i) {
        const nj_gfx_triangle_cmd *face = &packet->triangles[i];
        check(face->texture_width == expected_width && face->texture_height == 1 &&
              face->filter == expected_filter, "physical packet carries the expected mip and sampler");
    }
    if (!driver->gfx_hd_triangles) return NJ_GFX_ACCEL_DECLINE;
    int status = driver->gfx_hd_triangles(driver->gfx_hd_triangles_ud, packet, submitted);
    if (status == NJ_GFX_ACCEL_DONE) filtered_faces += *submitted;
    return status;
}
#endif

/* Compare opt-in smoothing against a separate physical-size binding where
 * the application explicitly asked for FILTER_LINEAR on the same 8x1 atlas. */
static void check_mesh_filter(nj_vm *vm, nj_port *port, const nj_port *driver,
                              canvas *c, canvas *ref, nj_ref tex, const nj_s2 xyz[9])
{
    nj_u4 original[64*64];
    nj_value a[2];
    const nj_u4 *expected = nj_array_data(ref->pixels);
    int intermediate = 0, outliers = 0;
    memcpy(original, nj_array_data(c->pixels), sizeof(original));
    for (int pass = 0; pass < 2; ++pass) {
        canvas *target = pass ? c : ref;
        vm->render_quality.scale = pass ? 2 : 1;
        vm->render_quality.smooth_assets = pass;
        a[0].i=208; a[1].i=pass ? 210 : 209;
        call(vm,tex,C("Texture2D"),"setFiltering","(II)V",a,NULL);
        a[0].i=0; a[1].r=tex;
        call(vm,target->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a,NULL);
#if NJ_ENABLE_GFX_ACCEL
        if (pass) {
            port->gfx_hd_triangles = trace_filtered_texture;
            port->gfx_hd_triangles_ud = (void *)driver;
        }
#else
        (void)port; (void)driver;
#endif
        default_clear(vm,target);
        triangle(vm,target,xyz);
        a[0].i=0; a[1].r=NULL;
        call(vm,target->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a,NULL);
    }
#if NJ_ENABLE_GFX_ACCEL
    check(!driver->gfx_hd_triangles || filtered_faces == 4,
          "the selected GPU commits all four textured faces");
    port->gfx_hd_triangles = NULL;
    port->gfx_hd_triangles_ud = NULL;
#endif
    nj_surface *hd = companion(vm,c);
    for (int i=0; i<128*128; ++i) {
        nj_u4 got = nj_surface_get(hd,i%128,i/128);
        int red = (int)((got >> 16) & 255u);
        intermediate += (red % 30) != 0;
        /* Hardware normalizes and rounds to UNORM8. Compare the individual
         * channels and tolerate a small number of top-left edge ties. */
        if (abs(red - (int)((expected[i] >> 16) & 255u)) > 1 ||
            abs((int)((got >> 8) & 255u) - (int)((expected[i] >> 8) & 255u)) > 1 ||
            (got & 0xff0000ffu) != (expected[i] & 0xff0000ffu)) ++outliers;
    }
    printf("8x1 linear: %d intermediate samples, %d outliers from independent reference\n",
           intermediate, outliers);
    check(intermediate > 200 && outliers < 8,"thin texture filters along its varying axis");
    check(!memcmp(original,nj_array_data(c->pixels),sizeof(original)),
          "Java image retains the eight requested nearest colour bands");
    /* Measure the width of the sharp transition, then compare every GPU
     * pixel with the same public draw using the CPU backend. */
    nj_u4 crisp[128*128];
    vm->render_quality.smooth_assets = NJ_ASSET_PIXEL_ART;
    for (int gpu=0; gpu<=1; ++gpu) {
        int mixed = 0, errors = 0;
#if NJ_ENABLE_GFX_ACCEL
        expected_filter = NJ_GFX_FILTER_SHARP;
        filtered_faces = 0;
        port->gfx_hd_triangles = gpu ? trace_filtered_texture : NULL;
        port->gfx_hd_triangles_ud = (void *)driver;
#endif
        a[0].i=0; a[1].r=tex;
        call(vm,c->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a,NULL);
        default_clear(vm,c); triangle(vm,c,xyz);
        for (int i=0; i<128*128; ++i) {
            nj_u4 got=nj_surface_get(hd,i%128,i/128);
            mixed += ((got>>16)&255u) % 30 != 0;
            if (!gpu) crisp[i]=got;
            else if (abs((int)((got>>16)&255u)-(int)((crisp[i]>>16)&255u))>1 ||
                     abs((int)((got>>8)&255u)-(int)((crisp[i]>>8)&255u))>1 ||
                     (got & 0xff0000ffu)!=(crisp[i] & 0xff0000ffu)) ++errors;
        }
        printf("8x1 sharp: %d intermediate samples, %d outliers from software\n",mixed,errors);
        check(mixed>20 && mixed<intermediate/2 && errors<8,"sharp filter narrows the ramp on all backends");
#if NJ_ENABLE_GFX_ACCEL
        check(!gpu || !driver->gfx_hd_triangles || filtered_faces==4,"GPU commits four sharp faces");
        port->gfx_hd_triangles = NULL;
        port->gfx_hd_triangles_ud = NULL;
#endif
    }
    check(!memcmp(original,nj_array_data(c->pixels),sizeof(original)),"sharp mode keeps logical nearest colours");
    a[0].i=0; a[1].r=NULL;
    call(vm,c->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a,NULL);
    vm->render_quality.smooth_assets = 0;
}

static void check_perspective(nj_vm *vm, nj_port *port, const nj_port *driver, canvas *c, canvas *ref)
{
    const nj_s2 xyz[9] = {-1,-1,-2, 5,-2,-8, -1,3,-4};
    const nj_s2 uv[6] = {1,0, 64,0, 1,64};
    nj_ref image = nj_root(vm,nj_object_new(vm,C("Image2D")));
    nj_ref tex = nj_root(vm,nj_object_new(vm,C("Texture2D")));
    nj_ref coords = nj_root(vm,nj_object_new(vm,C("VertexArray")));
    nj_ref pixels = nj_root(vm,nj_array_new(vm,"B",32));
    nj_ref values = nj_root(vm,nj_array_new(vm,"S",6));
    nj_u1 *bytes = nj_array_data(pixels);
    nj_value a[5];
    for (int i=0; i<8; ++i) {
        bytes[4*i]=30u*i; bytes[4*i+1]=255u-30u*i; bytes[4*i+2]=0; bytes[4*i+3]=255;
    }
    a[0].i=100; a[1].i=8; a[2].i=1;
    if (nj_call_special(vm,image,C("Image2D"),"<init>","(III)V",a,NULL)!=NJ_OK) exit(2);
    a[0].i=a[1].i=0; a[2].i=8; a[3].i=1; a[4].r=pixels;
    call(vm,image,C("Image2D"),"set","(IIII[B)V",a,NULL);
    a[0].r=image;
    if (nj_call_special(vm,tex,C("Texture2D"),"<init>","(L" C("Image2D") ";)V",a,NULL)!=NJ_OK) exit(2);
    a[0].i=228; call(vm,tex,C("Texture2D"),"setBlending","(I)V",a,NULL);
    a[0].i=a[1].i=240; call(vm,tex,C("Texture2D"),"setWrapping","(II)V",a,NULL);
    a[0].i=a[1].i=208; call(vm,tex,C("Texture2D"),"setFiltering","(II)V",a,NULL);
    a[0].i=3; a[1].i=a[2].i=2;
    if (nj_call_special(vm,coords,C("VertexArray"),"<init>","(III)V",a,NULL)!=NJ_OK) exit(2);
    memcpy(nj_array_data(values),uv,sizeof(uv));
    a[0].i=0; a[1].i=3; a[2].r=values;
    call(vm,coords,C("VertexArray"),"set","(II[S)V",a,NULL);
    for (int pass=0; pass<2; ++pass) {
        canvas *target = pass ? c : ref;
        vm->render_quality.scale=pass ? 2 : 1;
        a[0].i=0; a[1].r=coords; a[2].f=1.0f/64.0f; a[3].r=NULL;
        call(vm,target->vb,C("VertexBuffer"),"setTexCoords","(IL" C("VertexArray") ";F[F)V",a,NULL);
        a[0].i=0; a[1].r=tex;
        call(vm,target->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a,NULL);
        a[0].i=68; call(vm,target->cm,C("CompositingMode"),"setBlending","(I)V",a,NULL);
        default_clear(vm,target); triangle(vm,target,xyz);
        a[0].i=0; a[1].r=NULL; call(vm,target->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a,NULL);
    }
    nj_surface *hd=companion(vm,c);
    const nj_u4 *reference=nj_array_data(ref->pixels);
    int differences=0, bands=0;
    for (int i=0; i<128*128; ++i) {
        nj_u4 value=reference[i];
        differences += nj_surface_get(hd,i%128,i/128)!=value;
        if (((value >> 8)&255u) && (value&255u)==0u) bands |= 1 << (((value >> 16)&255u)/30u);
    }
    printf("perspective texture: sampled bands=%x, HD differences=%d\n",bands,differences);
    check(bands==255,"perspective fixture samples all eight texture bands");
    check(differences==0,"subdivision preserves perspective texture coordinates and coverage");
    check_mesh_filter(vm,port,driver,c,ref,tex,xyz);

    /* Minify the same atlas: keep the selected level and game sampler, even
     * for 8x1 (area alone misses minification when V is constant). */
    a[0].i=0; a[1].r=coords; a[2].f=16.0f; a[3].r=NULL;
    call(vm,c->vb,C("VertexBuffer"),"setTexCoords","(IL" C("VertexArray") ";F[F)V",a,NULL);
    a[0].i=0; a[1].r=tex;
    call(vm,c->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a,NULL);
    for (int mip=0; mip<=1; ++mip) {
        a[0].i=mip ? 210 : 208; a[1].i=mip ? 210 : 209;
        call(vm,tex,C("Texture2D"),"setFiltering","(II)V",a,NULL);
        vm->render_quality.smooth_assets=NJ_ASSET_PIXEL_ART;
#if NJ_ENABLE_GFX_ACCEL
        expected_width = mip ? 1 : 8;
        expected_filter = mip ? 208 : 209;
        filtered_faces = 0;
        port->gfx_hd_triangles=trace_filtered_texture; port->gfx_hd_triangles_ud=(void *)driver;
#endif
        default_clear(vm,c); triangle(vm,c,xyz);
#if NJ_ENABLE_GFX_ACCEL
        check(!driver->gfx_hd_triangles || filtered_faces==4,"GPU commits minified game filters");
        port->gfx_hd_triangles=NULL; port->gfx_hd_triangles_ud=NULL;
#endif
        nj_u4 saved[128*128];
        memcpy(saved,hd->pixels,sizeof(saved));
        vm->render_quality.smooth_assets=0;
        default_clear(vm,c); triangle(vm,c,xyz);
        int errors=0;
        for (int i=0; i<128*128; ++i) errors += saved[i]!=((nj_u4 *)hd->pixels)[i];
        check(errors < 8,"minification matches game sampling with and without mip selection");
    }
    a[0].i=0; a[1].r=NULL;
    call(vm,c->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a,NULL);

    /* Previously untested HD paths: an image Background at a cropped
     * viewport, then a scaled Sprite3D. Both use the physical sampling grid. */
    nj_ref sprite=nj_root(vm,nj_object_new(vm,C("Sprite3D")));
    a[0].i=1; a[1].r=image; a[2].r=c->app;
    if (nj_call_special(vm,sprite,C("Sprite3D"),"<init>","(ZL" C("Image2D") ";L" C("Appearance") ";)V",a,NULL)!=NJ_OK) exit(2);
    nj_ref model=make(vm,C("Transform"));
    a[0].f=0.25f; a[1].f=0; a[2].f=-3;
    call(vm,model,C("Transform"),"postTranslate","(FFF)V",a,NULL);
    a[0].f=2; a[1].f=2; a[2].f=1;
    call(vm,model,C("Transform"),"postScale","(FFF)V",a,NULL);
    for (int pass=0; pass<2; ++pass) {
        canvas *target = pass ? c : ref;
        int k=pass ? 1 : 2;
        vm->render_quality.scale=pass ? 2 : 1;
        default_clear(vm,target);
        viewport(vm,target,3*k,5*k,53*k,49*k);
        a[0].r=image; call(vm,target->bg,C("Background"),"setImage","(L" C("Image2D") ";)V",a,NULL);
        a[0].i=0; a[1].i=0; a[2].i=8; a[3].i=1;
        call(vm,target->bg,C("Background"),"setCrop","(IIII)V",a,NULL);
        clear(vm,target,1,1);
        a[0].r=sprite; a[1].r=model;
        call(vm,target->g3,C("Graphics3D"),"render","(L" C("Node") ";L" C("Transform") ";)V",a,NULL);
        a[0].r=NULL; call(vm,target->bg,C("Background"),"setImage","(L" C("Image2D") ";)V",a,NULL);
        viewport(vm,target,0,0,target->w,target->h);
    }
    check(reference[60*128+60] != reference[12*128+60],"scaled sprite visible over background");
    check(memcmp(hd->pixels,reference,128u*128u*sizeof(*reference))==0,
          "cropped image Background and scaled Sprite3D match 128x128 reference");

    /* The cropped frame has an opaque red texel beside transparent green.
     * Its opaque blue neighbours expose a sample that escapes the crop. */
    const nj_u1 atlas[] = {0,0,255,255, 255,0,0,255, 0,255,0,0, 0,0,255,255};
    nj_ref rgba = nj_root(vm,nj_object_new(vm,C("Image2D")));
    nj_ref data = nj_root(vm,nj_array_new(vm,"B",sizeof(atlas)));
    nj_u4 logical[64*64];
    memcpy(nj_array_data(data),atlas,sizeof(atlas));
    a[0].i=100; a[1].i=4; a[2].i=1; a[3].r=data;
    if (nj_call_special(vm,rgba,C("Image2D"),"<init>","(III[B)V",a,NULL)!=NJ_OK) exit(2);
    a[0].r=rgba; call(vm,sprite,C("Sprite3D"),"setImage","(L" C("Image2D") ";)V",a,NULL);
    a[0].i=1; a[1].i=0; a[2].i=2; a[3].i=1;
    call(vm,sprite,C("Sprite3D"),"setCrop","(IIII)V",a,NULL);
    a[0].i=64; call(vm,c->cm,C("CompositingMode"),"setBlending","(I)V",a,NULL);
    int smooth_partial=0;
    for (int smooth=0; smooth<=NJ_ASSET_PIXEL_ART; ++smooth) {
        int partial=0, contaminated=0;
        vm->render_quality.smooth_assets=smooth;
        default_clear(vm,c);
        a[0].r=sprite; a[1].r=model;
        call(vm,c->g3,C("Graphics3D"),"render","(L" C("Node") ";L" C("Transform") ";)V",a,NULL);
        for (int y=0; y<128; ++y) for (int x=0; x<128; ++x) {
            nj_u4 rgb=nj_surface_get(hd,x,y) & 0xffffffu;
            partial += rgb>0 && rgb<0xff0000u;
            contaminated += (rgb & 0xffffu)!=0;
        }
        check(contaminated==0,"neighbour frame and hidden RGB never tint the sprite");
        check(smooth ? partial>20 : partial==0,"linear sprite edges have fractional coverage");
        if (smooth == NJ_ASSET_SMOOTH) smooth_partial=partial;
        if (smooth == NJ_ASSET_PIXEL_ART) check(partial<smooth_partial/2,"sharp cropped sprite keeps a narrow coverage ramp");
        if (!smooth) memcpy(logical,nj_array_data(c->pixels),sizeof(logical));
        else check(!memcmp(logical,nj_array_data(c->pixels),sizeof(logical)),
                   "the cropped Sprite3D retains its logical image");
    }
    /* Render a mesh with arbitrary green in fully transparent texels. The
     * coverage ramp must produce only red when composited over black. */
    for (int i=0; i<8; ++i) {
        nj_u1 *p = (nj_u1 *)nj_array_data(pixels) + 4*i;
        p[0]=p[3]=i%2 ? 0 : 255; p[1]=255-p[0]; p[2]=0;
    }
    a[0].i=a[1].i=0; a[2].i=8; a[3].i=1; a[4].r=pixels;
    call(vm,image,C("Image2D"),"set","(IIII[B)V",a,NULL);
    a[0].i=0; a[1].r=coords; a[2].f=1.0f/64.0f; a[3].r=NULL;
    call(vm,c->vb,C("VertexBuffer"),"setTexCoords","(IL" C("VertexArray") ";F[F)V",a,NULL);
    a[0].i=0; a[1].r=tex;
    call(vm,c->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a,NULL);
#if NJ_ENABLE_GFX_ACCEL
    expected_width=8; expected_filter=NJ_GFX_FILTER_SHARP; filtered_faces=0;
    port->gfx_hd_triangles=trace_filtered_texture; port->gfx_hd_triangles_ud=(void *)driver;
#endif
    default_clear(vm,c); triangle(vm,c,xyz);
    int partial=0, fringe=0;
    for (int i=0; i<128*128; ++i) {
        nj_u4 rgb=nj_surface_get(hd,i%128,i/128)&0xffffffu;
        partial+=rgb>0 && rgb<0xff0000u; fringe+=(rgb&0xffffu)!=0;
    }
    check(partial>100 && !fringe,"transparent mesh texels contribute coverage without hidden RGB");
#if NJ_ENABLE_GFX_ACCEL
    check(!driver->gfx_hd_triangles || filtered_faces==1,"GPU commits the single alpha-blended face");
    port->gfx_hd_triangles=NULL; port->gfx_hd_triangles_ud=NULL;
#endif
    a[0].i=0; a[1].r=NULL;
    call(vm,c->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a,NULL);
    vm->render_quality.smooth_assets=0;
}

#if NJ_ENABLE_GFX_ACCEL
static unsigned list_calls, packet_calls;

static int decline_packet(void *ud, const nj_gfx_triangles *packet, nj_u4 *submitted)
{
    (void)ud; (void)packet; (void)submitted;
    ++packet_calls;
    return NJ_GFX_ACCEL_DECLINE;
}

static int decline_list(void *ud, const nj_gfx_triangles_list *list, nj_u4 *submitted)
{
    (void)ud; (void)list; (void)submitted;
    ++list_calls;
    return NJ_GFX_ACCEL_DECLINE;
}

/* Run the public M3G draw through exactly one physical packet. A CPU-only
 * callback declines; optional CTest variants forward to the real driver. */
typedef struct hd_probe {
    nj_gfx_triangles_fn forward;
    void *backend;
    nj_surface *target;
    void *logical_depth;
    unsigned calls, accepted;
} hd_probe;

static int trace_physical(void *ud, const nj_gfx_triangles *packet, nj_u4 *submitted)
{
    hd_probe *p = ud;
    ++p->calls;
    check(packet->surface == p->target && packet->depth != p->logical_depth &&
          packet->depth_stride == 128, "native draw uses physical colour and depth");
    check(packet->count == 4 && packet->width < 128 && packet->height < 128,
          "one bounded physical packet contains the four subdivided faces");
    if (!p->forward) return NJ_GFX_ACCEL_DECLINE;
    int result = p->forward(p->backend, packet, submitted);
    if (result == NJ_GFX_ACCEL_DONE) p->accepted += *submitted;
    return result;
}

static void check_physical(nj_vm *vm, nj_port *port, canvas *c, const canvas *ref,
                           const nj_u4 *logical, const nj_s4 *z, const nj_port *backend)
{
    hd_probe probe = {backend->gfx_hd_triangles, backend->gfx_hd_triangles_ud,
                      companion(vm,c), nj_array_data(zbuf(vm,c)), 0, 0};
    const nj_u4 *expected = nj_array_data(ref->pixels);
    unsigned before = packet_calls, different = 0, interior = 0;
    port->gfx_hd_triangles = trace_physical;
    port->gfx_hd_triangles_ud = &probe;
    port->gfx_triangles = decline_packet;
    vm->render_quality.subdivide = 1;
    default_clear(vm,c);
    diagonal(vm,c);
    check(probe.calls == 1 && packet_calls == before && !nj_m3g_pending(vm),
          "HD submission leaves Java pixels synchronous without a second GPU draw");
    check(!probe.forward || probe.accepted == 4, "selected driver committed all four faces");
    size_t logical_pixels = (size_t)c->w * c->h;
    check(!memcmp(logical,nj_array_data(c->pixels),logical_pixels*sizeof(*logical)) &&
          !memcmp(z,nj_array_data(zbuf(vm,c)),logical_pixels*sizeof(*z)), "logical colour and Z match software byte for byte");
    for (int y=0; y<128; ++y) for (int x=0; x<128; ++x) {
        int at = y*128+x;
        if (nj_surface_get(probe.target,x,y) == expected[at]) continue;
        ++different;
        /* Native coverage can differ at an edge. The entire interior of this
         * flat-colour scene and the untouched exterior must still be exact. */
        if (x==0 || y==0 || x==127 || y==127 ||
            (expected[at]==expected[at-1] && expected[at]==expected[at+1] &&
             expected[at]==expected[at-128] && expected[at]==expected[at+128])) ++interior;
    }
    printf("physical packet: %u accepted faces, %u / 16384 different pixels, %u interior differences\n",
           probe.accepted, different, interior);
    check(probe.forward ? interior==0 && different<=160 : different==0,
          "native edges stay bounded; declined draws replay the exact HD image");
    port->gfx_hd_triangles = NULL;
    port->gfx_hd_triangles_ud = NULL;
    port->gfx_triangles = NULL;
}

static void check_queue(nj_vm *vm, nj_port *port, canvas *c)
{
    nj_surface *hd = companion(vm,c);
    nj_u4 expected;
    default_clear(vm,c);
    draw(vm,c,3,0x8080ff00u,2);
    expected=nj_surface_get(hd,64,64);
    check((expected&0xffffffu)!=0,"single translucent draw produces a visible reference");
    default_clear(vm,c);
    port->gfx_triangles=decline_packet;
    port->gfx_triangles_list=decline_list;
    draw(vm,c,3,0x8080ff00u,2);
    check(nj_m3g_pending(vm) && pixel(c,32,32)==0,"native journal holds the authoritative draw");
    check(nj_surface_get(hd,64,64)==expected,"HD draw is visible before the deferred commit");
    nj_m3g_flush(vm);
    check(list_calls && packet_calls,"native collection and decline replay execute");
    check(nj_surface_get(hd,64,64)==expected && pixel(c,32,32)==(expected&0xffffffu),
          "logical fallback and HD companion each blend exactly once");
    /* Sprite.paint must commit earlier M3G work before drawing the overlay. */
    const char *sprite_class = "javax/microedition/lcdui/game/Sprite";
    nj_ref image = nj_root(vm,nj_midp_image_new(vm,1,1,1));
    nj_ref sprite = nj_root(vm,nj_object_new(vm,sprite_class));
    nj_value a[2];
    nj_midp_image_pixels(vm,image)[0] = 0xffa020e0u;
    a[0].r=image;
    if (nj_call_special(vm,sprite,sprite_class,"<init>","(L" IMAGE ";)V",a,NULL)!=NJ_OK) exit(2);
    a[0].i=a[1].i=32;
    call(vm,sprite,"javax/microedition/lcdui/game/Layer","setPosition","(II)V",a,NULL);
    default_clear(vm,c);
    draw(vm,c,3,0xff00ff00u,0);
    check(nj_m3g_pending(vm),"ordered overlay starts with a queued M3G draw");
    a[0].r=c->graphics;
    call(vm,sprite,sprite_class,"paint","(Ljavax/microedition/lcdui/Graphics;)V",a,NULL);
    check(!nj_m3g_pending(vm) && pixel(c,32,32)==0xa020e0u && pixel(c,31,32)==0x00ff00u &&
          nj_surface_get(hd,64,64)==0xffa020e0u,"Sprite.paint overlays the committed 3D colour on both targets");
    port->gfx_triangles= NULL;
    port->gfx_triangles_list= NULL;
}
#endif

static void check_weak_depth(nj_vm *vm, canvas *c)
{
    nj_u4 mark = nj_root_mark(vm);
    canvas other = *c;
    nj_value a[2] = {{0}};
    nj_u8 before;
    other.g3 = make(vm,C("Graphics3D"));
    a[0].r = NULL;
    nj_value camera;
    call(vm,c->g3,C("Graphics3D"),"getCamera","(L" C("Transform") ";)L" C("Camera") ";",a,&camera);
    a[0]=camera; a[1].r=NULL;
    call(vm,other.g3,C("Graphics3D"),"setCamera","(L" C("Camera") ";L" C("Transform") ";)V",a,NULL);
    a[0].r=c->graphics;
    call(vm,other.g3,C("Graphics3D"),"bindTarget","(Ljava/lang/Object;)V",a,NULL);
    draw(vm,&other,5,0xff0000ffu,0);
    check((nj_surface_get(companion(vm,c),64,64)&0xffffffu)==0xffu,"two Graphics3D bindings have independent HD depth");
    nj_gc_collect(vm); /* Drop unrelated unreachable objects while other is rooted. */
    before = vm->host_count;
    nj_root_release(vm, mark);
    nj_gc_collect(vm);
    check(before == vm->host_count + 1,"GC reclaims the one abandoned Graphics3D depth allocation");
}
#endif

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
#if !NJ_ENABLE_M3G
    puts("SKIP: NJ_ENABLE_M3G=0"); return 0;
#else
    nj_port port;
    nj_vm *vm;
    canvas c, ref;
    nj_u4 baseline[64*64];
    nj_s4 logical_z[64*64];
    nj_port backend = {0};
#if NJ_ENABLE_GFX_ACCEL
    if (argc > 1) {
#if NJ_ENABLE_VULKAN
        if (strcmp(argv[1],"vulkan")==0 && !nj_vk_renderer_attach(&backend)) return 77;
#endif
#if NJ_TEST_HD_GLES2
        if (strcmp(argv[1],"gles2")==0) {
            int w=64, h=64, stride=0;
            void *pixels=NULL;
            if (nj_port_sdl2_ex(&backend,"M3G HD test",1)!=NJ_OK) return 77;
            if (!backend.display_init || backend.display_init(backend.ud,&w,&h,&stride,&pixels)<0) {
                nj_port_shutdown(&backend);
                return 77;
            }
        }
#endif
        if (!backend.gfx_hd_triangles) { nj_port_shutdown(&backend); return 2; }
    }
#endif
    nj_port_headless_ex(&port,64,64,0,NULL);
    vm=nj_vm_create(&port,4u*1024u*1024u);
    if (!vm) return 2;
    (void)nj_lib_late_init(vm);
    init(vm,&c); init(vm,&ref);
    release(vm,&ref); bind_image(vm,&ref,128,128);
    vm->render_quality.scale=vm->render_quality.ssaa=1;
    default_clear(vm,&ref); diagonal(vm,&ref);
    default_clear(vm,&c); diagonal(vm,&c);
    memcpy(baseline,nj_array_data(c.pixels),sizeof(baseline));
    memcpy(logical_z,nj_array_data(zbuf(vm,&c)),sizeof(logical_z));
    /* An 8x8 Java binding at 8x + four samples shares the reference's
     * 128x128 physical geometry. Keep the Java colour and depth as oracles. */
    {
        canvas small;
        nj_u4 colour[64];
        nj_s4 depth[64];
        init(vm,&small); release(vm,&small); bind_image(vm,&small,8,8);
        default_clear(vm,&small); diagonal(vm,&small);
        memcpy(colour,nj_array_data(small.pixels),sizeof colour);
        memcpy(depth,nj_array_data(zbuf(vm,&small)),sizeof depth);
        vm->render_quality.scale=8; vm->render_quality.ssaa=2;
        default_clear(vm,&small); diagonal(vm,&small);
        nj_surface *target = companion(vm,&small);
        const nj_u4 *expected = nj_array_data(ref.pixels);
        int matches = target->width == 128 && target->height == 128;
        for (int y=0; matches && y<128; ++y)
            matches = !memcmp((nj_u1 *)target->pixels + (nj_size)y*target->stride,
                              expected+y*128,128*sizeof(*expected));
        check(matches,"8x with four samples matches independent 128x128 colour");
        check(!memcmp(colour,nj_array_data(small.pixels),sizeof colour) &&
              !memcmp(depth,nj_array_data(zbuf(vm,&small)),sizeof depth),
              "k=16 preserves every logical colour and depth sample");
#if NJ_ENABLE_GFX_ACCEL
        check_physical(vm,&port,&small,&ref,colour,depth,&backend);
#endif
        release(vm,&small);
    }
    vm->render_quality.scale=2; vm->render_quality.ssaa=1;
#if NJ_ENABLE_GFX_ACCEL
    check_physical(vm,&port,&c,&ref,baseline,logical_z,&backend);
#endif
    check_geometry(vm,&c,0,baseline,logical_z,&ref);
    check_geometry(vm,&c,1,baseline,logical_z,&ref);
    check_perspective(vm,&port,&backend,&c,&ref);
    nj_port_shutdown(&backend);
    default_clear(vm,&c); draw(vm,&c,3,0xffff0000u,0);
    viewport(vm,&c,0,0,32,64); clear(vm,&c,0,1);
    viewport(vm,&c,0,0,64,64); draw(vm,&c,4,0xff00ff00u,0);
    nj_surface *hd=companion(vm,&c);
    check((nj_surface_get(hd,48,64)&0xffffffu)==0x00ff00u && (nj_surface_get(hd,80,64)&0xffffffu)==0xff0000u,
          "partial HD depth clear preserves occlusion outside the viewport");
    check(pixel(&c,24,32)==0x00ff00u && pixel(&c,40,32)==0xff0000u,"logical partial depth clear matches");
    check_weak_depth(vm,&c);
#if NJ_ENABLE_GFX_ACCEL
    check_queue(vm,&port,&c);
#endif
    nj_root_release(vm,0); nj_vm_destroy(vm); nj_port_shutdown(&port);
    printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
#endif
}
