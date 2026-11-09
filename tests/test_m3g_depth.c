/* A host present may occur between M3G draws on a GameCanvas. Depth belongs
 * to the bind/release interval, not the host's scheduling/present counter. */
#include "nj2me/nj.h"
#include "nj2me/nj_config.h"
#include "nj2me/nj_vm.h"
#include "nj_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if NJ_ENABLE_M3G
#define C(x) "javax/microedition/m3g/" x
#define IMAGE "javax/microedition/lcdui/Image"
static int checks, failures;
static void check(int ok, const char *why)
{
    ++checks;
    if (!ok) { ++failures; printf("FAIL: %s\n", why); }
}
static void call(nj_vm *vm, nj_ref obj, const char *cls, const char *name,
                 const char *desc, nj_value *a, nj_value *r)
{
    if (nj_call(vm, obj, cls, name, desc, a, r) != NJ_OK) {
        fprintf(stderr, "fixture call failed: %s.%s%s\n", cls, name, desc);
        exit(2);
    }
}
static nj_ref make(nj_vm *vm, const char *cls)
{
    nj_ref o = nj_object_new(vm, cls);
    if (!o || nj_call_special(vm, o, cls, "<init>", "()V", NULL, NULL) != NJ_OK) exit(2);
    nj_root(vm, o);
    return o;
}
typedef struct canvas {
    nj_ref g3, vb, ib, app, cm, bg, data, image, graphics, pixels;
    int w, h;
} canvas;
static void viewport(nj_vm *vm, canvas *c, int x, int y, int w, int h)
{
    nj_value a[4]; a[0].i=x; a[1].i=y; a[2].i=w; a[3].i=h;
    call(vm,c->g3,C("Graphics3D"),"setViewport","(IIII)V",a,NULL);
}
static void bind_image(nj_vm *vm, canvas *c, int w, int h)
{
    nj_value a[2], r;
    a[0].i=w; a[1].i=h;
    if (nj_call_static(vm,IMAGE,"createImage","(II)Ljavax/microedition/lcdui/Image;",a,&r)!=NJ_OK) exit(2);
    c->image=nj_root(vm,r.r); c->w=w; c->h=h;
    call(vm,c->image,IMAGE,"getGraphics","()Ljavax/microedition/lcdui/Graphics;",NULL,&r);
    c->graphics=nj_root(vm,r.r); a[0].r=c->graphics;
    call(vm,c->g3,C("Graphics3D"),"bindTarget","(Ljava/lang/Object;)V",a,NULL);
    c->pixels=nj_rget(vm,c->image,nj_field_lookup(vm,IMAGE,"imgPixels","[I",0));
    viewport(vm,c,0,0,w,h);
}
static void init(nj_vm *vm, canvas *c)
{
    nj_ref va, lens, cam, poly;
    nj_value a[4];
    memset(c,0,sizeof(*c));
    c->g3=make(vm,C("Graphics3D")); c->vb=make(vm,C("VertexBuffer"));
    c->app=make(vm,C("Appearance")); c->cm=make(vm,C("CompositingMode"));
    c->bg=make(vm,C("Background")); cam=make(vm,C("Camera"));
    poly=make(vm,C("PolygonMode"));
    va=nj_root(vm,nj_object_new(vm,C("VertexArray")));
    a[0].i=3; a[1].i=3; a[2].i=2;
    if (nj_call_special(vm,va,C("VertexArray"),"<init>","(III)V",a,NULL)!=NJ_OK) exit(2);
    c->data=nj_root(vm,nj_array_new(vm,"S",9));
    a[0].i=0; a[1].i=3; a[2].r=c->data;
    call(vm,va,C("VertexArray"),"set","(II[S)V",a,NULL);
    c->data=nj_rget(vm,va,nj_field_lookup(vm,C("VertexArray"),"_nj_data","[S",0));
    a[0].r=va; a[1].f=1.0f; a[2].r=0;
    call(vm,c->vb,C("VertexBuffer"),"setPositions","(L" C("VertexArray") ";F[F)V",a,NULL);
    c->ib=nj_root(vm,nj_object_new(vm,C("TriangleStripArray")));
    lens=nj_root(vm,nj_array_new(vm,"I",1)); ((nj_s4*)nj_array_data(lens))[0]=3;
    a[0].i=0; a[1].r=lens;
    if (nj_call_special(vm,c->ib,C("TriangleStripArray"),"<init>","(I[I)V",a,NULL)!=NJ_OK) exit(2);
    a[0].i=162; call(vm,poly,C("PolygonMode"),"setCulling","(I)V",a,NULL);
    a[0].r=poly; call(vm,c->app,C("Appearance"),"setPolygonMode","(L" C("PolygonMode") ";)V",a,NULL);
    a[0].r=c->cm; call(vm,c->app,C("Appearance"),"setCompositingMode","(L" C("CompositingMode") ";)V",a,NULL);
    a[0].f=90; a[1].f=1; a[2].f=1; a[3].f=10;
    call(vm,cam,C("Camera"),"setPerspective","(FFFF)V",a,NULL);
    a[0].r=cam; a[1].r=0;
    call(vm,c->g3,C("Graphics3D"),"setCamera","(L" C("Camera") ";L" C("Transform") ";)V",a,NULL);
    bind_image(vm,c,64,64);
}
static void clear(nj_vm *vm, canvas *c, int colour, int depth)
{
    nj_value a[1]; a[0].i=colour;
    call(vm,c->bg,C("Background"),"setColorClearEnable","(Z)V",a,NULL);
    a[0].i=depth; call(vm,c->bg,C("Background"),"setDepthClearEnable","(Z)V",a,NULL);
    a[0].r=c->bg; call(vm,c->g3,C("Graphics3D"),"clear","(L" C("Background") ";)V",a,NULL);
}
static void draw(nj_vm *vm, canvas *c, int distance, nj_u4 colour, int mode)
{
    /* Identical projected coverage at any distance; interior pixels (24,32)
     * and (40,32) are well away from shared edges and viewport boundaries. */
    nj_s2 p[9]={-2*distance,-2*distance,-distance,
                2*distance,-2*distance,-distance,0,2*distance,-distance};
    nj_value a[5];
    memcpy(nj_array_data(c->data),p,sizeof(p));
    a[0].i=(nj_s4)colour; call(vm,c->vb,C("VertexBuffer"),"setDefaultColor","(I)V",a,NULL);
    a[0].i=mode==2?64:68; call(vm,c->cm,C("CompositingMode"),"setBlending","(I)V",a,NULL);
    a[0].i=mode!=2; call(vm,c->cm,C("CompositingMode"),"setDepthWriteEnable","(Z)V",a,NULL);
    a[0].f=mode==1?0.5f:0.0f; call(vm,c->cm,C("CompositingMode"),"setAlphaThreshold","(F)V",a,NULL);
    a[0].r=c->vb; a[1].r=c->ib; a[2].r=c->app; a[3].r=0; a[4].i=0;
    call(vm,c->g3,C("Graphics3D"),"render","(L" C("VertexBuffer") ";L" C("IndexBuffer") ";L" C("Appearance") ";L" C("Transform") ";I)V",a,NULL);
}
static nj_u4 pixel(canvas *c,int x,int y)
{ return ((nj_u4*)nj_array_data(c->pixels))[y*c->w+x]&0xFFFFFFu; }
static nj_ref zbuf(nj_vm *vm, canvas *c)
{ return nj_rget(vm,c->g3,nj_field_lookup(vm,C("Graphics3D"),"_nj_zbuf","[I",0)); }
static void release(nj_vm *vm,canvas *c)
{ call(vm,c->g3,C("Graphics3D"),"releaseTarget","()V",NULL,NULL); }
static void default_clear(nj_vm *vm, canvas *c)
{
    nj_value a; a.r=0;
    call(vm,c->g3,C("Graphics3D"),"clear","(L" C("Background") ";)V",&a,NULL);
}
static void test_default_clear(nj_vm *vm, canvas *c)
{
    nj_s4 saved[64*64];
    nj_u4 colours[64*64];
    nj_value a[2], r;
    nj_ref bg=make(vm,C("Background")), world=make(vm,C("World"));
    int i;
    viewport(vm,c,0,0,64,64);
    clear(vm,c,1,1); draw(vm,c,3,0xFFFF0000u,0);
    default_clear(vm,c);
    check(pixel(c,32,32)==0,"clear(null) clears color to black");
    draw(vm,c,4,0xFF0000FFu,0);
    check(pixel(c,32,32)==0x0000FFu,"clear(null) clears populated depth");

    clear(vm,c,1,1); draw(vm,c,3,0xFFFF0000u,0);
    a[0].r=bg;
    call(vm,c->g3,C("Graphics3D"),"clear","(L" C("Background") ";)V",a,NULL);
    memcpy(saved,nj_array_data(zbuf(vm,c)),sizeof(saved));
    memcpy(colours,nj_array_data(c->pixels),sizeof(colours));
    draw(vm,c,3,0xFFFF0000u,0); default_clear(vm,c);
    check(memcmp(saved,nj_array_data(zbuf(vm,c)),sizeof(saved))==0,
          "clear(null) depth matches a default Background");
    check(memcmp(colours,nj_array_data(c->pixels),sizeof(colours))==0,
          "clear(null) colors match a default Background");

    clear(vm,c,1,1); draw(vm,c,3,0xFFFF0000u,0);
    viewport(vm,c,0,0,32,64); default_clear(vm,c);
    check(pixel(c,24,32)==0 && pixel(c,40,32)==0xFF0000u,
          "clear(null) changes colors only inside the viewport");
    viewport(vm,c,0,0,64,64); draw(vm,c,4,0xFF0000FFu,0);
    check(pixel(c,24,32)==0x0000FFu && pixel(c,40,32)==0xFF0000u,
          "clear(null) preserves populated depth outside the viewport");
    memcpy(saved,nj_array_data(zbuf(vm,c)),sizeof(saved));
    memcpy(colours,nj_array_data(c->pixels),sizeof(colours));
    viewport(vm,c,80,80,8,8); default_clear(vm,c);
    check(memcmp(saved,nj_array_data(zbuf(vm,c)),sizeof(saved))==0 &&
          memcmp(colours,nj_array_data(c->pixels),sizeof(colours))==0,
          "clear(null) with an invisible viewport preserves both buffers");
    viewport(vm,c,0,0,64,64);

    /* The World overload clears even without a Background. The Node overload
     * treats that very same World as a Group and must preserve both buffers. */
    a[0].r=0;
    call(vm,c->g3,C("Graphics3D"),"getCamera","(L" C("Transform") ";)L" C("Camera") ";",a,&r);
    a[0].r=r.r; call(vm,world,C("Group"),"addChild","(L" C("Node") ";)V",a,NULL);
    call(vm,world,C("World"),"setActiveCamera","(L" C("Camera") ";)V",a,NULL);
    for(i=0;i<2;++i) {
        a[0].r=i?bg:0;
        call(vm,world,C("World"),"setBackground","(L" C("Background") ";)V",a,NULL);
        clear(vm,c,1,1); draw(vm,c,3,0xFFFF0000u,0);
        memcpy(saved,nj_array_data(zbuf(vm,c)),sizeof(saved));
        memcpy(colours,nj_array_data(c->pixels),sizeof(colours));
        a[0].r=world; a[1].r=0;
        call(vm,c->g3,C("Graphics3D"),"render","(L" C("Node") ";L" C("Transform") ";)V",a,NULL);
        check(memcmp(saved,nj_array_data(zbuf(vm,c)),sizeof(saved))==0 &&
              memcmp(colours,nj_array_data(c->pixels),sizeof(colours))==0,
              "render(Node,Transform) never clears a World's buffers");
        a[0].r=world;
        call(vm,c->g3,C("Graphics3D"),"render","(L" C("World") ";)V",a,NULL);
        check(pixel(c,32,32)==0,"render(World) clears color with null or default background");
        draw(vm,c,4,0xFF0000FFu,0);
        check(pixel(c,32,32)==0x0000FFu,"render(World) clears depth with null or default background");
    }
    release(vm,c);
    for(i=0;i<2;++i) {
        nj_status st;
        a[0].r=i?bg:0;
        st=nj_call(vm,c->g3,C("Graphics3D"),"clear","(L" C("Background") ";)V",a,NULL);
        check(st!=NJ_OK && strstr(nj_exception_describe(vm),"IllegalStateException")!=NULL,
              "clear requires a bound target even for null background");
        nj_clear_exception(vm);
    }
}
#endif
int main(void)
{
#if !NJ_ENABLE_M3G
    puts("SKIP: NJ_ENABLE_M3G=0"); return 0;
#else
    nj_port port; nj_vm *vm; canvas c;
    nj_s4 saved[64*64]; int mode, boundary;
    static const nj_u4 frames[]={1u,0x7FFFFFFFu,0x80000000u,0xFFFFFFFFu,0u};
    nj_port_headless_ex(&port,64,64,0,NULL);
    vm=nj_vm_create(&port,2u*1024u*1024u);
    if (!vm) return 2;
    (void)nj_lib_late_init(vm); init(vm,&c);
    for (mode=0;mode<3;++mode) for(boundary=0;boundary<5;++boundary) {
        clear(vm,&c,1,1); draw(vm,&c,3,0xFFFF0000u,0);
        check(pixel(&c,32,32)==0xFF0000u,"opaque foreground actually draws");
        memcpy(saved,nj_array_data(zbuf(vm,&c)),sizeof(saved));
        vm->frames=frames[boundary];
        draw(vm,&c,4,mode==2?0x800000FFu:0xFF0000FFu,mode);
        check(pixel(&c,32,32)==0xFF0000u,"far opaque/cutout/alpha remains occluded across host present");
        check(memcmp(saved,nj_array_data(zbuf(vm,&c)),sizeof(saved))==0,
              "occluded far draw preserves the entire depth buffer");
        draw(vm,&c,2,0xFF00FF00u,0);
        check(pixel(&c,32,32)==0x00FF00u,"near draw still passes depth after host present");
    }
    /* Explicit clear affects only the viewport even on a new host frame. */
    clear(vm,&c,1,1); draw(vm,&c,3,0xFFFF0000u,0);
    ++vm->frames; viewport(vm,&c,0,0,32,64); clear(vm,&c,0,1);
    viewport(vm,&c,0,0,64,64); draw(vm,&c,4,0xFF0000FFu,0);
    check(pixel(&c,24,32)==0x0000FFu,"explicit partial depth clear permits far draw inside viewport");
    check(pixel(&c,40,32)==0xFF0000u,"explicit partial depth clear preserves depth outside viewport across present");
    /* Color-only clears and disabled clears must not reset depth. */
    clear(vm,&c,1,1); draw(vm,&c,3,0xFFFF0000u,0); ++vm->frames;
    clear(vm,&c,1,0); draw(vm,&c,4,0xFF0000FFu,0);
    check(pixel(&c,32,32)==0,"color-only clear preserves occlusion across host present");
    clear(vm,&c,1,1); draw(vm,&c,3,0xFFFF0000u,0); ++vm->frames;
    clear(vm,&c,0,0); draw(vm,&c,4,0xFF0000FFu,2);
    check(pixel(&c,32,32)==0xFF0000u,"disabled clear preserves both color and depth across host present");
    /* A rebind, even to the same target, begins a new depth lifetime. */
    release(vm,&c);
    { nj_value a[1]; a[0].r=c.graphics; call(vm,c.g3,C("Graphics3D"),"bindTarget","(Ljava/lang/Object;)V",a,NULL); }
    draw(vm,&c,4,0xFF0000FFu,0);
    check(pixel(&c,32,32)==0x0000FFu,"rebind to same target resets stale depth without explicit clear");
    release(vm,&c); bind_image(vm,&c,64,64); draw(vm,&c,5,0xFF00FF00u,0);
    check(pixel(&c,32,32)==0x00FF00u,"new same-size target starts with clean depth");
    release(vm,&c); bind_image(vm,&c,32,48); draw(vm,&c,5,0xFF0000FFu,0);
    check(pixel(&c,16,24)==0x0000FFu,"resized target starts with clean depth");
    check(nj_array_length(zbuf(vm,&c))==32u*48u,"resized target has matching depth allocation");
    release(vm,&c); bind_image(vm,&c,64,64); clear(vm,&c,0,1); draw(vm,&c,5,0xFF00FF00u,0);
    check(pixel(&c,32,32)==0x00FF00u,"explicit depth clear initializes rebound target before render");
    test_default_clear(vm,&c);
    nj_root_release(vm,0); nj_vm_destroy(vm); nj_port_shutdown(&port);
    printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
#endif
}
