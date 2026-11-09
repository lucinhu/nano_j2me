/* Projected window depth, depth ranges, and polygon offsets, through public
 * M3G rendering/state APIs. Reference depth uses double matrix projection and
 * screen barycentrics, never the implementation depth buffer or key format.
 * Only screen target backing uses internal state. */
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
    int target,projection;
    double points[3][3];
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
static void positions(nj_vm *vm,scene *s,int distance,int tilted,double gap)
{
    nj_s2 p[9]; nj_value a[3]; int i;
    nj_u4 mark=nj_root_mark(vm);
    nj_ref data=root(vm,nj_array_new(vm,"S",9)),bias=root(vm,nj_array_new(vm,"F",3));
    /* Camera coordinates are exact multiples of 1/4096. A public position
     * bias supplies the small gap without depending on depth quantization. */
    for(i=0;i<3;++i) {
        double d=distance+tilted*(i==0?-1:i==1?1:0);
        double xy=s->projection==0?d:2;
        s->points[i][0]=i==0?-xy:i==1?xy:0;
        s->points[i][1]=i==2?xy:-xy;
        s->points[i][2]=-d-gap;
        p[3*i]=(nj_s2)(s->points[i][0]*4096);
        p[3*i+1]=(nj_s2)(s->points[i][1]*4096);
        p[3*i+2]=(nj_s2)(-d*4096);
    }
    ((float*)nj_array_data(bias))[2]=(float)-gap;
    memcpy(nj_array_data(data),p,sizeof(p)); set_array(vm,s->va,data,2);
    a[0].r=s->va; a[1].f=1.0f/4096; a[2].r=bias;
    call(vm,s->vb,C("VertexBuffer"),"setPositions","(L" C("VertexArray") ";F[F)V",a,NULL);
    nj_root_release(vm,mark);
}
static void camera(nj_vm *vm,scene *s,int projection)
{
    nj_value a[4]; s->projection=projection;
    if(projection<2) {
        a[0].f=projection?4:90; a[1].f=1; a[2].f=1; a[3].f=17;
        call(vm,s->cam,C("Camera"),projection?"setParallel":"setPerspective","(FFFF)V",a,NULL);
    } else {
        /* Row-major Transform: w=1; oblique clip Z depends on X as well as Z.
         * Same camera W at every depth catches reciprocal-W substitutes. */
        const float m[]={.5f,0,0,0, 0,.5f,0,0, .0625f,0,-.125f,-1, 0,0,0,1};
        nj_u4 mark=nj_root_mark(vm); nj_ref t=make(vm,C("Transform"));
        nj_ref data=root(vm,nj_array_new(vm,"F",16)); memcpy(nj_array_data(data),m,sizeof(m));
        a[0].r=data; call(vm,t,C("Transform"),"set","([F)V",a,NULL);
        a[0].r=t; call(vm,s->cam,C("Camera"),"setGeneric","(L" C("Transform") ";)V",a,NULL);
        nj_root_release(vm,mark);
    }
    a[0].r=s->cam; a[1].r=NULL;
    call(vm,s->g3,C("Graphics3D"),"setCamera","(L" C("Camera") ";L" C("Transform") ";)V",a,NULL);
}
static void init(nj_vm *vm,scene *s,int target)
{
    nj_value a[4],r; nj_ref lens,tc,data;
    memset(s,0,sizeof(*s)); s->target=target;
    s->g3=make(vm,C("Graphics3D"));
    snprintf(context,sizeof(context),"target%d default depth range",target);
    call(vm,s->g3,C("Graphics3D"),"getDepthRangeNear","()F",NULL,&r);
    check(r.f==0,"new Graphics3D depth range starts at zero");
    call(vm,s->g3,C("Graphics3D"),"getDepthRangeFar","()F",NULL,&r);
    check(r.f==1,"new Graphics3D depth range ends at one");
    s->vb=make(vm,C("VertexBuffer"));
    s->app=make(vm,C("Appearance")); s->cm=make(vm,C("CompositingMode"));
    s->poly=make(vm,C("PolygonMode")); s->bg=make(vm,C("Background"));
    s->cam=make(vm,C("Camera")); s->fog=make(vm,C("Fog"));
    s->va=va(vm,3,2); s->rgb=va(vm,3,1); s->rgba=va(vm,4,1); positions(vm,s,3,0,0);
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
    camera(vm,s,0);
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
static void draw(nj_vm *vm,scene *s,int path,int distance,int tilted,double gap,
                 int channel,int alpha,int depth_test,int depth_write,float threshold)
{
    nj_ref tex=path==FLAT || path==GOURAUD_UNTEXTURED?NULL:
               path==NEAREST_CLAMP?s->texrgb:s->texrgba;
    nj_value a[5];
    positions(vm,s,distance,tilted,gap); colors(vm,s,path,channel,alpha);
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
    integer(vm,s->cm,C("CompositingMode"),"setBlending",alpha==255?68:64);
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

static const int samples[][2]={{24,40},{32,40},{40,40},{32,24}};
typedef struct reference_image {
    double z[4],near,far;
    nj_u4 rgb[4];
} reference_image;
static void range(nj_vm *vm,scene *s,double near,double far)
{
    nj_value a[2]; a[0].f=(float)near; a[1].f=(float)far;
    call(vm,s->g3,C("Graphics3D"),"setDepthRange","(FF)V",a,NULL);
}
static void offset(nj_vm *vm,scene *s,float factor,float units)
{
    nj_value a[2]; a[0].f=factor; a[1].f=units;
    call(vm,s->cm,C("CompositingMode"),"setDepthOffset","(FF)V",a,NULL);
}
static void begin(nj_vm *vm,scene *s,reference_image *ref,double near,double far)
{
    int i; range(vm,s,near,far); offset(vm,s,0,0); clear(vm,s);
    ref->near=near; ref->far=far;
    for(i=0;i<4;++i) { ref->z[i]=1; ref->rgb[i]=0; }
}
static void reference_draw(scene *s,reference_image *ref,int path,int channel,
                           int alpha,int test,int write,double threshold,double factor)
{
    double x[3],y[3],z[3],den,dzdx,dzdy; int i;
    for(i=0;i<3;++i) {
        double px=s->points[i][0],py=s->points[i][1],pz=s->points[i][2];
        double w=s->projection==0?-pz:1;
        double cx=s->projection==0?px:px*.5,cy=s->projection==0?py:py*.5;
        double cz=s->projection==0?-1.125*pz-2.125:
                  s->projection==1?-.125*pz-1.125:.0625*px-.125*pz-1;
        x[i]=32+32*cx/w; y[i]=32-32*cy/w;
        z[i]=.5*(ref->far-ref->near)*(cz/w+1)+ref->near;
    }
    den=(x[1]-x[0])*(y[2]-y[0])-(x[2]-x[0])*(y[1]-y[0]);
    dzdx=((z[1]-z[0])*(y[2]-y[0])-(z[2]-z[0])*(y[1]-y[0]))/den;
    dzdy=((x[1]-x[0])*(z[2]-z[0])-(x[2]-x[0])*(z[1]-z[0]))/den;
    for(i=0;i<4;++i) {
        double dx=samples[i][0]+.5-x[0],dy=samples[i][1]+.5-y[0];
        double d=z[0]+dzdx*dx+dzdy*dy+factor*fmax(fabs(dzdx),fabs(dzdy));
        /* Near/far clipping occurs before offset/range clamping. Recover NDC
         * from the un-biased affine window depth for the clip-only fixture. */
        double unshifted=z[0]+dzdx*dx+dzdy*dy;
        if(ref->near!=ref->far) {
            double ndc=2*(unshifted-ref->near)/(ref->far-ref->near)-1;
            if(ndc < -1-1e-10 || ndc > 1+1e-10)continue;
        }
        if(d<0)d=0;
        if(d>1)d=1;
        if(alpha/255.0>=threshold && (!test || d<=ref->z[i]+1e-12)) {
            nj_u4 src=source(path,channel,samples[i][0],samples[i][1]);
            if(path==GOURAUD_NEAREST || path==GOURAUD_UNTEXTURED) {
                double b=((y[2]-y[0])*dx-(x[2]-x[0])*dy)/den;
                double c=(-(y[1]-y[0])*dx+(x[1]-x[0])*dy)/den;
                src=(nj_u4)floor(255*(1-b-c)+127*b+191*c+.5)<<(16-8*channel);
            }
            ref->rgb[i]=alpha==255?src:over(src,ref->rgb[i],alpha);
            if(write)ref->z[i]=d;
        }
    }
}
static void layer(nj_vm *vm,scene *s,reference_image *ref,int path,int distance,
                  int tilted,double gap,int channel,int alpha,int test,int write,
                  float threshold,float factor)
{
    offset(vm,s,factor,0);
    draw(vm,s,path,distance,tilted,gap,channel,alpha,test,write,threshold);
    reference_draw(s,ref,path,channel,alpha,test,write,threshold,factor);
}
static void finish(nj_vm *vm,scene *s,reference_image *ref,const char *why)
{
    int i,k;
    /* Image reads happen only after releaseTarget, as Graphics3D requires. */
    call(vm,s->g3,C("Graphics3D"),"releaseTarget","()V",NULL,NULL);
    for(i=0;i<4;++i) {
        nj_u4 actual=pixel(vm,s,samples[i][0],samples[i][1]),expected=ref->rgb[i];
        int ok=1;
        for(k=0;k<3;++k) {
            int a=(actual>>(16-8*k))&255u,e=(expected>>(16-8*k))&255u;
            int tolerance=2+(s->target==1?(k==1?4:8):0);
            if(abs(a-e)>tolerance)ok=0;
        }
        if(!ok)fprintf(stderr,"  (%d,%d) actual=%06x expected=%06x\n",samples[i][0],samples[i][1],actual,expected);
        check(ok,why);
    }
    reference(vm,s->g3,C("Graphics3D"),"bindTarget","(Ljava/lang/Object;)V",s->graphics);
}
static void projection_matrix(nj_vm *vm,scene *s,int path)
{
    reference_image ref; int projection,order,kind;
    static const double ranges[][2]={{0,1},{.2,.8},{.9,.1},{.5,.5},{0,0},{1,1}};
    for(projection=0;projection<3;++projection) {
        camera(vm,s,projection);
        for(kind=0;kind<6;++kind)for(order=0;order<2;++order) {
            snprintf(context,sizeof(context),"target%d %s projection%d range%d order%d",s->target,path_names[path],projection,kind,order);
            begin(vm,s,&ref,ranges[kind][0],ranges[kind][1]);
            layer(vm,s,&ref,path,order?2:4,0,0,0,255,1,1,0,0);
            layer(vm,s,&ref,path,order?4:2,0,0,1,255,1,1,0,0);
            finish(vm,s,&ref,"window Z sorting respects projection, mapping, and LEQUAL");
        }
        /* Different per-draw ranges must map into the SAME window-depth space. */
        snprintf(context,sizeof(context),"target%d %s projection%d per-draw range",s->target,path_names[path],projection);
        begin(vm,s,&ref,.7,.9); layer(vm,s,&ref,path,2,0,0,0,255,1,1,0,0);
        range(vm,s,0,.1); ref.near=0; ref.far=.1;
        layer(vm,s,&ref,path,4,0,0,1,255,1,1,0,0);
        finish(vm,s,&ref,"far geometry mapped to smaller window Z beats nearer geometry");
        begin(vm,s,&ref,0,.1); layer(vm,s,&ref,path,4,0,0,0,255,1,1,0,0);
        range(vm,s,.7,.9); ref.near=.7; ref.far=.9;
        layer(vm,s,&ref,path,2,0,0,1,255,1,1,0,0);
        finish(vm,s,&ref,"near geometry mapped to larger window Z stays occluded");
        snprintf(context,sizeof(context),"target%d %s projection%d tilted depth",s->target,path_names[path],projection);
        begin(vm,s,&ref,0,1); layer(vm,s,&ref,path,3,1,0,0,255,1,1,0,0);
        layer(vm,s,&ref,path,3,0,0,1,255,1,1,0,0);
        finish(vm,s,&ref,"interpolated projected depth orders a crossing plane");
        begin(vm,s,&ref,0,1); layer(vm,s,&ref,path,3,0,0,0,255,1,1,0,0);
        layer(vm,s,&ref,path,3,1,0,1,255,1,1,0,0);
        finish(vm,s,&ref,"crossing plane order is independent of submission order");
        snprintf(context,sizeof(context),"target%d %s projection%d depth flags/alpha",s->target,path_names[path],projection);
        begin(vm,s,&ref,0,1); layer(vm,s,&ref,path,2,0,0,0,255,1,0,0,0);
        layer(vm,s,&ref,path,4,0,0,1,255,1,1,0,0);
        finish(vm,s,&ref,"disabled depth write leaves farther geometry visible");
        begin(vm,s,&ref,0,1); layer(vm,s,&ref,path,2,0,0,0,255,1,1,0,0);
        layer(vm,s,&ref,path,4,0,0,1,128,0,0,0,0);
        layer(vm,s,&ref,path,3,0,0,2,255,1,1,0,0);
        finish(vm,s,&ref,"disabled depth test blends color without replacing stored depth");
        begin(vm,s,&ref,0,1); layer(vm,s,&ref,path,4,0,0,0,255,1,1,0,0);
        layer(vm,s,&ref,path,2,0,0,1,64,1,1,.5f,0);
        layer(vm,s,&ref,path,3,0,0,2,255,1,1,0,0);
        finish(vm,s,&ref,"alpha rejection precedes projected depth writes");
    }
}
static void clipping_matrix(nj_vm *vm,scene *s,int path)
{
    reference_image ref;
    camera(vm,s,1);
    snprintf(context,sizeof(context),"target%d %s near-plane clip",s->target,path_names[path]);
    begin(vm,s,&ref,0,1); layer(vm,s,&ref,path,4,0,0,0,255,1,1,0,0);
    layer(vm,s,&ref,path,1,1,0,1,255,1,1,0,-1);
    finish(vm,s,&ref,"near-clipped polygon retains projected depth and per-polygon offset");
    begin(vm,s,&ref,.8,.2); layer(vm,s,&ref,path,4,0,0,0,255,1,1,0,0);
    layer(vm,s,&ref,path,1,1,0,1,255,1,1,0,1);
    finish(vm,s,&ref,"near clipping and reversed mapped depth remain distinct operations");
}
static void range_api(nj_vm *vm,scene *s)
{
    static const float bad[][2]={{-.01f,.75f},{1.01f,.75f},{.25f,-.01f},{.25f,1.01f}};
    unsigned i; nj_value a[2],r;
    snprintf(context,sizeof(context),"target%d depth range API",s->target);
    range(vm,s,.25,.75);
    for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        a[0].f=bad[i][0]; a[1].f=bad[i][1];
        check(nj_call(vm,s->g3,C("Graphics3D"),"setDepthRange","(FF)V",a,NULL)!=NJ_OK && strstr(nj_exception_describe(vm),"IllegalArgumentException")!=NULL,"out-of-range setter throws IllegalArgumentException");
        nj_clear_exception(vm);
        call(vm,s->g3,C("Graphics3D"),"getDepthRangeNear","()F",NULL,&r);
        check(r.f==.25f,"invalid range keeps previous near value");
        call(vm,s->g3,C("Graphics3D"),"getDepthRangeFar","()F",NULL,&r);
        check(r.f==.75f,"invalid range keeps previous far value");
        /* Repair bad legacy state so each case has the same starting point. */
        range(vm,s,.25,.75);
    }
}
static void units_matrix(nj_vm *vm,scene *s,int path)
{
    reference_image ref; int projection,write,i;
    const double gap=1e-5;
    for(projection=0;projection<3;++projection) {
        camera(vm,s,projection);
        for(write=0;write<2;++write) {
            snprintf(context,sizeof(context),"target%d %s projection%d units write%d",s->target,path_names[path],projection,write);
            begin(vm,s,&ref,0,1);
            draw(vm,s,path,3,0,0,0,255,1,1,0);
            offset(vm,s,0,65536);
            draw(vm,s,path,3,0,-gap,1,255,1,write,0);
            for(i=0;i<4;++i)ref.rgb[i]=source(path,0,samples[i][0],samples[i][1]);
            finish(vm,s,&ref,"positive units pushes a slightly NEARER fragment behind stored depth");
            begin(vm,s,&ref,0,1);
            draw(vm,s,path,3,0,0,0,255,1,1,0);
            offset(vm,s,0,-65536);
            draw(vm,s,path,3,0,gap,1,255,1,write,0);
            for(i=0;i<4;++i)ref.rgb[i]=source(path,1,samples[i][0],samples[i][1]);
            finish(vm,s,&ref,"negative units pulls a slightly FARTHER fragment through stored depth");
            begin(vm,s,&ref,0,1);
            draw(vm,s,path,3,0,0,0,255,1,1,0);
            offset(vm,s,0,-65536); draw(vm,s,path,3,0,gap,1,255,1,write,0);
            offset(vm,s,0,0); draw(vm,s,path,3,0,0,2,255,1,1,0);
            for(i=0;i<4;++i)ref.rgb[i]=source(path,write?1:2,samples[i][0],samples[i][1]);
            finish(vm,s,&ref,"negative offset is stored only when depthWrite is enabled");
            begin(vm,s,&ref,0,1);
            offset(vm,s,0,65536); draw(vm,s,path,3,0,0,0,255,1,write,0);
            offset(vm,s,0,0); draw(vm,s,path,3,0,gap,1,255,1,1,0);
            for(i=0;i<4;++i)ref.rgb[i]=source(path,1,samples[i][0],samples[i][1]);
            finish(vm,s,&ref,"positive offset writes a farther depth visible to the following layer");
        }
    }
}
static void factor_matrix(nj_vm *vm,scene *s,int path)
{
    reference_image ref; int projection,sign,write;
    for(projection=0;projection<3;++projection) {
        camera(vm,s,projection);
        for(sign=-1;sign<=1;sign+=2)for(write=0;write<2;++write) {
            snprintf(context,sizeof(context),"target%d %s projection%d factor%d write%d",s->target,path_names[path],projection,sign,write);
            begin(vm,s,&ref,0,1);
            layer(vm,s,&ref,path,3,1,0,0,255,1,1,0,0);
            layer(vm,s,&ref,path,3,1,sign>0?-1e-5:1e-5,1,255,1,write,0,(float)sign);
            finish(vm,s,&ref,"screen-space depth slope determines factor bias before testing");
            begin(vm,s,&ref,0,1);
            layer(vm,s,&ref,path,3,1,0,0,255,1,1,0,0);
            layer(vm,s,&ref,path,3,1,sign>0?-1e-5:1e-5,1,255,1,write,0,(float)sign);
            layer(vm,s,&ref,path,3,1,0,2,255,1,1,0,0);
            finish(vm,s,&ref,"slope-biased depth controls the following un-biased layer");
        }
        /* Test two different slopes against a finite gap. In the parallel
         * case the .05 camera gap falls between the two factor=-1 biases.
         * For other projections the oracle computes their screen gradients. */
        {
            int tilt;
            for(tilt=1;tilt<=2;++tilt) {
                snprintf(context,sizeof(context),"target%d %s projection%d slope magnitude%d",s->target,path_names[path],projection,tilt);
                begin(vm,s,&ref,0,1); layer(vm,s,&ref,path,3,tilt,0,0,255,1,1,0,0);
                layer(vm,s,&ref,path,3,tilt,.05,1,255,1,1,0,-1);
                finish(vm,s,&ref,"offset magnitude follows maximum screen-space Z gradient");
            }
        }
        if(projection<2) {
            snprintf(context,sizeof(context),"target%d %s projection%d zero slope",s->target,path_names[path],projection);
            begin(vm,s,&ref,0,1); layer(vm,s,&ref,path,3,0,0,0,255,1,1,0,0);
            layer(vm,s,&ref,path,3,0,1e-3,1,255,1,1,0,-16);
            finish(vm,s,&ref,"factor has no effect on a constant window-depth plane");
        }
    }
}
static void disabled_depth(nj_vm *vm,scene *s,int path)
{
    reference_image ref; nj_value a[3],r; int i,sign;
    camera(vm,s,1);
    for(sign=-1;sign<=1;sign+=2) {
        snprintf(context,sizeof(context),"target%d %s depthBuffer=false offset%d",s->target,path_names[path],sign);
        call(vm,s->g3,C("Graphics3D"),"releaseTarget","()V",NULL,NULL);
        a[0].r=s->graphics; a[1].i=0; a[2].i=0;
        call(vm,s->g3,C("Graphics3D"),"bindTarget","(Ljava/lang/Object;ZI)V",a,NULL);
        call(vm,s->g3,C("Graphics3D"),"isDepthBufferEnabled","()Z",NULL,&r);
        check(!r.i,"explicit false bind reports depth buffer disabled");
        begin(vm,s,&ref,0,1); draw(vm,s,path,2,0,0,0,255,1,1,0);
        offset(vm,s,16*sign,65536*sign); draw(vm,s,path,4,1,0,1,255,1,1,0);
        for(i=0;i<4;++i)ref.rgb[i]=source(path,1,samples[i][0],samples[i][1]);
        finish(vm,s,&ref,"global depthBuffer=false ignores test/write/offset and draws farther layer");
        call(vm,s->g3,C("Graphics3D"),"isDepthBufferEnabled","()Z",NULL,&r);
        check(r.i,"one-argument rebind restores depth buffering by default");
        begin(vm,s,&ref,0,1); layer(vm,s,&ref,path,2,0,0,0,255,1,1,0,0);
        layer(vm,s,&ref,path,4,0,0,1,255,1,1,0,0);
        finish(vm,s,&ref,"default depth buffer occludes farther layer after disabled binding");
    }
}
static void saturation(nj_vm *vm,scene *s)
{
    reference_image ref; int sign,i;
    camera(vm,s,1);
    for(sign=-1;sign<=1;sign+=2) {
        snprintf(context,sizeof(context),"target%d saturating units sign%d",s->target,sign);
        begin(vm,s,&ref,0,1); draw(vm,s,FLAT,2,0,0,0,255,1,1,0);
        offset(vm,s,0,sign*1e30f); draw(vm,s,FLAT,4,0,0,1,255,1,1,0);
        for(i=0;i<4;++i)ref.rgb[i]=sign<0?0x00FF00u:0xFF0000u;
        finish(vm,s,&ref,"huge signed units saturate to near/far depth instead of wrapping");
        begin(vm,s,&ref,0,1);
        offset(vm,s,0,sign*1e30f); draw(vm,s,FLAT,4,0,0,1,255,1,1,0);
        for(i=0;i<4;++i)ref.rgb[i]=0x00FF00u;
        finish(vm,s,&ref,"saturated endpoint still passes LEQUAL against cleared far depth");
        begin(vm,s,&ref,0,1);
        offset(vm,s,0,sign*1e30f); draw(vm,s,FLAT,4,0,0,1,255,1,1,0);
        offset(vm,s,0,0); draw(vm,s,FLAT,3,0,0,2,255,1,1,0);
        for(i=0;i<4;++i)ref.rgb[i]=sign<0?0x00FF00u:0x0000FFu;
        finish(vm,s,&ref,"saturated endpoint is stored and orders the following normal layer");
    }
}
static nj_ref sprite(nj_vm *vm,scene *s,int channel)
{
    nj_value a[4]; nj_ref data=root(vm,nj_byte_array_new(vm,4)),im;
    unsigned char *p=nj_array_data(data); p[channel]=255; p[3]=255;
    a[0].i=100; a[1].i=1; a[2].i=1; a[3].r=data;
    im=construct(vm,C("Image2D"),"(III[B)V",a);
    a[0].i=1; a[1].r=im; a[2].r=s->app;
    return construct(vm,C("Sprite3D"),"(ZL" C("Image2D") ";L" C("Appearance") ";)V",a);
}
static void sprite_draw(nj_vm *vm,scene *s,nj_ref spr,double distance)
{
    nj_value a[3]; nj_u4 mark=nj_root_mark(vm); nj_ref model=make(vm,C("Transform"));
    a[0].f=0; a[1].f=0; a[2].f=(float)-distance;
    call(vm,model,C("Transform"),"postTranslate","(FFF)V",a,NULL);
    a[0].f=a[1].f=a[2].f=8;
    call(vm,model,C("Transform"),"postScale","(FFF)V",a,NULL);
    a[0].r=spr; a[1].r=model;
    call(vm,s->g3,C("Graphics3D"),"render","(L" C("Node") ";L" C("Transform") ";)V",a,NULL);
    nj_root_release(vm,mark);
}
static void sprite_matrix(nj_vm *vm,scene *s)
{
    nj_u4 mark=nj_root_mark(vm); nj_ref red=sprite(vm,s,0),green=sprite(vm,s,1);
    reference_image ref; nj_value a; int projection,reverse,order,i;
    integer(vm,s->cm,C("CompositingMode"),"setBlending",68);
    boolean(vm,s->cm,C("CompositingMode"),"setDepthTestEnable",1);
    boolean(vm,s->cm,C("CompositingMode"),"setDepthWriteEnable",1);
    a.f=0; call(vm,s->cm,C("CompositingMode"),"setAlphaThreshold","(F)V",&a,NULL);
    reference(vm,s->app,C("Appearance"),"setFog","(L" C("Fog") ";)V",NULL);
    for(projection=0;projection<2;++projection)for(reverse=0;reverse<2;++reverse)for(order=0;order<2;++order) {
        camera(vm,s,projection);
        snprintf(context,sizeof(context),"Sprite projection%d reversed%d order%d",projection,reverse,order);
        begin(vm,s,&ref,reverse?.9:0,reverse?.1:1);
        sprite_draw(vm,s,red,order?2:4); sprite_draw(vm,s,green,order?4:2);
        for(i=0;i<4;++i)ref.rgb[i]=order==reverse?0x00FF00u:0xFF0000u;
        finish(vm,s,&ref,"Sprite3D origin clip Z/w and depth range order the billboard fragments");
    }
    camera(vm,s,1);
    snprintf(context,sizeof(context),"Sprite units offset");
    begin(vm,s,&ref,0,1); sprite_draw(vm,s,red,3);
    offset(vm,s,0,65536); sprite_draw(vm,s,green,3-1e-5);
    for(i=0;i<4;++i)ref.rgb[i]=0xFF0000u;
    finish(vm,s,&ref,"positive Sprite3D units offset pushes a nearer billboard behind depth");
    begin(vm,s,&ref,0,1); sprite_draw(vm,s,red,3);
    offset(vm,s,0,-65536); sprite_draw(vm,s,green,3+1e-5);
    for(i=0;i<4;++i)ref.rgb[i]=0x00FF00u;
    finish(vm,s,&ref,"negative Sprite3D units offset pulls a farther billboard through depth");
    nj_root_release(vm,mark);
}
static void invariant(nj_vm *vm,scene *s)
{
    nj_value a[4]; nj_ref saved=s->texrgba; nj_u4 mark=nj_root_mark(vm),baseline[4];
    const unsigned char rgba[]={255,0,0,255, 64,255,0,255, 128,0,255,255, 192,255,255,255};
    nj_ref data=root(vm,nj_byte_array_new(vm,sizeof(rgba))),im; int projection,mode,i;
    memcpy(nj_array_data(data),rgba,sizeof(rgba));
    a[0].i=100; a[1].i=2; a[2].i=2; a[3].r=data;
    im=construct(vm,C("Image2D"),"(III[B)V",a);
    a[0].r=im; s->texrgba=construct(vm,C("Texture2D"),"(L" C("Image2D") ";)V",a);
    integer(vm,s->fog,C("Fog"),"setColor",0x2060A0);
    a[0].f=1; a[1].f=8; call(vm,s->fog,C("Fog"),"setLinear","(FF)V",a,NULL);
    for(projection=0;projection<3;++projection) {
        camera(vm,s,projection);
        for(mode=0;mode<3;++mode) {
            reference_image unused;
            begin(vm,s,&unused,0,1); offset(vm,s,mode==1?-16:mode==2?16:0,mode==1?-65536:mode==2?65536:0);
            draw(vm,s,NEAR_FOG,3,1,0,0,255,1,1,0);
            call(vm,s->g3,C("Graphics3D"),"releaseTarget","()V",NULL,NULL);
            snprintf(context,sizeof(context),"target%d projection%d UV/fog offset%d",s->target,projection,mode);
            for(i=0;i<4;++i) {
                nj_u4 rgb=pixel(vm,s,samples[i][0],samples[i][1]);
                if(!mode)baseline[i]=rgb;
                else check(rgb==baseline[i],"offset changes neither texture coordinates nor fog depth");
            }
            if(!mode)check(baseline[0]!=baseline[2] && baseline[1]!=0,"UV/fog control contains nonuniform visible pixels");
            reference(vm,s->g3,C("Graphics3D"),"bindTarget","(Ljava/lang/Object;)V",s->graphics);
        }
    }
    a[0].f=10; a[1].f=20; call(vm,s->fog,C("Fog"),"setLinear","(FF)V",a,NULL);
    s->texrgba=saved; nj_root_release(vm,mark);
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
        nj_u4 mark=nj_root_mark(vm); init(vm,&s,target); range_api(vm,&s);
        for(path=0;path<PATH_COUNT;++path) {
            nj_u4 temp=nj_root_mark(vm); projection_matrix(vm,&s,path);
            units_matrix(vm,&s,path); factor_matrix(vm,&s,path);
            clipping_matrix(vm,&s,path); disabled_depth(vm,&s,path); nj_root_release(vm,temp);
        }
        saturation(vm,&s);
        if(target==0)sprite_matrix(vm,&s);
        invariant(vm,&s);
        call(vm,s.g3,C("Graphics3D"),"releaseTarget","()V",NULL,NULL);
        vm->screen=saved; vm->screen_w=sw; vm->screen_h=sh;
        nj_root_release(vm,mark); nj_gc_collect(vm);
    }
    nj_vm_destroy(vm); nj_port_shutdown(&port);
    printf("%d checks, %d failures\n",checks,failures); return failures?1:0;
#endif
}
