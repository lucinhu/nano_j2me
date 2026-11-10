/* java.util.Random.nextInt(bound): independent rejection/seed oracle.
 * The desktop java.util.Random digests below were generated with OpenJDK
 * 27.0.1.0.3. For each seed, restart for each bound, take 64 bounded values,
 * and interleave nextInt() after every eighth value; FNV-1a mixes int values.
 * The integer oracle uses the accepted 31-bit interval, rather than Java's
 * overflowing rejection expression. No Random fields or native entry points
 * are read, so both returned values and consumed LCG steps are tested.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RANDOM "java/util/Random"
static unsigned checks, failures;
static const char *context;
#define CHECK(c, message) do { \
    ++checks; \
    if (!(c)) { \
        if (failures < 20) fprintf(stderr, "FAIL %s:%d [%s]: %s\n", \
            __FILE__, __LINE__, context, message); \
        ++failures; \
    } \
} while (0)

static void fixture(int ok, const char *message)
{
    if (!ok) { fprintf(stderr, "fixture failure: %s\n", message); exit(2); }
}
static nj_value integer(nj_s4 value)
{
    nj_value out; memset(&out, 0, sizeof(out)); out.i=value; return out;
}
static void call(nj_vm *vm, nj_ref random, const char *name, const char *desc,
                 nj_value *args, nj_value *out)
{
    if (nj_call(vm, random, RANDOM, name, desc, args, out)!=NJ_OK || nj_has_exception(vm)) {
        fprintf(stderr, "unexpected %s%s: %s\n", name, desc, nj_exception_describe(vm));
        exit(2);
    }
}
static void set_seed(nj_vm *vm, nj_ref random, nj_s8 seed)
{
    nj_value args[2]; memset(args, 0, sizeof(args)); args[0].j=seed;
    call(vm, random, "setSeed", "(J)V", args, NULL);
}
static nj_s4 bounded(nj_vm *vm, nj_ref random, nj_s4 bound)
{
    nj_value arg=integer(bound), out;
    call(vm, random, "nextInt", "(I)I", &arg, &out); return out.i;
}
static nj_u4 unbounded(nj_vm *vm, nj_ref random)
{
    nj_value out; call(vm, random, "nextInt", "()I", NULL, &out); return (nj_u4)out.i;
}

typedef struct oracle { uint64_t state; unsigned draws, rejects; } oracle;
static void oracle_seed(oracle *o, nj_s8 seed)
{
    o->state=((uint64_t)seed ^ UINT64_C(0x5deece66d)) & UINT64_C(0xffffffffffff);
    o->draws=0; o->rejects=0;
}
static uint32_t oracle_bits(oracle *o, unsigned bits)
{
    o->state=(o->state*UINT64_C(25214903917)+UINT64_C(11)) & UINT64_C(0xffffffffffff);
    ++o->draws;
    return (uint32_t)(o->state>>(48-bits));
}
static uint32_t oracle_bound(oracle *o, uint32_t bound)
{
    uint32_t bits;
    uint64_t limit;
    if ((bound & (bound-1u))==0)
        return (uint32_t)(((uint64_t)bound*oracle_bits(o,31))>>31);
    limit=(UINT64_C(0x80000000)/bound)*bound;
    do {
        bits=oracle_bits(o,31);
        if ((uint64_t)bits < limit) return bits % bound;
        ++o->rejects;
    } while (1);
}
static uint32_t mix(uint32_t hash, uint32_t value)
{
    return (hash ^ value)*UINT32_C(16777619);
}
static const nj_s8 seeds[]={0,1,-1,INT64_MIN,INT64_MAX,
    INT64_C(20015998343868),INT64_C(186643064605892),100};
static const nj_s4 bounds[]={1,2,3,7,10,31,32,33,127,255,256,257,
    65535,65536,65537,1073741823,1073741824,1073741825,1073741826,
    1431655765,2147483646,2147483647};
static const uint32_t desktop[]={UINT32_C(0x36e70bce),UINT32_C(0x53f0f2e3),
    UINT32_C(0xe10e547f),UINT32_C(0x36e70bce),UINT32_C(0xe10e547f),
    UINT32_C(0xa0d285d6),UINT32_C(0xacc32c90),UINT32_C(0xa00a5c54)};

static void rejection_controls(nj_vm *vm, nj_ref random)
{
    oracle o;
    context="seed 0, bound 2^30+1 rejects the first two candidates";
    set_seed(vm,random,0); oracle_seed(&o,0);
    CHECK(oracle_bound(&o,1073741825)==516548029u,"independent accepted value agrees with JVM");
    CHECK(o.draws==3 && o.rejects==2,"known seed consumes three LCG candidates");
    CHECK(bounded(vm,random,1073741825)==516548029,"JVM large-bound value");
    CHECK(unbounded(vm,random)==(nj_u4)-1690734402,"rejection preserves subsequent seed sequence");
    context="INT_MAX bound rejects an exact INT_MAX candidate";
    set_seed(vm,random,INT64_C(186643064605892)); oracle_seed(&o,INT64_C(186643064605892));
    CHECK(oracle_bits(&o,31)==UINT32_C(2147483647),"constructed seed yields exact endpoint");
    oracle_seed(&o,INT64_C(186643064605892));
    CHECK(oracle_bound(&o,2147483647)==554899859u && o.rejects==1,"endpoint must be rejected once");
    CHECK(bounded(vm,random,2147483647)==554899859,"JVM INT_MAX-bound value");
    CHECK(unbounded(vm,random)==(nj_u4)-1753186469,"INT_MAX rejection seed continuation");
}
static void sequences(nj_vm *vm, nj_ref random)
{
    unsigned s,b,i, total_rejections=0;
    char label[100];
    for (s=0;s<sizeof(seeds)/sizeof(seeds[0]);++s) {
        uint32_t actual_hash=UINT32_C(0x811c9dc5), oracle_hash=actual_hash;
        for (b=0;b<sizeof(bounds)/sizeof(bounds[0]);++b) {
            oracle o;
            snprintf(label,sizeof(label),"seed %lld bound %d",(long long)seeds[s],(int)bounds[b]);
            context=label; oracle_seed(&o,seeds[s]); set_seed(vm,random,seeds[s]);
            for (i=0;i<64;++i) {
                uint32_t want=oracle_bound(&o,(uint32_t)bounds[b]);
                nj_s4 got=bounded(vm,random,bounds[b]);
                CHECK(got>=0 && got<bounds[b],"result is within [0,bound)");
                CHECK((uint32_t)got==want,"value matches independent unsigned rejection oracle");
                actual_hash=mix(actual_hash,(uint32_t)got); oracle_hash=mix(oracle_hash,want);
                if ((i&7u)==7u) {
                    uint32_t next=unbounded(vm,random), reference=oracle_bits(&o,32);
                    CHECK(next==reference,"unbounded continuation uses correct number of LCG steps");
                    actual_hash=mix(actual_hash,next); oracle_hash=mix(oracle_hash,reference);
                }
            }
            total_rejections+=o.rejects;
        }
        context="desktop JVM whole-sequence digest";
        CHECK(actual_hash==desktop[s],"runtime sequence matches recorded java.util.Random");
        CHECK(oracle_hash==desktop[s],"independent oracle agrees with desktop JVM");
    }
    context="rejection coverage";
    CHECK(total_rejections>100,"large-bound matrix really exercises rejected candidates");
}
static void invalid_and_reset(nj_vm *vm, nj_ref random)
{
    static const nj_s4 invalid[]={0,-1,INT32_MIN};
    unsigned s,i;
    for (s=0;s<sizeof(seeds)/sizeof(seeds[0]);++s) {
        oracle o;
        context="invalid bounds do not consume seed";
        set_seed(vm,random,seeds[s]); oracle_seed(&o,seeds[s]);
        for (i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
            nj_value arg=integer(invalid[i]), out;
            nj_status status=nj_call(vm,random,RANDOM,"nextInt","(I)I",&arg,&out);
            const char *description=nj_exception_describe(vm);
            CHECK(status!=NJ_OK && nj_has_exception(vm),"invalid bound throws");
            CHECK(description && strstr(description,"IllegalArgumentException"),"invalid bound has correct Java exception");
            nj_clear_exception(vm);
            CHECK(unbounded(vm,random)==oracle_bits(&o,32),"invalid call leaves generator state unchanged");
        }
        context="setSeed restarts bounded rejection after prior calls";
        set_seed(vm,random,seeds[s]); oracle_seed(&o,seeds[s]);
        CHECK((uint32_t)bounded(vm,random,1073741825)==oracle_bound(&o,1073741825),"reset repeats large-bound sequence");
        CHECK(unbounded(vm,random)==oracle_bits(&o,32),"reset continuation");
    }
}
int main(int argc, char **argv)
{
    nj_port port; nj_vm *vm; nj_ref random; nj_value args[2]; nj_u4 mark;
    memset(&port,0,sizeof(port));
    fixture(nj_port_headless(&port)==NJ_OK,"headless port");
    vm=nj_vm_create(&port,2u*1024u*1024u); fixture(vm!=NULL,"VM");
    mark=nj_root_mark(vm); random=nj_object_new(vm,RANDOM); fixture(random!=NULL,"Random object");
    nj_root(vm,random); fixture(nj_root_mark(vm)==mark+1u,"root Random");
    memset(args,0,sizeof(args));
    fixture(nj_call_special(vm,random,RANDOM,"<init>","(J)V",args,NULL)==NJ_OK,"Random(0) constructor");
    rejection_controls(vm,random);
    if (argc!=2 || strcmp(argv[1],"--rejection-only")!=0) {
        sequences(vm,random); invalid_and_reset(vm,random);
    }
    context="final VM state";
    CHECK(!nj_has_exception(vm),"no pending exception");
    CHECK(nj_root_mark(vm)==mark+1u,"calls leave temporary roots balanced");
    nj_root_release(vm,mark); nj_vm_destroy(vm); nj_port_shutdown(&port);
    printf("Random bounds: %u checks, %u passed, %u failed\n",checks,checks-failures,failures);
    return failures?1:0;
}
