/* Reuse the public Display/Graphics fixture and its presentation observer.
 * A slow producer may take hundreds of VM frames to finish its next image. */
#define main display_serially_fixture_main
#include "test_display_serially.c"
#undef main

static void fill_top(nj_ref graphics)
{
    nj_value a[4];
    nj_surface *s = nj_midp_screen(active->vm);
    a[0].i = 0xff0000;
    (void)call(graphics, NJ_C_GRAPHICS, "setColor", "(I)V", a, NULL);
    a[0].i=0; a[1].i=0; a[2].i=s->width; a[3].i=s->height/2;
    (void)call(graphics, NJ_C_GRAPHICS, "fillRect", "(IIII)V", a, NULL);
}

static void run_case(void)
{
    fixture f;
    nj_ref c, g, other;
    nj_value ret;
    int i;
    if (!begin(&f, "slow GameCanvas producer keeps its last flushed frame")) return;
    c=canvas(1); frame();
    ret.r=NULL;
    CHECK(call(c,NJ_C_GAMECANVAS,"getGraphics","()" D_GRAPHICS,NULL,&ret));
    g=root(ret.r);
    fill(g,BLUE); flush(c); frame();
    fill_top(g); /* Next image is deliberately incomplete. */
    CHECK(nj_midp_key(f.vm,NJ_KEY_RIGHT,1)); frame();
    CHECK(f.presented_pixel==screen_color(BLUE));
    ret.i=0;
    CHECK(call(c,NJ_C_GAMECANVAS,"getKeyStates","()I",NULL,&ret));
    CHECK((ret.i & 0x20)!=0); /* Input is delivered without publishing pixels. */
    CHECK(nj_midp_key(f.vm,NJ_KEY_RIGHT,0)); frame();
    CHECK(f.presented_pixel==screen_color(BLUE));
    CHECK(nj_midp_pointer(f.vm,8,8,NJ_POINTER_DOWN)); frame();
    CHECK(f.presented_pixel==screen_color(BLUE));
    CHECK(nj_midp_pointer(f.vm,10,8,NJ_POINTER_MOVE)); frame();
    CHECK(f.presented_pixel==screen_color(BLUE));
    CHECK(nj_midp_pointer(f.vm,10,8,NJ_POINTER_UP)); frame();
    CHECK(f.presented_pixel==screen_color(BLUE));
    for (i=0;i<310;i++) {
        frame();
        if (f.presented_pixel != screen_color(BLUE)) break;
    }
    CHECK(i==310); /* Also crosses the old 8-bit idle/cadence saturation. */
    CHECK(nj_surface_get(nj_midp_screen(f.vm),8,8)==screen_color(BLUE));
    fill(g,GREEN); flush(c); frame();
    CHECK(f.presented_pixel==screen_color(GREEN));
    /* Explicit repaint is still an instruction to publish the buffer. */
    fill(g,BLUE);
    CHECK(call(c,NJ_C_CANVAS,"repaint","()V",NULL,NULL)); frame();
    CHECK(f.presented_pixel==screen_color(BLUE));
    fill(g,GREEN);
    CHECK(call(c,NJ_C_CANVAS,"repaint","()V",NULL,NULL));
    CHECK(call(c,NJ_C_CANVAS,"serviceRepaints","()V",NULL,NULL)); frame();
    CHECK(f.presented_pixel==screen_color(GREEN));
    fill_top(g);
    for(i=0;i<310;i++) frame();
    CHECK(f.presented_pixel==screen_color(GREEN)); /* Ownership survives serviceRepaints. */
    fill(g,GREEN);
    /* A new displayable must not inherit the old GameCanvas's ownership. */
    other=canvas(1);
    ret.r=NULL;
    CHECK(call(other,NJ_C_GAMECANVAS,"getGraphics","()" D_GRAPHICS,NULL,&ret));
    fill(ret.r,BLUE); frame();
    CHECK(f.presented_pixel==screen_color(BLUE));
    /* Older non-flushing GameCanvas titles retain their adaptive paint. */
    g=root(ret.r);
    CHECK(call(other,NJ_C_CANVAS,"repaint","()V",NULL,NULL));
    CHECK(call(other,NJ_C_CANVAS,"serviceRepaints","()V",NULL,NULL)); frame();
    fill(g,GREEN);
    for(i=0;i<310;i++) frame();
    CHECK(f.presented_pixel==screen_color(GREEN));
    other=canvas(0); frame();
    CHECK(f.presented_pixel==screen_color(GREEN));
    CHECK(call(other,NJ_C_CANVAS,"repaint","()V",NULL,NULL));
    CHECK(call(other,NJ_C_CANVAS,"serviceRepaints","()V",NULL,NULL)); frame();
    f.paint_color=BLUE;
    for(i=0;i<310;i++) frame();
    CHECK(f.presented_pixel==screen_color(BLUE)); /* Legacy Canvas fallback. */
    set_current(c); frame();
    CHECK(f.presented_pixel==screen_color(GREEN));
    end(&f);
}

int main(void)
{
    int i;
    for(i=0;i<3;i++) run_case();
    printf("GameCanvas flush ownership: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
