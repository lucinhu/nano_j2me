/* VertexBuffer/Texture2D alpha, checked through public M3G rendering APIs.
 * SDK: VertexBuffer.setColors/setDefaultColor, PolygonMode shading, Texture2D
 * blend table, CompositingMode ALPHA and alpha-test-before-depth semantics.
 * The oracle uses the source-over equation, not the runtime's raster helpers.
 * Lighting RGB equations, Node.alphaFactor and depth ties are separate. */
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
static char context[160];
static void check(int ok, const char *why)
{
    ++checks;
    if (!ok) { ++failures; fprintf(stderr,"FAIL [%s]: %s\n",context,why); }
}
static void call(nj_vm *vm,nj_ref obj,const char *cls,const char *name,
                 const char *desc,nj_value *a,nj_value *r)
{
    if (nj_call(vm,obj,cls,name,desc,a,r)!=NJ_OK) {
        fprintf(stderr,"fixture %s.%s%s: %s\n",cls,name,desc,nj_exception_describe(vm));
        exit(2);
    }
}
static nj_ref root(nj_vm *vm,nj_ref obj)
{
    nj_u4 n=nj_root_mark(vm);
    if (!obj) exit(2);
    nj_root(vm,obj);
    if (nj_root_mark(vm)!=n+1u) exit(2);
    return obj;
}
static nj_ref construct(nj_vm *vm,const char *cls,const char *desc,nj_value *a)
{
    nj_ref obj=root(vm,nj_object_new(vm,cls));
    if (nj_call_special(vm,obj,cls,"<init>",desc,a,NULL)!=NJ_OK) exit(2);
    return obj;
}
static nj_ref make(nj_vm *vm,const char *cls) { return construct(vm,cls,"()V",NULL); }
static void integer(nj_vm *vm,nj_ref o,const char *cls,const char *name,int i)
{ nj_value a; a.i=i; call(vm,o,cls,name,"(I)V",&a,NULL); }
static void boolean(nj_vm *vm,nj_ref o,const char *cls,const char *name,int i)
{ nj_value a; a.i=i; call(vm,o,cls,name,"(Z)V",&a,NULL); }
static void reference(nj_vm *vm,nj_ref o,const char *cls,const char *name,
                      const char *desc,nj_ref ref)
{ nj_value a; a.r=ref; call(vm,o,cls,name,desc,&a,NULL); }

typedef struct scene {
    nj_ref g3, vb, ib, app, cm, poly, bg, camera, positions, rgb, rgba;
    nj_ref image, graphics, readback, white, half, zero, rgbtex;
    int target; /* 0: MIDP image; 1..3: actual 565/888/ARGB screen backends */
    nj_u4 storage[SIZE*SIZE];
} scene;
static nj_ref vertex_array(nj_vm *vm,int components,int bytes)
{
    nj_value a[3]; a[0].i=3; a[1].i=components; a[2].i=bytes;
    return construct(vm,C("VertexArray"),"(III)V",a);
}
static void array_set(nj_vm *vm,nj_ref va,nj_ref data,int bytes)
{
    nj_value a[3]; a[0].i=0; a[1].i=3; a[2].r=data;
    call(vm,va,C("VertexArray"),"set",bytes==1?"(II[B)V":"(II[S)V",a,NULL);
}
static nj_ref texture(nj_vm *vm,int format,int alpha)
{
    nj_ref data=root(vm,nj_byte_array_new(vm,format==99?12u:16u)), image, tex;
    nj_value a[4]; unsigned char *p=nj_array_data(data); int i, n=format==99?3:4;
    for(i=0;i<4;++i) { p[i*n]=p[i*n+1]=p[i*n+2]=255; if(n==4)p[i*n+3]=(unsigned char)alpha; }
    a[0].i=format; a[1].i=2; a[2].i=2; a[3].r=data;
    image=construct(vm,C("Image2D"),"(III[B)V",a);
    a[0].r=image; tex=construct(vm,C("Texture2D"),"(L" C("Image2D") ";)V",a);
    integer(vm,tex,C("Texture2D"),"setBlending",227); /* MODULATE */
    a[0].i=208; a[1].i=210; call(vm,tex,C("Texture2D"),"setFiltering","(II)V",a,NULL);
    return tex;
}
static void positions(nj_vm *vm,scene *s,int geometry,int distance)
{
    /* Parallel camera: corners (0,64),(64,64),(32,0). With near clipping,
     * the last vertex is outside z=-1 and the surviving polygon ends at y=32.
     * Perspective variant uses the same coverage for constant depth. */
    nj_ref data; nj_s2 p[9]={-4,-4,-4, 4,-4,-4, 0,4,-4};
    nj_value a[4]; int i;
    if(geometry==1) p[8]=0;
    if(geometry>=2) {
        for(i=0;i<3;++i) { p[3*i]=(nj_s2)(p[3*i]*distance/2); p[3*i+1]=(nj_s2)(p[3*i+1]*distance/2); p[3*i+2]=(nj_s2)(-2*distance); }
        if(geometry==3)p[8]=-1; /* z=-0.5, outside perspective near=1 */
    }
    data=root(vm,nj_array_new(vm,"S",9)); memcpy(nj_array_data(data),p,sizeof(p));
    array_set(vm,s->positions,data,2);
    a[0].r=s->positions; a[1].f=0.5f; a[2].r=0;
    call(vm,s->vb,C("VertexBuffer"),"setPositions","(L" C("VertexArray") ";F[F)V",a,NULL);
    if(geometry>=2) { a[0].f=90; a[1].f=1; a[2].f=1; a[3].f=10; }
    else { a[0].f=4; a[1].f=1; a[2].f=1; a[3].f=10; }
    call(vm,s->camera,C("Camera"),geometry>=2?"setPerspective":"setParallel","(FFFF)V",a,NULL);
}
static void init(nj_vm *vm,scene *s,int target)
{
    nj_value a[4], r; nj_ref lens, tc, data;
    memset(s,0,sizeof(*s)); s->target=target;
    s->g3=make(vm,C("Graphics3D")); s->vb=make(vm,C("VertexBuffer"));
    s->app=make(vm,C("Appearance")); s->cm=make(vm,C("CompositingMode"));
    s->poly=make(vm,C("PolygonMode")); s->bg=make(vm,C("Background")); s->camera=make(vm,C("Camera"));
    s->positions=vertex_array(vm,3,2); s->rgb=vertex_array(vm,3,1); s->rgba=vertex_array(vm,4,1);
    positions(vm,s,0,2);
    tc=vertex_array(vm,2,2); data=root(vm,nj_array_new(vm,"S",6));
    { const nj_s2 uv[]={0,0, 64,0, 32,64}; memcpy(nj_array_data(data),uv,sizeof(uv)); }
    array_set(vm,tc,data,2); a[0].i=0; a[1].r=tc; a[2].f=1.0f/64; a[3].r=0;
    call(vm,s->vb,C("VertexBuffer"),"setTexCoords","(IL" C("VertexArray") ";F[F)V",a,NULL);
    lens=root(vm,nj_array_new(vm,"I",1)); ((nj_s4*)nj_array_data(lens))[0]=3;
    a[0].i=0; a[1].r=lens; s->ib=construct(vm,C("TriangleStripArray"),"(I[I)V",a);
    integer(vm,s->poly,C("PolygonMode"),"setCulling",162);
    reference(vm,s->app,C("Appearance"),"setPolygonMode","(L" C("PolygonMode") ";)V",s->poly);
    reference(vm,s->app,C("Appearance"),"setCompositingMode","(L" C("CompositingMode") ";)V",s->cm);
    integer(vm,s->cm,C("CompositingMode"),"setBlending",64);
    boolean(vm,s->cm,C("CompositingMode"),"setDepthTestEnable",0);
    boolean(vm,s->cm,C("CompositingMode"),"setDepthWriteEnable",0);
    integer(vm,s->bg,C("Background"),"setColor",(nj_s4)0xFF0000FFu);
    s->white=texture(vm,100,255); s->half=texture(vm,100,128); s->zero=texture(vm,100,0); s->rgbtex=texture(vm,99,255);
    a[0].i=SIZE; a[1].i=SIZE;
    if(nj_call_static(vm,NJ_C_IMAGE,"createImage","(II)L" NJ_C_IMAGE ";",a,&r)!=NJ_OK)exit(2);
    s->image=root(vm,r.r); call(vm,s->image,NJ_C_IMAGE,"getGraphics","()L" NJ_C_GRAPHICS ";",NULL,&r);
    s->graphics=root(vm,r.r); s->readback=root(vm,nj_int_array_new(vm,1));
    if(target) {
        /* Only the backend fixture uses internal state. Render controls and
         * vertex/texture data are set through the public Java API throughout. */
        nj_field_write_ref(vm,s->graphics,NJ_F(vm,NJ_C_GRAPHICS,NJ_F_G_TARGET,"L" NJ_C_IMAGE ";"),NULL);
        vm->screen.width=vm->screen.height=vm->screen_w=vm->screen_h=SIZE;
        vm->screen.bpp=target==1?2:target==2?3:4;
        vm->screen.stride=SIZE*vm->screen.bpp;
        vm->screen.format=target==1?NJ_PIXFMT_RGB565:target==2?NJ_PIXFMT_RGB888:NJ_PIXFMT_ARGB8888;
        vm->screen.pixels=s->storage;
    }
    reference(vm,s->g3,C("Graphics3D"),"bindTarget","(Ljava/lang/Object;)V",s->graphics);
    a[0].i=0; a[1].i=0; a[2].i=SIZE; a[3].i=SIZE; call(vm,s->g3,C("Graphics3D"),"setViewport","(IIII)V",a,NULL);
    a[0].r=s->camera; a[1].r=0; call(vm,s->g3,C("Graphics3D"),"setCamera","(L" C("Camera") ";L" C("Transform") ";)V",a,NULL);
}
static nj_u4 pixel_argb(nj_vm *vm,scene *s,int x,int y)
{
    nj_value a[7];
    if(s->target)return nj_surface_get(&vm->screen,x,y);
    a[0].r=s->readback; a[1].i=0; a[2].i=1; a[3].i=x; a[4].i=y; a[5].i=1; a[6].i=1;
    call(vm,s->image,NJ_C_IMAGE,"getRGB","([IIIIIII)V",a,NULL);
    return ((nj_u4*)nj_array_data(s->readback))[0];
}
static nj_u4 pixel(nj_vm *vm,scene *s,int x,int y)
{ return pixel_argb(vm,s,x,y)&0xFFFFFFu; }
static void clear(nj_vm *vm,scene *s)
{ reference(vm,s->g3,C("Graphics3D"),"clear","(L" C("Background") ";)V",s->bg); }
static void render(nj_vm *vm,scene *s)
{
    nj_value a[5]; a[0].r=s->vb; a[1].r=s->ib; a[2].r=s->app; a[3].r=0; a[4].i=0;
    call(vm,s->g3,C("Graphics3D"),"render","(L" C("VertexBuffer") ";L" C("IndexBuffer") ";L" C("Appearance") ";L" C("Transform") ";I)V",a,NULL);
}
static void colors(nj_vm *vm,scene *s,int source,const nj_u4 values[3])
{
    nj_value a; int i,k,components=source==1?3:4; nj_ref data;
    integer(vm,s->vb,C("VertexBuffer"),"setDefaultColor",(nj_s4)values[0]);
    if(!source) a.r=0;
    else {
        data=root(vm,nj_byte_array_new(vm,(nj_u4)(components*3)));
        for(i=0;i<3;++i) {
            unsigned char *p=nj_array_data(data);
            for(k=0;k<3;++k)p[i*components+k]=(unsigned char)(values[i]>>(16-8*k));
            if(components==4)p[i*components+3]=(unsigned char)(values[i]>>24);
        }
        array_set(vm,source==1?s->rgb:s->rgba,data,1); a.r=source==1?s->rgb:s->rgba;
    }
    call(vm,s->vb,C("VertexBuffer"),"setColors","(L" C("VertexArray") ";)V",&a,NULL);
}
static void texturing(nj_vm *vm,scene *s,nj_ref texture_ref,int func)
{
    nj_value a[2];
    if(texture_ref)integer(vm,texture_ref,C("Texture2D"),"setBlending",func);
    a[0].i=0; a[1].r=texture_ref; call(vm,s->app,C("Appearance"),"setTexture","(IL" C("Texture2D") ";)V",a,NULL);
}
/* Independent normalized RGB source-over on an opaque blue destination.
 * Keep doubles until final quantization; no runtime blend/texture helpers. */
static nj_u4 over_blue(nj_u4 source,double alpha)
{
    unsigned out=0; int k;
    for(k=0;k<3;++k) {
        unsigned s=(source>>(16-8*k))&255u, d=k==2?255u:0u;
        unsigned c=(unsigned)floor(s*alpha+d*(1.0-alpha)+0.5);
        out|=c<<(16-8*k);
    }
    return out;
}
static void expect_rgb(nj_vm *vm,scene *s,int x,int y,nj_u4 expected,int tolerance,const char *why)
{
    nj_u4 actual=pixel(vm,s,x,y); int k,ok=1;
    /* RGB565 loses at most 7 red/blue and 3 green levels. Allow the output
     * quantization error without using the backend's conversion as oracle. */
    for(k=0;k<3;++k) {
        int a=(int)((actual>>(16-8*k))&255u), e=(int)((expected>>(16-8*k))&255u);
        int t=tolerance+(s->target==1?(k==1?4:8):0);
        if(abs(a-e)>t)ok=0;
    }
    if(!ok)fprintf(stderr,"  pixel(%d,%d) actual=%06x expected=%06x\n",x,y,actual,expected);
    check(ok,why);
}
static void uniform_matrix(nj_vm *vm,scene *s)
{
    static const unsigned alpha[]={0,1,128,204,254,255}; int shade,source,t,i;
    nj_ref textures[]={0,s->white,s->half,s->zero,s->rgbtex};
    for(shade=0;shade<2;++shade)for(source=0;source<3;++source)for(t=0;t<5;++t)for(i=0;i<6;++i) {
        nj_u4 values[3]; double incoming=source==1?1:alpha[i]/255.0;
        double ta=t==2?128/255.0:t==3?0:1;
        nj_u4 mark=nj_root_mark(vm); int j;
        for(j=0;j<3;++j)values[j]=(alpha[i]<<24)|0xFF0000u;
        snprintf(context,sizeof(context),"target%d shade%d source%d texture%d alpha%u",s->target,shade,source,t,alpha[i]);
        integer(vm,s->poly,C("PolygonMode"),"setShading",shade?165:164);
        colors(vm,s,source,values); texturing(vm,s,textures[t],227); clear(vm,s); render(vm,s);
        expect_rgb(vm,s,32,40,over_blue(0xFF0000u,incoming*ta),alpha[i]==1?0:1,"MODULATE/source-over RGB");
        expect_rgb(vm,s,1,1,0x0000FFu,0,"outside triangle stays blue");
        nj_root_release(vm,mark);
    }
    /* RGBA REPLACE discards incoming alpha, including incoming zero. */
    for(source=0;source<3;++source)for(t=1;t<=3;++t)for(i=0;i<6;++i) {
        nj_u4 value=(alpha[i]<<24)|0xFF0000u, values[]={value,value,value};
        double ta=t==2?128/255.0:t==3?0:1; nj_u4 mark=nj_root_mark(vm);
        snprintf(context,sizeof(context),"target%d RGBA REPLACE source%d tex%d alpha%u",s->target,source,t,alpha[i]);
        colors(vm,s,source,values); texturing(vm,s,textures[t],228); clear(vm,s); render(vm,s);
        expect_rgb(vm,s,32,40,over_blue(0xFFFFFFu,ta),1,"RGBA REPLACE uses texture alpha only");
        nj_root_release(vm,mark);
    }
}
static void texture_modes(nj_vm *vm,scene *s)
{
    static const unsigned alpha[]={0,1,128,204,254,255};
    static const int funcs[]={224,225,226,228}; /* ADD, BLEND, DECAL, REPLACE */
    nj_ref textures[]={s->rgbtex,s->white,s->half,s->zero}; int source,t,f,i;
    for(source=0;source<3;++source)for(t=0;t<4;++t)for(f=0;f<4;++f)for(i=0;i<6;++i) {
        nj_u4 v=(alpha[i]<<24)|0xFFFFFFu, values[]={v,v,v};
        double af=source==1?1:alpha[i]/255.0, at=t==2?128/255.0:t==3?0:1, out;
        nj_u4 mark=nj_root_mark(vm);
        /* Constant white inputs and white blend color keep output RGB white
         * in all these functions, isolating their DIFFERENT alpha equations.
         * RGB images retain Af even after internal conversion to RGBA. */
        if(t==0 || funcs[f]==226)out=af;
        else if(funcs[f]==228)out=at;
        else out=af*at;
        snprintf(context,sizeof(context),"target%d texture function%d format%s source%d alpha%u",s->target,funcs[f],t?"RGBA":"RGB",source,alpha[i]);
        integer(vm,s->poly,C("PolygonMode"),"setShading",165);
        colors(vm,s,source,values); texturing(vm,s,textures[t],funcs[f]);
        integer(vm,textures[t],C("Texture2D"),"setBlendColor",0xFFFFFF);
        clear(vm,s); render(vm,s);
        expect_rgb(vm,s,32,40,over_blue(0xFFFFFFu,out),1,"texture environment alpha follows original Image2D format");
        nj_root_release(vm,mark);
    }
}
static void material_alpha(nj_vm *vm,scene *s)
{
    static const unsigned alpha[]={0,1,128,204,254,255};
    nj_ref mat=make(vm,C("Material")), normals=vertex_array(vm,3,1);
    nj_ref data=root(vm,nj_byte_array_new(vm,9)), light=make(vm,C("Light"));
    nj_value a[2]; int track,source,t,i;
    const unsigned char n[]={0,0,127,0,0,127,0,0,127};
    memcpy(nj_array_data(data),n,sizeof(n)); array_set(vm,normals,data,1);
    reference(vm,s->vb,C("VertexBuffer"),"setNormals","(L" C("VertexArray") ";)V",normals);
    /* A zero-intensity ambient light enables the lighting path without
     * contributing RGB. Emissive red then isolates diffuse/tracked alpha. */
    integer(vm,light,C("Light"),"setMode",128);
    a[0].f=0; call(vm,light,C("Light"),"setIntensity","(F)V",a,NULL);
    a[0].r=light; a[1].r=NULL;
    call(vm,s->g3,C("Graphics3D"),"addLight","(L" C("Light") ";L" C("Transform") ";)I",a,NULL);
    a[0].i=1024; a[1].i=0; call(vm,mat,C("Material"),"setColor","(II)V",a,NULL);
    a[0].i=4096; a[1].i=0xFF0000; call(vm,mat,C("Material"),"setColor","(II)V",a,NULL);
    a[0].i=8192; a[1].i=0; call(vm,mat,C("Material"),"setColor","(II)V",a,NULL);
    reference(vm,s->app,C("Appearance"),"setMaterial","(L" C("Material") ";)V",mat);
    for(track=0;track<2;++track)for(source=0;source<2;++source)for(t=0;t<3;++t)for(i=0;i<6;++i) {
        unsigned va=track?alpha[i]:0, ma=track?128:alpha[i];
        nj_u4 v=(va<<24)|0xFF0000u, values[]={v,v,v};
        double af=alpha[i]/255.0, at=t==2?128/255.0:1;
        nj_u4 mark=nj_root_mark(vm);
        snprintf(context,sizeof(context),"target%d material tracking%d source%d texture%d alpha%u",s->target,track,source,t,alpha[i]);
        a[0].i=2048; a[1].i=(nj_s4)(ma<<24); call(vm,mat,C("Material"),"setColor","(II)V",a,NULL);
        boolean(vm,mat,C("Material"),"setVertexColorTrackingEnable",track);
        colors(vm,s,source?2:0,values); texturing(vm,s,t==0?0:t==1?s->white:s->half,227);
        integer(vm,s->poly,C("PolygonMode"),"setShading",165);
        clear(vm,s); render(vm,s);
        expect_rgb(vm,s,32,40,over_blue(0xFF0000u,af*at),1,"material diffuse alpha, or tracked vertex alpha, survives lighting");
        nj_root_release(vm,mark);
    }
    reference(vm,s->app,C("Appearance"),"setMaterial","(L" C("Material") ";)V",NULL);
    call(vm,s->g3,C("Graphics3D"),"resetLights","()V",NULL,NULL);
}
static void interpolation(nj_vm *vm,scene *s)
{
    int clipped,flat,t,i; static const int points[][2]={{20,48},{32,40},{44,48}};
    const nj_u4 values[]={0x00FF0000u,0x00FF0000u,0xFFFF0000u};
    for(clipped=0;clipped<2;++clipped)for(flat=0;flat<2;++flat)for(t=0;t<3;++t) {
        nj_u4 mark=nj_root_mark(vm); nj_ref tex=t==0?0:t==1?s->white:s->half;
        positions(vm,s,clipped,2); colors(vm,s,2,values); texturing(vm,s,tex,227);
        integer(vm,s->poly,C("PolygonMode"),"setShading",flat?164:165);
        snprintf(context,sizeof(context),"target%d alpha gradient clipped%d flat%d texture%d",s->target,clipped,flat,t);
        clear(vm,s); render(vm,s);
        for(i=0;i<3;++i) {
            /* Barycentric third weight for vertices (0,64),(64,64),(32,0).
             * With parallel projection, clipping must preserve this affine
             * color plane. Pixel centers, not integer corners, are sampled. */
            double alpha=flat?1.0:1.0-(points[i][1]+0.5)/SIZE;
            if(t==2)alpha*=128/255.0;
            expect_rgb(vm,s,points[i][0],points[i][1],over_blue(0xFF0000u,alpha),2,"interpolated alpha after optional near clipping");
        }
        if(clipped)expect_rgb(vm,s,32,16,0x0000FFu,0,"near-clipped portion is not rasterized");
        nj_root_release(vm,mark);
    }
    /* Perspective near clipping: uniform alpha must stay uniform on both
     * generated triangles, independently of perspective-correction hints. */
    for(t=0;t<2;++t) {
        const nj_u4 half[]={0x80FF0000u,0x80FF0000u,0x80FF0000u};
        nj_u4 mark=nj_root_mark(vm);
        positions(vm,s,3,2); colors(vm,s,2,half); texturing(vm,s,t?s->white:0,227);
        integer(vm,s->poly,C("PolygonMode"),"setShading",165);
        snprintf(context,sizeof(context),"target%d perspective near clip texture%d",s->target,t);
        clear(vm,s); render(vm,s);
        expect_rgb(vm,s,24,48,over_blue(0xFF0000u,128/255.0),1,"perspective clip preserves uniform alpha");
        expect_rgb(vm,s,40,48,over_blue(0xFF0000u,128/255.0),1,"both generated triangles preserve alpha");
        nj_root_release(vm,mark);
    }
    positions(vm,s,0,2);
}
static void threshold_depth(nj_vm *vm,scene *s)
{
    int blend,source,t,pass,write; const nj_u4 blue[]={0xFF0000FFu,0xFF0000FFu,0xFF0000FFu};
    for(blend=0;blend<2;++blend)for(source=0;source<2;++source)for(t=0;t<2;++t)for(pass=0;pass<4;++pass)for(write=0;write<2;++write) {
        unsigned a=pass==3?255u:pass==2?128u:pass?129u:127u;
        nj_u4 v=(a<<24)|0xFF0000u, red[]={v,v,v};
        nj_u4 front=blend?0xFF0000u:over_blue(0xFF0000u,a/255.0);
        nj_value arg; nj_u4 mark=nj_root_mark(vm);
        positions(vm,s,2,2); colors(vm,s,source?2:0,red); texturing(vm,s,t?s->white:0,227);
        integer(vm,s->poly,C("PolygonMode"),"setShading",165);
        boolean(vm,s->cm,C("CompositingMode"),"setDepthTestEnable",1);
        boolean(vm,s->cm,C("CompositingMode"),"setDepthWriteEnable",write);
        integer(vm,s->cm,C("CompositingMode"),"setBlending",blend?68:64);
        arg.f=pass==3?1.0f:pass==2?128.0f/255:0.5f;
        call(vm,s->cm,C("CompositingMode"),"setAlphaThreshold","(F)V",&arg,NULL);
        snprintf(context,sizeof(context),"target%d alpha test blend%d source%d tex%d pass%d write%d",s->target,blend,source,t,pass,write);
        clear(vm,s); render(vm,s);
        expect_rgb(vm,s,32,40,pass?front:0x0000FFu,1,"alpha test precedes blending");
        /* The next triangle is strictly farther (3 vs 2), never tied. If
         * alpha test rejected, it must not have written occluding depth. */
        arg.f=0; call(vm,s->cm,C("CompositingMode"),"setAlphaThreshold","(F)V",&arg,NULL);
        positions(vm,s,2,3); colors(vm,s,0,blue); texturing(vm,s,0,227); render(vm,s);
        expect_rgb(vm,s,32,40,pass&&write?front:0x0000FFu,1,"alpha-rejected/depthWrite=false draw cannot occlude farther geometry");
        nj_root_release(vm,mark);
    }
    boolean(vm,s->cm,C("CompositingMode"),"setDepthTestEnable",0);
    boolean(vm,s->cm,C("CompositingMode"),"setDepthWriteEnable",0);
    integer(vm,s->cm,C("CompositingMode"),"setBlending",64);
    positions(vm,s,0,2);
}
static void alpha_write_and_zero_depth(nj_vm *vm,scene *s)
{
    static const unsigned alpha[]={0,128,255};
    const nj_u4 green[]={0xFF00FF00u,0xFF00FF00u,0xFF00FF00u};
    int blend,source,t,i; nj_value arg;
    for(blend=0;blend<2;++blend)for(source=0;source<2;++source)for(t=0;t<2;++t)for(i=0;i<3;++i) {
        nj_u4 v=(alpha[i]<<24)|0xFF0000u, red[]={v,v,v};
        nj_u4 expected=blend?0xFF0000u:over_blue(0xFF0000u,alpha[i]/255.0);
        nj_u4 mark=nj_root_mark(vm);
        snprintf(context,sizeof(context),"target%d alphaWrite=false blend%d source%d tex%d alpha%u",s->target,blend,source,t,alpha[i]);
        integer(vm,s->bg,C("Background"),"setColor",(nj_s4)0xFF0000FFu);
        boolean(vm,s->cm,C("CompositingMode"),"setDepthTestEnable",1);
        boolean(vm,s->cm,C("CompositingMode"),"setDepthWriteEnable",1);
        boolean(vm,s->cm,C("CompositingMode"),"setAlphaWriteEnable",0);
        integer(vm,s->cm,C("CompositingMode"),"setBlending",blend?68:64);
        arg.f=0; call(vm,s->cm,C("CompositingMode"),"setAlphaThreshold","(F)V",&arg,NULL);
        positions(vm,s,2,2); colors(vm,s,source?2:0,red); texturing(vm,s,t?s->white:0,227);
        clear(vm,s); render(vm,s);
        expect_rgb(vm,s,32,40,expected,1,"alphaWrite=false does not disable RGB alpha blending");
        if(s->target==0 || s->target==3)
            check((pixel_argb(vm,s,32,40)>>24)==255u,"alphaWrite=false preserves existing destination alpha");
        /* Alpha zero passes threshold zero. It must still write depth even
         * though ALPHA blending cannot change the blue destination RGB. A
         * distinct green farther layer exposes incorrectly skipped depth. */
        positions(vm,s,2,3); colors(vm,s,0,green); texturing(vm,s,0,227);
        integer(vm,s->cm,C("CompositingMode"),"setBlending",68); render(vm,s);
        expect_rgb(vm,s,32,40,expected,1,"alpha zero at threshold zero still writes enabled depth");
        nj_root_release(vm,mark);
    }
    integer(vm,s->bg,C("Background"),"setColor",(nj_s4)0xFF0000FFu);
    boolean(vm,s->cm,C("CompositingMode"),"setAlphaWriteEnable",1);
    boolean(vm,s->cm,C("CompositingMode"),"setDepthTestEnable",0);
    boolean(vm,s->cm,C("CompositingMode"),"setDepthWriteEnable",0);
    integer(vm,s->cm,C("CompositingMode"),"setBlending",64);
    positions(vm,s,0,2);
}
#endif
int main(void)
{
#if !NJ_ENABLE_M3G
    puts("SKIP: NJ_ENABLE_M3G=0"); return 0;
#else
    nj_port port; nj_vm *vm; int target; scene s;
    nj_surface saved; nj_u2 sw,sh;
    nj_port_headless_ex(&port,SIZE,SIZE,0,NULL);
    vm=nj_vm_create(&port,16u*1024u*1024u);
    if(!vm || nj_lib_late_init(vm)!=NJ_OK)return 2;
    (void)nj_midp_screen(vm); saved=vm->screen; sw=vm->screen_w; sh=vm->screen_h;
    for(target=0;target<4;++target) {
        nj_u4 mark=nj_root_mark(vm);
        init(vm,&s,target); uniform_matrix(vm,&s); texture_modes(vm,&s);
        material_alpha(vm,&s); interpolation(vm,&s); threshold_depth(vm,&s);
        alpha_write_and_zero_depth(vm,&s);
        call(vm,s.g3,C("Graphics3D"),"releaseTarget","()V",NULL,NULL);
        vm->screen=saved; vm->screen_w=sw; vm->screen_h=sh;
        nj_root_release(vm,mark); nj_gc_collect(vm);
    }
    nj_vm_destroy(vm); nj_port_shutdown(&port);
    printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
#endif
}
