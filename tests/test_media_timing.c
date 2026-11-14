/* Player routing through public MIDP calls: synthetic audio metadata hooks plus
 * complete H.263 pixels from the original I/P/P/P reference clip. */
#include "nj2me/nj_config.h"
#if NJ_ENABLE_MEDIA && NJ_ENABLE_VIDEO && NJ_ENABLE_H263
#include "nj2me/nj.h"
#include "nj2me/nj_port.h"
#include "nj_internal.h"
#include "nj_midp.h"
#include "h263_clip.h"
#include "m4v_clip.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MANAGER "javax/microedition/media/Manager"
#define PLAYER "javax/microedition/media/Player"
#define STREAM "java/io/ByteArrayInputStream"
static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

typedef struct audio_probe { int mode, queries, decodes; } audio_probe;
static audio_probe *current_probe;
static nj_status audio_decode(void *ud, const nj_u1 *in, nj_u4 in_len,
                               nj_u1 *out, nj_u4 cap, nj_u4 *need,
                               nj_u4 *rate, nj_u4 *channels)
{
    audio_probe *s = current_probe;
    (void)ud; (void)in; (void)in_len;
    *need = 8; *rate = 22050; *channels = 2;
    if (!out) {
        ++s->queries;
        if (s->mode == 1) *rate = 0x80000000u;
        if (s->mode == 2) *channels = 3;
        if (s->mode == 3) *need = 7;
    } else {
        ++s->decodes;
        memset(out, 0, cap);
        if (s->mode == 4) ++*rate;
        if (s->mode == 5) *channels = 0;
        if (s->mode == 6) *need = cap + 4;
        if (s->mode == 7) *need = 0;
        if (s->mode == 8) *need = 7;
    }
    return NJ_OK;
}

static nj_vm *new_vm(nj_port *port, audio_probe *probe)
{
    CHECK(nj_port_headless(port) == NJ_OK);
    current_probe = probe;
    port->audio_decode = audio_decode;
    return nj_vm_create(port, 2u * 1024u * 1024u);
}

static nj_status player(nj_vm *vm, const unsigned char *bytes, unsigned size,
                         nj_ref *out)
{
    nj_value a[2], result;
    nj_ref data = nj_byte_array_new(vm, size), stream;
    CHECK(data != NULL);
    memcpy(data->u.bytes, bytes, size);
    a[0].r = data;
    stream = nj_object_new(vm, STREAM);
    CHECK(stream != NULL);
    CHECK(nj_call_special(vm, stream, STREAM, "<init>", "([B)V", a, NULL) == NJ_OK);
    a[0].r = stream;
    a[1].r = NULL;
    result.r = NULL;
    {
        nj_status rc = nj_call_static(vm, MANAGER, "createPlayer",
            "(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player;",
            a, &result);
        *out = result.r;
        return rc;
    }
}

static nj_s4 field(nj_vm *vm, nj_ref object, const char *name)
{
    const nj_field *f = nj_field_lookup(vm, PLAYER, name, "I", 0);
    CHECK(f != NULL);
    return nj_field_read_int(vm, object, f);
}

static void audio_metadata(void)
{
    /* Valid ADTS signature; the hook owns actual format validation. */
    static const unsigned char adts[] = {255,241,80,128,1,63,252,0,0};
    int mode;
    for (mode = 0; mode <= 8; ++mode) {
        nj_port port;
        audio_probe probe = {0,0,0};
        nj_vm *vm = new_vm(&port, &probe);
        nj_ref p = NULL;
        nj_status rc;
        CHECK(vm != NULL);
        probe.mode = mode;
        rc = player(vm, adts, sizeof(adts), &p);
        if (mode == 0) {
            nj_ref type;
            CHECK(rc == NJ_OK && p != NULL);
            CHECK(field(vm, p, "pKind") == 1);
            type = nj_call_ref(vm, p, PLAYER, "getContentType", "()Ljava/lang/String;", NULL);
            CHECK(type != NULL && nj_string_equals_utf8(type, "audio/aac"));
        } else {
            CHECK(rc != NJ_OK && nj_has_exception(vm));
            nj_clear_exception(vm);
        }
        CHECK(probe.queries == 1);
        CHECK(probe.decodes == (mode >= 1 && mode <= 3 ? 0 : 1));
        nj_vm_destroy(vm);
        nj_port_shutdown(&port);
    }
}

static void movie_not_audio(void)
{
    nj_port port;
    audio_probe probe = {0,0,0};
    nj_vm *vm = new_vm(&port, &probe);
    nj_ref p = NULL;
    CHECK(vm != NULL);
    CHECK(player(vm, clip_3gp, sizeof(clip_3gp), &p) == NJ_OK && p != NULL);
    CHECK(field(vm, p, "pKind") == 2);
    CHECK(probe.queries == 0 && probe.decodes == 0);
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
}

#include "private/media_timing_fixture.h"
#define FRAME_BYTES (128u*96u*2u)
static nj_u1 reference[4][FRAME_BYTES];
static nj_u4 fake_clock;
static nj_u4 clock_ms(void *ud) { (void)ud; return fake_clock; }
static nj_vm *movie_vm(nj_port *port)
{
    CHECK(nj_port_headless(port)==NJ_OK);
    fake_clock=10000; port->millis=clock_ms; port->fps_cap=30;
    return nj_vm_create(port,2u*1024u*1024u);
}
static void call0(nj_vm *vm,nj_ref p,const char *method)
{ CHECK(nj_call(vm,p,PLAYER,method,"()V",NULL,NULL)==NJ_OK); }
static nj_s8 getj(nj_vm *vm,nj_ref p,const char *method)
{
    nj_value r; r.j=-9;
    CHECK(nj_call(vm,p,PLAYER,method,"()J",NULL,&r)==NJ_OK); return r.j;
}
static nj_s8 seek_us(nj_vm *vm,nj_ref p,nj_s8 t)
{
    nj_value a,r; a.j=t; r.j=-9;
    CHECK(nj_call(vm,p,PLAYER,"setMediaTime","(J)J",&a,&r)==NJ_OK); return r.j;
}
static nj_ref frame(nj_vm *vm,nj_ref p)
{
    const nj_field *f=nj_field_lookup(vm,PLAYER,"pVbuf","[B",0);
    nj_ref b;
    CHECK(f!=NULL); b=nj_field_read_ref(vm,p,f);
    CHECK(b!=NULL && b->length==FRAME_BYTES); return b;
}
static void expect_frame(nj_vm *vm,nj_ref p,int index)
{
    static const nj_u1 empty[FRAME_BYTES]={0};
    nj_ref b=frame(vm,p);
    CHECK(!memcmp(b->u.bytes,index<0?empty:reference[index],FRAME_BYTES));
}
static void elapsed(nj_vm *vm,nj_ref p,nj_s8 ms)
{
    const nj_field *f=nj_field_lookup(vm,PLAYER,"pVstart","J",0);
    CHECK(f!=NULL); nj_field_write_long(vm,p,f,nj_now_ms(vm)-ms);
}
static void pump_at(nj_vm *vm,nj_ref p,nj_s8 ms)
{ elapsed(vm,p,ms); nj_media_frame(vm); }
static void close_movie(nj_vm *vm,nj_port *port,nj_ref p)
{ call0(vm,p,"close"); nj_vm_destroy(vm); nj_port_shutdown(port); }
static void reference_frames(void)
{
    nj_port port; nj_vm *vm=movie_vm(&port); nj_ref p=NULL; int i;
    CHECK(vm!=NULL); port.fps_cap=0;
    CHECK(player(vm,clip_3gp,sizeof(clip_3gp),&p)==NJ_OK && p!=NULL);
    call0(vm,p,"start");
    for(i=0;i<4;i++) { nj_ref b; nj_media_frame(vm); b=frame(vm,p); memcpy(reference[i],b->u.bytes,FRAME_BYTES); }
    for(i=1;i<4;i++) CHECK(memcmp(reference[i-1],reference[i],FRAME_BYTES)!=0);
    close_movie(vm,&port,p);
}
static nj_ref open_movie(nj_vm *vm,const timing_movie *f)
{
    nj_ref p=NULL;
    CHECK(player(vm,f->bytes,f->n,&p)==NJ_OK && p!=NULL);
    CHECK(field(vm,p,"pKind")==2 && field(vm,p,"pFrames")==4);
    return p;
}
static void vfr_spacing_and_duration(void)
{
    timing_movie f; nj_port port; nj_vm *vm=movie_vm(&port); nj_ref p;
    timing_fixture(&f,0,-1,0,0); p=open_movie(vm,&f);
    CHECK(getj(vm,p,"getDuration")==650000);
    call0(vm,p,"start"); pump_at(vm,p,0); expect_frame(vm,p,0);
    pump_at(vm,p,99); expect_frame(vm,p,0);
    pump_at(vm,p,100); expect_frame(vm,p,1);
    pump_at(vm,p,399); expect_frame(vm,p,1);
    pump_at(vm,p,400); expect_frame(vm,p,2);
    pump_at(vm,p,449); expect_frame(vm,p,2);
    pump_at(vm,p,450); expect_frame(vm,p,3);
    pump_at(vm,p,649); CHECK(field(vm,p,"pState")==400); expect_frame(vm,p,3);
    pump_at(vm,p,650); CHECK(field(vm,p,"pState")==300);
    CHECK(getj(vm,p,"getMediaTime")==650000); expect_frame(vm,p,3);
    close_movie(vm,&port,p);
}
static void seek_keeps_target(void)
{
    timing_movie f; nj_port port; nj_vm *vm=movie_vm(&port); nj_ref p;
    timing_fixture(&f,0,-1,0,0); p=open_movie(vm,&f);
    CHECK(seek_us(vm,p,400000)==400000);
    call0(vm,p,"start"); pump_at(vm,p,400); expect_frame(vm,p,2);
    pump_at(vm,p,449); expect_frame(vm,p,2);
    pump_at(vm,p,450); expect_frame(vm,p,3);
    CHECK(seek_us(vm,p,100000)==100000);
    pump_at(vm,p,100); expect_frame(vm,p,1); /* Target P picture wasn't consumed by seek. */
    pump_at(vm,p,399); expect_frame(vm,p,1);
    CHECK(seek_us(vm,p,0)==0); pump_at(vm,p,0); expect_frame(vm,p,0);
    close_movie(vm,&port,p);
}
static void leading_gap_and_reseek(void)
{
    timing_movie f; nj_port port; nj_vm *vm=movie_vm(&port); nj_ref p;
    timing_fixture(&f,200,0,650,0); p=open_movie(vm,&f);
    CHECK(getj(vm,p,"getDuration")==850000);
    call0(vm,p,"start"); pump_at(vm,p,0); expect_frame(vm,p,-1);
    pump_at(vm,p,199); expect_frame(vm,p,-1);
    pump_at(vm,p,200); expect_frame(vm,p,0);
    pump_at(vm,p,300); expect_frame(vm,p,1);
    CHECK(seek_us(vm,p,0)==0); expect_frame(vm,p,-1); /* Clear the old visible picture. */
    pump_at(vm,p,199); expect_frame(vm,p,-1);
    pump_at(vm,p,200); expect_frame(vm,p,0);
    CHECK(seek_us(vm,p,650000)==650000); pump_at(vm,p,650); expect_frame(vm,p,3);
    pump_at(vm,p,849); CHECK(field(vm,p,"pState")==400); expect_frame(vm,p,3);
    pump_at(vm,p,850); CHECK(field(vm,p,"pState")==300);
    close_movie(vm,&port,p);
}
static void nonaligned_window_and_preroll(void)
{
    timing_movie f; nj_port port; nj_vm *vm=movie_vm(&port); nj_ref p;
    timing_fixture(&f,0,50,500,0); p=open_movie(vm,&f);
    CHECK(getj(vm,p,"getDuration")==500000);
    call0(vm,p,"start"); pump_at(vm,p,0); expect_frame(vm,p,0); /* [-50,50) overlaps start. */
    pump_at(vm,p,49); expect_frame(vm,p,0);
    pump_at(vm,p,50); expect_frame(vm,p,1);
    pump_at(vm,p,349); expect_frame(vm,p,1);
    pump_at(vm,p,350); expect_frame(vm,p,2);
    pump_at(vm,p,400); expect_frame(vm,p,3);
    pump_at(vm,p,499); CHECK(field(vm,p,"pState")==400); expect_frame(vm,p,3);
    pump_at(vm,p,500); CHECK(field(vm,p,"pState")==300); /* Clip tail of the last frame. */
    close_movie(vm,&port,p);
    vm=movie_vm(&port); timing_fixture(&f,0,150,400,0); p=open_movie(vm,&f);
    call0(vm,p,"start"); pump_at(vm,p,0); expect_frame(vm,p,-1); /* First reference frame is entirely preroll. */
    pump_at(vm,p,0); expect_frame(vm,p,1); /* Decode its dependent P picture, which overlaps start. */
    pump_at(vm,p,249); expect_frame(vm,p,1);
    pump_at(vm,p,250); expect_frame(vm,p,2);
    close_movie(vm,&port,p);
}
static void pause_resume_pixels(void)
{
    timing_movie f; nj_port port; nj_vm *vm=movie_vm(&port); nj_ref p;
    timing_fixture(&f,0,-1,0,0); p=open_movie(vm,&f);
    call0(vm,p,"start"); pump_at(vm,p,0); pump_at(vm,p,100); expect_frame(vm,p,1);
    call0(vm,p,"stop"); CHECK(field(vm,p,"pState")==300);
    fake_clock+=5000; nj_media_frame(vm); expect_frame(vm,p,1);
    call0(vm,p,"start"); nj_media_frame(vm); expect_frame(vm,p,2); /* Resume presents next pending picture. */
    nj_media_frame(vm); expect_frame(vm,p,2); /* Next sample's 50 ms hold remains. */
    fake_clock+=49; nj_media_frame(vm); expect_frame(vm,p,2);
    fake_clock+=1; nj_media_frame(vm); expect_frame(vm,p,3);
    close_movie(vm,&port,p);
}
static void reject_reordered_presentation(void)
{
    timing_movie f; nj_port port; nj_vm *vm=movie_vm(&port); nj_ref p=NULL;
    nj_mp4 m; nj_mp4_sample s; nj_s8 pts[4]; int i;
    timing_fixture(&f,0,-1,0,1);
    CHECK(nj_mp4_open(&m,f.bytes,f.n)==NJ_MP4_OK);
    for(i=0;i<4;i++) { CHECK(nj_mp4_next(&m,&s)==1); pts[i]=s.pts; }
    CHECK(pts[0]==0 && pts[1]==600 && pts[2]==400 && pts[3]==450);
    CHECK(player(vm,f.bytes,f.n,&p)!=NJ_OK && nj_has_exception(vm));
    nj_clear_exception(vm); nj_vm_destroy(vm); nj_port_shutdown(&port);
}
int main(void)
{
    audio_metadata();
    movie_not_audio();
    reference_frames();
    vfr_spacing_and_duration(); seek_keeps_target(); leading_gap_and_reseek();
    nonaligned_window_and_preroll(); pause_resume_pixels(); reject_reordered_presentation();
    printf("test_media_timing: %u checks passed\n", checks);
    return 0;
}
#else
#include <stdio.h>
int main(void)
{
    puts("SKIP test_media_timing: requires MEDIA, VIDEO and H263");
    return 0;
}
#endif
