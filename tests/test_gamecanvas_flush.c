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

/* A hidden GameCanvas may still paint into another off-screen Graphics, but
 * neither flush overload may publish its pixels or claim the visible frame. */
static void hidden_flush_case(int rect)
{
    fixture f;
    nj_ref hidden, visible, graphics;
    nj_value a[4], ret;
    int i;
    if (!begin(&f, rect ? "hidden rectangular flush is a no-op" :
                         "hidden full flush is a no-op")) return;
    hidden=canvas(1);
    ret.r=NULL;
    CHECK(call(hidden,NJ_C_GAMECANVAS,"getGraphics","()" D_GRAPHICS,NULL,&ret));
    graphics=root(ret.r);
    fill(graphics,BLUE);
    visible=canvas(1);
    ret.r=NULL;
    CHECK(call(visible,NJ_C_GAMECANVAS,"getGraphics","()" D_GRAPHICS,NULL,&ret));
    graphics=root(ret.r);
    fill(graphics,GREEN); frame(); frame();
    CHECK(f.presented_pixel==screen_color(GREEN));
    CHECK((f.vm->app_painted & 2u)==0u); /* Read state; never manufacture it. */
    if (rect) {
        a[0].i=0; a[1].i=0; a[2].i=16; a[3].i=16;
        CHECK(call(hidden,NJ_C_GAMECANVAS,"flushGraphics","(IIII)V",a,NULL));
    } else {
        flush(hidden);
    }
    CHECK(nj_surface_get(nj_midp_screen(f.vm),8,8)==screen_color(GREEN));
    CHECK((f.vm->app_painted & 2u)==0u);
    frame();
    CHECK(f.presented_pixel==screen_color(GREEN));
    fill_top(graphics);
    for(i=0;i<310;i++) frame();
    CHECK(f.presented_pixel==screen_color(0xFFFF0000u)); /* B still owns its fallback. */
    /* paint(Graphics) is intentionally different from flushGraphics: an
     * application can copy a hidden canvas into an arbitrary off-screen image. */
    a[0].r=graphics;
    CHECK(call(hidden,NJ_C_GAMECANVAS,"paint","(" D_GRAPHICS ")V",a,NULL));
    CHECK(nj_surface_get(nj_midp_screen(f.vm),8,8)==screen_color(0xFFFF0000u));
    for(i=0;i<310;i++) frame();
    CHECK(f.presented_pixel==screen_color(BLUE));
    CHECK((f.vm->app_painted & 2u)==0u);
    end(&f);
}

/* nj_vm_load requires an attached class source even for an all-builtin graph.
 * begin() registers the same two native fixture classes in every VM; no class
 * file or custom instance is needed for these GameCanvas-only snapshots. */
static nj_status no_class_bytes(nj_vm *vm, void *ud, const char *name,
                                nj_u1 **bytes, nj_u4 *length)
{
    (void)vm; (void)ud; (void)name;
    *bytes=NULL; *length=0u;
    return NJ_ERR;
}

static nj_class_source boundary_source={no_class_bytes,NULL};

static int snapshot_begin(fixture *f, const char *name)
{
    if (!begin(f,name)) return 0;
    f->vm->source=&boundary_source;
    return 1;
}

/* Never carry a Java object pointer across a successful heap replacement.
 * The fixture's display pointer is a C observer, not a GC root. */
static nj_ref refreshed_current(fixture *f)
{
    f->display=nj_midp_display(f->vm);
    f->form=NULL; f->from_paint=NULL; f->graphics=NULL;
    CHECK(f->display!=NULL);
    CHECK(nj_midp_current(f->vm)!=NULL);
    return nj_midp_current(f->vm);
}

static FILE *legacy_snapshot(void)
{
    fixture f;
    nj_ref c;
    nj_value ret;
    FILE *saved=NULL;
    if (!snapshot_begin(&f,"save a non-flushing GameCanvas")) return NULL;
    c=canvas(1);
    ret.r=NULL;
    CHECK(call(c,NJ_C_GAMECANVAS,"getGraphics","()" D_GRAPHICS,NULL,&ret));
    fill(ret.r,GREEN); frame(); frame();
    CHECK(f.presented_pixel==screen_color(GREEN));
    CHECK(f.vm->repaint_pending==0u && f.vm->painted_in_frame==0u);
    CHECK((f.vm->app_painted & 2u)==0u);
    nj_root_release(f.vm,0u);
    CHECK(f.vm->nroots==0u);
    (void)nj_gc_collect(f.vm); /* Only Display.current retains the real graph. */
    CHECK(nj_vm_can_save(f.vm));
    saved=tmpfile();
    CHECK(saved!=NULL);
    if (saved) {
        int st=nj_vm_save(f.vm,saved);
        CHECK(st==NJ_OK);
        CHECK(ftell(saved)>0L);
        if (st!=NJ_OK) { fclose(saved); saved=NULL; }
    }
    end(&f);
    return saved;
}

static void loaded_fallback(fixture *f)
{
    nj_ref c=refreshed_current(f);
    nj_value ret;
    int i;
    frame(); frame();
    CHECK(f->presented_pixel==screen_color(GREEN));
    ret.r=NULL;
    CHECK(call(c,NJ_C_GAMECANVAS,"getGraphics","()" D_GRAPHICS,NULL,&ret));
    fill_top(ret.r); /* No flush/repaint: the loaded title uses legacy paint. */
    for(i=0;i<310;i++) frame();
    CHECK(f->presented_pixel==screen_color(0xFFFF0000u));
    CHECK((f->vm->app_painted & 2u)==0u);
}

static void snapshot_load_case(FILE *saved, int live)
{
    fixture f;
    nj_ref c;
    nj_value ret;
    int st;
    if (!snapshot_begin(&f,live ? "live load forgets previous flush ownership" :
                                  "fresh load retains legacy fallback")) return;
    if (live) {
        c=canvas(1); frame(); /* Complete startup repaint before taking ownership. */
        ret.r=NULL;
        CHECK(call(c,NJ_C_GAMECANVAS,"getGraphics","()" D_GRAPHICS,NULL,&ret));
        fill(ret.r,BLUE); flush(c); frame();
        CHECK(f.presented_pixel==screen_color(BLUE));
        CHECK((f.vm->app_painted & 2u)!=0u);
        CHECK(f.vm->repaint_pending==0u && f.vm->painted_in_frame==0u);
    }
    nj_root_release(f.vm,0u);
    CHECK(f.vm->nroots==0u);
    CHECK(fseek(saved,0L,SEEK_SET)==0);
    st=nj_vm_load(f.vm,saved);
    CHECK(st==NJ_OK);
    if (st==NJ_OK) loaded_fallback(&f);
    end(&f);
}

/* Corrupt only an experimental tmpfile copy's trailing CRC. The original
 * public snapshot is used by both successful loads above and is unchanged. */
static FILE *bad_crc_copy(FILE *saved)
{
    unsigned char bytes[4096];
    FILE *bad=tmpfile();
    size_t n;
    int byte, ok=1;
    CHECK(bad!=NULL);
    if (!bad) return NULL;
    CHECK(fseek(saved,0L,SEEK_SET)==0);
    while ((n=fread(bytes,1u,sizeof(bytes),saved))!=0u) {
        if (fwrite(bytes,1u,n,bad)!=n) { ok=0; break; }
    }
    if (ferror(saved) || !ok || fseek(bad,-1L,SEEK_END)!=0) {
        CHECK(0); fclose(bad); return NULL;
    }
    byte=fgetc(bad);
    if (byte==EOF || fseek(bad,-1L,SEEK_END)!=0 ||
        fputc(byte^1,bad)==EOF || fflush(bad)!=0) {
        CHECK(0); fclose(bad); return NULL;
    }
    return bad;
}

static void refused_load_case(FILE *bad)
{
    fixture f;
    nj_ref c;
    nj_value ret;
    int i;
    if (!snapshot_begin(&f,"refused load preserves the live completed frame")) return;
    c=canvas(1); frame(); /* Do not leave a legitimate startup repaint pending. */
    ret.r=NULL;
    CHECK(call(c,NJ_C_GAMECANVAS,"getGraphics","()" D_GRAPHICS,NULL,&ret));
    fill(ret.r,BLUE); flush(c); frame();
    fill_top(ret.r); /* Incomplete next image must remain off-screen. */
    CHECK(f.presented_pixel==screen_color(BLUE));
    CHECK((f.vm->app_painted & 2u)!=0u);
    nj_root_release(f.vm,0u);
    CHECK(f.vm->nroots==0u);
    CHECK(fseek(bad,0L,SEEK_SET)==0);
    CHECK(nj_vm_load(f.vm,bad)==NJ_ERR_FORMAT);
    CHECK(nj_midp_current(f.vm)==c); /* Early refusal did not install a new graph. */
    CHECK((f.vm->app_painted & 2u)!=0u);
    for(i=0;i<310;i++) frame();
    CHECK(f.presented_pixel==screen_color(BLUE));
    CHECK(nj_surface_get(nj_midp_screen(f.vm),8,8)==screen_color(BLUE));
    end(&f);
}

static void snapshot_boundaries(void)
{
    FILE *saved=legacy_snapshot(), *bad;
    if (!saved) return;
    snapshot_load_case(saved,0);
    snapshot_load_case(saved,1);
    bad=bad_crc_copy(saved);
    if (bad) { refused_load_case(bad); fclose(bad); }
    fclose(saved);
}

int main(void)
{
    int i;
    for(i=0;i<3;i++) run_case();
    hidden_flush_case(0);
    hidden_flush_case(1);
    snapshot_boundaries();
    printf("GameCanvas flush ownership: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
