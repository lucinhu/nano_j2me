/* MIT. Actual MIDP Player audio timeline: immutable MPEG-4 picture samples and
 * AMR blocks, independently edited tracks, sample-exact PCM including silence.
 * CHECK deliberately remains active in Release builds with NDEBUG. */
#include "nj2me/nj_config.h"
#if NJ_ENABLE_MEDIA && NJ_ENABLE_VIDEO && NJ_ENABLE_M4V && NJ_ENABLE_AMR
#include "nj2me/nj.h"
#include "nj2me/nj_port.h"
#include "nj_internal.h"
#include "nj_midp.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do { ++checks; if(!(c)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while(0)
#include "private/movie_amr_timing_fixture.h"
#define MANAGER "javax/microedition/media/Manager"
#define PLAYER "javax/microedition/media/Player"
#define STREAM "java/io/ByteArrayInputStream"
static nj_u4 now;
static int opened,closed;
static nj_u4 milliseconds(void *ud) { (void)ud; return now; }
static int pcm_open(void *ud,int rate,int channels)
{ (void)ud; CHECK(rate==8000 && channels==1); ++opened; return 1; }
static int pcm_write(void *ud,const nj_s2 *samples,int frames)
{ (void)ud; (void)samples; CHECK(frames>=0); return frames; }
static void pcm_close(void *ud) { (void)ud; ++closed; }
static nj_vm *movie_vm(nj_port *port)
{
    CHECK(nj_port_headless(port)==NJ_OK); now=10000; opened=closed=0;
    port->millis=milliseconds; port->fps_cap=0;
    port->pcm_open=pcm_open; port->pcm_write=pcm_write; port->pcm_close=pcm_close;
    return nj_vm_create(port,4u*1024u*1024u);
}
static nj_s4 int_field(nj_vm *vm,nj_ref p,const char *name)
{
    const nj_field *f=nj_field_lookup(vm,PLAYER,name,"I",0);
    CHECK(f!=NULL); return nj_field_read_int(vm,p,f);
}
static nj_ref ref_field(nj_vm *vm,nj_ref p,const char *name)
{
    const nj_field *f=nj_field_lookup(vm,PLAYER,name,"[B",0);
    CHECK(f!=NULL); return nj_field_read_ref(vm,p,f);
}
static void call0(nj_vm *vm,nj_ref p,const char *name)
{ CHECK(nj_call(vm,p,PLAYER,name,"()V",NULL,NULL)==NJ_OK); }
static nj_ref make_player(nj_vm *vm,const movie_amr_fixture *f)
{
    nj_u4 mark=nj_root_mark(vm);
    nj_ref data=nj_root(vm,nj_byte_array_new(vm,f->n)),stream;
    nj_value a[2],r;
    CHECK(data!=NULL); memcpy(data->u.bytes,f->bytes,f->n);
    stream=nj_root(vm,nj_object_new(vm,STREAM)); CHECK(stream!=NULL);
    a[0].r=data;
    CHECK(nj_call_special(vm,stream,STREAM,"<init>","([B)V",a,NULL)==NJ_OK);
    a[0].r=stream; a[1].r=NULL; r.r=NULL;
    CHECK(nj_call_static(vm,MANAGER,"createPlayer",
        "(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player;",a,&r)==NJ_OK);
    CHECK(r.r!=NULL); nj_root_release(vm,mark); return r.r;
}
static nj_u1 baseline[800u*2u];
static void baseline_pcm(void)
{
    movie_amr_fixture f; movie_amr_options o=movie_amr_defaults();
    nj_port port; nj_vm *vm=movie_vm(&port); unsigned i,nonzero=0;
    CHECK(vm!=NULL); movie_amr_make(&f,&o);
    CHECK(nj_media_movie_amr_decode(vm,f.bytes,f.n,baseline,sizeof(baseline))==5);
    for(i=0;i<sizeof(baseline);i++) if(baseline[i]) ++nonzero;
    CHECK(nonzero>100); nj_vm_destroy(vm); nj_port_shutdown(&port);
}
/* The oracle is explicit source/destination ranges supplied by each test, not
 * the production rescaling/intersection algorithm. All other samples are zero. */
static void expect_pcm(nj_vm *vm,nj_ref p,nj_u4 total,nj_u4 dest,nj_u4 src,nj_u4 n)
{
    nj_ref b=ref_field(vm,p,"pApcm"); nj_u4 i;
    CHECK(int_field(vm,p,"pApcmn")== (nj_s4)total);
    CHECK(b!=NULL && b->length==total*2u);
    CHECK(dest<=total && n<=total-dest && src<=800u && n<=800u-src);
    for(i=0;i<total;i++) {
        const nj_u1 *want=NULL;
        if(i>=dest && i-dest<n) want=baseline+(src+i-dest)*2u;
        CHECK(b->u.bytes[2u*i]==(want?want[0]:0));
        CHECK(b->u.bytes[2u*i+1]==(want?want[1]:0));
    }
}
static void run_case(const char *name,const movie_amr_options *o,
                     nj_u4 total,nj_u4 dest,nj_u4 src,nj_u4 n,int seek)
{
    movie_amr_fixture f; nj_port port; nj_vm *vm=movie_vm(&port); nj_ref p,video;
    CHECK(vm!=NULL); movie_amr_make(&f,o);
    p=make_player(vm,&f); CHECK(int_field(vm,p,"pKind")==2);
    call0(vm,p,"start");
    /* Actual decoder routes through a real picture track, not a demux stub. */
    video=ref_field(vm,p,"pVbuf"); CHECK(video!=NULL && video->length==64u*48u*2u);
    CHECK(!nj_has_exception(vm));
    if(total) {
        expect_pcm(vm,p,total,dest,src,n); CHECK(opened==1);
        if(seek) {
            nj_value a,r; nj_s8 expected;
            a.j=50000; r.j=-1;
            CHECK(nj_call(vm,p,PLAYER,"setMediaTime","(J)J",&a,&r)==NJ_OK);
            CHECK(r.j>=0); expected=r.j*8000/1000000;
            if(expected>total) expected=total;
            CHECK(int_field(vm,p,"pApush")==expected);
            expect_pcm(vm,p,total,dest,src,n);
            call0(vm,p,"stop"); call0(vm,p,"start");
            expect_pcm(vm,p,total,dest,src,n);
        }
    } else {
        CHECK(int_field(vm,p,"pApcmn")==0);
        CHECK(ref_field(vm,p,"pApcm")==NULL);
        CHECK(opened==0);
    }
    call0(vm,p,"close"); CHECK(!nj_has_exception(vm));
    nj_vm_destroy(vm); nj_port_shutdown(&port);
    printf("PASS %s\n",name);
}
/* On an uncapped port (Android's default, libretro) a movie's pictures still
 * have to follow the audio clock: pumping twice in the same millisecond must
 * not present two pictures, or the sound and the picture separate. */
static void video_follows_audio_clock(void)
{
    movie_amr_fixture f; movie_amr_options o=movie_amr_defaults();
    nj_port port; nj_vm *vm=movie_vm(&port); nj_ref p; nj_s4 a,b,c;
    CHECK(vm!=NULL); movie_amr_make(&f,&o); p=make_player(vm,&f);
    call0(vm,p,"start");
    CHECK(int_field(vm,p,"pKind")==2 && int_field(vm,p,"pApcmn")>0);
    nj_media_frame(vm); a=int_field(vm,p,"pPushed");
    nj_media_frame(vm); b=int_field(vm,p,"pPushed");
    CHECK(a==b);                    /* same millisecond: the picture holds */
    now+=3000;                      /* well past the whole clip */
    nj_media_frame(vm); c=int_field(vm,p,"pPushed");
    CHECK(c>a || int_field(vm,p,"pState")==300);
    call0(vm,p,"close"); CHECK(!nj_has_exception(vm));
    nj_vm_destroy(vm); nj_port_shutdown(&port);
    printf("PASS video follows the audio clock on an uncapped port\n");
}
int main(void)
{
    movie_amr_options o;
    baseline_pcm();
    video_follows_audio_clock();
    o=movie_amr_defaults(); run_case("unmodified actual Player",&o,800,0,0,800,1);
    o.audio_media=0; o.audio_leading=4000; o.audio_segment=800;
    run_case("500ms leading silence",&o,4800,4000,0,800,1);
    o=movie_amr_defaults(); o.audio_media=80; o.audio_segment=720;
    run_case("non-frame-aligned 80-sample preroll",&o,720,0,80,720,1);
    o.audio_segment=560;
    run_case("80-sample preroll and cropped tail",&o,560,0,80,560,0);
    o.audio_leading=4000;
    run_case("leading silence plus both crops",&o,4560,4000,80,560,1);
    o.video_media=100; o.video_leading=1600; o.video_segment=2400;
    run_case("independently different video and audio ELST",&o,4560,4000,80,560,1);
    o=movie_amr_defaults(); o.video_media=100; o.video_leading=1600; o.video_segment=2400;
    run_case("video edit leaves unedited audio intact",&o,800,0,0,800,1);
    o=movie_amr_defaults(); o.audio_media=0; o.audio_segment=400;
    run_case("tail crop",&o,400,0,0,400,0);
    o.audio_segment=1040;
    run_case("edit beyond source zeroes tail",&o,1040,0,0,800,0);
    o.audio_media=1000; o.audio_segment=400;
    run_case("empty intersection is explicit silence",&o,400,0,0,0,0);
    o=movie_amr_defaults(); o.bundle=5;
    run_case("legal five-frame sample bundling",&o,800,0,0,800,0);
    o.audio_media=80; o.audio_segment=560;
    run_case("bundled samples with subframe edit",&o,560,0,80,560,0);
    o=movie_amr_defaults(); o.audio_scale=16000; o.audio_media=1; o.audio_segment=800;
    run_case("negative fractional offset floors to minus one",&o,800,0,1,799,0);
    o=movie_amr_defaults(); o.audio_media=0; o.audio_leading=319200; o.audio_segment=800;
    run_case("mapped capacity exactly 320000",&o,320000,319200,0,800,0);
    o.audio_leading=319201;
    run_case("mapped capacity exceeded by one sample",&o,0,0,0,0,0);
    o=movie_amr_defaults(); o.frames=2001; o.audio_media=320000; o.audio_segment=160;
    run_case("raw capacity exceeded despite tiny edited result",&o,0,0,0,0,0);
    o=movie_amr_defaults(); o.audio_delta=161;
    run_case("non-contiguous AMR timing rejected",&o,0,0,0,0,0);
    o=movie_amr_defaults(); o.audio_ctts=1;
    run_case("positive audio CTTS rejected",&o,0,0,0,0,0);
    o.audio_ctts=-1;
    run_case("negative audio CTTS rejected",&o,0,0,0,0,0);
    printf("movie AMR timing: %u checks PASS (NDEBUG assertions active)\n",checks);
    return 0;
}
#else
#include <stdio.h>
int main(void) { puts("movie AMR timing: SKIP (requires MEDIA VIDEO M4V AMR)"); return 0; }
#endif
