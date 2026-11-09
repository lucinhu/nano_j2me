/* Nokia UI 1.1 byte pixels: SDK packing examples are independent oracles.
 * Gray polarity follows actual Nokia 3310/3410 behavior documented in
 * FreeJ2ME-Plus PlatformGraphics.java:1348-1351 (larger means darker).
 * No Java fixtures or device files are needed. */
#include "nj2me/nj.h"
#include "../src/midp/nj_midp.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int passed, failed;
#define OK(c) do { if (c) ++passed; else { ++failed; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
static nj_value vi(int i) { nj_value v; v.bits=0; v.i=i; return v; }
static nj_value vr(nj_ref r) { nj_value v; v.bits=0; v.r=r; return v; }
static nj_ref graphics(nj_vm *vm, nj_ref img)
{
    nj_value r; r.bits=0;
    OK(nj_call(vm,img,NJ_C_IMAGE,"getGraphics","()L" NJ_C_GRAPHICS ";",NULL,&r)==NJ_OK);
    return r.r;
}
static nj_status draw(nj_vm *vm,nj_ref g,nj_ref p,nj_ref m,int off,int scan,
                      int x,int y,int w,int h,int op,int fmt)
{
    nj_value a[10];
    a[0]=vr(p); a[1]=vr(m); a[2]=vi(off); a[3]=vi(scan); a[4]=vi(x);
    a[5]=vi(y); a[6]=vi(w); a[7]=vi(h); a[8]=vi(op); a[9]=vi(fmt);
    return nj_call(vm,g,NJ_C_GRAPHICS,"drawPixels","([B[BIIIIIIII)V",a,NULL);
}
static nj_status get(nj_vm *vm,nj_ref g,nj_ref p,nj_ref m,int off,int scan,
                     int x,int y,int w,int h,int fmt)
{
    nj_value a[9];
    a[0]=vr(p); a[1]=vr(m); a[2]=vi(off); a[3]=vi(scan); a[4]=vi(x);
    a[5]=vi(y); a[6]=vi(w); a[7]=vi(h); a[8]=vi(fmt);
    return nj_call(vm,g,NJ_C_GRAPHICS,"getPixels","([B[BIIIIIII)V",a,NULL);
}
static void exc(nj_vm *vm,const char *name)
{
    OK(vm->exception && vm->exception->cls && strcmp(vm->exception->cls->name,name)==0);
    nj_clear_exception(vm);
}
static void trans(nj_vm *vm,nj_ref g,int x,int y)
{
    nj_value a[2]; a[0]=vi(x); a[1]=vi(y);
    OK(nj_call(vm,g,NJ_C_GRAPHICS,"translate","(II)V",a,NULL)==NJ_OK);
}
static void clip(nj_vm *vm,nj_ref g,int x,int y,int w,int h)
{
    nj_value a[4]; a[0]=vi(x); a[1]=vi(y); a[2]=vi(w); a[3]=vi(h);
    OK(nj_call(vm,g,NJ_C_GRAPHICS,"setClip","(IIII)V",a,NULL)==NJ_OK);
}
static unsigned bits(int fmt) { return fmt==-1 ? 1u : fmt==332 ? 8u : (unsigned)fmt; }
/* Test-side packing works one bit at a time; the runtime extracts whole fields.
 * For vertical format, project the image coordinates onto byte pages of 8 rows. */
static void put(unsigned char *p,int fmt,int off,int scan,int x,int y,unsigned value)
{
    unsigned b=bits(fmt), k;
    long long first;
    if (fmt==-1) {
        int row=off/scan+y;
        int index=(row/8)*scan+off%scan+x;
        unsigned char mask=(unsigned char)(1u<<(row%8));
        p[index]=(unsigned char)((p[index]&~mask) | (value ? mask : 0u));
        return;
    }
    first=((long long)off+(long long)y*scan+x)*b;
    for (k=0;k<b;++k) {
        long long at=first+k;
        unsigned char mask=(unsigned char)(0x80u>>(at%8));
        p[at/8]=(unsigned char)((p[at/8]&~mask) | (((value>>(b-1-k))&1u) ? mask : 0u));
    }
}
static unsigned val(const unsigned char *p,int fmt,int off,int scan,int x,int y)
{
    unsigned b=bits(fmt), k, v=0;
    long long first;
    if (fmt==-1) {
        int row=off/scan+y;
        return (p[(row/8)*scan+off%scan+x]>>(row%8))&1u;
    }
    first=((long long)off+(long long)y*scan+x)*b;
    for(k=0;k<b;++k) v=(v<<1) | ((p[(first+k)/8]>>(7-(first+k)%8))&1u);
    return v;
}
static nj_u4 color(unsigned v,int fmt)
{
    unsigned r,g,b;
    if(fmt==332) { r=(v>>5)*255u/7u; g=((v>>2)&7u)*255u/7u; b=(v&3u)*85u; }
    else r=g=b=255u-v*255u/((1u<<bits(fmt))-1u);
    return 0xFF000000u | r<<16 | g<<8 | b;
}
static unsigned encode(nj_u4 c,int fmt)
{
    unsigned r=(c>>16)&255u,g=(c>>8)&255u,b=c&255u;
    if(fmt==332) return (r/32u)*32u+(g/32u)*4u+b/64u;
    return (255u-(r*77u+g*151u+b*28u)/256u)>>(8-bits(fmt));
}
static unsigned alpha_code(unsigned a,int fmt)
{
    unsigned max=(1u<<bits(fmt))-1u, v=a*max/255u;
    return a && !v ? 1u : v;
}
static nj_u4 over(nj_u4 rgb,unsigned a,nj_u4 dst)
{
    unsigned k; nj_u4 result=0;
    for(k=0;k<24;k+=8) result|=((((rgb>>k)&255u)*a+((dst>>k)&255u)*(255u-a)+127u)/255u)<<k;
    return result|0xFF000000u;
}
static void sdk_examples(nj_vm *vm)
{
    static const unsigned char horizontal[11]={0xC1,0xF0,0xFC,0x7F,0x3D,0xDE,0x7F,0x1F,0x87,0xC1,0x80};
    static const unsigned char vertical[18]={0xFF,0xFF,0x0E,0x1C,0x38,0x70,0xE0,0xFF,0xFF,1,1,0,0,0,0,0,1,1};
    nj_u4 mark=nj_root_mark(vm), *pixels;
    nj_ref img=nj_root(vm,nj_midp_image_new(vm,10,13,1));
    nj_ref g=nj_root(vm,graphics(vm,img));
    nj_ref p=nj_root(vm,nj_byte_array_new(vm,30));
    int x,y;
    pixels=nj_midp_image_pixels(vm,img);
    /* Derive the image solely from the SDK's horizontal literal bytes. */
    for(y=0;y<13;++y) for(x=0;x<10;++x) {
        int on=x<9 && y<9 && (horizontal[(y*9+x)/8]&(0x80u>>((y*9+x)%8)));
        pixels[y*10+x]=on ? 0xFF000000u : 0xFFFFFFFFu;
    }
    memset(p->u.bytes,0,30);
    OK(get(vm,g,p,NULL,0,9,0,0,9,9,1)==NJ_OK);
    OK(memcmp(p->u.bytes,horizontal,11)==0);
    memset(p->u.bytes,0,30);
    OK(get(vm,g,p,NULL,0,9,0,0,9,9,-1)==NJ_OK);
    OK(memcmp(p->u.bytes,vertical,18)==0);
    /* SDK vertical example 2 is an offset rectangle spanning two byte pages. */
    for(y=0;y<13;++y) for(x=0;x<10;++x) pixels[y*10+x]=x>=4 && x<9 && y>=4 ? 0xFF000000u : 0xFFFFFFFFu;
    memset(p->u.bytes,0,30);
    OK(get(vm,g,p,NULL,16,15,4,4,5,9,-1)==NJ_OK);
    for(x=0;x<30;++x) OK(p->u.bytes[x]==(x>=1 && x<=5 ? 0xFE : x>=16 && x<=20 ? 3 : 0));
    /* Same rectangle over a sentinel: all untouched bits must survive. */
    memset(p->u.bytes,0x55,30);
    OK(get(vm,g,p,NULL,16,15,4,4,5,9,-1)==NJ_OK);
    for(x=0;x<30;++x) OK(p->u.bytes[x]==(x>=1 && x<=5 ? 0xFF : x>=16 && x<=20 ? 0x57 : 0x55));
    /* Decode the SDK vertical literal independently of runtime getPixels. */
    memcpy(p->u.bytes,vertical,18);
    OK(draw(vm,g,p,NULL,0,9,0,0,9,9,0,-1)==NJ_OK);
    for(y=0;y<9;++y) for(x=0;x<9;++x) {
        int on=(horizontal[(y*9+x)/8]>>(7-(y*9+x)%8))&1;
        OK(pixels[y*10+x]==(on ? 0xFF000000u : 0xFFFFFFFFu));
    }
    nj_root_release(vm,mark);
}
static void draw_matrix(nj_vm *vm)
{
    static const int formats[]={1,-1,2,4,8,332};
    const nj_u4 background=0xFF264A70u;
    nj_u4 mark=nj_root_mark(vm);
    nj_ref img=nj_root(vm,nj_midp_image_new(vm,9,9,1));
    nj_ref g=nj_root(vm,graphics(vm,img));
    nj_ref p=nj_root(vm,nj_byte_array_new(vm,64));
    nj_ref m=nj_root(vm,nj_byte_array_new(vm,64));
    nj_u4 *pixels=nj_midp_image_pixels(vm,img), expected[81];
    int f,mode,mask,op,clipped,x,y,k;
    nj_value arg=vi(0x123456), out;
    trans(vm,g,1,2);
    OK(nj_call(vm,g,NJ_C_GRAPHICS,"setColor","(I)V",&arg,NULL)==NJ_OK);
    for(f=0;f<6;++f) for(mode=0;mode<(formats[f]==-1?1:3);++mode)
    for(mask=0;mask<2;++mask) for(op=0;op<16;++op) for(clipped=0;clipped<2;++clipped) {
        int fmt=formats[f],scan=mode==0 ? 7 : mode==1 ? -7 : 0;
        int off=fmt==-1 ? 51 : mode==1 ? 19 : 3;
        int rot=(op%4)*90, dw=(rot==90||rot==270)?2:3, dh=(rot==90||rot==270)?3:2;
        unsigned max=(1u<<bits(fmt))-1u;
        memset(p->u.bytes,0xA6,64); memset(m->u.bytes,0x59,64);
        for(y=0;y<2;++y) for(x=0;x<3;++x) {
            put(p->u.bytes,fmt,off,scan,x,y,(unsigned)(x+y*3)*47u&max);
            put(m->u.bytes,fmt,off,scan,x,y,(unsigned)(x+y*3)%(max+1));
        }
        for(k=0;k<81;++k) pixels[k]=expected[k]=background;
        clip(vm,g,clipped?1:0,clipped?0:0,clipped?1:8,clipped?2:7);
        for(y=0;y<2;++y) for(x=0;x<3;++x) {
            int u=x,v=y,dx,dy;
            switch(rot) {
            case 90:u=y;v=2-x;break;
            case 180:u=2-x;v=1-y;break;
            case 270:u=1-y;v=x;break;
            default:break;
            }
            if(op&4) v=dh-1-v;
            if(op&8) u=dw-1-u;
            dx=1+u;dy=2+v;
            if(clipped && (dx!=2 || dy>=4)) continue;
            expected[dy*9+dx]=over(color(val(p->u.bytes,fmt,off,scan,x,y),fmt),mask?val(m->u.bytes,fmt,off,scan,x,y)*255u/max:255u,background);
        }
        OK(draw(vm,g,p,mask?m:NULL,off,scan,0,0,3,2,rot|((op&4)?0x4000:0)|((op&8)?0x2000:0),fmt)==NJ_OK);
        OK(memcmp(pixels,expected,sizeof(expected))==0);
        OK(nj_call(vm,g,NJ_C_GRAPHICS,"getColor","()I",NULL,&out)==NJ_OK);
        OK(out.i==0x123456);
    }
    nj_root_release(vm,mark);
}
static void get_matrix(nj_vm *vm)
{
    static const int formats[]={1,-1,2,4,8,332};
    static const nj_u4 samples[]={0x00000000,0x01000000,0x55000000,0xAA000000,0xFF000000,0xFFFFFFFF,0xFFCC6633,0x804488CC};
    nj_u4 mark=nj_root_mark(vm);
    nj_ref img=nj_root(vm,nj_midp_image_new(vm,6,5,1));
    nj_ref g=nj_root(vm,graphics(vm,img));
    nj_ref p=nj_root(vm,nj_byte_array_new(vm,64));
    nj_ref m=nj_root(vm,nj_byte_array_new(vm,64));
    nj_u4 *pixels=nj_midp_image_pixels(vm,img);
    unsigned char ep[64],em[64]; int f,mode,x,y;
    for(y=0;y<5;++y) for(x=0;x<6;++x) pixels[y*6+x]=samples[(y*6+x)%8];
    trans(vm,g,1,1); clip(vm,g,0,0,0,0);
    for(f=0;f<6;++f) for(mode=0;mode<(formats[f]==-1?1:3);++mode) {
        int fmt=formats[f],scan=mode==0?9:mode==1?-9:0,off=fmt==-1?58:mode==1?31:3;
        memset(p->u.bytes,0xA5,64); memset(m->u.bytes,0x5A,64);
        memset(ep,0xA5,64); memset(em,0x5A,64);
        for(y=0;y<3;++y) for(x=0;x<4;++x) {
            nj_u4 c=pixels[(y+1)*6+x+1];
            put(ep,fmt,off,scan,x,y,encode(c,fmt));
            put(em,fmt,off,scan,x,y,alpha_code(c>>24,fmt));
        }
        OK(get(vm,g,p,m,off,scan,0,0,4,3,fmt)==NJ_OK);
        OK(memcmp(p->u.bytes,ep,64)==0); OK(memcmp(m->u.bytes,em,64)==0);
        /* Optional mask does not change color conversion. */
        memset(p->u.bytes,0xA5,64);
        OK(get(vm,g,p,NULL,off,scan,0,0,4,3,fmt)==NJ_OK);
        OK(memcmp(p->u.bytes,ep,64)==0);
    }
    nj_root_release(vm,mark);
}
static void errors(nj_vm *vm)
{
    static const int formats[]={1,-1,2,4,8,332};
    nj_u4 mark=nj_root_mark(vm);
    nj_ref img=nj_root(vm,nj_midp_image_new(vm,3,3,1));
    nj_ref g=nj_root(vm,graphics(vm,img));
    nj_ref p=nj_root(vm,nj_byte_array_new(vm,8));
    nj_ref m=nj_root(vm,nj_byte_array_new(vm,1));
    nj_u4 before[9],*pixels=nj_midp_image_pixels(vm,img);
    unsigned char saved[8]; int f,k;
    memset(p->u.bytes,0xA5,8); memcpy(saved,p->u.bytes,8); m->u.bytes[0]=0x5A;
    memcpy(before,pixels,sizeof(before));
    for(f=0;f<6;++f) {
        int fmt=formats[f], mask_end=fmt==-1 ? 8 : 8/(int)bits(fmt);
        /* A later row leaves the pixel array: validate before the first pixel. */
        int stride=fmt==-1 ? 1 : 8/(int)bits(fmt), rows=fmt==-1 ? 9 : 2;
        OK(draw(vm,g,m,NULL,0,stride,0,0,1,rows,0,fmt)!=NJ_OK); exc(vm,"java/lang/ArrayIndexOutOfBoundsException");
        OK(memcmp(pixels,before,sizeof(before))==0);
        OK(get(vm,g,m,NULL,0,stride,0,0,1,rows,fmt)!=NJ_OK); exc(vm,"java/lang/ArrayIndexOutOfBoundsException");
        OK(m->u.bytes[0]==0x5A);
        /* A later row leaves only the mask; neither destination may change. */
        OK(draw(vm,g,p,m,0,stride,0,0,1,rows,0,fmt)!=NJ_OK); exc(vm,"java/lang/ArrayIndexOutOfBoundsException");
        OK(memcmp(pixels,before,sizeof(before))==0);
        OK(get(vm,g,p,m,0,stride,0,0,1,rows,fmt)!=NJ_OK); exc(vm,"java/lang/ArrayIndexOutOfBoundsException");
        OK(memcmp(p->u.bytes,saved,8)==0); OK(m->u.bytes[0]==0x5A);
        /* Mask is too small although source/destination pixels fit. */
        OK(draw(vm,g,p,m,mask_end,1,0,0,1,1,0,fmt)!=NJ_OK); exc(vm,"java/lang/ArrayIndexOutOfBoundsException");
        OK(memcmp(pixels,before,sizeof(before))==0);
        OK(get(vm,g,p,m,mask_end,1,0,0,1,1,fmt)!=NJ_OK); exc(vm,"java/lang/ArrayIndexOutOfBoundsException");
        OK(memcmp(p->u.bytes,saved,8)==0); OK(m->u.bytes[0]==0x5A);
        OK(draw(vm,g,p,NULL,-1,1,0,0,1,1,0,fmt)!=NJ_OK); exc(vm,"java/lang/ArrayIndexOutOfBoundsException");
        OK(get(vm,g,p,NULL,-1,1,0,0,1,1,fmt)!=NJ_OK); exc(vm,"java/lang/ArrayIndexOutOfBoundsException");
        /* Vertical offset INT_MAX / scan INT_MAX is only row 1, column 0;
         * cross a full eight-row page instead to require an invalid byte. */
        OK(draw(vm,g,p,NULL,fmt==-1?0:INT_MAX,INT_MAX,0,0,2,fmt==-1?10:3,0,fmt)!=NJ_OK); exc(vm,"java/lang/ArrayIndexOutOfBoundsException");
        OK(get(vm,g,p,NULL,fmt==-1?0:INT_MAX,INT_MAX,0,0,2,fmt==-1?10:3,fmt)!=NJ_OK); exc(vm,"java/lang/ArrayIndexOutOfBoundsException");
        OK(draw(vm,g,p,NULL,0,1,0,0,1,1,91,fmt)!=NJ_OK); exc(vm,"java/lang/IllegalArgumentException");
        OK(draw(vm,g,p,NULL,0,1,0,0,-1,1,0,fmt)!=NJ_OK); exc(vm,"java/lang/IllegalArgumentException");
        OK(get(vm,g,p,NULL,0,1,-1,0,1,1,fmt)!=NJ_OK); exc(vm,"java/lang/IllegalArgumentException");
        OK(get(vm,g,p,NULL,0,1,0,0,1,-1,fmt)!=NJ_OK); exc(vm,"java/lang/IllegalArgumentException");
        OK(draw(vm,g,NULL,NULL,0,1,0,0,0,0,0,fmt)!=NJ_OK); exc(vm,"java/lang/NullPointerException");
        OK(get(vm,g,NULL,NULL,0,1,0,0,0,0,fmt)!=NJ_OK); exc(vm,"java/lang/NullPointerException");
        OK(draw(vm,g,p,m,INT_MIN,1,INT_MAX,INT_MAX,0,2,0,fmt)==NJ_OK);
        OK(get(vm,g,p,m,INT_MIN,1,0,0,0,2,fmt)==NJ_OK);
    }
    for(k=0;k<3;++k) {
        int fmt=k==0 ? 565 : k==1 ? 8888 : 3;
        OK(draw(vm,g,p,NULL,0,1,0,0,1,1,0,fmt)!=NJ_OK); exc(vm,"java/lang/IllegalArgumentException");
        OK(get(vm,g,p,NULL,0,1,0,0,1,1,fmt)!=NJ_OK); exc(vm,"java/lang/IllegalArgumentException");
    }
    for(k=-1;k<=0;++k) {
        OK(draw(vm,g,p,NULL,0,k,0,0,1,1,0,-1)!=NJ_OK); exc(vm,"java/lang/IllegalArgumentException");
        OK(get(vm,g,p,NULL,0,k,0,0,1,1,-1)!=NJ_OK); exc(vm,"java/lang/IllegalArgumentException");
    }
    trans(vm,g,INT_MAX,0);
    OK(get(vm,g,p,m,0,1,1,0,1,1,8)!=NJ_OK); exc(vm,"java/lang/IllegalArgumentException");
    OK(memcmp(p->u.bytes,saved,8)==0); OK(m->u.bytes[0]==0x5A);
    nj_root_release(vm,mark);
}
static void screen_pixels(nj_vm *vm)
{
    const int formats[]={NJ_PIXFMT_RGB565,NJ_PIXFMT_RGB888,NJ_PIXFMT_ARGB8888};
    nj_u4 mark=nj_root_mark(vm),storage[4];
    nj_surface saved=vm->screen;
    nj_u2 sw=vm->screen_w,sh=vm->screen_h;
    nj_ref img=nj_root(vm,nj_midp_image_new(vm,2,2,1));
    nj_ref g=nj_root(vm,graphics(vm,img));
    nj_ref p=nj_root(vm,nj_byte_array_new(vm,1));
    nj_ref m=nj_root(vm,nj_byte_array_new(vm,1));
    nj_field *target=NJ_F(vm,NJ_C_GRAPHICS,NJ_F_G_TARGET,"L" NJ_C_IMAGE ";");
    int f;
    nj_field_write_ref(vm,g,target,NULL);
    for(f=0;f<3;++f) {
        vm->screen_w=vm->screen_h=2;
        vm->screen.width=vm->screen.height=2;
        vm->screen.format=formats[f]; vm->screen.bpp=f==0?2:f==1?3:4;
        vm->screen.stride=2*vm->screen.bpp; vm->screen.pixels=storage;
        nj_surface_set(&vm->screen,0,0,0x0100FF00u);
        p->u.bytes[0]=0; m->u.bytes[0]=0;
        OK(get(vm,g,p,m,0,1,0,0,1,1,332)==NJ_OK);
        OK(p->u.bytes[0]==0x1C); OK(m->u.bytes[0]==255);
    }
    vm->screen=saved; vm->screen_w=sw; vm->screen_h=sh;
    nj_root_release(vm,mark);
}
/* Kept separate so the old implementation never runs its unbounded huge loop. */
static void bounded(nj_vm *vm)
{
    nj_u4 mark=nj_root_mark(vm), *pixels;
    nj_ref img=nj_root(vm,nj_midp_image_new(vm,2,2,1));
    nj_ref g=nj_root(vm,graphics(vm,img));
    nj_ref p=nj_root(vm,nj_byte_array_new(vm,1));
    p->u.bytes[0]=0xFF;
    pixels=nj_midp_image_pixels(vm,img);
    OK(draw(vm,g,p,NULL,0,0,0,0,1,INT_MAX,0,8)==NJ_OK);
    OK(pixels[0]==0xFF000000u && pixels[2]==0xFF000000u);
    OK(draw(vm,g,p,NULL,0,0,INT_MAX,INT_MAX,1,INT_MAX,0,8)==NJ_OK);
    OK(draw(vm,g,p,NULL,0,0,INT_MIN,INT_MIN,1,INT_MAX,270,8)==NJ_OK);
    /* Large pixel offsets can still address the first byte in vertical format. */
    OK(draw(vm,g,p,NULL,INT_MAX,INT_MAX,0,0,1,1,0,-1)==NJ_OK);
    p->u.bytes[0]=0;
    OK(get(vm,g,p,NULL,INT_MAX,INT_MAX,0,0,1,1,-1)==NJ_OK);
    OK(p->u.bytes[0]==2);
    nj_root_release(vm,mark);
}
int main(int argc,char **argv)
{
    nj_port port; nj_vm *vm; (void)argv;
    memset(&port,0,sizeof(port));
    if(nj_port_headless(&port)!=NJ_OK) return 2;
    vm=nj_vm_create(&port,4u*1024u*1024u);
    if(!vm) { nj_port_shutdown(&port); return 2; }
    sdk_examples(vm); draw_matrix(vm); get_matrix(vm); errors(vm);
    if(argc>1) { screen_pixels(vm); bounded(vm); }
    nj_vm_destroy(vm); nj_port_shutdown(&port);
    printf("%d checks passed, %d failed\n",passed,failed);
    return failed ? 1 : 0;
}
