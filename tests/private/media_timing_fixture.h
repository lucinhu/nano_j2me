/* Container-only writer. All four H.263 samples are copied intact from the
 * existing FFmpeg I/P/P/P clip; only stts/ctts/elst metadata is synthesized.
 * Expected times are stated independently in test_media_timing.c. */
#ifndef NJ_TEST_MEDIA_TIMING_FIXTURE_H
#define NJ_TEST_MEDIA_TIMING_FIXTURE_H
#include "nj_mp4.h"
typedef struct timing_movie { nj_u1 bytes[8192]; nj_u4 n; } timing_movie;
static void timing_u32(timing_movie *f, nj_u4 v)
{
    CHECK(f->n+4<=sizeof(f->bytes));
    f->bytes[f->n++]=(nj_u1)(v>>24); f->bytes[f->n++]=(nj_u1)(v>>16);
    f->bytes[f->n++]=(nj_u1)(v>>8); f->bytes[f->n++]=(nj_u1)v;
}
static nj_u4 timing_box(timing_movie *f, const char *name)
{
    nj_u4 at=f->n; timing_u32(f,0);
    CHECK(f->n+4<=sizeof(f->bytes)); memcpy(f->bytes+f->n,name,4); f->n+=4;
    return at;
}
static void timing_end(timing_movie *f, nj_u4 at)
{
    nj_u4 save=f->n; f->n=at; timing_u32(f,save-at); f->n=save;
}
static void timing_zeros(timing_movie *f, nj_u4 n)
{ CHECK(n<=sizeof(f->bytes)-f->n); memset(f->bytes+f->n,0,n); f->n+=n; }
/* leading ms; media_start ms (-1 disables ELST); visible segment ms.
 * Reordered=1 deliberately requires a presentation queue the Player lacks. */
static void timing_fixture(timing_movie *f, nj_u4 leading, nj_s4 media_start,
                            nj_u4 segment, int reordered)
{
    static const nj_u4 delta[4]={100,300,50,200};
    nj_mp4 original; nj_mp4_sample samples[4];
    nj_u4 media,top,trak,mdia,minf,stbl,at,entry,i;
    CHECK(nj_mp4_open(&original,clip_3gp,sizeof(clip_3gp))==NJ_MP4_OK);
    for(i=0;i<4;i++) CHECK(nj_mp4_next(&original,&samples[i])==1);
    memset(f,0,sizeof(*f));
    at=timing_box(f,"ftyp"); timing_u32(f,0x33677034); timing_end(f,at);
    at=timing_box(f,"mdat"); media=f->n;
    for(i=0;i<4;i++) {
        CHECK(samples[i].size<=sizeof(f->bytes)-f->n);
        memcpy(f->bytes+f->n,clip_3gp+samples[i].offset,samples[i].size);
        f->n+=samples[i].size;
    }
    timing_end(f,at); top=timing_box(f,"moov");
    at=timing_box(f,"mvhd"); timing_zeros(f,12); timing_u32(f,1000);
    timing_u32(f,media_start>=0?leading+segment:650); timing_zeros(f,80); timing_end(f,at);
    trak=timing_box(f,"trak");
    if(media_start>=0) {
        nj_u4 ed=timing_box(f,"edts"); at=timing_box(f,"elst");
        timing_u32(f,0); timing_u32(f,leading?2:1);
        if(leading) { timing_u32(f,leading); timing_u32(f,0xffffffffu); timing_u32(f,0x10000); }
        timing_u32(f,segment); timing_u32(f,(nj_u4)media_start); timing_u32(f,0x10000);
        timing_end(f,at); timing_end(f,ed);
    }
    mdia=timing_box(f,"mdia"); at=timing_box(f,"mdhd");
    timing_zeros(f,12); timing_u32(f,1000); timing_u32(f,650); timing_u32(f,0); timing_end(f,at);
    at=timing_box(f,"hdlr"); timing_zeros(f,8); timing_u32(f,0x76696465); timing_zeros(f,12); timing_end(f,at);
    minf=timing_box(f,"minf"); stbl=timing_box(f,"stbl"); at=timing_box(f,"stsd");
    timing_u32(f,0); timing_u32(f,1); entry=timing_box(f,"s263"); timing_zeros(f,78);
    f->bytes[entry+8+25]=128; f->bytes[entry+8+27]=96;
    timing_end(f,entry); timing_end(f,at);
    at=timing_box(f,"stts"); timing_u32(f,0); timing_u32(f,4);
    for(i=0;i<4;i++) { timing_u32(f,1); timing_u32(f,delta[i]); } timing_end(f,at);
    if(reordered) {
        at=timing_box(f,"ctts"); timing_u32(f,0); timing_u32(f,4);
        for(i=0;i<4;i++) { timing_u32(f,1); timing_u32(f,i==1?500:0); } timing_end(f,at);
    }
    at=timing_box(f,"stsc"); timing_u32(f,0); timing_u32(f,1);
    timing_u32(f,1); timing_u32(f,4); timing_u32(f,1); timing_end(f,at);
    at=timing_box(f,"stsz"); timing_u32(f,0); timing_u32(f,0); timing_u32(f,4);
    for(i=0;i<4;i++) timing_u32(f,samples[i].size);
    timing_end(f,at);
    at=timing_box(f,"stco"); timing_u32(f,0); timing_u32(f,1); timing_u32(f,media); timing_end(f,at);
    timing_end(f,stbl); timing_end(f,minf); timing_end(f,mdia); timing_end(f,trak); timing_end(f,top);
}
#endif
