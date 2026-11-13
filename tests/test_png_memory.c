/* Synthetic PNGs exercise filtered rows, compaction and allocation cleanup.
 * No game assets are needed. Copyright follows the repository's MIT license. */
#include "nj2me/nj_png.h"
#include "nj2me/nj_inflate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static void check(int ok, const char *message) {
    if (!ok) { ++failures; fprintf(stderr, "FAIL: %s\n", message); }
}
static void be32(nj_u1 *p, nj_u4 n) {
    p[0]=(nj_u1)(n>>24); p[1]=(nj_u1)(n>>16); p[2]=(nj_u1)(n>>8); p[3]=(nj_u1)n;
}
static void chunk(nj_u1 *png, nj_u4 *n, const char *type, const nj_u1 *data, nj_u4 size) {
    be32(png+*n,size); memcpy(png+*n+4,type,4);
    if (size) memcpy(png+*n+8,data,size);
    be32(png+*n+8+size,nj_crc32(png+*n+4,size+4,0)); *n+=size+12;
}
static nj_u4 wrap(nj_u1 *png, int w, int h, int depth, int colour, int interlace,
                   const nj_u1 *z, nj_u4 zsize) {
    static const nj_u1 signature[]={137,80,78,71,13,10,26,10};
    nj_u1 ihdr[13]={0}; nj_u4 n=8;
    memcpy(png,signature,8); be32(ihdr,w); be32(ihdr+4,h);
    ihdr[8]=depth; ihdr[9]=colour; ihdr[12]=interlace;
    chunk(png,&n,"IHDR",ihdr,sizeof(ihdr)); chunk(png,&n,"IDAT",z,zsize);
    chunk(png,&n,"IEND",NULL,0); return n;
}
static unsigned extent(unsigned n, unsigned start, unsigned step) {
    return n>start?(n-start+step-1)/step:0;
}
static unsigned channel(int x, int y, int c) {
    return (unsigned)(x*(17+c*11)+y*(29+c*7)+c*41)&255;
}
static unsigned paeth(unsigned a,unsigned b,unsigned c) {
    int p=(int)a+(int)b-(int)c;
    int da=abs(p-(int)a),db=abs(p-(int)b),dc=abs(p-(int)c);
    return da<=db && da<=dc?a:db<=dc?b:c;
}
/* A stored DEFLATE block makes each filtered sample directly reproducible. */
static nj_u4 filtered_png(nj_u1 *png,int depth,int colour,int interlace,int bad_filter) {
    static const unsigned adam[7][4]={{0,0,8,8},{4,0,8,8},{0,4,4,8},
        {2,0,4,4},{0,2,2,4},{1,0,2,2},{0,1,1,2}};
    nj_u1 raw[2048],z[2060],row[136],previous[136]; nj_u4 n=0;
    unsigned channels=colour==6?4:colour==2?3:2,bpp=channels*(depth/8);
    int pass;
    for(pass=0;pass<(interlace?7:1);++pass) {
        unsigned x0=interlace?adam[pass][0]:0,y0=interlace?adam[pass][1]:0;
        unsigned dx=interlace?adam[pass][2]:1,dy=interlace?adam[pass][3]:1;
        unsigned pw=extent(17,x0,dx),ph=extent(11,y0,dy),y,x,c;
        if(!pw || !ph)continue;
        memset(previous,0,sizeof(previous));
        for(y=0;y<ph;++y) {
            unsigned filter=y%5,i;
            for(x=0;x<pw;++x)for(c=0;c<channels;++c) {
                unsigned component=colour==4?(c?3:0):c;
                unsigned v=channel(x0+x*dx,y0+y*dy,component),at=x*bpp+c*(depth/8);
                row[at]=v;
                if(depth==16)row[at+1]=(x*13+y*7+c)&255;
            }
            raw[n++]=bad_filter && y+1==ph?5:filter;
            for(i=0;i<pw*bpp;++i) {
                unsigned a=i>=bpp?row[i-bpp]:0,b=previous[i],c0=i>=bpp?previous[i-bpp]:0;
                unsigned prediction=filter==0?0:filter==1?a:filter==2?b:
                    filter==3?(a+b)/2:paeth(a,b,c0);
                raw[n++]=(nj_u1)(row[i]-prediction);
            }
            memcpy(previous,row,pw*bpp);
        }
    }
    z[0]=0x78;z[1]=1;z[2]=1;z[3]=n&255;z[4]=n>>8;
    z[5]=(~n)&255;z[6]=((~n)>>8)&255;memcpy(z+7,raw,n);
    be32(z+7+n,nj_adler32(raw,n));
    return wrap(png,17,11,depth,colour,interlace,z,n+11);
}
typedef union allocation_header { nj_u8 alignment[2]; struct {size_t size;} value; } allocation_header;
typedef struct budget {size_t live,peak,cap,calls,fail_at;} budget;
static void *limited_alloc(void *ud,nj_size size) {
    budget *b=ud;allocation_header *h;
    if(++b->calls==b->fail_at || size>b->cap-b->live)return NULL;
    h=malloc(sizeof(*h)+size);if(!h)return NULL;h->value.size=size;
    b->live+=size;if(b->live>b->peak)b->peak=b->live;return h+1;
}
static void limited_free(void *ud,void *p) {
    budget *b=ud;allocation_header *h;
    if(!p)return;h=(allocation_header*)p-1;
    if(h->value.size>b->live)abort();b->live-=h->value.size;free(h);
}
static void colour_cases(void) {
    static const int formats[][2]={{8,6},{16,6},{16,2},{16,4},{8,2},{8,4}};
    unsigned f;int interlace;
    for(f=0;f<sizeof(formats)/sizeof(formats[0]);++f)for(interlace=0;interlace<2;++interlace) {
        nj_u1 png[4096];int depth=formats[f][0],colour=formats[f][1],x,y;
        nj_u4 n=filtered_png(png,depth,colour,interlace,0),before=nj_crc32(png,n,0);
        budget b={0,0,(size_t)-1,0,0};const char *error=NULL;
        nj_image *image=nj_png_decode(png,n,limited_alloc,&b,limited_free,&error);
        check(image!=NULL,"filtered PNG decodes");
        if(image) {
            for(y=0;y<11;++y)for(x=0;x<17;++x) {
                unsigned r=channel(x,y,0),g=colour==4?r:channel(x,y,1);
                unsigned blue=colour==4?r:channel(x,y,2),a=colour==2?255:channel(x,y,3);
                check(image->pixels[y*17+x]==((a<<24)|(r<<16)|(g<<8)|blue),"exact ARGB samples");
            }
            check(image->has_alpha==(colour!=2),"alpha-channel semantics");
            nj_image_free(image,limited_free,&b);
        }
        check(b.live==0,"all decoded allocations released");
        check(nj_crc32(png,n,0)==before,"source PNG remains unchanged");
        n=filtered_png(png,depth,colour,interlace,1);
        image=nj_png_decode(png,n,limited_alloc,&b,limited_free,&error);
        check(!image && error && strstr(error,"filter"),"late invalid filter rejected");
        if(image)nj_image_free(image,limited_free,&b);
        check(b.live==0,"malformed decode leaves no allocations");
    }
}
static void memory_case(void) {
    /* zlib-compressed 128x64 RGBA (64,32,16,128), row filters cycle 0..4.
     * Generated from synthetic pixels; unlike a stored stream, its small
     * IDAT allows a meaningful live-allocation limit below two full images. */
    static const nj_u1 compressed[]={
        0x78,0xda,0xed,0xd6,0xb1,0x11,0xc0,0x20,0x0c,0x04,0x41,0x6c,0x1c,0x38,0xa4,0x04,
        0x95,0x42,0x69,0x94,0xce,0xd0,0x03,0xd9,0xef,0xcd,0xbc,0x0a,0x50,0xb4,0x6d,0xd6,
        0x58,0x96,0xbb,0xe7,0x9c,0xa6,0xd8,0x5e,0x2f,0xc8,0xae,0xd7,0xf8,0xa7,0x37,0xe4,
        0xf6,0x79,0x41,0x78,0x20,0x04,0x81,0x10,0x08,0x81,0x82,0x40,0x41,0xa0,0x20,0xd0,
        0x20,0x50,0x10,0x28,0x08,0x14,0x04,0x0a,0x02,0x0d,0x02,0x05,0x81,0x82,0x40,0x41,
        0xa0,0x20,0xd0,0x20,0x50,0x10,0x28,0x08,0x14,0x04,0x0a,0x02,0x0d,0x02,0x05,0x81,
        0x82,0x40,0x41,0xa0,0x20,0xd0,0x20,0x50,0x10,0x28,0x08,0x14,0x04,0x0a,0x02,0x0d,
        0x02,0x05,0x81,0x82,0x40,0x41,0xa0,0x20,0xd0,0x20,0x50,0x10,0x28,0x08,0x14,0x04,
        0x0a,0x02,0x0d,0x02,0x05,0x81,0x82,0x40,0x41,0xa0,0x20,0xd0,0x20,0x50,0x10,0x28,
        0x08,0x14,0x04,0x0a,0x02,0x0d,0x02,0x05,0x81,0x82,0x40,0x41,0xa0,0x20,0xd0,0x20,
        0x50,0x10,0x28,0x08,0xd4,0xed,0x36,0x7f,0x64,0x2b,0x21};
    nj_u1 png[512];nj_u4 n=wrap(png,128,64,8,6,0,compressed,sizeof(compressed));
    budget b={0,0,36000,0,0};const char *error=NULL;size_t calls,fail;
    nj_image *image=nj_png_decode(png,n,limited_alloc,&b,limited_free,&error);
    check(image!=NULL,"RGBA decode fits one image plus bounded IDAT overhead");
    if(image) {
        unsigned i;for(i=0;i<128*64;++i)check(image->pixels[i]==0x80402010u,"filtered RGBA retains every pixel");
        nj_image_free(image,limited_free,&b);
    }
    check(b.live==0,"memory-limited image releases its retained buffer");
    printf("RGBA decode peak=%lu bytes (two pixel images need 65536)\n",(unsigned long)b.peak);
    calls=b.calls;
    for(fail=1;fail<=calls;++fail) {
        budget failing={0,0,(size_t)-1,0,fail};
        image=nj_png_decode(png,n,limited_alloc,&failing,limited_free,&error);
        check(!image,"each allocation failure is reported");
        if(image)nj_image_free(image,limited_free,&failing);
        check(failing.live==0,"allocation failure leaves no live memory");
    }
}
int main(void) {
    const char *error=NULL;
    nj_image *probe=nj_png_decode(NULL,0,NULL,NULL,NULL,&error);
    if(probe)nj_image_free(probe,NULL,NULL);
    if(error && strstr(error,"not built")) {
        puts("test_png_memory: SKIP (PNG support not built)");return 0;
    }
    colour_cases();memory_case();return failures?1:0;
}
