/* Reuse public-API geometry/state fixtures; compare every color/depth byte
 * against the serial renderer with a genuinely concurrent second CPU. */
#define main m3g_targets_fixture_main
#include "test_m3g_targets.c"
#undef main
#include <pthread.h>

typedef struct { void (*work)(void *, nj_u4); void *context; } fork_job;
static unsigned fork_count;
static void *other_half(void *pointer)
{
    fork_job *job = pointer;
    job->work(job->context, 1u);
    return NULL;
}
static void fork_join(void *ud, void (*work)(void *, nj_u4), void *context, nj_u4 count)
{
    pthread_t thread;
    fork_job job = {work, context};
    (void)ud;
    CHECK(count == 2u);
    CHECK(pthread_create(&thread, NULL, other_half, &job) == 0);
    work(context, 0u);
    CHECK(pthread_join(thread, NULL) == 0);
    ++fork_count;
}
static void run_cases(FILE *file)
{
    capture = file;
    cases(NJ_PIXFMT_RGB565);
    cases(NJ_PIXFMT_RGB888);
    cases(NJ_PIXFMT_ARGB8888);
    CHECK(fflush(file) == 0);
    rewind(file);
}
static void feedback_is_serial(void)
{
    target t;
    nj_value a[5];
    nj_ref image, pixels, texture_object, backing;
    init(&t, NJ_PIXFMT_ARGB8888);
    image = nj_root(t.vm, nj_object_new(t.vm, C("Image2D")));
    a[0].i=100; a[1].i=W; a[2].i=H;
    CHECK(nj_call_special(t.vm, image, C("Image2D"), "<init>", "(III)V", a, NULL)==NJ_OK);
    pixels = nj_root(t.vm, nj_array_new(t.vm, "B", W*H*4u));
    CHECK(pixels);
    memset(nj_array_data(pixels), 0x7f, W*H*4u);
    a[0].i=0; a[1].i=0; a[2].i=W; a[3].i=H; a[4].r=pixels;
    call(&t, image, C("Image2D"), "set", "(IIII[B)V", a);
    texture_object = nj_root(t.vm, nj_object_new(t.vm, C("Texture2D")));
    a[0].r=image;
    CHECK(nj_call_special(t.vm, texture_object, C("Texture2D"), "<init>",
                         "(L" C("Image2D") ";)V", a, NULL)==NJ_OK);
    a[0].i=0; a[1].r=texture_object;
    call(&t, t.app, C("Appearance"), "setTexture", "(IL" C("Texture2D") ";)V", a);
    backing = nj_rget(t.vm, image, nj_field_lookup(t.vm,C("Image2D"),"_nj_pixels","[B",0));
    CHECK(backing);
    t.surface.pixels=nj_array_data(backing); t.surface.stride=W*4;
    t.vm->screen=t.surface;
    unsigned before=fork_count;
    render(&t);
    CHECK(fork_count==before);
    destroy(&t);
}
int main(void)
{
    FILE *serial = tmpfile(), *concurrent = tmpfile();
    unsigned char a[8192], b[8192];
    size_t na, nb, total = 0;
    CHECK(serial && concurrent);
    run_cases(serial);
    test_parallel_for = fork_join;
    run_cases(concurrent);
    do {
        na = fread(a, 1, sizeof a, serial);
        nb = fread(b, 1, sizeof b, concurrent);
        CHECK(na == nb && memcmp(a, b, na) == 0);
        total += na;
    } while (na);
    CHECK(fork_count > 0);
    feedback_is_serial();
    CHECK(fclose(serial) == 0 && fclose(concurrent) == 0);
    capture = NULL;
    printf("PASS M3G concurrent equivalence: %u forks, %zu color/depth bytes\n",
           fork_count, total);
    return 0;
}
