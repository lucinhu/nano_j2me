/* Public LCDUI calls: overflow must reveal actual pixels, keep chrome intact,
 * and hit-test the scrolled row. No MIDlet/resources are needed. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include "nj_midp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define F "javax/microedition/lcdui/Form"
#define I "javax/microedition/lcdui/Item"
#define IMG "javax/microedition/lcdui/Image"
#define G "javax/microedition/lcdui/Graphics"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static unsigned fill_calls;
static int fill(void *ud,const nj_surface *s,int x,int y,int w,int h,nj_u4 color)
{
    (void)ud;
    CHECK(x>=0 && y>=0 && x+w<=s->width && y+h<=s->height);
    if(s->bpp!=2 && s->bpp!=4) return 0;
    for(int row=y;row<y+h;++row) for(int col=x;col<x+w;++col)
        memcpy((char*)s->pixels+row*s->stride+col*s->bpp,&color,s->bpp);
    ++fill_calls;return 1;
}
static nj_value call(nj_vm *vm, nj_ref o, const char *c, const char *n, const char *d, nj_value *a)
{ nj_value r = {0}; CHECK(nj_call(vm,o,c,n,d,a,&r)==NJ_OK); return r; }
static nj_ref form(nj_vm *vm, const char *title)
{
    nj_ref o=nj_root(vm,nj_object_new(vm,F));
    nj_value a[1]; a[0].r=nj_string_new(vm,title);
    CHECK(o && nj_call_special(vm,o,F,"<init>","(Ljava/lang/String;)V",a,NULL)==NJ_OK); return o;
}
static int field(nj_vm *vm, nj_ref o, const char *name)
{ nj_field *f=nj_field_lookup(vm,F,name,"I",0); CHECK(f); return nj_field_read_int(vm,o,f); }
static void key(nj_vm *vm, int code)
{ CHECK(nj_midp_key(vm,code,1)); (void)nj_midp_key(vm,code,0); CHECK(nj_midp_paint(vm)==NJ_OK); }
static int find(nj_surface *s, nj_u4 color, int *x, int *y)
{
    for (int row=21;row<s->height-20;++row) for(int col=0;col<s->width;++col)
        if(nj_surface_get(s,col,row)==color) { *x=col; *y=row; return 1; }
    return 0;
}
static void exercise(nj_vm *vm)
{
    nj_ref f=form(vm,"Images");
    nj_value a[4]; int x,y;
    for(int i=0;i<40;++i) {
        nj_value r={0}; a[0].i=3;a[1].i=3;
        CHECK(nj_call_static(vm,IMG,"createImage","(II)L" IMG ";",a,&r)==NJ_OK);
        nj_ref image=nj_root(vm,r.r);
        nj_ref graphics=call(vm,image,IMG,"getGraphics","()L" G ";",NULL).r;
        a[0].i=i==39?0xff0000:0x0000ff;call(vm,graphics,G,"setColor","(I)V",a);
        a[0].i=a[1].i=0;a[2].i=a[3].i=3;call(vm,graphics,G,"fillRect","(IIII)V",a);
        a[0].r=image;call(vm,f,F,"append","(L" IMG ";)I",a);
        a[0].i=i;nj_ref item=call(vm,f,F,"get","(I)L" I ";",a).r;
        a[0].i=-1;a[1].i=32;call(vm,item,I,"setPreferredSize","(II)V",a);
    }
    unsigned fill_before=fill_calls;
    CHECK(nj_midp_set_current(vm,f)==NJ_OK && nj_midp_paint(vm)==NJ_OK);
    CHECK(fill_calls>fill_before);
    nj_surface *s=nj_midp_screen(vm);
    CHECK(!find(s,0xffff0000u,&x,&y));
    for(int i=0;i<45;++i) key(vm,NJ_KEY_DOWN);
    CHECK(find(s,0xffff0000u,&x,&y));
    CHECK(field(vm,f,"fmIndex")==39 && field(vm,f,"fmScroll")>0);
    CHECK(find(s,0xff0000ffu,&x,&y));
    int expected=(y-21+field(vm,f,"fmScroll"))/34;
    CHECK(nj_midp_pointer(vm,x,y,NJ_POINTER_DOWN));
    CHECK(field(vm,f,"fmIndex")==expected && expected<39);
    for(int i=0;i<45;++i) key(vm,NJ_KEY_UP);
    CHECK(field(vm,f,"fmScroll")==0 && field(vm,f,"fmIndex")==0);

    f=form(vm,"Long text");
    char text[8192];size_t length=0;
    for(int i=0;i<80;++i) length+=snprintf(text+length,sizeof(text)-length,"ROW %02d: a long line that wraps to the next row\r\n",i);
    a[0].r=nj_string_new(vm,text);call(vm,f,F,"append","(Ljava/lang/String;)I",a);
    CHECK(nj_midp_set_current(vm,f)==NJ_OK && nj_midp_paint(vm)==NJ_OK);
    size_t bytes=s->stride*s->height, bar=s->stride*20;
    void *before=malloc(bytes),*title=malloc(bar),*soft=malloc(bar);
    CHECK(before&&title&&soft);memcpy(before,s->pixels,bytes);memcpy(title,s->pixels,bar);
    memcpy(soft,(char*)s->pixels+bytes-bar,bar);
    key(vm,NJ_KEY_DOWN);
    CHECK(field(vm,f,"fmIndex")==0 && field(vm,f,"fmScroll")>0);
    CHECK(memcmp(before,s->pixels,bytes)!=0);
    CHECK(memcmp(title,s->pixels,bar)==0 && memcmp(soft,(char*)s->pixels+bytes-bar,bar)==0);
    for(int i=0;i<160;++i) key(vm,NJ_KEY_DOWN);
    int bottom=field(vm,f,"fmScroll");key(vm,NJ_KEY_DOWN);
    CHECK(field(vm,f,"fmScroll")==bottom && bottom>0);
    for(int i=0;i<160;++i) key(vm,NJ_KEY_UP);
    CHECK(field(vm,f,"fmScroll")==0);
    call(vm,f,F,"deleteAll","()V",NULL);CHECK(nj_midp_paint(vm)==NJ_OK);
    CHECK(field(vm,f,"fmScroll")==0);
    free(before);free(title);free(soft);

    f=form(vm,"Progress");
    nj_ref gauge=nj_root(vm,nj_object_new(vm,"javax/microedition/lcdui/Gauge"));
    a[0].r=nj_string_new(vm,"Progress");a[1].i=0;a[2].i=100;a[3].i=50;
    CHECK(nj_call_special(vm,gauge,"javax/microedition/lcdui/Gauge","<init>","(Ljava/lang/String;ZII)V",a,NULL)==NJ_OK);
    a[0].r=gauge;call(vm,f,F,"append","(L" I ";)I",a);
    a[0].r=nj_string_new(vm,"Next");call(vm,f,F,"append","(Ljava/lang/String;)I",a);
    CHECK(nj_midp_set_current(vm,f)==NJ_OK && nj_midp_paint(vm)==NJ_OK);key(vm,NJ_KEY_DOWN);
    CHECK(field(vm,f,"fmIndex")==1);
    CHECK(call(vm,gauge,"javax/microedition/lcdui/Gauge","getValue","()I",NULL).i==50);
}
int main(void)
{
    unsigned released=0;
    for(int round=0;round<3;++round) {
        nj_port port;CHECK(nj_port_headless(&port)==NJ_OK);
        port.gfx_fill_rect=fill;
        nj_vm *vm=nj_vm_create(&port,4*1024*1024);CHECK(vm);exercise(vm);
        unsigned n=nj_vm_destroy(vm);CHECK(n>0 && (!round || n==released));released=n;
        nj_port_shutdown(&port);
    }
    puts("PASS Form: overflow pixels, wrapped text, chrome, pointer focus, progress Gauge; 3 identical teardowns");
}
