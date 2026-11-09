/* Snapshot v3 depth-semantics tag: old ordinary Java states still load,
 * ambiguous legacy Graphics3D states fail before touching the target heap. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEAP_BYTES (2u * 1024u * 1024u)
#define HOLDER "test/DepthSnapshotRoots"
#define SUBCLASS "test/DepthSnapshotGraphics"
#define C(x) "javax/microedition/m3g/" x
#define IMAGE "javax/microedition/lcdui/Image"
static int checks, failures;
static const nj_builtin_field holder_fields[] = {
    {"state", "[Ljava/lang/Object;", NJ_ACC_PUBLIC | NJ_ACC_STATIC},
    {"marker", "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC}
};
static const nj_builtin_class holder_class = {
    HOLDER, "java/lang/Object", NJ_ACC_PUBLIC, holder_fields, 2, NULL, 0
};
#if NJ_ENABLE_M3G
static const nj_builtin_class subclass = {
    SUBCLASS, C("Graphics3D"), NJ_ACC_PUBLIC, NULL, 0, NULL, 0
};
#endif
static void check(int ok, const char *why)
{
    ++checks;
    if (!ok) { ++failures; printf("FAIL: %s\n", why); }
}
static void require(int ok, const char *why)
{
    if (!ok) { fprintf(stderr, "fixture failure: %s\n", why); exit(2); }
}
static nj_field *field(nj_vm *vm, const char *name, const char *desc)
{
    nj_field *f = nj_field_lookup(vm, HOLDER, name, desc, 1);
    require(f != NULL, "holder field exists");
    return f;
}
static nj_ref state(nj_vm *vm)
{
    return nj_field_read_ref(vm, NULL, field(vm, "state", "[Ljava/lang/Object;"));
}
static void set_state(nj_vm *vm, nj_ref refs)
{
    nj_field_write_ref(vm, NULL, field(vm, "state", "[Ljava/lang/Object;"), refs);
}
static nj_ref seed_state(nj_vm *vm, int n)
{
    nj_ref refs = nj_array_new(vm, "Ljava/lang/Object;", (nj_size)n);
    require(refs != NULL, "root array allocates");
    set_state(vm, refs);
    nj_field_write_int(vm, NULL, field(vm, "marker", "I"), 12345);
    return refs;
}
static nj_vm *create(const nj_port *port, const nj_u1 *jar, nj_u4 len, nj_app **app)
{
    nj_vm *vm = nj_vm_create(port, HEAP_BYTES);
    require(vm != NULL, "VM allocates");
    *app = nj_app_open(vm, jar, len);
    require(*app != NULL, "fixture JAR attaches");
    require(nj_class_create_builtin(vm, &holder_class) != NULL, "holder registers");
#if NJ_ENABLE_M3G
    require(nj_class_create_builtin(vm, &subclass) != NULL, "subclass registers");
#endif
    require(nj_class_link(vm, nj_class_find(vm, HOLDER)) == NJ_OK, "holder links");
    return vm;
}
static nj_u4 read_u32(const unsigned char *b)
{
    return (nj_u4)b[0] | ((nj_u4)b[1] << 8) | ((nj_u4)b[2] << 16) | ((nj_u4)b[3] << 24);
}
static nj_u4 header(FILE *f, long off)
{
    unsigned char b[4];
    require(fseek(f, off, SEEK_SET) == 0 && fread(b, 1, 4, f) == 4, "read header");
    return read_u32(b);
}
static void put_header(FILE *f, long off, nj_u4 value)
{
    unsigned char b[4]; int i;
    for (i = 0; i < 4; ++i) b[i] = (unsigned char)(value >> (i * 8));
    require(fseek(f, off, SEEK_SET) == 0 && fwrite(b, 1, 4, f) == 4, "write header");
    rewind(f);
}
static FILE *save(nj_vm *vm)
{
    FILE *f = tmpfile();
    require(f != NULL, "temporary snapshot stream opens");
    check(nj_vm_save(vm, f) == NJ_OK, "snapshot saves without active native roots");
    check(header(f, 4) == 3, "depth semantics retain format version 3");
    check((header(f, 20) & 2u) != 0, "writer tags window-depth semantics");
    rewind(f);
    return f;
}
static nj_u4 file_hash(FILE *f)
{
    unsigned char b[512]; size_t n, i; nj_u4 h = 2166136261u;
    rewind(f);
    while ((n = fread(b, 1, sizeof(b), f)) != 0) {
        for (i = 0; i < n; ++i) h = (h ^ b[i]) * 16777619u;
    }
    require(!ferror(f), "hash snapshot"); rewind(f); return h;
}
static void start_fresh(nj_vm *vm)
{
    nj_ref exc = NULL, midlet = nj_vm_new_midlet(vm, "FieldDemo", &exc);
    int frames;
    check(midlet != NULL && exc == NULL, "refused/ordinary restore can construct fresh MIDlet");
    if (!midlet) return;
    check(nj_vm_start_app(vm, midlet) == NJ_OK, "fresh MIDlet startApp succeeds");
    for (frames = 0; !nj_midp_current(vm) && frames < 90; ++frames) (void)nj_midp_frame(vm);
    check(nj_midp_current(vm) != NULL && vm->exception == NULL,
          "fresh MIDlet reaches its display after snapshot decision");
}
static void ordinary(const nj_port *port, const nj_u1 *jar, nj_u4 len, int motorola)
{
    nj_app *sa, *ta; nj_vm *s = create(port, jar, len, &sa), *t;
    nj_ref roots = seed_state(s, 2);
    FILE *f;
#if NJ_ENABLE_M3G
    /* A registered/linked Graphics3D class, its Class mirror and a null-filled
     * array are all harmless; none is a Graphics3D instance. */
    roots->u.refs[0] = nj_class_object(s, C("Graphics3D"));
    roots->u.refs[1] = nj_array_new(s, "L" C("Graphics3D") ";", 1);
    require(roots->u.refs[0] && roots->u.refs[1], "mirror and Graphics3D array allocate");
#else
    (void)roots;
#endif
    nj_vm_set_device(s, motorola ? nj_device_find("motorola-e398") : NULL);
    f = save(s);
    check(header(f, 20) == (nj_u4)(motorola | 2), "tag preserves Motorola flag");
    put_header(f, 20, (nj_u4)motorola); /* Header is outside the body CRC. */
    t = create(port, jar, len, &ta);
    nj_vm_set_device(t, motorola ? NULL : nj_device_find("motorola-e398"));
    check(nj_vm_load(t, f) == NJ_OK, "untagged snapshot without Graphics3D instance loads");
    check(t->motorola_softkeys == motorola, "old flag 0/1 restores soft-key convention");
    roots = state(t);
    check(roots && roots->kind == NJ_OBJ_ARRAY && roots->length == 2,
          "legacy ordinary static reference is restored");
#if NJ_ENABLE_M3G
    if (roots) {
        check(roots->u.refs[0] && roots->u.refs[0]->kind == NJ_OBJ_CLASS,
              "legacy Graphics3D Class mirror remains valid");
        check(roots->u.refs[1] && roots->u.refs[1]->kind == NJ_OBJ_ARRAY &&
              roots->u.refs[1]->u.refs[0] == NULL, "legacy Graphics3D array remains valid");
    }
#endif
    start_fresh(t);
    fclose(f); nj_app_close(t, ta); nj_vm_destroy(t); nj_app_close(s, sa); nj_vm_destroy(s);
}

#if NJ_ENABLE_M3G
static void call(nj_vm *vm, nj_ref obj, const char *cls, const char *name,
                 const char *desc, nj_value *args, nj_value *ret)
{
    if (nj_call(vm, obj, cls, name, desc, args, ret) != NJ_OK) {
        fprintf(stderr, "fixture call failed: %s.%s%s: %s\n", cls, name, desc,
                nj_exception_describe(vm)); exit(2);
    }
}
static nj_ref make(nj_vm *vm, const char *cls)
{
    nj_ref o = nj_object_new(vm, cls);
    require(o != NULL, "M3G object allocates"); nj_root(vm, o);
    require(nj_call_special(vm, o, cls, "<init>", "()V", NULL, NULL) == NJ_OK, "M3G constructor");
    return o;
}
typedef struct scene { nj_ref g3, image, vb, ib, app, data; } scene;
static void draw(nj_vm *vm, scene *s, int distance, nj_u4 colour)
{
    nj_s2 p[9] = {-2*distance,-2*distance,-distance, 2*distance,-2*distance,-distance,
                  0,2*distance,-distance};
    nj_value a[5];
    memcpy(nj_array_data(s->data), p, sizeof(p)); a[0].i = (nj_s4)colour;
    call(vm,s->vb,C("VertexBuffer"),"setDefaultColor","(I)V",a,NULL);
    a[0].r=s->vb; a[1].r=s->ib; a[2].r=s->app; a[3].r=NULL; a[4].i=0;
    call(vm,s->g3,C("Graphics3D"),"render","(L" C("VertexBuffer") ";L" C("IndexBuffer") ";L" C("Appearance") ";L" C("Transform") ";I)V",a,NULL);
}
static nj_ref zbuf(nj_vm *vm, scene *s)
{ return nj_field_read_ref(vm,s->g3,nj_field_lookup(vm,C("Graphics3D"),"_nj_zbuf","[I",0)); }
static nj_u4 pixel(nj_vm *vm, scene *s)
{
    nj_ref pixels = nj_field_read_ref(vm,s->image,nj_field_lookup(vm,IMAGE,"imgPixels","[I",0));
    require(pixels != NULL, "image pixels exist");
    return ((nj_u4*)nj_array_data(pixels))[32*64+32] & 0xFFFFFFu;
}
static void init_scene(nj_vm *vm, scene *s)
{
    nj_ref va, lens, cam, poly, cm, refs;
    nj_value a[4], r;
    s->g3=make(vm,C("Graphics3D")); s->vb=make(vm,C("VertexBuffer"));
    s->app=make(vm,C("Appearance")); cam=make(vm,C("Camera"));
    poly=make(vm,C("PolygonMode")); cm=make(vm,C("CompositingMode"));
    va=nj_root(vm,nj_object_new(vm,C("VertexArray")));
    a[0].i=3; a[1].i=3; a[2].i=2;
    require(va && nj_call_special(vm,va,C("VertexArray"),"<init>","(III)V",a,NULL)==NJ_OK,"vertex array initializes");
    s->data=nj_root(vm,nj_array_new(vm,"S",9)); a[0].i=0; a[1].i=3; a[2].r=s->data;
    call(vm,va,C("VertexArray"),"set","(II[S)V",a,NULL);
    s->data=nj_field_read_ref(vm,va,nj_field_lookup(vm,C("VertexArray"),"_nj_data","[S",0));
    a[0].r=va; a[1].f=1; a[2].r=NULL;
    call(vm,s->vb,C("VertexBuffer"),"setPositions","(L" C("VertexArray") ";F[F)V",a,NULL);
    s->ib=nj_root(vm,nj_object_new(vm,C("TriangleStripArray")));
    lens=nj_root(vm,nj_array_new(vm,"I",1)); ((nj_s4*)nj_array_data(lens))[0]=3;
    a[0].i=0; a[1].r=lens;
    require(s->ib && nj_call_special(vm,s->ib,C("TriangleStripArray"),"<init>","(I[I)V",a,NULL)==NJ_OK,"indices initialize");
    a[0].i=162; call(vm,poly,C("PolygonMode"),"setCulling","(I)V",a,NULL);
    a[0].r=poly; call(vm,s->app,C("Appearance"),"setPolygonMode","(L" C("PolygonMode") ";)V",a,NULL);
    a[0].i=68; call(vm,cm,C("CompositingMode"),"setBlending","(I)V",a,NULL);
    a[0].r=cm; call(vm,s->app,C("Appearance"),"setCompositingMode","(L" C("CompositingMode") ";)V",a,NULL);
    a[0].f=90; a[1].f=1; a[2].f=1; a[3].f=10;
    call(vm,cam,C("Camera"),"setPerspective","(FFFF)V",a,NULL);
    a[0].r=cam; a[1].r=NULL;
    call(vm,s->g3,C("Graphics3D"),"setCamera","(L" C("Camera") ";L" C("Transform") ";)V",a,NULL);
    a[0].i=64; a[1].i=64;
    require(nj_call_static(vm,IMAGE,"createImage","(II)Ljavax/microedition/lcdui/Image;",a,&r)==NJ_OK,"image creates");
    s->image=nj_root(vm,r.r);
    call(vm,s->image,IMAGE,"getGraphics","()Ljavax/microedition/lcdui/Graphics;",NULL,&r);
    a[0].r=r.r; call(vm,s->g3,C("Graphics3D"),"bindTarget","(Ljava/lang/Object;)V",a,NULL);
    a[0].f=.2f; a[1].f=.8f; call(vm,s->g3,C("Graphics3D"),"setDepthRange","(FF)V",a,NULL);
    a[0].r=NULL; call(vm,s->g3,C("Graphics3D"),"clear","(L" C("Background") ";)V",a,NULL);
    refs=seed_state(vm,6);
    refs->u.refs[0]=s->g3; refs->u.refs[1]=s->image; refs->u.refs[2]=s->vb;
    refs->u.refs[3]=s->ib; refs->u.refs[4]=s->app; refs->u.refs[5]=s->data;
    nj_root_release(vm,0);
}
static void restore_scene(nj_vm *vm, scene *s)
{
    nj_ref refs=state(vm);
    require(refs && refs->length==6,"restored scene roots exist");
    s->g3=refs->u.refs[0]; s->image=refs->u.refs[1]; s->vb=refs->u.refs[2];
    s->ib=refs->u.refs[3]; s->app=refs->u.refs[4]; s->data=refs->u.refs[5];
}
static void roundtrip(const nj_port *port, const nj_u1 *jar, nj_u4 len)
{
    nj_app *sa, *ta; nj_vm *s=create(port,jar,len,&sa), *t;
    scene before, after; nj_s4 depths[64*64], near_bits, far_bits; FILE *f; nj_value a;
    init_scene(s,&before); draw(s,&before,3,0xFFFF0000u);
    check(pixel(s,&before)==0xFF0000u,"snapshot source really renders foreground");
    require(zbuf(s,&before)!=NULL,"source depth buffer exists");
    memcpy(depths,nj_array_data(zbuf(s,&before)),sizeof(depths));
    near_bits=nj_field_read_int(s,before.g3,nj_field_lookup(s,C("Graphics3D"),"_nj_drnear","F",0));
    far_bits=nj_field_read_int(s,before.g3,nj_field_lookup(s,C("Graphics3D"),"_nj_drfar","F",0));
    f=save(s); t=create(port,jar,len,&ta);
    check(nj_vm_load(t,f)==NJ_OK,"tagged bound Graphics3D snapshot loads");
    restore_scene(t,&after);
    check(pixel(t,&after)==0xFF0000u,"snapshot preserves target image pixels");
    check(zbuf(t,&after) && memcmp(depths,nj_array_data(zbuf(t,&after)),sizeof(depths))==0,
          "snapshot preserves exact active depth keys");
    check(nj_field_read_int(t,after.g3,nj_field_lookup(t,C("Graphics3D"),"_nj_drnear","F",0))==near_bits &&
          nj_field_read_int(t,after.g3,nj_field_lookup(t,C("Graphics3D"),"_nj_drfar","F",0))==far_bits,
          "snapshot preserves explicit depth-range endpoints");
    draw(t,&after,4,0xFF0000FFu);
    check(pixel(t,&after)==0xFF0000u,"first restored far render honors saved occlusion");
    draw(t,&after,2,0xFF00FF00u);
    check(pixel(t,&after)==0x00FF00u,"restored near render passes saved depth");
    a.r=NULL; call(t,after.g3,C("Graphics3D"),"clear","(L" C("Background") ";)V",&a,NULL);
    draw(t,&after,4,0xFF0000FFu);
    check(pixel(t,&after)==0x0000FFu,"explicit clear after restore resets depth");
    fclose(f); nj_app_close(t,ta); nj_vm_destroy(t); nj_app_close(s,sa); nj_vm_destroy(s);
}
#endif

static void refusal(const nj_port *port, const nj_u1 *jar, nj_u4 len, int mode)
{
    nj_app *sa, *ta; nj_vm *s=create(port,jar,len,&sa), *t;
    FILE *f; nj_ref sentinel; nj_u1 *base; nj_size bump; nj_u4 hash;
    nj_thread *thread;
#if NJ_ENABLE_M3G
    if (mode==1) {
        scene sc; init_scene(s,&sc); draw(s,&sc,3,0xFFFF0000u);
    } else if (mode==0 || mode==2) {
        nj_ref refs=seed_state(s,1);
        nj_ref g=nj_object_new(s,mode==2?SUBCLASS:C("Graphics3D"));
        require(g!=NULL,"legacy instance allocates"); refs->u.refs[0]=g;
        require(nj_call_special(s,g,C("Graphics3D"),"<init>","()V",NULL,NULL)==NJ_OK,"legacy constructor");
    } else
#endif
    { seed_state(s,1); }
    f=save(s);
    put_header(f,20,mode==3?4u:0u); /* Unknown flag bit 2 vs old untagged states. */
    if (mode==4) put_header(f,32,HEAP_BYTES); /* Bounded heap, beyond body end. */
    if (mode==5) put_header(f,76,header(f,32)/8u); /* Intern table exceeds body. */
    hash=file_hash(f);
    t=create(port,jar,len,&ta); sentinel=seed_state(t,1);
    nj_vm_set_device(t,nj_device_find("motorola-e398"));
    base=t->heap.base; bump=t->heap.bump; thread=t->cur;
    check(nj_vm_load(t,f)==(mode>=4?NJ_ERR_FORMAT:NJ_ERR_UNSUPPORTED),
          "legacy live Graphics3D / unknown bit / truncated body rejects clearly");
    check(t->heap.base==base && t->heap.bump==bump && state(t)==sentinel,
          "refusal leaves target arena and static root untouched");
    check(nj_field_read_int(t,NULL,field(t,"marker","I"))==12345 && t->cur==thread &&
          t->motorola_softkeys==1,"refusal preserves target scalar/static/thread state");
    check(file_hash(f)==hash,"reader leaves snapshot bytes unchanged");
    start_fresh(t);
    fclose(f); nj_app_close(t,ta); nj_vm_destroy(t); nj_app_close(s,sa); nj_vm_destroy(s);
}
int main(void)
{
    FILE *f=fopen("tests/fixtures/fielddemo.jar","rb");
    nj_port port; nj_u1 *jar; long len;
    require(f!=NULL,"fielddemo.jar exists (run tools/make_field_jar.sh)");
    require(fseek(f,0,SEEK_END)==0 && (len=ftell(f))>0 && fseek(f,0,SEEK_SET)==0,"size fixture JAR");
    jar=(nj_u1*)malloc((size_t)len); require(jar!=NULL,"JAR buffer allocates");
    require(fread(jar,1,(size_t)len,f)==(size_t)len,"read fixture JAR"); fclose(f);
    require(nj_port_headless_ex(&port,64,64,0,NULL)==NJ_OK,"headless port opens");
    ordinary(&port,jar,(nj_u4)len,0); ordinary(&port,jar,(nj_u4)len,1);
    refusal(&port,jar,(nj_u4)len,3);
    refusal(&port,jar,(nj_u4)len,4); refusal(&port,jar,(nj_u4)len,5);
#if NJ_ENABLE_M3G
    roundtrip(&port,jar,(nj_u4)len);
    refusal(&port,jar,(nj_u4)len,0); refusal(&port,jar,(nj_u4)len,1);
    refusal(&port,jar,(nj_u4)len,2);
#endif
    nj_port_shutdown(&port); free(jar);
    printf("%d checks, %d failures\n",checks,failures); return failures?1:0;
}
