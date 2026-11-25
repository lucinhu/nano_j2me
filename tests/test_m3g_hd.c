/* Render through the public M3G API at 64x64, with an independent 128x128
 * binding as a raster reference. Compare both products of each HD draw. */
#define main m3g_depth_fixture_main
#include "test_m3g_depth.c"
#undef main
#include "../src/midp/nj_midp.h"

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

static void check_perspective(nj_vm *vm, canvas *c, canvas *ref)
{
    const nj_s2 xyz[9] = {-1,-1,-2, 5,-2,-8, -1,3,-4};
    const nj_s2 uv[6] = {1,0, 64,0, 1,64};
    nj_ref image = nj_root(vm,nj_object_new(vm,C("Image2D")));
    nj_ref tex = nj_root(vm,nj_object_new(vm,C("Texture2D")));
    nj_ref coords = nj_root(vm,nj_object_new(vm,C("VertexArray")));
    nj_ref pixels = nj_root(vm,nj_array_new(vm,"B",24));
    nj_ref values = nj_root(vm,nj_array_new(vm,"S",6));
    nj_u1 *bytes = nj_array_data(pixels);
    nj_value a[5];
    for (int i=0; i<8; ++i) {
        bytes[3*i]=30u*i; bytes[3*i+1]=255u-30u*i; bytes[3*i+2]=0;
    }
    a[0].i=99; a[1].i=8; a[2].i=1;
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

int main(void)
{
#if !NJ_ENABLE_M3G
    puts("SKIP: NJ_ENABLE_M3G=0"); return 0;
#else
    nj_port port;
    nj_vm *vm;
    canvas c, ref;
    nj_u4 baseline[64*64];
    nj_s4 logical_z[64*64];
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
    vm->render_quality.scale=2;
    check_geometry(vm,&c,0,baseline,logical_z,&ref);
    check_geometry(vm,&c,1,baseline,logical_z,&ref);
    check_perspective(vm,&c,&ref);
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
