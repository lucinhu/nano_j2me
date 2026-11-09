/* CompositingMode requires LEQUAL: a later coplanar fragment passes.
 * Public Java rendering/state APIs, positive perspective depths, zero offset.
 * Independent barycentric RGB/source-over oracle; no depth/raster internals.
 * Screen backing fixtures alone use C surface state for target selection. */
#include "nj2me/nj.h"
#include "nj2me/nj_config.h"
#include "../src/midp/nj_midp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if NJ_ENABLE_M3G
#define C(x) "javax/microedition/m3g/" x
#define SIZE 64
static int checks, failures;
static char context[128];
static void check(int ok,const char *why)
{
    ++checks;
    if(!ok) { ++failures; fprintf(stderr,"FAIL [%s]: %s\n",context,why); }
}
static void call(nj_vm *vm,nj_ref o,const char *cls,const char *name,
                 const char *desc,nj_value *a,nj_value *r)
{
    if(nj_call(vm,o,cls,name,desc,a,r)!=NJ_OK) {
        fprintf(stderr,"fixture %s.%s%s: %s\n",cls,name,desc,nj_exception_describe(vm));
        exit(2);
    }
}
static nj_ref root(nj_vm *vm,nj_ref o)
{
    nj_u4 n=nj_root_mark(vm);
    if(!o)exit(2);
    nj_root(vm,o); if(nj_root_mark(vm)!=n+1u)exit(2);
    return o;
}
static nj_ref construct(nj_vm *vm,const char *cls,const char *desc,nj_value *a)
{
    nj_ref o=root(vm,nj_object_new(vm,cls));
    if(nj_call_special(vm,o,cls,"<init>",desc,a,NULL)!=NJ_OK)exit(2);
    return o;
}
static nj_ref make(nj_vm *vm,const char *cls) { return construct(vm,cls,"()V",NULL); }
static void integer(nj_vm *vm,nj_ref o,const char *cls,const char *name,int v)
{ nj_value a; a.i=v; call(vm,o,cls,name,"(I)V",&a,NULL); }
static void boolean(nj_vm *vm,nj_ref o,const char *cls,const char *name,int v)
{ nj_value a; a.i=v; call(vm,o,cls,name,"(Z)V",&a,NULL); }
static void reference(nj_vm *vm,nj_ref o,const char *cls,const char *name,const char *desc,nj_ref v)
{ nj_value a; a.r=v; call(vm,o,cls,name,desc,&a,NULL); }

typedef struct scene {
    nj_ref g3,vb,ib,app,cm,poly,bg,cam,fog,va,rgb,rgba;
    nj_ref image,graphics,readback,texrgb,texrgba;
    int target;
    nj_u4 storage[SIZE*SIZE];
} scene;
/* With FAST_PATHS=1 these configurations jointly enter all six depth sites:
 * flat opaque; 565 flat nearest; ARGB near-fog/tinted specialization;
 * ARGB clamp and repeat flat nearest; and the general raster loop. */
enum { FLAT, NEAREST_CLAMP, NEAREST_REPEAT, NEAR_FOG, GOURAUD_NEAREST,
       LINEAR_GENERAL, GOURAUD_UNTEXTURED, PATH_COUNT };
static const char *const path_names[]={"flat","nearest-clamp","nearest-repeat",
    "near-fog-special","gouraud-nearest","linear-general","gouraud-untextured"};
static nj_ref va(nj_vm *vm,int components,int bytes)
{
    nj_value a[3]; a[0].i=3; a[1].i=components; a[2].i=bytes;
    return construct(vm,C("VertexArray"),"(III)V",a);
}
static void set_array(nj_vm *vm,nj_ref v,nj_ref data,int bytes)
{
    nj_value a[3]; a[0].i=0; a[1].i=3; a[2].r=data;
    call(vm,v,C("VertexArray"),"set",bytes==1?"(II[B)V":"(II[S)V",a,NULL);
}
static nj_ref texture(nj_vm *vm,int format)
{
    nj_value a[4]; nj_ref data=root(vm,nj_byte_array_new(vm,format==99?12u:16u)),im;
    memset(nj_array_data(data),255,format==99?12u:16u);
    a[0].i=format; a[1].i=2; a[2].i=2; a[3].r=data;
    im=construct(vm,C("Image2D"),"(III[B)V",a);
    a[0].r=im; return construct(vm,C("Texture2D"),"(L" C("Image2D") ";)V",a);
}
static void positions(nj_vm *vm,scene *s,int distance)
{
    /* scale .5 yields x/y +/-distance and z=-distance. For the 90-degree
     * perspective camera all depths project onto (0,64),(64,64),(32,0).
     * Repeating the SAME inputs gives a genuinely identical positive key. */
    const nj_s2 p[9]={-2*distance,-2*distance,-2*distance,
        2*distance,-2*distance,-2*distance,0,2*distance,-2*distance};
    nj_ref data=root(vm,nj_array_new(vm,"S",9)); nj_value a[3];
    memcpy(nj_array_data(data),p,sizeof(p)); set_array(vm,s->va,data,2);
    a[0].r=s->va; a[1].f=.5f; a[2].r=NULL;
    call(vm,s->vb,C("VertexBuffer"),"setPositions","(L" C("VertexArray") ";F[F)V",a,NULL);
}
static void init(nj_vm *vm,scene *s,int target)
{
    nj_value a[4],r; nj_ref lens,tc,data;
    memset(s,0,sizeof(*s)); s->target=target;
    s->g3=make(vm,C("Graphics3D")); s->vb=make(vm,C("VertexBuffer"));
    s->app=make(vm,C("Appearance")); s->cm=make(vm,C("CompositingMode"));
    s->poly=make(vm,C("PolygonMode")); s->bg=make(vm,C("Background"));
    s->cam=make(vm,C("Camera")); s->fog=make(vm,C("Fog"));
    s->va=va(vm,3,2); s->rgb=va(vm,3,1); s->rgba=va(vm,4,1); positions(vm,s,3);
    tc=va(vm,2,2); data=root(vm,nj_array_new(vm,"S",6));
    { const nj_s2 uv[]={0,0,64,0,32,64}; memcpy(nj_array_data(data),uv,sizeof(uv)); }
    set_array(vm,tc,data,2); a[0].i=0; a[1].r=tc; a[2].f=1.0f/64; a[3].r=NULL;
    call(vm,s->vb,C("VertexBuffer"),"setTexCoords","(IL" C("VertexArray") ";F[F)V",a,NULL);
    lens=root(vm,nj_int_array_new(vm,1)); ((nj_s4*)nj_array_data(lens))[0]=3;
    a[0].i=0; a[1].r=lens; s->ib=construct(vm,C("TriangleStripArray"),"(I[I)V",a);
    integer(vm,s->poly,C("PolygonMode"),"setCulling",162);
    integer(vm,s->poly,C("PolygonMode"),"setShading",165);
    reference(vm,s->app,C("Appearance"),"setPolygonMode","(L" C("PolygonMode") ";)V",s->poly);
    reference(vm,s->app,C("Appearance"),"setCompositingMode","(L" C("CompositingMode") ";)V",s->cm);
    a[0].f=0; a[1].f=0;
    call(vm,s->cm,C("CompositingMode"),"setDepthOffset","(FF)V",a,NULL);
    integer(vm,s->bg,C("Background"),"setColor",(nj_s4)0xFF000000u);
    integer(vm,s->fog,C("Fog"),"setMode",81);
    a[0].f=10; a[1].f=20; call(vm,s->fog,C("Fog"),"setLinear","(FF)V",a,NULL);
    s->texrgb=texture(vm,99); s->texrgba=texture(vm,100);
    a[0].i=SIZE; a[1].i=SIZE;
    if(nj_call_static(vm,NJ_C_IMAGE,"createImage","(II)L" NJ_C_IMAGE ";",a,&r)!=NJ_OK)exit(2);
    s->image=root(vm,r.r); call(vm,s->image,NJ_C_IMAGE,"getGraphics","()L" NJ_C_GRAPHICS ";",NULL,&r);
    s->graphics=root(vm,r.r); s->readback=root(vm,nj_int_array_new(vm,1));
    if(target) {
        nj_field_write_ref(vm,s->graphics,NJ_F(vm,NJ_C_GRAPHICS,NJ_F_G_TARGET,"L" NJ_C_IMAGE ";"),NULL);
        vm->screen.width=vm->screen.height=vm->screen_w=vm->screen_h=SIZE;
        vm->screen.bpp=target==1?2:target==2?3:4; vm->screen.stride=SIZE*vm->screen.bpp;
        vm->screen.format=target==1?NJ_PIXFMT_RGB565:target==2?NJ_PIXFMT_RGB888:NJ_PIXFMT_ARGB8888;
        vm->screen.pixels=s->storage;
    }
    reference(vm,s->g3,C("Graphics3D"),"bindTarget","(Ljava/lang/Object;)V",s->graphics);
    a[0].i=0; a[1].i=0; a[2].i=SIZE; a[3].i=SIZE;
    call(vm,s->g3,C("Graphics3D"),"setViewport","(IIII)V",a,NULL);
    a[0].f=90; a[1].f=1; a[2].f=1; a[3].f=10;
    call(vm,s->cam,C("Camera"),"setPerspective","(FFFF)V",a,NULL);
    a[0].r=s->cam; a[1].r=NULL;
    call(vm,s->g3,C("Graphics3D"),"setCamera","(L" C("Camera") ";L" C("Transform") ";)V",a,NULL);
}
static void colors(nj_vm *vm,scene *s,int path,int channel,int alpha)
{
    nj_value a; int i,k,n=alpha==255?3:4;
    integer(vm,s->vb,C("VertexBuffer"),"setDefaultColor",(nj_s4)(((nj_u4)alpha<<24)|(255u<<(16-8*channel))));
    a.r=NULL;
    if(path==GOURAUD_NEAREST || path==GOURAUD_UNTEXTURED) {
        static const unsigned char shade[]={255,127,191};
        nj_ref data=root(vm,nj_byte_array_new(vm,(nj_u4)(3*n))); unsigned char *p=nj_array_data(data);
        for(i=0;i<3;++i) {
            for(k=0;k<3;++k)p[i*n+k]=k==channel?shade[i]:0;
            if(n==4)p[i*n+3]=(unsigned char)alpha;
        }
        set_array(vm,n==3?s->rgb:s->rgba,data,1); a.r=n==3?s->rgb:s->rgba;
    }
    call(vm,s->vb,C("VertexBuffer"),"setColors","(L" C("VertexArray") ";)V",&a,NULL);
}
static void draw(nj_vm *vm,scene *s,int path,int distance,int channel,int alpha,
                 int blending,int depth_test,int depth_write,float threshold)
{
    nj_ref tex=path==FLAT || path==GOURAUD_UNTEXTURED?NULL:
               path==NEAREST_CLAMP?s->texrgb:s->texrgba;
    nj_value a[5];
    positions(vm,s,distance); colors(vm,s,path,channel,alpha);
    a[0].i=0; a[1].r=tex;
    call(vm,s->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a,NULL);
    if(tex) {
        integer(vm,tex,C("Texture2D"),"setBlending",227);
        a[0].i=a[1].i=path==NEAREST_REPEAT?241:240;
        call(vm,tex,C("Texture2D"),"setWrapping","(II)V",a,NULL);
        a[0].i=208; a[1].i=path==LINEAR_GENERAL?209:210;
        call(vm,tex,C("Texture2D"),"setFiltering","(II)V",a,NULL);
    }
    reference(vm,s->app,C("Appearance"),"setFog","(L" C("Fog") ";)V",path==NEAR_FOG?s->fog:NULL);
    integer(vm,s->cm,C("CompositingMode"),"setBlending",blending);
    boolean(vm,s->cm,C("CompositingMode"),"setDepthTestEnable",depth_test);
    boolean(vm,s->cm,C("CompositingMode"),"setDepthWriteEnable",depth_write);
    a[0].f=threshold; call(vm,s->cm,C("CompositingMode"),"setAlphaThreshold","(F)V",a,NULL);
    a[0].r=s->vb; a[1].r=s->ib; a[2].r=s->app; a[3].r=NULL; a[4].i=0;
    call(vm,s->g3,C("Graphics3D"),"render","(L" C("VertexBuffer") ";L" C("IndexBuffer") ";L" C("Appearance") ";L" C("Transform") ";I)V",a,NULL);
}
static void clear(nj_vm *vm,scene *s)
{ reference(vm,s->g3,C("Graphics3D"),"clear","(L" C("Background") ";)V",s->bg); }
static nj_u4 pixel(nj_vm *vm,scene *s,int x,int y)
{
    nj_value a[7];
    if(s->target)return nj_surface_get(&vm->screen,x,y)&0xFFFFFFu;
    a[0].r=s->readback; a[1].i=0; a[2].i=1; a[3].i=x; a[4].i=y; a[5].i=1; a[6].i=1;
    call(vm,s->image,NJ_C_IMAGE,"getRGB","([IIIIIII)V",a,NULL);
    return ((nj_u4*)nj_array_data(s->readback))[0]&0xFFFFFFu;
}
static nj_u4 source(int path,int channel,int x,int y)
{
    double value=255;
    if(path==GOURAUD_NEAREST || path==GOURAUD_UNTEXTURED) {
        double top=(64-(y+.5))/64, right=(x+.5)/64-.5*top, left=1-top-right;
        value=255*left+127*right+191*top;
    }
    return (nj_u4)floor(value+.5)<<(16-8*channel);
}
static nj_u4 over(nj_u4 src,nj_u4 dst,int alpha)
{
    nj_u4 out=0; int k;
    for(k=0;k<3;++k) {
        double s=(src>>(16-8*k))&255u,d=(dst>>(16-8*k))&255u;
        out|=(nj_u4)floor((s*alpha+d*(255-alpha))/255.0+.5)<<(16-8*k);
    }
    return out;
}
static void expect(nj_vm *vm,scene *s,int path,int state,const char *why)
{
    static const int xy[][2]={{24,40},{32,40},{40,40},{32,24}};
    unsigned i;
    for(i=0;i<sizeof(xy)/sizeof(xy[0]);++i) {
        int x=xy[i][0],y=xy[i][1],k,ok=1;
        nj_u4 actual=pixel(vm,s,x,y),red=source(path,0,x,y),green=source(path,1,x,y),blue=source(path,2,x,y);
        nj_u4 expected=state==0?red:state==1?green:state==2?blue:
                       state==3?over(green,red,128):state==4?over(blue,over(green,red,128),128):over(blue,red,128);
        for(k=0;k<3;++k) {
            int a=(actual>>(16-8*k))&255u,e=(expected>>(16-8*k))&255u;
            int tolerance=2+(s->target==1?(k==1?4:8):0);
            if(abs(a-e)>tolerance)ok=0;
        }
        if(!ok)fprintf(stderr,"  pixel(%d,%d): actual=%06x expected=%06x\n",x,y,actual,expected);
        check(ok,why);
    }
}
static void matrix(nj_vm *vm,scene *s,int path)
{
    int write;
    snprintf(context,sizeof(context),"target%d %s opaque coplanar",s->target,path_names[path]);
    clear(vm,s); draw(vm,s,path,3,0,255,68,1,1,0); expect(vm,s,path,0,"first positive-depth layer is visible");
    draw(vm,s,path,3,1,255,68,1,1,0); expect(vm,s,path,1,"LEQUAL: later equal-depth opaque layer replaces earlier layer");
    draw(vm,s,path,3,2,255,68,1,1,0); expect(vm,s,path,2,"LEQUAL: repeated equal-depth replacement keeps draw order");
    for(write=0;write<2;++write) {
        snprintf(context,sizeof(context),"target%d %s coplanar ALPHA depthWrite%d",s->target,path_names[path],write);
        clear(vm,s); draw(vm,s,path,3,0,255,68,1,1,0);
        draw(vm,s,path,3,1,128,64,1,write,0); expect(vm,s,path,3,"LEQUAL permits a coplanar alpha layer over populated depth");
        draw(vm,s,path,3,2,128,64,1,write,0); expect(vm,s,path,4,"multiple coplanar alpha layers compose in draw order");
    }
    snprintf(context,sizeof(context),"target%d %s strict near/far",s->target,path_names[path]);
    clear(vm,s); draw(vm,s,path,3,0,255,68,1,1,0);
    draw(vm,s,path,4,1,255,68,1,1,0); expect(vm,s,path,0,"strictly farther layer remains occluded");
    draw(vm,s,path,2,1,255,68,1,1,0); expect(vm,s,path,1,"strictly nearer layer passes");
    draw(vm,s,path,3,2,255,68,1,1,0); expect(vm,s,path,1,"updated nearer depth occludes subsequent farther layer");
    snprintf(context,sizeof(context),"target%d %s depthWrite=false",s->target,path_names[path]);
    clear(vm,s); draw(vm,s,path,3,0,255,68,1,0,0);
    draw(vm,s,path,4,1,255,68,1,1,0); expect(vm,s,path,1,"depthWrite=false leaves a later farther layer visible");
    draw(vm,s,path,4,2,255,68,1,0,0); expect(vm,s,path,2,"LEQUAL still applies when the equal layer disables depth writes");
    snprintf(context,sizeof(context),"target%d %s depthTest=false",s->target,path_names[path]);
    clear(vm,s); draw(vm,s,path,3,0,255,68,1,1,0);
    draw(vm,s,path,4,1,255,68,0,0,0); expect(vm,s,path,1,"depthTest=false admits a farther layer");
    draw(vm,s,path,4,2,255,68,1,1,0); expect(vm,s,path,1,"disabled depth writes preserve the original nearer depth");
    snprintf(context,sizeof(context),"target%d %s alpha rejection/depth",s->target,path_names[path]);
    clear(vm,s); draw(vm,s,path,4,0,255,68,1,1,0);
    draw(vm,s,path,2,1,64,68,1,1,.5f); expect(vm,s,path,0,"alpha-rejected nearer fragments preserve color");
    draw(vm,s,path,3,2,255,68,1,1,0); expect(vm,s,path,2,"alpha rejection must not write occluding depth");
    snprintf(context,sizeof(context),"target%d %s alpha rejection/equal",s->target,path_names[path]);
    clear(vm,s); draw(vm,s,path,3,0,255,68,1,1,0);
    draw(vm,s,path,3,1,64,68,1,1,.5f); expect(vm,s,path,0,"equal-depth alpha rejection preserves the earlier color");
    draw(vm,s,path,3,2,128,64,1,1,.5f); expect(vm,s,path,5,"LEQUAL plus passing alpha test admits the following coplanar layer");
}
#endif
int main(void)
{
#if !NJ_ENABLE_M3G
    puts("SKIP: NJ_ENABLE_M3G=0"); return 0;
#else
    nj_port port; nj_vm *vm; scene s; nj_surface saved; nj_u2 sw,sh; int target,path;
    nj_port_headless_ex(&port,SIZE,SIZE,0,NULL);
    vm=nj_vm_create(&port,16u*1024u*1024u);
    if(!vm || nj_lib_late_init(vm)!=NJ_OK)return 2;
    (void)nj_midp_screen(vm); saved=vm->screen; sw=vm->screen_w; sh=vm->screen_h;
    for(target=0;target<4;++target) {
        nj_u4 mark=nj_root_mark(vm); init(vm,&s,target);
        for(path=0;path<PATH_COUNT;++path) {
            nj_u4 temp=nj_root_mark(vm); matrix(vm,&s,path); nj_root_release(vm,temp);
        }
        call(vm,s.g3,C("Graphics3D"),"releaseTarget","()V",NULL,NULL);
        vm->screen=saved; vm->screen_w=sw; vm->screen_h=sh;
        nj_root_release(vm,mark); nj_gc_collect(vm);
    }
    nj_vm_destroy(vm); nj_port_shutdown(&port);
    printf("%d checks, %d failures\n",checks,failures); return failures?1:0;
#endif
}
