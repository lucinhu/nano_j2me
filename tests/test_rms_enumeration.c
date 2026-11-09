/*
 * RecordEnumeration semantic regressions, using public RMS calls only.
 * MIDP 2.0: RecordEnumeration and RecordStore.enumerateRecords javadocs.
 *
 * Rebuild/live-update cursor anchoring is unspecified: reset before asserting
 * the new set/order. Unsorted/equivalent records have no prescribed order.
 * Filter/comparator arguments are read-only; callbacks may collect garbage,
 * but these tests do not demand a particular result from reentrant mutations.
 * Fixture callback classes are test-only and do not assume runtime enum fields.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define RS "javax/microedition/rms/RecordStore"
#define RE "javax/microedition/rms/RecordEnumeration"
#define ENUM_DESC "(Ljavax/microedition/rms/RecordFilter;Ljavax/microedition/rms/RecordComparator;Z)L" RE ";"
static int checks, failures;
static const char *context;
#define CHECK(x, msg) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr,"FAIL %s:%d [%s]: %s\n",__FILE__,__LINE__,context,(msg)); } } while (0)
static nj_ref filters[2], comparators[2];
static int limits[2], reverse_order[2], filter_calls[2], compare_calls[2];
static int collect_callbacks, fail_filter, fail_compare;

static nj_value vi(int i) { nj_value v; v.bits=0; v.i=i; return v; }
static nj_value vr(nj_ref r) { nj_value v; v.bits=0; v.r=r; return v; }
static void fixture(nj_vm *vm, int valid, const char *where)
{
    if (!valid || nj_has_exception(vm)) {
        fprintf(stderr,"fixture failure %s [%s]: %s\n",where,context,
                nj_has_exception(vm)?nj_exception_describe(vm):"no exception");
        exit(2);
    }
}
static nj_ref rooted(nj_vm *vm, nj_ref ref)
{
    nj_u4 mark=nj_root_mark(vm);
    fixture(vm,ref!=NULL,"allocate fixture object");
    nj_root(vm,ref); fixture(vm,nj_root_mark(vm)==mark+1u,"root fixture object");
    return ref;
}
static void invoke(nj_vm *vm, nj_ref obj, const char *cls, const char *name,
                   const char *desc, nj_value *a, nj_value *r)
{
    fixture(vm,nj_call(vm,obj,cls,name,desc,a,r)==NJ_OK,name);
}
static int query(nj_vm *vm, nj_ref en, const char *name, const char *desc)
{
    nj_value r; nj_status st; r.bits=0;
    st=nj_call(vm,en,RE,name,desc,NULL,&r);
    CHECK(st==NJ_OK && !nj_has_exception(vm),name);
    if (nj_has_exception(vm)) { nj_clear_exception(vm); return -999; }
    return st==NJ_OK?r.i:-999;
}
static void action(nj_vm *vm, nj_ref en, const char *name)
{
    nj_status st=nj_call(vm,en,RE,name,"()V",NULL,NULL);
    CHECK(st==NJ_OK && !nj_has_exception(vm),name);
    if (nj_has_exception(vm)) nj_clear_exception(vm);
}
static void expect_exception(nj_vm *vm, nj_status st, const char *name)
{
    const char *got=nj_has_exception(vm)?nj_exception_describe(vm):NULL;
    CHECK(st!=NJ_OK && got && strstr(got,name),name);
    nj_clear_exception(vm);
}
static nj_ref open_store(nj_vm *vm, const char *name)
{
    nj_value a[2], r; a[0]=vr(nj_string_new(vm,name)); a[1]=vi(1);
    fixture(vm,a[0].r!=NULL,"store name");
    fixture(vm,nj_call_static(vm,RS,"openRecordStore","(Ljava/lang/String;Z)L" RS ";",a,&r)==NJ_OK,"openRecordStore");
    return rooted(vm,r.r);
}
static int add(nj_vm *vm, nj_ref rs, int value)
{
    nj_u4 mark=nj_root_mark(vm); nj_value a[3],r;
    nj_ref bytes=rooted(vm,nj_byte_array_new(vm,value<0?0u:1u));
    if (value>=0)bytes->u.bytes[0]=(nj_u1)value;
    a[0]=vr(bytes); a[1]=vi(0); a[2]=vi(value<0?0:1);
    invoke(vm,rs,RS,"addRecord","([BII)I",a,&r);
    nj_root_release(vm,mark); return r.i;
}
static void change(nj_vm *vm, nj_ref rs, int id, int value)
{
    nj_u4 mark=nj_root_mark(vm); nj_value a[4];
    nj_ref bytes=rooted(vm,nj_byte_array_new(vm,1)); bytes->u.bytes[0]=(nj_u1)value;
    a[0]=vi(id); a[1]=vr(bytes); a[2]=vi(0); a[3]=vi(1);
    invoke(vm,rs,RS,"setRecord","(I[BII)V",a,NULL); nj_root_release(vm,mark);
}
static void erase(nj_vm *vm, nj_ref rs, int id)
{ nj_value a=vi(id); invoke(vm,rs,RS,"deleteRecord","(I)V",&a,NULL); }
static void close_store(nj_vm *vm, nj_ref rs)
{ invoke(vm,rs,RS,"closeRecordStore","()V",NULL,NULL); }
static int candidate_value(nj_ref bytes)
{ return bytes && bytes->length?(int)bytes->u.bytes[0]:-1; }
static nj_status matches(nj_vm *vm, nj_thread *th, nj_value *args, nj_value *ret)
{
    nj_ref self=NJ_THIS(args),bytes=args[0].r; int which=self==filters[1]; (void)th;
    CHECK(self==filters[0] || self==filters[1],"retained filter identity");
    ++filter_calls[which]; if(collect_callbacks)nj_gc_collect(vm);
    if(fail_filter)return nj_throw(vm,"java/lang/IllegalArgumentException","enum filter fixture");
    ret->i=candidate_value(bytes)>=limits[which]; return NJ_OK;
}
static nj_status compare(nj_vm *vm, nj_thread *th, nj_value *args, nj_value *ret)
{
    nj_ref self=NJ_THIS(args),a=args[0].r,b=args[1].r; int which=self==comparators[1]; (void)th;
    CHECK(self==comparators[0] || self==comparators[1],"retained comparator identity");
    ++compare_calls[which]; if(collect_callbacks)nj_gc_collect(vm);
    if(fail_compare)return nj_throw(vm,"java/lang/IllegalArgumentException","enum comparator fixture");
    ret->i=(candidate_value(a)>candidate_value(b))-(candidate_value(a)<candidate_value(b));
    if(reverse_order[which])ret->i=-ret->i;
    return NJ_OK;
}
static void callbacks(nj_vm *vm)
{
    static const nj_builtin_method fm[]={{"matches","([B)Z",NJ_ACC_PUBLIC}};
    static const nj_builtin_method cm[]={{"compare","([B[B)I",NJ_ACC_PUBLIC}};
    static const nj_builtin_class fc={"test/EnumFilter","java/lang/Object",NJ_ACC_PUBLIC,NULL,0,fm,1};
    static const nj_builtin_class cc={"test/EnumComparator","java/lang/Object",NJ_ACC_PUBLIC,NULL,0,cm,1};
    nj_class *c; int i;
    c=nj_class_create_builtin(vm,&fc); fixture(vm,c!=NULL,"filter class"); c->methods[0].native=matches;
    c=nj_class_create_builtin(vm,&cc); fixture(vm,c!=NULL,"comparator class"); c->methods[0].native=compare;
    for(i=0;i<2;++i) {
        filters[i]=rooted(vm,nj_object_new(vm,"test/EnumFilter"));
        comparators[i]=rooted(vm,nj_object_new(vm,"test/EnumComparator"));
        limits[i]=0; reverse_order[i]=i; filter_calls[i]=compare_calls[i]=0;
    }
    collect_callbacks=1; fail_filter=fail_compare=0;
}
static nj_ref enumeration(nj_vm *vm, nj_ref rs, nj_ref filter, nj_ref comp, int live)
{
    nj_value a[3],r; nj_u4 mark=nj_root_mark(vm);
    a[0]=vr(filter); a[1]=vr(comp); a[2]=vi(live);
    invoke(vm,rs,RS,"enumerateRecords",ENUM_DESC,a,&r);
    CHECK(nj_root_mark(vm)==mark,"enumeration creation balances temporary roots");
    return rooted(vm,r.r);
}
static void keep(nj_vm *vm, nj_ref en, int live)
{
    nj_value a=vi(live); nj_u4 mark=nj_root_mark(vm);
    nj_status st=nj_call(vm,en,RE,"keepUpdated","(Z)V",&a,NULL);
    CHECK(st==NJ_OK && !nj_has_exception(vm),"keepUpdated");
    if(nj_has_exception(vm))nj_clear_exception(vm);
    CHECK(nj_root_mark(vm)==mark,"keepUpdated balances temporary roots");
    CHECK(query(vm,en,"isKeptUpdated","()Z")==live,"isKeptUpdated reflects setting");
}
static void rebuild(nj_vm *vm, nj_ref en)
{
    nj_u4 mark=nj_root_mark(vm); action(vm,en,"rebuild");
    CHECK(nj_root_mark(vm)==mark,"rebuild balances temporary roots");
}
static void ids(nj_vm *vm, nj_ref en, const int *want, int count)
{
    int i; nj_value r;
    /* No exact cursor-anchor assumption after rebuild or live mutation. */
    action(vm,en,"reset"); CHECK(query(vm,en,"numRecords","()I")==count,"current filtered record count");
    for(i=0;i<count;++i) {
        CHECK(query(vm,en,"hasNextElement","()Z")==1,"forward availability");
        CHECK(query(vm,en,"nextRecordId","()I")==want[i],"filtered comparator order");
    }
    CHECK(query(vm,en,"hasNextElement","()Z")==0,"forward exhaustion");
    expect_exception(vm,nj_call(vm,en,RE,"nextRecordId","()I",NULL,&r),"InvalidRecordIDException");
}
static void seed(nj_vm *vm, nj_ref rs)
{
    CHECK(add(vm,rs,30)==1,"first ID"); CHECK(add(vm,rs,10)==2,"second ID"); CHECK(add(vm,rs,20)==3,"third ID");
}
static void traversal(nj_vm *vm)
{
    nj_u4 mark=nj_root_mark(vm); nj_ref rs,en,empty,copy; nj_value a,r; int i;
    const int sorted[]={2,3,1};
    context="static traversal and copy"; rs=open_store(vm,"enum-walk"); seed(vm,rs);
    en=enumeration(vm,rs,NULL,comparators[0],0); ids(vm,en,sorted,3);
    action(vm,en,"reset");
    CHECK(query(vm,en,"hasPreviousElement","()Z")==1,"fresh/reset enumeration can start backwards");
    for(i=2;i>=0;--i)CHECK(query(vm,en,"previousRecordId","()I")==sorted[i],"first previous starts at last record");
    CHECK(query(vm,en,"hasPreviousElement","()Z")==0,"backward exhaustion");
    expect_exception(vm,nj_call(vm,en,RE,"previousRecordId","()I",NULL,&r),"InvalidRecordIDException");
    expect_exception(vm,nj_call(vm,en,RE,"previousRecordId","()I",NULL,&r),"InvalidRecordIDException");
    action(vm,en,"reset");
    r.bits=0;
    { nj_status st=nj_call(vm,en,RE,"previousRecord","()[B",NULL,&r);
      CHECK(st==NJ_OK && !nj_has_exception(vm),"first previousRecord is available");
      if(nj_has_exception(vm)) { nj_clear_exception(vm); r.r=NULL; } }
    CHECK(r.r && r.r->length==1u && r.r->u.bytes[0]==30,"previousRecord data starts at last element");
    action(vm,en,"reset"); invoke(vm,en,RE,"nextRecord","()[B",NULL,&r); copy=rooted(vm,r.r);
    CHECK(copy->length==1u && copy->u.bytes[0]==10,"nextRecord starts at first sorted element");
    copy->u.bytes[0]=99; a=vi(2); invoke(vm,rs,RS,"getRecord","(I)[B",&a,&r);
    CHECK(r.r && r.r->u.bytes[0]==10,"enumerated bytes are a copy");
    /* Snapshot membership may be stale; the retrieved data must be current. */
    change(vm,rs,2,11); action(vm,en,"reset"); invoke(vm,en,RE,"nextRecord","()[B",NULL,&r);
    CHECK(r.r && r.r->u.bytes[0]==11,"non-live enumeration retrieves current record contents");
    empty=open_store(vm,"enum-empty"); en=enumeration(vm,empty,NULL,NULL,0);
    CHECK(query(vm,en,"numRecords","()I")==0,"empty enumeration count");
    CHECK(query(vm,en,"hasNextElement","()Z")==0,"empty has no next");
    CHECK(query(vm,en,"hasPreviousElement","()Z")==0,"empty has no previous");
    expect_exception(vm,nj_call(vm,en,RE,"nextRecordId","()I",NULL,&r),"InvalidRecordIDException");
    expect_exception(vm,nj_call(vm,en,RE,"previousRecord","()[B",NULL,&r),"InvalidRecordIDException");
    close_store(vm,empty); close_store(vm,rs); nj_root_release(vm,mark);
}
static void explicit_rebuild(nj_vm *vm)
{
    nj_u4 mark=nj_root_mark(vm); nj_ref rs,a,b; int before;
    const int first[]={3,1}, desc[]={1,3,2}, onlynew[]={4}, all_desc[]={4,2,1}, all_asc[]={1,2,4};
    context="rebuild retains filter/comparator per enumeration";
    rs=open_store(vm,"enum-rebuild"); seed(vm,rs); limits[0]=15; limits[1]=0; reverse_order[0]=0; reverse_order[1]=1;
    a=enumeration(vm,rs,filters[0],comparators[0],0); b=enumeration(vm,rs,filters[1],comparators[1],0);
    ids(vm,a,first,2); ids(vm,b,desc,3);
    change(vm,rs,1,5); erase(vm,rs,3); CHECK(add(vm,rs,40)==4,"monotonic ID after delete");
    before=filter_calls[0]; rebuild(vm,a); CHECK(filter_calls[0]>before,"rebuild reapplies original filter");
    rebuild(vm,b); ids(vm,a,onlynew,1); ids(vm,b,all_desc,3);
    limits[0]=0; limits[1]=15; rebuild(vm,a); rebuild(vm,b);
    ids(vm,a,all_asc,3); ids(vm,b,onlynew,1);
    limits[0]=250; rebuild(vm,a); ids(vm,a,NULL,0);
    limits[0]=0; rebuild(vm,a); ids(vm,a,all_asc,3);
    action(vm,a,"destroy"); action(vm,b,"destroy"); close_store(vm,rs); nj_root_release(vm,mark);
}
static void live_updates(nj_vm *vm)
{
    nj_u4 mark=nj_root_mark(vm); nj_ref rs,a,b;
    const int initial[]={3,1}, added[]={3,4,1}, deleted[]={4,1}, lost[]={4}, entered[]={4,2}, reordered[]={2,4};
    const int reverse_all[]={4,2,1}, after_off[]={2,5,4}, after_toggle[]={5,4}, resorted[]={4,5};
    context="live add delete filter entry exit and comparator reorder";
    rs=open_store(vm,"enum-live"); seed(vm,rs); limits[0]=15; limits[1]=0; reverse_order[0]=0; reverse_order[1]=1;
    a=enumeration(vm,rs,filters[0],comparators[0],1); b=enumeration(vm,rs,filters[1],comparators[1],1);
    CHECK(query(vm,a,"isKeptUpdated","()Z")==1,"constructor keepUpdated=true"); ids(vm,a,initial,2);
    CHECK(add(vm,rs,25)==4,"live append ID"); ids(vm,a,added,3);
    erase(vm,rs,3); ids(vm,a,deleted,2);
    change(vm,rs,1,5); ids(vm,a,lost,1);
    change(vm,rs,2,40); ids(vm,a,entered,2);
    change(vm,rs,4,50); ids(vm,a,reordered,2); ids(vm,b,reverse_all,3);
    keep(vm,a,0); CHECK(add(vm,rs,45)==5,"append while auto-update disabled");
    ids(vm,a,reordered,2);
    keep(vm,a,1); ids(vm,a,after_off,3);
    /* Calling true is documented as rebuild, including an already-true flag.
     * Mutable filter/comparator state changes without a store notification. */
    limits[0]=44; keep(vm,a,1); ids(vm,a,after_toggle,2);
    reverse_order[0]=1; keep(vm,a,1); ids(vm,a,resorted,2);
    action(vm,a,"destroy"); change(vm,rs,1,60);
    { const int other[]={1,4,5,2}; ids(vm,b,other,4); }
    action(vm,b,"destroy"); close_store(vm,rs); nj_root_release(vm,mark);
}
static void unsorted_and_empty(nj_vm *vm)
{
    nj_u4 mark=nj_root_mark(vm); nj_ref rs,en; unsigned seen=0; int i,n,id;
    context="null comparator set membership including empty records";
    rs=open_store(vm,"enum-unsorted"); CHECK(add(vm,rs,-1)==1,"zero-length record ID");
    CHECK(add(vm,rs,30)==2,"ordinary record ID"); en=enumeration(vm,rs,NULL,NULL,1);
    CHECK(add(vm,rs,10)==3,"new unsorted live record"); action(vm,en,"reset");
    n=query(vm,en,"numRecords","()I"); CHECK(n==3,"zero-length and live-added records belong to set");
    for(i=0;i<n && i<8;++i) {
        id=query(vm,en,"nextRecordId","()I");
        CHECK(id>=1 && id<=3,"unsorted ID belongs to expected set");
        if(id>=1 && id<=3) { CHECK(!(seen & (1u<<(unsigned)id)),"unsorted IDs are not repeated"); seen|=1u<<(unsigned)id; }
    }
    CHECK(seen==14u,"unsorted traversal includes every record without prescribing order");
    action(vm,en,"destroy"); close_store(vm,rs); nj_root_release(vm,mark);
}
static void lifetime(nj_vm *vm)
{
    static const struct { const char *name,*desc; } methods[]={
        {"numRecords","()I"},{"nextRecord","()[B"},{"previousRecord","()[B"},
        {"nextRecordId","()I"},{"previousRecordId","()I"},
        {"hasNextElement","()Z"},{"hasPreviousElement","()Z"},{"reset","()V"},
        {"rebuild","()V"},{"isKeptUpdated","()Z"},{"keepUpdated","(Z)V"},{"destroy","()V"}};
    nj_u4 mark=nj_root_mark(vm); nj_ref rs,en,dead; nj_value a=vi(1),r; unsigned i;
    context="closed and destroyed enumeration contracts";
    rs=open_store(vm,"enum-lifetime"); seed(vm,rs);
    en=enumeration(vm,rs,NULL,comparators[0],1); dead=enumeration(vm,rs,NULL,NULL,0);
    action(vm,dead,"destroy");
    for(i=0;i<sizeof(methods)/sizeof(methods[0]);++i)
        expect_exception(vm,nj_call(vm,dead,RE,methods[i].name,methods[i].desc,&a,&r),"IllegalStateException");
    close_store(vm,rs);
    CHECK(query(vm,en,"hasNextElement","()Z")==0,"closed enumeration has no next");
    CHECK(query(vm,en,"hasPreviousElement","()Z")==0,"closed enumeration has no previous");
    /* Other methods after close have no portable outcome requirement. */
    rs=open_store(vm,"enum-lifetime");
    CHECK(query(vm,en,"hasNextElement","()Z")==0,"reopening does not validate old enumeration (next)");
    CHECK(query(vm,en,"hasPreviousElement","()Z")==0,"reopening does not validate old enumeration (previous)");
    action(vm,en,"destroy"); close_store(vm,rs); nj_root_release(vm,mark);
}
static void shared_open(nj_vm *vm)
{
    nj_u4 mark=nj_root_mark(vm); nj_ref rs,again,en,name; nj_value a,r; nj_status st;
    const int both[]={1,2};
    context="shared opens and final-close lifetime";
    reverse_order[0]=0;
    rs=open_store(vm,"enum-shared"); CHECK(add(vm,rs,10)==1,"shared store first ID");
    en=enumeration(vm,rs,NULL,comparators[0],1);
    again=open_store(vm,"enum-shared"); CHECK(again==rs,"repeated open returns same RecordStore object");
    change(vm,rs,1,20); a=vi(1); r.bits=0;
    st=nj_call(vm,again,RS,"getRecord","(I)[B",&a,&r);
    CHECK(st==NJ_OK && !nj_has_exception(vm) && r.r && r.r->length==1u && r.r->u.bytes[0]==20,
          "changes visible through every open handle");
    if(nj_has_exception(vm))nj_clear_exception(vm);
    CHECK(add(vm,again,25)==2,"shared next record ID"); ids(vm,en,both,2);
    close_store(vm,again);
    CHECK(query(vm,en,"hasNextElement","()Z")==0,"cursor remains exhausted after nonfinal close");
    action(vm,en,"reset"); CHECK(query(vm,en,"hasNextElement","()Z")==1,"nonfinal close keeps enumeration usable");
    r.bits=0; st=nj_call(vm,rs,RS,"getNumRecords","()I",NULL,&r);
    CHECK(st==NJ_OK && !nj_has_exception(vm) && r.i==2,"nonfinal close leaves store open");
    if(nj_has_exception(vm))nj_clear_exception(vm);
    name=rooted(vm,nj_string_new(vm,"enum-shared")); a=vr(name);
    expect_exception(vm,nj_call_static(vm,RS,"deleteRecordStore","(Ljava/lang/String;)V",&a,NULL),"RecordStoreException");
    close_store(vm,rs);
    CHECK(query(vm,en,"hasNextElement","()Z")==0,"final close invalidates enumeration (next)");
    CHECK(query(vm,en,"hasPreviousElement","()Z")==0,"final close invalidates enumeration (previous)");
    expect_exception(vm,nj_call(vm,rs,RS,"getNumRecords","()I",NULL,&r),"RecordStoreNotOpenException");
    action(vm,en,"destroy");
    fixture(vm,nj_call_static(vm,RS,"deleteRecordStore","(Ljava/lang/String;)V",&a,NULL)==NJ_OK,"delete after final close");
    nj_root_release(vm,mark);
}
static void callback_retention(nj_vm *vm)
{
    nj_u4 outer=nj_root_mark(vm),temporary; nj_ref rs,en; int f,c;
    const int initial[]={3,1}, later[]={3,4,1};
    context="enumeration alone retains callbacks across explicit GC";
    rs=open_store(vm,"enum-retain"); seed(vm,rs); temporary=nj_root_mark(vm);
    filters[0]=rooted(vm,nj_object_new(vm,"test/EnumFilter"));
    comparators[0]=rooted(vm,nj_object_new(vm,"test/EnumComparator"));
    limits[0]=15; reverse_order[0]=0;
    en=enumeration(vm,rs,filters[0],comparators[0],0);
    nj_root_release(vm,temporary); en=rooted(vm,en); nj_gc_collect(vm);
    ids(vm,en,initial,2); CHECK(add(vm,rs,25)==4,"retention append");
    f=filter_calls[0]; c=compare_calls[0]; rebuild(vm,en);
    CHECK(filter_calls[0]>f && compare_calls[0]>c,"retained callbacks invoked on rebuild");
    ids(vm,en,later,3); keep(vm,en,1); change(vm,rs,4,40);
    { const int updated[]={3,1,4}; ids(vm,en,updated,3); }
    action(vm,en,"destroy"); close_store(vm,rs); nj_root_release(vm,outer);
}
static void explicit_callback_errors(nj_vm *vm)
{
    nj_u4 mark=nj_root_mark(vm),roots; nj_ref rs,en; nj_status st; int which;
    const int sorted[]={2,3,1};
    context="explicit rebuild propagates callback exceptions and releases roots";
    rs=open_store(vm,"enum-callback-errors"); seed(vm,rs); limits[0]=0; reverse_order[0]=0;
    /* The earlier retention case replaced callback zero; allocate/root fresh
     * fixtures here, since a destroyed enumeration no longer owns the old ones. */
    filters[0]=rooted(vm,nj_object_new(vm,"test/EnumFilter"));
    comparators[0]=rooted(vm,nj_object_new(vm,"test/EnumComparator"));
    en=enumeration(vm,rs,filters[0],comparators[0],0);
    for(which=0;which<2;++which) {
        fail_filter=which==0; fail_compare=which==1; roots=nj_root_mark(vm);
        st=nj_call(vm,en,RE,"rebuild","()V",NULL,NULL);
        expect_exception(vm,st,"IllegalArgumentException");
        CHECK(nj_root_mark(vm)==roots,"callback error balances temporary roots");
        fail_filter=fail_compare=0; rebuild(vm,en); ids(vm,en,sorted,3);
    }
    action(vm,en,"destroy"); close_store(vm,rs); nj_root_release(vm,mark);
}
int main(void)
{
    nj_port port; nj_vm *vm; char dir[1024]; const char *base=getenv("NANO_J2ME_RMS");
    context="fixture initialization";
    if(!base || !*base)base="tests/out";
    if(snprintf(dir,sizeof(dir),"%s/rms-enum-%ld",base,(long)getpid())>=(int)sizeof(dir))return 2;
    (void)mkdir(base,0777); if(mkdir(dir,0777)!=0) { perror("create isolated RMS directory"); return 2; }
    if(nj_port_headless(&port)!=NJ_OK)return 2;
    port.rms_dir=dir; vm=nj_vm_create(&port,4u*1024u*1024u); if(!vm)return 2;
    callbacks(vm); traversal(vm); explicit_rebuild(vm); live_updates(vm);
    unsorted_and_empty(vm); lifetime(vm); shared_open(vm); callback_retention(vm); explicit_callback_errors(vm);
    fixture(vm,!nj_has_exception(vm),"suite final state");
    nj_vm_destroy(vm); nj_port_shutdown(&port);
    printf("test_rms_enumeration: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
