/* CP fast-hit coverage, not a change to Java resolution semantics.
 * The original home-slot-only inline lookup legitimately falls back to the
 * resolver on a collision. New collision-hit assertions therefore fail on
 * that baseline while resolution correctness controls still run to the end.
 * No fixture JAR or production builtin table additions are needed. */
#include "nj_internal.h"
#include "nj2me/nj.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

static unsigned checks, failures;
static const char *context;
#define CHECK(x, msg) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr,"FAIL %s:%d [%s]: %s\n",__FILE__,__LINE__,context,(msg)); } } while (0)
static void fixture(int ok, const char *msg)
{
    if (!ok) { fprintf(stderr,"fixture failure [%s]: %s\n",context,msg); exit(2); }
}

/* In addition to pointer identity, every call must preserve the whole table,
 * miss counter and bookkeeping; exactly one hit is charged per successful
 * inline lookup, including an entry reached through a long probe chain. */
static nj_cpentry *lookup(nj_vm *vm, nj_class *c, nj_u2 index, nj_cpentry *want)
{
    nj_cpentry *got, *saved = NULL;
    nj_cpentry *table = vm->cp_cache;
    nj_u2 size=vm->cp_cache_size, used=vm->cp_cache_used, limit=vm->cp_cache_limit;
    nj_u4 hits=vm->cp_cache_hits, misses=vm->cp_cache_misses;
    size_t bytes=(size_t)size*sizeof(*table);
    if (bytes) {
        saved=(nj_cpentry *)malloc(bytes); fixture(saved!=NULL,"table snapshot");
        memcpy(saved,table,bytes);
    }
    got=nj_cp_hit(vm,c,index);
    CHECK(got==want,"fast-hit entry identity / miss");
    CHECK(vm->cp_cache_hits==hits+(want?1u:0u),"hit counter increments exactly once or not at all");
    CHECK(vm->cp_cache_misses==misses,"inline lookup does not count resolver misses");
    CHECK(vm->cp_cache==table && vm->cp_cache_size==size &&
          vm->cp_cache_used==used && vm->cp_cache_limit==limit,"lookup preserves cache bookkeeping");
    CHECK(!bytes || memcmp(saved,table,bytes)==0,"lookup does not modify entries or inline-call caches");
    free(saved); return got;
}
static nj_u2 next_index(nj_class *c, unsigned home, unsigned mask, unsigned first)
{
    unsigned i;
    for(i=first;i<=65535u;++i)
        if((nj_cp_key_hash(c,i)&mask)==home)return (nj_u2)i;
    fixture(0,"find colliding CP index"); return 0;
}
static void artificial_chains(void)
{
    static const unsigned lengths[]={0,1,3,7};
    static const nj_u1 kinds[]={NJ_LINK_FIELD_INST,NJ_LINK_FIELD_STATIC,
        NJ_LINK_METHOD_INST,NJ_LINK_METHOD_STATIC,NJ_LINK_STRING,NJ_LINK_CLASS};
    nj_vm vm; nj_class owner, other; nj_cpentry table[8]; nj_field field;
    unsigned start, l, j, k; nj_u2 index, target_index;
    memset(&vm,0,sizeof(vm)); memset(&owner,0,sizeof(owner));
    memset(&other,0,sizeof(other)); memset(&field,0,sizeof(field));
    vm.cp_cache=table; vm.cp_cache_size=8; vm.cp_cache_limit=5;
    vm.cp_cache_hits=17; vm.cp_cache_misses=29;
    context="same-hash chains at every start bucket";
    for(start=0;start<8;++start)for(l=0;l<sizeof(lengths)/sizeof(lengths[0]);++l) {
        memset(table,0,sizeof(table)); index=0;
        for(j=0;j<=lengths[l];++j) {
            nj_cpentry *e=&table[(start+j)&7u];
            index=next_index(&owner,start,7u,(unsigned)index+1u);
            e->used=1; e->cls=&owner; e->cp_index=index;
            e->kind=NJ_LINK_FIELD_INST; e->target=&field;
            e->recv_cls=&other; e->ic_target=&field; e->slot=(nj_u2)(j+9u);
            CHECK((nj_cp_key_hash(e->cls,e->cp_index)&7u)==start,"blocker has the same real hash bucket");
        }
        target_index=index; vm.cp_cache_used=(nj_u2)(lengths[l]+1u);
        for(k=0;k<sizeof(kinds)/sizeof(kinds[0]);++k) {
            table[(start+lengths[l])&7u].kind=kinds[k];
            lookup(&vm,&owner,target_index,&table[(start+lengths[l])&7u]);
        }
        /* An empty bucket terminates the chain, even when stale matching
         * fields remain in that bucket or a matching entry exists beyond it. */
        table[start].used=0; --vm.cp_cache_used;
        lookup(&vm,&owner,target_index,NULL);
        /* Same CP number in a different declaring class is a different key. */
        lookup(&vm,&other,target_index,NULL);
        for(j=0;j<8;++j) { table[j].used=1; table[j].cls=&other; }
        vm.cp_cache_used=8;
        lookup(&vm,&owner,target_index,NULL); /* bounded full-table miss */
    }
    context="zero capacity and single-slot table";
    vm.cp_cache=NULL; vm.cp_cache_size=0; vm.cp_cache_used=0;
    lookup(&vm,&owner,1,NULL);
    vm.cp_cache=table; lookup(&vm,&owner,1,NULL);
    vm.cp_cache_size=1; memset(table,0,sizeof(table));
    lookup(&vm,&owner,1,NULL);
    table[0].used=1; table[0].cls=&owner; table[0].cp_index=1;
    vm.cp_cache_used=1;
    lookup(&vm,&owner,1,&table[0]);
    lookup(&vm,&other,1,NULL); lookup(&vm,&owner,2,NULL);
}
static void class_key_separation(void)
{
    nj_vm vm; nj_class owners[16]; nj_cpentry table[16];
    nj_cpentry *entries[16]; unsigned i,h,probes;
    context="different declaring classes with identical CP indices";
    memset(&vm,0,sizeof(vm)); memset(owners,0,sizeof(owners)); memset(table,0,sizeof(table));
    vm.cp_cache=table; vm.cp_cache_size=16; vm.cp_cache_used=16;
    for(i=0;i<16;++i) {
        h=nj_cp_key_hash(&owners[i],11)&15u;
        for(probes=0;table[h].used && probes<16;++probes)h=(h+1u)&15u;
        fixture(probes<16,"insert distinct class key"); entries[i]=&table[h];
        table[h].used=1; table[h].cls=&owners[i]; table[h].cp_index=11;
        table[h].slot=(nj_u2)i; table[h].target=&owners[i]; table[h].kind=NJ_LINK_CLASS;
    }
    for(i=0;i<16;++i)lookup(&vm,&owners[i],11,entries[i]);
    lookup(&vm,&owners[0],12,NULL); /* no empty bucket: must still terminate */
}

/* Byte-level CP records are passed to the real resolver. Only the caller's
 * CP metadata is constructed here; field/method owners are real linked test
 * classes registered with the VM, and strings use its real interning/GC. */
#define CP_COUNT 96
static void cp_record(nj_class *c, nj_u1 *data, nj_u4 *info, unsigned index,
                      unsigned tag, unsigned a, unsigned b)
{
    unsigned off=1u+index*4u;
    data[off]=(nj_u1)(a>>8); data[off+1]=(nj_u1)a;
    data[off+2]=(nj_u1)(b>>8); data[off+3]=(nj_u1)b;
    info[index]=(nj_u4)((off<<4)|tag);
    (void)c;
}
static nj_class *create_owner(nj_vm *vm)
{
    static const nj_builtin_field fields[]={
        {"instanceValue","I",NJ_ACC_PUBLIC},
        {"staticValue","I",NJ_ACC_PUBLIC|NJ_ACC_STATIC}};
    static const nj_builtin_method methods[]={
        {"instanceMethod","()I",NJ_ACC_PUBLIC},
        {"staticMethod","()I",NJ_ACC_PUBLIC|NJ_ACC_STATIC}};
    static const nj_builtin_class def={"test/CPResolutionOwner","java/lang/Object",
        NJ_ACC_PUBLIC,fields,2,methods,2};
    nj_class *c=nj_class_create_builtin(vm,&def);
    fixture(c!=NULL && nj_class_link(vm,c)==NJ_OK,"link real CP target class"); return c;
}
static void malformed(nj_vm *vm, nj_class *caller, nj_u2 index,
                      nj_status status, const char *error)
{
    nj_cpentry *entry=NULL; nj_u4 hits=vm->cp_cache_hits;
    vm->resolve_fail=NJ_RESOLVE_OK;
    vm->resolve_cls=NULL; vm->resolve_name=NULL; vm->resolve_desc=NULL;
    lookup(vm,caller,index,NULL);
    CHECK(nj_cp_resolve(vm,caller,index,&entry)==status,"malformed/missing CP follows existing resolver status");
    CHECK(vm->cp_cache_hits==hits,"malformed lookup does not invent a cache hit");
    lookup(vm,caller,index,NULL); /* failed resolution must not become used */
    {
        const char *description;
        CHECK(nj_resolve_error(vm)!=NJ_OK,"resolver failure translates to Java error");
        description=nj_exception_describe(vm);
        CHECK(description && strstr(description,error?error:"NoClassDefFoundError"),"existing Java resolution error class");
        nj_clear_exception(vm);
    }
}
static void real_resolver(void)
{
    nj_port port; nj_vm *vm; nj_class caller, *owner;
    nj_u1 data[CP_COUNT*4+4]; nj_u4 info[CP_COUNT];
    const char *utf[CP_COUNT]; char strings[12][32];
    void *targets[17]; nj_u1 kinds[17];
    unsigned i,old_size,growths=0; nj_cpentry *entry=NULL;
    context="real resolver, growth and interned strings";
    fixture(nj_port_headless(&port)==NJ_OK,"headless port");
    vm=nj_vm_create(&port,2u*1024u*1024u); fixture(vm!=NULL,"create VM");
    owner=create_owner(vm);
    memset(&caller,0,sizeof(caller)); memset(data,0,sizeof(data));
    memset(info,0,sizeof(info)); memset(utf,0,sizeof(utf));
    caller.name="test/CPCaller"; caller.state=NJ_CLASS_READY;
    caller.cp_count=CP_COUNT; caller.cp_info=info; caller.cp_utf8=utf;
    caller.data=data; caller.data_len=sizeof(data);
    for(i=1;i<=12;++i) {
        snprintf(strings[i-1],sizeof(strings[i-1]),"cp-cache-string-%u",i);
        utf[40+i]=strings[i-1]; cp_record(&caller,data,info,i,NJ_CP_STRING,40+i,0);
    }
    utf[60]=owner->name; cp_record(&caller,data,info,20,NJ_CP_CLASS,60,0);
    utf[61]="instanceValue"; utf[62]="I"; utf[63]="staticValue";
    utf[64]="instanceMethod"; utf[65]="()I"; utf[66]="staticMethod";
    cp_record(&caller,data,info,21,NJ_CP_NAMEANDTYPE,61,62);
    cp_record(&caller,data,info,22,NJ_CP_NAMEANDTYPE,63,62);
    cp_record(&caller,data,info,23,NJ_CP_NAMEANDTYPE,64,65);
    cp_record(&caller,data,info,24,NJ_CP_NAMEANDTYPE,66,65);
    for(i=13;i<=16;++i)cp_record(&caller,data,info,i,
        i<=14?NJ_CP_FIELDREF:NJ_CP_METHODREF,20,i+8);
    /* Force real resolver growth at several load-factor thresholds. The old
     * VM-owned table is freed explicitly before replacing its capacity. */
    nj_host_free(vm,vm->cp_cache); vm->cp_cache=NULL;
    fixture(nj_cp_cache_init(vm,8)==NJ_OK,"start with small real cache");
    old_size=vm->cp_cache_size;
    for(i=1;i<=16;++i) {
        nj_status status=nj_cp_resolve(vm,&caller,(nj_u2)i,&entry);
        CHECK(status==NJ_OK && entry!=NULL,"resolve real CP entry");
        fixture(status==NJ_OK && entry!=NULL,"resolved entry required for controls");
        if(vm->cp_cache_size!=old_size) { ++growths; old_size=vm->cp_cache_size; }
        targets[i]=entry->target; kinds[i]=entry->kind;
        CHECK(entry->used && entry->cls==&caller && entry->cp_index==i,"resolver key identity");
        if(i<=12) {
            CHECK(entry->kind==NJ_LINK_STRING,"resolved String kind");
            CHECK(nj_string_equals_utf8((nj_ref)entry->target,strings[i-1]),"resolved String contents");
        } else {
            void *want=i==13?(void *)&owner->fields[0]:i==14?(void *)&owner->fields[1]:
                i==15?(void *)&owner->methods[0]:(void *)&owner->methods[1];
            nj_u1 kind=i==13?NJ_LINK_FIELD_INST:i==14?NJ_LINK_FIELD_STATIC:
                i==15?NJ_LINK_METHOD_INST:NJ_LINK_METHOD_STATIC;
            CHECK(entry->target==want && entry->kind==kind,"resolved member target and kind");
            if(i<=14)CHECK(entry->slot==((nj_field *)want)->offset,"resolved field slot");
        }
    }
    CHECK(growths>=2 && vm->cp_cache_size>=32,"resolver grew and rehashed real entries");
    for(i=0;i<3;++i) {
        unsigned index;
        nj_gc_collect(vm);
        for(index=1;index<=16;++index) {
            /* Fetch the CURRENT entry after rehash; pointers into the old
             * table are invalid. Resolver fallback remains a correctness
             * control even when the baseline fast lookup misses. */
            fixture(nj_cp_resolve(vm,&caller,(nj_u2)index,&entry)==NJ_OK,"resolve after growth/GC");
            lookup(vm,&caller,(nj_u2)index,entry);
            CHECK(entry->target==targets[index] && entry->kind==kinds[index],"stable kind/target after growth and GC");
            if(index<=12)CHECK(nj_string_equals_utf8((nj_ref)entry->target,strings[index-1]),"interned String survives GC");
        }
    }
    context="invalid indices and resolver errors remain misses";
    malformed(vm,&caller,0,NJ_ERR,NULL);
    malformed(vm,&caller,CP_COUNT,NJ_ERR,NULL);
    malformed(vm,&caller,65535,NJ_ERR,NULL);
    malformed(vm,&caller,30,NJ_ERR_FORMAT,NULL); /* absent CP offset */
    cp_record(&caller,data,info,31,NJ_CP_INTEGER,1,0);
    malformed(vm,&caller,31,NJ_ERR_FORMAT,NULL); /* unsupported resolver tag */
    utf[67]="missingMember";
    cp_record(&caller,data,info,25,NJ_CP_NAMEANDTYPE,67,62);
    cp_record(&caller,data,info,32,NJ_CP_FIELDREF,20,25);
    malformed(vm,&caller,32,NJ_ERR,"NoSuchFieldError");
    cp_record(&caller,data,info,26,NJ_CP_NAMEANDTYPE,67,65);
    cp_record(&caller,data,info,33,NJ_CP_METHODREF,20,26);
    malformed(vm,&caller,33,NJ_ERR,"NoSuchMethodError");
    CHECK(!nj_has_exception(vm),"all expected resolver exceptions cleared");
    nj_vm_destroy(vm); nj_port_shutdown(&port);
}
int main(void)
{
#if !defined(_WIN32)
    alarm(20); /* turns a broken full-table probe loop into a bounded failure */
#endif
    artificial_chains(); class_key_separation(); real_resolver();
    printf("CP cache: %u checks, %u passed, %u failed\n",checks,checks-failures,failures);
    return failures?1:0;
}
