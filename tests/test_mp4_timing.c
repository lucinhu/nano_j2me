/* Pure container/descriptor tests. Byte fixtures need no codec or system library.
 * AAC ESDS reference: FFmpeg native AAC-LC, mono 44100 Hz, 0.12 s sine,
 * `ffmpeg -f lavfi -i sine=frequency=997:sample_rate=44100:duration=0.12
 *         -c:a aac -profile:a aac_low -b:a 48k tone.m4a`.
 * ffprobe extradata is 12 08 56 e5 00; the 0x04 fixed fields are NOT config. */
#include "nj_mp4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static unsigned checks;
static const nj_u1 aac_esds[] = {
    0x00,0x00,0x00,0x00,0x03,0x80,0x80,0x80,0x25,0x00,0x01,0x00,
    0x04,0x80,0x80,0x80,0x17,0x40,0x15,0x00,0x00,0x00,0x00,0x00,
    0xd7,0x93,0x00,0x00,0xd7,0x93,0x05,0x80,0x80,0x80,0x05,0x12,
    0x08,0x56,0xe5,0x00,0x06,0x80,0x80,0x80,0x01,0x02
};
static void no_config(const nj_u1 *b, nj_u4 n)
{
    nj_u4 at=123,size=456;
    CHECK(!nj_mp4_esds_find(b,0,n,&at,&size));
    CHECK(at==123 && size==456); /* A failure never publishes partial output. */
}
static void test_esds(void)
{
    nj_u1 b[128]; nj_u4 at=0,size=0,i,j;
    CHECK(nj_mp4_esds_find(aac_esds,0,sizeof(aac_esds),&at,&size));
    CHECK(at==35 && size==5 && !memcmp(aac_esds+at,"\x12\x08\x56\xe5\0",5));
    for(i=0;i<sizeof(aac_esds);i++) no_config(aac_esds,i);
    memcpy(b,aac_esds,sizeof(aac_esds));
    b[16]=13; no_config(b,sizeof(aac_esds)); /* DSI outside DecoderConfig parent. */
    memcpy(b,aac_esds,sizeof(aac_esds));
    b[34]=8; no_config(b,sizeof(aac_esds)); /* DSI escapes its parent into SL. */
    memcpy(b,aac_esds,sizeof(aac_esds));
    b[8]=0x80; no_config(b,sizeof(aac_esds)); /* Fifth size byte forbidden. */
    memcpy(b,aac_esds,sizeof(aac_esds));
    b[3]=1; no_config(b,sizeof(aac_esds)); /* Reserved FullBox flags. */
    memcpy(b,aac_esds,12); b[8]+=8; b[11]=0xe0;
    b[12]=0; b[13]=1; b[14]=3; b[15]='a'; b[16]='b'; b[17]='c'; b[18]=0; b[19]=2;
    memcpy(b+20,aac_esds+12,sizeof(aac_esds)-12);
    CHECK(nj_mp4_esds_find(b,0,sizeof(aac_esds)+8,&at,&size));
    CHECK(at==43 && size==5 && !memcmp(b+at,aac_esds+35,5));
    b[14]=127; no_config(b,sizeof(aac_esds)+8); /* URL length is one byte. */
    CHECK(!nj_mp4_esds_find(aac_esds,0xffffffffu,0xffffffffu,&at,&size));
    CHECK(!nj_mp4_esds_find(aac_esds,5,4,&at,&size));
    CHECK(!nj_mp4_esds_find(NULL,0,4,&at,&size));
    /* Exhaustive single-byte mutations; ASan/UBSan verify every parent boundary. */
    for(i=0;i<sizeof(aac_esds);i++) for(j=0;j<256;j++) {
        memcpy(b,aac_esds,sizeof(aac_esds)); b[i]=(nj_u1)j;
        at=123; size=456;
        if(nj_mp4_esds_find(b,0,sizeof(aac_esds),&at,&size)) {
            CHECK(size && at<=sizeof(aac_esds) && size<=sizeof(aac_esds)-at);
        } else CHECK(at==123 && size==456);
    }
}
/* Minimal valid sample table with independently stated FFmpeg reference timing.
 * The five fake media payloads are not decoded: this checks container semantics. */
typedef struct movie {
    nj_u1 b[4096];
    nj_u4 n, ctts, stts, elst, mvhd;
} movie;
static void set32(nj_u1 *b, nj_u4 v)
{ b[0]=(nj_u1)(v>>24); b[1]=(nj_u1)(v>>16); b[2]=(nj_u1)(v>>8); b[3]=(nj_u1)v; }
static void put32(movie *f, nj_u4 v)
{ CHECK(f->n+4<=sizeof(f->b)); set32(f->b+f->n,v); f->n+=4; }
static void put64(movie *f, nj_u8 v) { put32(f,(nj_u4)(v>>32)); put32(f,(nj_u4)v); }
static void edit_entry(movie *f, int v1, nj_u8 duration, nj_s8 time)
{
    if(v1) { put64(f,duration); put64(f,(nj_u8)time); }
    else { put32(f,(nj_u4)duration); put32(f,(nj_u4)time); }
    put32(f,0x10000);
}
static void zeros(movie *f, nj_u4 n)
{ CHECK(n<=sizeof(f->b)-f->n); memset(f->b+f->n,0,n); f->n+=n; }
static nj_u4 box(movie *f, const char *name)
{
    nj_u4 at=f->n;
    put32(f,0); CHECK(f->n+4<=sizeof(f->b)); memcpy(f->b+f->n,name,4); f->n+=4;
    return at;
}
static void endbox(movie *f, nj_u4 at) { set32(f->b+at,f->n-at); }
static void make_movie(movie *f, int ctts, int edit)
{
    static const nj_u4 offsets[5]={1024,2048,0,2048,0};
    nj_u4 top,trak,mdia,minf,stbl,at,entry,media,i;
    memset(f,0,sizeof(*f));
    at=box(f,"ftyp"); put32(f,0x6d703432); endbox(f,at);
    at=box(f,"mdat"); media=f->n; zeros(f,20); endbox(f,at);
    top=box(f,"moov");
    at=box(f,"mvhd"); f->mvhd=f->n; zeros(f,100);
    set32(f->b+f->mvhd+12,1000); set32(f->b+f->mvhd+16,500); endbox(f,at);
    trak=box(f,"trak");
    if(edit) {
        nj_u4 edts=box(f,"edts"); at=box(f,"elst"); f->elst=f->n;
        int v1=edit>=3, empty=edit==2||edit==4;
        put32(f,v1?0x01000000u:0); put32(f,empty?2:1);
        if(empty) edit_entry(f,v1,100,-1);
        edit_entry(f,v1,500,1024);
        endbox(f,at); endbox(f,edts);
    }
    mdia=box(f,"mdia");
    at=box(f,"mdhd"); zeros(f,12); put32(f,10240); put32(f,5120); put32(f,0); endbox(f,at);
    at=box(f,"hdlr"); zeros(f,8); put32(f,0x76696465); zeros(f,12); endbox(f,at);
    minf=box(f,"minf"); stbl=box(f,"stbl");
    at=box(f,"stsd"); put32(f,0); put32(f,1); entry=box(f,"s263");
    zeros(f,78); f->b[entry+8+25]=32; f->b[entry+8+27]=32;
    endbox(f,entry); endbox(f,at);
    at=box(f,"stts"); f->stts=f->n; put32(f,0); put32(f,1); put32(f,5); put32(f,1024); endbox(f,at);
    if(ctts) {
        at=box(f,"ctts"); f->ctts=f->n;
        put32(f,ctts==2?0x01000000:0); put32(f,5);
        for(i=0;i<5;i++) { put32(f,1); put32(f,offsets[i]-(ctts==2?1024u:0u)); }
        endbox(f,at);
    }
    at=box(f,"stsc"); put32(f,0); put32(f,1); put32(f,1); put32(f,5); put32(f,1); endbox(f,at);
    at=box(f,"stsz"); put32(f,0); put32(f,4); put32(f,5); endbox(f,at);
    at=box(f,"stco"); put32(f,0); put32(f,1); put32(f,media); endbox(f,at);
    endbox(f,stbl); endbox(f,minf); endbox(f,mdia); endbox(f,trak); endbox(f,top);
}
static void test_ctts(void)
{
    static const nj_s8 reordered[5]={0,2048,1024,4096,3072};
    movie f; nj_mp4 m; nj_mp4_sample s; nj_u4 i; int version;
    for(version=0;version<3;version++) {
        make_movie(&f,version,0);
        CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_OK);
        for(i=0;i<5;i++) {
            CHECK(nj_mp4_next(&m,&s)==1 && s.dts==i*1024 && s.duration==1024);
            CHECK(s.pts==(version?reordered[i]+(version==1?1024:0):(nj_s8)i*1024));
        }
        CHECK(nj_mp4_next(&m,&s)==0);
        nj_mp4_rewind(&m); CHECK(nj_mp4_next(&m,&s)==1 && s.dts==0);
    }
    make_movie(&f,2,0); set32(f.b+f.ctts+12,0x80000000u);
    CHECK(nj_mp4_open(&m,f.b,f.n)==0 && nj_mp4_next(&m,&s)==1 && s.pts==-2147483648LL);
    make_movie(&f,1,0); set32(f.b+f.ctts+12,0xffffffffu);
    CHECK(nj_mp4_open(&m,f.b,f.n)==0 && nj_mp4_next(&m,&s)==1 && s.pts==4294967295LL);
    make_movie(&f,1,0); set32(f.b+f.ctts+8,0);
    CHECK(nj_mp4_open(&m,f.b,f.n)<0);
    make_movie(&f,1,0); set32(f.b+f.ctts+8,2);
    CHECK(nj_mp4_open(&m,f.b,f.n)<0);
    make_movie(&f,1,0); set32(f.b+f.ctts+4,4);
    CHECK(nj_mp4_open(&m,f.b,f.n)<0);
    make_movie(&f,1,0); set32(f.b+f.ctts,0x02000000);
    CHECK(nj_mp4_open(&m,f.b,f.n)<0);
    make_movie(&f,1,0); set32(f.b+f.ctts,1);
    CHECK(nj_mp4_open(&m,f.b,f.n)<0);
    make_movie(&f,1,0); set32(f.b+f.ctts+4,0xffffffffu);
    CHECK(nj_mp4_open(&m,f.b,f.n)<0);
    make_movie(&f,0,0); set32(f.b+f.stts+12,0xffffffffu);
    CHECK(nj_mp4_open(&m,f.b,f.n)<0); /* Legacy DTS cannot wrap. */
}
static void test_edits(void)
{
    static const nj_s8 reordered[5]={0,2048,1024,4096,3072};
    movie f; nj_mp4 m; nj_mp4_sample s; int mode; nj_u4 i;
    for(mode=1;mode<=4;mode++) {
        nj_s8 start=(mode==2||mode==4)?1024:0;
        make_movie(&f,1,mode);
        CHECK(nj_mp4_open(&m,f.b,f.n)==0);
        CHECK(m.movie_timescale==1000 && m.timescale==10240 && m.has_edit);
        CHECK(m.presentation_start==start && m.presentation_end==start+5120);
        CHECK(m.presentation_duration==(nj_u8)(start+5120) && m.presentation_offset==start-1024);
        for(i=0;i<5;i++) {
            CHECK(nj_mp4_next(&m,&s)==1 && s.dts==i*1024 && s.pts==reordered[i]+start);
        }
        nj_mp4_rewind(&m); CHECK(nj_mp4_next(&m,&s)==1 && s.pts==start);
    }
    make_movie(&f,0,1); CHECK(nj_mp4_open(&m,f.b,f.n)==0);
    CHECK(nj_mp4_next(&m,&s)==1 && s.pts==-1024 && s.dts==0); /* Preroll retained. */
    make_movie(&f,1,1); set32(f.b+f.elst+16,0x00008000);
    CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_ERR_FEATURE); /* Rate 0.5 */
    make_movie(&f,1,1); set32(f.b+f.elst+16,0);
    CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_ERR_FEATURE); /* Dwell unsupported. */
    make_movie(&f,1,2); set32(f.b+f.elst+12,0);
    CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_ERR_FEATURE); /* Two media segments. */
    make_movie(&f,1,1); set32(f.b+f.elst+12,0xffffffffu);
    CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_ERR_FEATURE); /* Empty-only timeline. */
    make_movie(&f,1,1); set32(f.b+f.elst+12,0xfffffffeu);
    CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_ERR_FORMAT); /* Invalid negative media time. */
    make_movie(&f,1,1); set32(f.b+f.elst+4,2);
    CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_ERR_FORMAT); /* Truncated entry. */
    make_movie(&f,1,1); set32(f.b+f.elst,0x02000000);
    CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_ERR_FORMAT);
    make_movie(&f,1,1); set32(f.b+f.mvhd+12,0);
    CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_ERR_FORMAT);
    make_movie(&f,1,3); set32(f.b+f.elst+8,0xffffffffu); set32(f.b+f.elst+12,0xffffffffu);
    CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_ERR_FEATURE); /* Rescale overflow. */
    make_movie(&f,1,4); set32(f.b+f.elst+8,0xffffffffu); set32(f.b+f.elst+12,0xffffffffu);
    CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_ERR_FEATURE); /* Sum overflow. */
    make_movie(&f,1,3); set32(f.b+f.elst+16,0x80000000u);
    CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_ERR_FORMAT); /* INT64_MIN time. */
    make_movie(&f,1,3); set32(f.b+f.elst+16,1);
    CHECK(nj_mp4_open(&m,f.b,f.n)==NJ_MP4_ERR_FEATURE); /* Beyond supported DTS range. */
    make_movie(&f,1,2); set32(f.b+f.elst+8,1); set32(f.b+f.elst+20,1);
    CHECK(nj_mp4_open(&m,f.b,f.n)==0 && m.presentation_start==10 && m.presentation_end==20);
    /* Boundary conversion floors cumulative endpoints, not each duration. */
}
int main(void)
{
    test_esds(); test_ctts(); test_edits();
    printf("PASS MP4 descriptors/timing: %u checks\n",checks);
    return 0;
}
