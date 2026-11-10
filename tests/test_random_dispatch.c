/* Random subclass extension points. CLDC specifies next/setSeed dispatch;
 * the existing Boolean/Gaussian extensions follow the Java SE 8 algorithms.
 * Native test subclasses count exact calls, throw, delegate to super and GC.
 * All observations use public calls; no Random field layout is assumed. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RND "java/util/Random"
static unsigned checks, failures;
static const char *context;
#define CHECK(c,m) do { ++checks; if (!(c)) { if (failures<30) { \
    fprintf(stderr,"FAIL %s:%d [%s]: %s\n",__FILE__,__LINE__,context,m); } \
    ++failures; } } while (0)
static void fixture(int ok,const char *s) { if (!ok) { fprintf(stderr,"fixture: %s\n",s); exit(2); } }
static nj_value vi(nj_s4 n) { nj_value v; memset(&v,0,sizeof(v)); v.i=n; return v; }
static void good(nj_status s,nj_vm *vm) { if(s!=NJ_OK || nj_has_exception(vm)) { fprintf(stderr,"unexpected: %s\n",nj_exception_describe(vm)); exit(2); } }
static nj_u4 clock_ms(void *ud) { (void)ud; return 1234567890u; }
static nj_ref expected_receiver;
static int next_calls, widths[32], throw_next, seed_calls, throw_seed, double_calls, throw_double;
static int delegate_next, delegate_double, collect, reenter;
static nj_s8 seen_seed;
static nj_s4 values[32];
static double doubles[32];
static unsigned gc_calls;
static void reset(void)
{
    next_calls=seed_calls=double_calls=0; throw_next=throw_seed=throw_double=0;
    delegate_next=delegate_double=collect=reenter=0;
    memset(widths,0,sizeof(widths)); memset(values,0,sizeof(values)); memset(doubles,0,sizeof(doubles));
}
static void callback(nj_vm *vm,nj_ref r)
{
    CHECK(r==expected_receiver,"callback receiver identity");
    if (collect) {
        nj_gc_collect(vm); ++gc_calls;
        CHECK(nj_heap_owns_object(vm,r),"receiver survives GC in native frame without host root");
    }
}
NJ_NATIVE_FN(next_override)
{
    int index=next_calls++; nj_ref r=NJ_THIS(args); (void)th;
    fixture(index<32,"bounded scripted next calls"); widths[index]=args[0].i;
    callback(vm,r);
    if (throw_next==index+1) return nj_throw(vm,"java/lang/IllegalStateException","next scripted failure");
    if (delegate_next) return nj_call_special(vm,r,RND,"next","(I)I",args,ret);
    ret->i=values[index]; return NJ_OK;
}
NJ_NATIVE_FN(seed_override)
{
    nj_ref r=NJ_THIS(args); (void)th; ++seed_calls; seen_seed=args[0].j;
    callback(vm,r);
    if (throw_seed) return nj_throw(vm,"java/lang/IllegalStateException","seed scripted failure");
    return nj_call_special(vm,r,RND,"setSeed","(J)V",args,ret);
}
NJ_NATIVE_FN(double_override)
{
    int index=double_calls++; nj_ref r=NJ_THIS(args); (void)th; (void)args;
    fixture(index<32,"bounded scripted double calls"); callback(vm,r);
    if (throw_double==index+1) return nj_throw(vm,"java/lang/IllegalStateException","double scripted failure");
    if (reenter && index==0) {
        nj_value arg=vi(8), out;
        nj_status status=nj_call(vm,r,RND,"nextInt","(I)I",&arg,&out);
        if(status!=NJ_OK) return status;
        CHECK(out.i==4,"reentrant bounded call uses same receiver override");
    }
    if (delegate_double) return nj_call_special(vm,r,RND,"nextDouble","()D",NULL,ret);
    ret->d=doubles[index]; return NJ_OK;
}
static void classes(nj_vm *vm)
{
    static const nj_builtin_method methods[]={
        {"next","(I)I",NJ_ACC_PROTECTED|NJ_ACC_NATIVE},
        {"setSeed","(J)V",NJ_ACC_PUBLIC|NJ_ACC_NATIVE},
        {"nextDouble","()D",NJ_ACC_PUBLIC|NJ_ACC_NATIVE}
    };
    static const nj_builtin_class defs[]={
        {"test/NextRandom",RND,NJ_ACC_PUBLIC,NULL,0,&methods[0],1},
        {"test/InheritedRandom","test/NextRandom",NJ_ACC_PUBLIC,NULL,0,NULL,0},
        {"test/SeedRandom",RND,NJ_ACC_PUBLIC,NULL,0,&methods[1],1},
        {"test/DoubleRandom","test/NextRandom",NJ_ACC_PUBLIC,NULL,0,&methods[2],1},
        {"test/PlainRandom",RND,NJ_ACC_PUBLIC,NULL,0,NULL,0}
    };
    unsigned i;
    for(i=0;i<sizeof(defs)/sizeof(defs[0]);++i) {
        nj_class *c=nj_class_create_builtin(vm,&defs[i]); fixture(c!=NULL,"register subclass");
        if(i==0) c->methods[0].native=next_override;
        if(i==2) c->methods[0].native=seed_override;
        if(i==3) c->methods[0].native=double_override;
    }
}
static nj_ref object(nj_vm *vm,const char *cls)
{
    nj_ref r=nj_object_new(vm,cls); nj_value args[2]; nj_u4 mark=nj_root_mark(vm);
    fixture(r!=NULL,"object"); nj_root(vm,r); fixture(nj_root_mark(vm)==mark+1,"object root");
    expected_receiver=r; reset(); memset(args,0,sizeof(args));
    good(nj_call_special(vm,r,RND,"<init>","(J)V",args,NULL),vm);
    reset(); return r;
}
static nj_value call(nj_vm *vm,nj_ref r,const char *name,const char *desc,nj_value *args)
{
    nj_value out[2]; nj_u4 mark=nj_root_mark(vm);
    memset(out,0,sizeof(out)); good(nj_call(vm,r,RND,name,desc,args,out),vm);
    CHECK(nj_root_mark(vm)==mark,"successful call balances host roots"); return out[0];
}
static void failure(nj_vm *vm,nj_ref r,const char *name,const char *desc,nj_value *args,int special)
{
    nj_value out[2]; nj_u4 mark=nj_root_mark(vm);
    nj_status status=special ? nj_call_special(vm,r,RND,name,desc,args,out):nj_call(vm,r,RND,name,desc,args,out);
    CHECK(nj_root_mark(vm)==mark,"throwing call balances host roots");
    CHECK(status!=NJ_OK && nj_has_exception(vm),"override exception propagates");
    CHECK(strstr(nj_exception_describe(vm),"IllegalStateException")!=NULL,"original exception type survives");
    CHECK(strstr(nj_exception_describe(vm),"scripted failure")!=NULL,"original exception message survives");
    nj_clear_exception(vm);
}
static void draws(nj_vm *vm,nj_ref r)
{
    nj_value out,arg; int i; (void)i;
    reset(); values[0]=-123456789; out=call(vm,r,"nextInt","()I",NULL);
    CHECK(out.i==-123456789 && next_calls==1 && widths[0]==32,"nextInt forwards next(32)");
    reset(); values[0]=1073741824; arg=vi(8); out=call(vm,r,"nextInt","(I)I",&arg);
    CHECK(out.i==4 && next_calls==1 && widths[0]==31,"power of two uses high next(31) bits");
    reset(); values[0]=INT32_MAX; values[1]=INT32_MAX-1; values[2]=17; arg=vi(1073741825);
    out=call(vm,r,"nextInt","(I)I",&arg);
    CHECK(out.i==17 && next_calls==3 && widths[0]==31 && widths[1]==31 && widths[2]==31,"large bound rejects two virtual draws");
    reset(); arg=vi(0);
    { nj_status s=nj_call(vm,r,RND,"nextInt","(I)I",&arg,&out);
      CHECK(s!=NJ_OK && strstr(nj_exception_describe(vm),"IllegalArgumentException"),"invalid bound throws before next");
      CHECK(next_calls==0,"invalid bound does not call override"); nj_clear_exception(vm); }
    reset(); values[0]=-1; values[1]=INT32_MIN; out=call(vm,r,"nextLong","()J",NULL);
    CHECK(out.j==-INT64_C(6442450944) && next_calls==2 && widths[0]==32 && widths[1]==32,"nextLong preserves signed low half");
    reset(); values[0]=1; out=call(vm,r,"nextBoolean","()Z",NULL);
    CHECK(out.i==1 && next_calls==1 && widths[0]==1,"Boolean calls next(1)");
    reset(); values[0]=8388608; out=call(vm,r,"nextFloat","()F",NULL);
    CHECK(out.f==0.5f && next_calls==1 && widths[0]==24,"Float calls next(24)");
    reset(); values[0]=33554432; values[1]=1; out=call(vm,r,"nextDouble","()D",NULL);
    CHECK(out.d==0.5+1.0/9007199254740992.0 && next_calls==2 && widths[0]==26 && widths[1]==27,"Double calls 26 then 27 bits");
    /* Do not mask invalid subclass results: Java algorithm consumes raw ints. */
    reset(); values[0]=-1; values[1]=0; out=call(vm,r,"nextDouble","()D",NULL);
    CHECK(out.d==-1.0/67108864.0,"out of contract next value is not silently masked");
    for(i=1;i<=2;++i) {
        reset(); throw_next=i; failure(vm,r,"nextLong","()J",NULL,0);
        CHECK(next_calls==i,"Long stops at failing draw");
        reset(); throw_next=i; failure(vm,r,"nextDouble","()D",NULL,0);
        CHECK(next_calls==i,"Double stops at failing draw");
    }
    reset(); throw_next=1; failure(vm,r,"nextInt","()I",NULL,0); CHECK(next_calls==1,"Int exception");
    reset(); throw_next=1; failure(vm,r,"nextBoolean","()Z",NULL,0); CHECK(next_calls==1,"Boolean exception");
    reset(); throw_next=1; failure(vm,r,"nextFloat","()F",NULL,0); CHECK(next_calls==1,"Float exception");
    reset(); throw_next=2; values[0]=INT32_MAX; arg=vi(1073741825);
    failure(vm,r,"nextInt","(I)I",&arg,0); CHECK(next_calls==2,"bounded rejection stops on second draw exception");
}
static void constructors(nj_vm *vm)
{
    nj_ref r=object(vm,"test/SeedRandom"); nj_value args[2];
    memset(args,0,sizeof(args)); args[0].j=INT64_C(-81985529216486895);
    reset(); collect=1; good(nj_call_special(vm,r,RND,"<init>","(J)V",args,NULL),vm);
    CHECK(seed_calls==1 && seen_seed==args[0].j,"seed constructor virtually dispatches all 64 seed bits");
    reset(); collect=1; good(nj_call_special(vm,r,RND,"<init>","()V",NULL,NULL),vm);
    CHECK(seed_calls==1 && seen_seed==nj_now_ms(vm),"default constructor virtually seeds wall clock once");
    reset(); throw_seed=1; failure(vm,r,"<init>","(J)V",args,1); CHECK(seed_calls==1,"seed constructor exception");
    reset(); throw_seed=1; failure(vm,r,"<init>","()V",NULL,1); CHECK(seed_calls==1,"default constructor exception");
}
static void gaussian(nj_vm *vm)
{
    nj_ref r=object(vm,"test/DoubleRandom"); nj_value out,args[2]; int i;
    double expected=sqrt(-2.0*log(0.5)/0.5)*0.5;
    reset(); doubles[0]=0.75; doubles[1]=0.25; collect=1;
    out=call(vm,r,"nextGaussian","()D",NULL);
    CHECK(fabs(out.d-expected)<1e-14 && double_calls==2 && next_calls==0,"Gaussian uses virtual Double pair");
    out=call(vm,r,"nextGaussian","()D",NULL);
    CHECK(fabs(out.d+expected)<1e-14 && double_calls==2,"Gaussian second value is cached");
    memset(args,0,sizeof(args));
    call(vm,r,"setSeed","(J)V",args);
    reset(); doubles[0]=0.5; doubles[1]=0.5; doubles[2]=1; doubles[3]=1; doubles[4]=0.75; doubles[5]=0.25;
    out=call(vm,r,"nextGaussian","()D",NULL);
    CHECK(fabs(out.d-expected)<1e-14 && double_calls==6,"Gaussian rejects zero and outside unit circle");
    for(i=1;i<=2;++i) {
        call(vm,r,"setSeed","(J)V",args); reset(); throw_double=i;
        failure(vm,r,"nextGaussian","()D",NULL,0); CHECK(double_calls==i,"Gaussian stops at throwing Double");
        reset(); doubles[0]=0.75; doubles[1]=0.25;
        out=call(vm,r,"nextGaussian","()D",NULL);
        CHECK(double_calls==2 && fabs(out.d-expected)<1e-14,"failed Gaussian did not cache partial result");
    }
    call(vm,r,"setSeed","(J)V",args); reset(); delegate_double=1;
    values[0]=50331648; values[1]=0; values[2]=16777216; values[3]=0;
    out=call(vm,r,"nextGaussian","()D",NULL);
    CHECK(fabs(out.d-expected)<1e-14 && double_calls==2 && next_calls==4,"Double override super delegates back through virtual next");
    CHECK(widths[0]==26 && widths[1]==27 && widths[2]==26 && widths[3]==27,"Gaussian super Double draw order");
    call(vm,r,"setSeed","(J)V",args); reset(); reenter=collect=1;
    doubles[0]=0.75; doubles[1]=0.25; values[0]=1073741824;
    out=call(vm,r,"nextGaussian","()D",NULL);
    CHECK(fabs(out.d-expected)<1e-14 && next_calls==1 && double_calls==2,"callback can reenter bounded method");
}
static void inherited_defaults(nj_vm *vm)
{
    static const struct { const char *name,*desc; char kind; } methods[]={
        {"nextInt","()I",'I'}, {"nextInt","(I)I",'I'}, {"nextLong","()J",'J'},
        {"nextBoolean","()Z",'I'}, {"nextFloat","()F",'F'},
        {"nextDouble","()D",'D'}, {"nextGaussian","()D",'D'}
    };
    nj_ref base=object(vm,RND), inherited=object(vm,"test/PlainRandom");
    nj_value seed[2], bound=vi(1073741825); unsigned i,j;
    memset(seed,0,sizeof(seed)); seed[0].j=INT64_C(-81985529216486895);
    call(vm,base,"setSeed","(J)V",seed); call(vm,inherited,"setSeed","(J)V",seed);
    for(i=0;i<8;++i) for(j=0;j<sizeof(methods)/sizeof(methods[0]);++j) {
        nj_value *args=j==1?&bound:NULL;
        nj_value a=call(vm,base,methods[j].name,methods[j].desc,args);
        nj_value b=call(vm,inherited,methods[j].name,methods[j].desc,args);
        int same=methods[j].kind=='I'?a.i==b.i:methods[j].kind=='J'?a.j==b.j:
                 methods[j].kind=='F'?a.f==b.f:a.d==b.d;
        CHECK(same,"inherited default algorithms preserve the ordinary seeded sequence");
    }
}
static void super_and_frame_gc(nj_vm *vm)
{
    unsigned before=gc_calls;
    nj_u4 mark=nj_root_mark(vm); nj_ref r=object(vm,"test/InheritedRandom"); nj_value out;
    reset(); delegate_next=collect=1;
    /* Drop the only test root BEFORE entry: callback GC must find native frames. */
    nj_root_release(vm,mark);
    out=call(vm,r,"nextDouble","()D",NULL);
    CHECK(out.d==0.730967787376657 && next_calls==2,"super.next advances LCG without recursive virtual dispatch");
    CHECK(nj_root_mark(vm)==mark,"frame-only call does not leak host roots");
    CHECK(gc_calls==before+2,"GC actually ran in both next calls");
    /* Re-root only after successful return; no GC/allocation intervenes. */
    nj_root(vm,r); fixture(nj_root_mark(vm)==mark+1,"re-root receiver");
}
static void constructor_gaussian_frame_gc(nj_vm *vm)
{
    nj_ref r; nj_value seed[2],out; nj_u4 mark; unsigned before; int i;
    memset(seed,0,sizeof(seed)); seed[0].j=INT64_C(81985529216486895);
    for(i=0;i<2;++i) {
        mark=nj_root_mark(vm); r=object(vm,"test/SeedRandom");
        reset(); collect=1; before=gc_calls; nj_root_release(vm,mark);
        good(nj_call_special(vm,r,RND,"<init>",i?"()V":"(J)V",i?NULL:seed,NULL),vm);
        CHECK(seed_calls==1 && gc_calls==before+1,"constructor callback GC with only actual frame roots");
        CHECK(nj_root_mark(vm)==mark,"constructor does not add host roots");
        nj_root(vm,r); fixture(nj_root_mark(vm)==mark+1,"re-root constructed receiver");
    }
    mark=nj_root_mark(vm); r=object(vm,"test/DoubleRandom"); reset(); collect=1;
    doubles[0]=0.75; doubles[1]=0.25; before=gc_calls; nj_root_release(vm,mark);
    out=call(vm,r,"nextGaussian","()D",NULL);
    CHECK(fabs(out.d-sqrt(-log(0.5)))<1e-14 && gc_calls==before+2,"Gaussian pair GC with only actual frame roots");
    CHECK(nj_root_mark(vm)==mark,"Gaussian does not add host roots");
    nj_root(vm,r); fixture(nj_root_mark(vm)==mark+1,"re-root Gaussian receiver");
}
int main(void)
{
    nj_port port; nj_vm *vm; nj_u4 mark; nj_ref r;
    memset(&port,0,sizeof(port)); fixture(nj_port_headless(&port)==NJ_OK,"headless port"); port.millis=clock_ms;
    vm=nj_vm_create(&port,2u*1024u*1024u); fixture(vm!=NULL,"VM"); classes(vm); mark=nj_root_mark(vm);
    context="direct next override"; r=object(vm,"test/NextRandom"); draws(vm,r);
    context="inherited next override"; r=object(vm,"test/InheritedRandom"); draws(vm,r);
    context="constructors virtual setSeed"; constructors(vm);
    context="Gaussian virtual Double and cache"; gaussian(vm);
    context="inherited default implementation"; inherited_defaults(vm);
    context="inherited override, super primitive, native frame GC"; super_and_frame_gc(vm);
    context="constructor and Gaussian actual frame roots"; constructor_gaussian_frame_gc(vm);
    CHECK(!nj_has_exception(vm),"no pending exception");
    nj_root_release(vm,mark); nj_vm_destroy(vm); nj_port_shutdown(&port);
    printf("Random dispatch: %u checks, %u passed, %u failed\n",checks,checks-failures,failures);
    return failures?1:0;
}
