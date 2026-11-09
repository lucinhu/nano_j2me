/* RMS lookup equivalence: public operations and version-1 unordered/duplicate
 * IDs. Private-field corruption is restricted to optimization guard negatives;
 * it deliberately preserves the prior virtual Vector/Integer call behavior. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

#define RS "javax/microedition/rms/RecordStore"
#define VEC "java/util/Vector"
#define AT "(I)Ljava/lang/Object;"
static int checks, failures;
static const char *context="initialization";
#define CHECK(x,msg) do { ++checks; if(!(x)) { ++failures; \
    fprintf(stderr,"FAIL %s:%d [%s]: %s\n",__FILE__,__LINE__,context,msg); } } while(0)
static nj_value vi(int i) { nj_value v; v.bits=0; v.i=i; return v; }
static nj_value vr(nj_ref r) { nj_value v; v.bits=0; v.r=r; return v; }
static void fixture(nj_vm *vm,int ok,const char *where)
{
    if(!ok || nj_has_exception(vm)) {
        fprintf(stderr,"fixture failure [%s] %s: %s\n",context,where,
                nj_has_exception(vm)?nj_exception_describe(vm):"no exception"); exit(2);
    }
}
static nj_ref root(nj_vm *vm,nj_ref r)
{
    nj_u4 n=nj_root_mark(vm); fixture(vm,r!=NULL,"allocate");
    nj_root(vm,r); fixture(vm,nj_root_mark(vm)==n+1u,"protect fixture ref"); return r;
}
static void call(nj_vm *vm,nj_ref o,const char *c,const char *n,const char *d,nj_value *a,nj_value *r)
{ fixture(vm,nj_call(vm,o,c,n,d,a,r)==NJ_OK,n); }
static int number(nj_vm *vm,nj_ref rs,const char *name)
{ nj_value r; call(vm,rs,RS,name,"()I",NULL,&r); return r.i; }
static void exception(nj_vm *vm,nj_status st,const char *kind)
{
    const char *s=nj_has_exception(vm)?nj_exception_describe(vm):NULL;
    CHECK(st!=NJ_OK && s && (!kind || strstr(s,kind)),kind?kind:"callback must cause an error");
    nj_clear_exception(vm);
}
static nj_ref open_store(nj_vm *vm,const char *name)
{
    nj_value a[2],r; a[0]=vr(nj_string_new(vm,name)); a[1]=vi(1);
    fixture(vm,a[0].r!=NULL,"store name");
    fixture(vm,nj_call_static(vm,RS,"openRecordStore","(Ljava/lang/String;Z)L" RS ";",a,&r)==NJ_OK,"open");
    return root(vm,r.r);
}
static void close_store(nj_vm *vm,nj_ref rs)
{ call(vm,rs,RS,"closeRecordStore","()V",NULL,NULL); }
static nj_ref bytes(nj_vm *vm,const void *p,size_t len)
{
    nj_ref r=root(vm,nj_byte_array_new(vm,(nj_u4)len));
    if(len)memcpy(r->u.bytes,p,len);
    return r;
}
static int add(nj_vm *vm,nj_ref rs,const void *p,size_t len)
{
    nj_u4 mark=nj_root_mark(vm); nj_value a[3],r;
    a[0]=vr(bytes(vm,p,len)); a[1]=vi(0); a[2]=vi((int)len);
    call(vm,rs,RS,"addRecord","([BII)I",a,&r); nj_root_release(vm,mark); return r.i;
}
static void set(nj_vm *vm,nj_ref rs,int id,const void *p,size_t len)
{
    nj_u4 mark=nj_root_mark(vm); nj_value a[4];
    a[0]=vi(id); a[1]=vr(bytes(vm,p,len)); a[2]=vi(0); a[3]=vi((int)len);
    call(vm,rs,RS,"setRecord","(I[BII)V",a,NULL); nj_root_release(vm,mark);
}
static void erase(nj_vm *vm,nj_ref rs,int id)
{ nj_value a=vi(id); call(vm,rs,RS,"deleteRecord","(I)V",&a,NULL); }
static void record(nj_vm *vm,nj_ref rs,int id,const void *p,size_t len)
{
    nj_u4 mark=nj_root_mark(vm); nj_value a[3],r; nj_ref dest; nj_status st;
    a[0]=vi(id); r.bits=0;
    st=nj_call(vm,rs,RS,"getRecord","(I)[B",a,&r);
    CHECK(st==NJ_OK && !nj_has_exception(vm),"getRecord succeeds");
    if(nj_has_exception(vm))nj_clear_exception(vm);
    CHECK((!len && (!r.r || !r.r->length)) || (r.r && r.r->length==len && !memcmp(r.r->u.bytes,p,len)),"exact record bytes");
    /* Mutating the returned copy must not change either getRecord overload. */
    if(st==NJ_OK && r.r && len)r.r->u.bytes[0]^=0xffu;
    st=nj_call(vm,rs,RS,"getRecordSize","(I)I",a,&r);
    CHECK(st==NJ_OK && !nj_has_exception(vm) && r.i==(int)len,"record size agrees");
    if(nj_has_exception(vm))nj_clear_exception(vm);
    dest=root(vm,nj_byte_array_new(vm,(nj_u4)len+4u)); memset(dest->u.bytes,0xa5,len+4u);
    a[1]=vr(dest); a[2]=vi(2);
    st=nj_call(vm,rs,RS,"getRecord","(I[BI)I",a,&r);
    CHECK(st==NJ_OK && !nj_has_exception(vm) && r.i==(int)len,"getRecord into slice agrees");
    if(nj_has_exception(vm))nj_clear_exception(vm);
    CHECK(!len || !memcmp(dest->u.bytes+2,p,len),"record copy cannot alter stored bytes");
    CHECK(dest->u.bytes[0]==0xa5 && dest->u.bytes[1]==0xa5 && dest->u.bytes[len+2]==0xa5 && dest->u.bytes[len+3]==0xa5,"getRecord preserves destination padding");
    nj_root_release(vm,mark);
}
static void absent(nj_vm *vm,nj_ref rs,int id)
{
    nj_u4 mark=nj_root_mark(vm); nj_value a[4],r; nj_ref b=bytes(vm,"xxx",3);
    a[0]=vi(id); exception(vm,nj_call(vm,rs,RS,"getRecord","(I)[B",a,&r),"InvalidRecordIDException");
    exception(vm,nj_call(vm,rs,RS,"getRecordSize","(I)I",a,&r),"InvalidRecordIDException");
    a[1]=vr(b); a[2]=vi(0);
    exception(vm,nj_call(vm,rs,RS,"getRecord","(I[BI)I",a,&r),"InvalidRecordIDException");
    CHECK(!memcmp(b->u.bytes,"xxx",3),"missing getRecord leaves destination unchanged");
    a[3]=vi(3); exception(vm,nj_call(vm,rs,RS,"setRecord","(I[BII)V",a,NULL),"InvalidRecordIDException");
    exception(vm,nj_call(vm,rs,RS,"deleteRecord","(I)V",a,NULL),"InvalidRecordIDException");
    nj_root_release(vm,mark);
}
static void normal(nj_vm *vm)
{
    nj_u4 mark=nj_root_mark(vm); nj_ref rs; unsigned char data[32][3]; int live[32],i;
    static const int removed[]={1,16,32},changed[]={2,17,31},bad[]={0,-1,INT_MIN,INT_MAX,1,16,32};
    context="empty and non-contiguous normal IDs"; rs=open_store(vm,"lookup-normal");
    CHECK(number(vm,rs,"getNumRecords")==0,"empty store"); absent(vm,rs,1);
    for(i=0;i<32;i++) { data[i][0]=(unsigned char)i; data[i][1]=(unsigned char)(i^0xa5); data[i][2]=(unsigned char)(i+7); live[i]=1; CHECK(add(vm,rs,data[i],3)==i+1,"sequential allocated IDs"); }
    CHECK(add(vm,rs,NULL,0)==33,"empty record allocated"); record(vm,rs,33,NULL,0);
    for(i=0;i<32;i++)record(vm,rs,i+1,data[i],3);
    for(i=0;i<3;i++) { erase(vm,rs,removed[i]); live[removed[i]-1]=0; }
    for(i=0;i<3;i++) { int k=changed[i]-1; data[k][0]^=0x40u; set(vm,rs,changed[i],data[k],3); }
    for(i=0;i<7;i++)absent(vm,rs,bad[i]);
    CHECK(number(vm,rs,"getNumRecords")==30,"missing operations do not mutate count");
    for(i=0;i<32;i++)if(live[i])record(vm,rs,i+1,data[i],3);
    close_store(vm,rs); rs=open_store(vm,"lookup-normal");
    for(i=0;i<32;i++)if(live[i])record(vm,rs,i+1,data[i],3);
    record(vm,rs,33,NULL,0); close_store(vm,rs); nj_root_release(vm,mark);
}
static void u2(FILE *f,unsigned n)
{ unsigned char b[2]={(unsigned char)(n>>8),(unsigned char)n}; if(fwrite(b,1,2,f)!=2)exit(2); }
static unsigned read2(FILE *f)
{ unsigned char b[2]; if(fread(b,1,2,f)!=2)exit(2); return (unsigned)b[0]*256u+b[1]; }
static void legacy_file(const char *dir)
{
    static const unsigned ids[]={42,7,500,7,1,32768,65533};
    static const char *const payload[]={"forty-two","A","","BB","one","high","last"};
    char path[1200]; FILE *f; size_t i;
    if(snprintf(path,sizeof(path),"%s/lookup-legacy.rms",dir)>=(int)sizeof(path))exit(2);
    f=fopen(path,"wb"); if(!f){perror(path);exit(2);} if(fwrite("NJRS",1,4,f)!=4)exit(2);
    u2(f,1);u2(f,0);u2(f,65534);u2(f,7);
    for(i=0;i<7;i++) { size_t n=strlen(payload[i]);u2(f,ids[i]);u2(f,(unsigned)n);if(n && fwrite(payload[i],1,n,f)!=n)exit(2); }
    if(fclose(f))exit(2);
}
static void legacy(nj_vm *vm,int reload)
{
    nj_u4 mark=nj_root_mark(vm); nj_ref rs=open_store(vm,"lookup-legacy");
    context=reload?"v1 rewrite read by fresh VM":"v1 unordered and duplicate ID FIRST match";
    CHECK(number(vm,rs,"getNextRecordID")==65534,"v1 nextID preserved");
    if(!reload) {
        CHECK(number(vm,rs,"getNumRecords")==7,"all v1 entries including duplicates accepted");
        record(vm,rs,7,"A",1);set(vm,rs,7,"FIRST",5);record(vm,rs,7,"FIRST",5);
        erase(vm,rs,7);record(vm,rs,7,"BB",2); /* delete reveals untouched second duplicate */
        set(vm,rs,7,"SECOND",6);close_store(vm,rs);
    } else {
        CHECK(number(vm,rs,"getNumRecords")==6,"rewritten v1 count"); record(vm,rs,7,"SECOND",6);
        erase(vm,rs,7);absent(vm,rs,7);
        record(vm,rs,42,"forty-two",9);record(vm,rs,500,NULL,0);record(vm,rs,1,"one",3);
        record(vm,rs,32768,"high",4);record(vm,rs,65533,"last",4);close_store(vm,rs);
    }
    nj_root_release(vm,mark);
}
static void legacy_prefix(const char *dir)
{
    static const unsigned ids[]={42,500,1,32768,65533};
    static const char *const payload[]={"forty-two","","one","high","last"};
    char path[1200],magic[4],buf[32]; FILE *f; size_t i;
    context="old reader sees unchanged v1 order and bytes";
    if(snprintf(path,sizeof(path),"%s/lookup-legacy.rms",dir)>=(int)sizeof(path))exit(2);
    f=fopen(path,"rb");if(!f)exit(2);
    CHECK(fread(magic,1,4,f)==4 && !memcmp(magic,"NJRS",4),"v1 magic");
    CHECK(read2(f)==1,"v1 version");CHECK(read2(f)==0,"v1 reserved");
    CHECK(read2(f)==65534,"v1 nextID");CHECK(read2(f)==5,"v1 record count");
    for(i=0;i<5;i++) { unsigned id=read2(f),n=read2(f);CHECK(id==ids[i],"no sorting or ID renumbering");CHECK(n==strlen(payload[i]),"v1 length");if(n>=sizeof(buf))exit(2);CHECK(fread(buf,1,n,f)==n && !memcmp(buf,payload[i],n),"v1 payload unchanged"); }
    fclose(f); /* trailing compatible metadata is intentionally ignored */
}
static nj_native_fn original_at;
static int vector_calls,box_calls,hide_first,throw_vector,throw_box;
static nj_status vector_at(nj_vm *vm,nj_thread *th,nj_value *args,nj_value *ret)
{
    ++vector_calls;nj_gc_collect(vm);
    if(throw_vector)return nj_throw(vm,"java/lang/IllegalArgumentException","lookup vector fixture");
    if(hide_first && args[0].i==0){ret->r=NULL;return NJ_OK;}
    return original_at(vm,th,args,ret);
}
static nj_status box_value(nj_vm *vm,nj_thread *th,nj_value *args,nj_value *ret)
{
    (void)th;(void)args;++box_calls;nj_gc_collect(vm);
    if(throw_box)return nj_throw(vm,"java/lang/IllegalArgumentException","lookup box fixture");
    ret->i=77;return NJ_OK;
}
static nj_field *field(nj_vm *vm,const char *cls,const char *name,const char *desc)
{ nj_field *f=nj_field_lookup(vm,cls,name,desc,0);fixture(vm,f!=NULL,name);return f; }
static void guards(nj_vm *vm)
{
    static const nj_builtin_method methods[]={{"elementAt",AT,NJ_ACC_PUBLIC}};
    static const nj_builtin_class subclass={"test/LookupVector",VEC,NJ_ACC_PUBLIC,NULL,0,methods,1};
    static const nj_builtin_method bm[]={{"intValue","()I",NJ_ACC_PUBLIC}};
    static const nj_builtin_class bc={"test/LookupID","java/lang/Object",NJ_ACC_PUBLIC,NULL,0,bm,1};
    nj_u4 mark=nj_root_mark(vm),before;nj_ref rs,ids,sub,data,boxed,fake;nj_class *c;nj_method *m;
    nj_field *rf,*df,*cf;nj_value a[2],r;nj_status st;int saved_count;
    context="Vector subclass virtual lookup with callback GC";rs=open_store(vm,"lookup-guards");
    CHECK(add(vm,rs,"a",1)==1,"guard seed 1");CHECK(add(vm,rs,"bb",2)==2,"guard seed 2");CHECK(add(vm,rs,"ccc",3)==3,"guard seed 3");
    rf=field(vm,RS,"rsIds","Ljava/util/Vector;");df=field(vm,VEC,"elementData","[Ljava/lang/Object;");cf=field(vm,VEC,"elementCount","I");
    ids=root(vm,nj_field_read_ref(vm,rs,rf));
    a[0]=vi(0);call(vm,ids,VEC,"elementAt",AT,a,&r);
    m=nj_method_lookup(nj_class_resolve(vm,VEC),"elementAt",AT);fixture(vm,m && m->native,"builtin elementAt binding");original_at=m->native;
    c=nj_class_create_builtin(vm,&subclass);fixture(vm,c!=NULL,"subclass");c->methods[0].native=vector_at;
    sub=root(vm,nj_object_new(vm,"test/LookupVector"));call(vm,sub,VEC,"<init>","()V",NULL,NULL);
    data=nj_field_read_ref(vm,ids,df);saved_count=nj_field_read_int(vm,ids,cf);
    nj_field_write_ref(vm,sub,df,data);nj_field_write_int(vm,sub,cf,saved_count);nj_field_write_ref(vm,rs,rf,sub);
    vector_calls=0;a[0]=vi(3);before=nj_root_mark(vm);
    call(vm,rs,RS,"getRecordSize","(I)I",a,&r);
    CHECK(r.i==3 && vector_calls==3,"elementAt override used for each ID before match");
    CHECK(nj_root_mark(vm)==before,"callback GC lookup balances roots");
    record(vm,rs,1,"a",1);record(vm,rs,2,"bb",2);record(vm,rs,3,"ccc",3);
    hide_first=1;a[0]=vi(1);vector_calls=0;
    exception(vm,nj_call(vm,rs,RS,"getRecordSize","(I)I",a,&r),"InvalidRecordIDException");
    CHECK(vector_calls==3,"override hiding first ID cannot be bypassed");hide_first=0;
    throw_vector=1;vector_calls=0;before=nj_root_mark(vm);a[0]=vi(2);
    st=nj_call(vm,rs,RS,"getRecord","(I)[B",a,&r);exception(vm,st,NULL);
    CHECK(vector_calls>0 && nj_root_mark(vm)==before,"throwing Vector override runs and balances roots");throw_vector=0;
    nj_field_write_ref(vm,rs,rf,ids);
    context="non-Integer element virtual intValue guard";
    a[0]=vi(1);call(vm,ids,VEC,"elementAt",AT,a,&r);boxed=root(vm,r.r);
    c=nj_class_create_builtin(vm,&bc);fixture(vm,c!=NULL,"non-Integer class");c->methods[0].native=box_value;
    fake=root(vm,nj_object_new(vm,"test/LookupID"));a[0]=vr(fake);a[1]=vi(1);call(vm,ids,VEC,"setElementAt","(Ljava/lang/Object;I)V",a,NULL);
    box_calls=0;record(vm,rs,77,"bb",2);CHECK(box_calls>0,"non-Integer intValue dispatch retained");
    set(vm,rs,77,"changed",7);record(vm,rs,77,"changed",7);
    throw_box=1;box_calls=0;a[0]=vi(77);before=nj_root_mark(vm);
    st=nj_call(vm,rs,RS,"getRecord","(I)[B",a,&r);exception(vm,st,NULL);
    CHECK(box_calls>0 && nj_root_mark(vm)==before,"throwing intValue used and roots balanced");throw_box=0;
    a[0]=vr(boxed);a[1]=vi(1);call(vm,ids,VEC,"setElementAt","(Ljava/lang/Object;I)V",a,NULL);
    record(vm,rs,2,"changed",7);record(vm,rs,1,"a",1);record(vm,rs,3,"ccc",3);
    context="inconsistent Vector bounds use legacy behavior";
    nj_field_write_int(vm,ids,cf,(int)data->length+1);a[0]=vi(1);call(vm,rs,RS,"getRecordSize","(I)I",a,&r);CHECK(r.i==1,"over-capacity count retains first-element lookup");
    nj_field_write_int(vm,ids,cf,-1);a[0]=vi(1);exception(vm,nj_call(vm,rs,RS,"getRecordSize","(I)I",a,&r),"InvalidRecordIDException");
    nj_field_write_int(vm,ids,cf,saved_count);record(vm,rs,2,"changed",7);
    close_store(vm,rs);nj_root_release(vm,mark);
}
int main(void)
{
    nj_port port;nj_vm *vm;char dir[1024];const char *base=getenv("NANO_J2ME_RMS");
    if(!base || !*base)base="tests/out";
    if(snprintf(dir,sizeof(dir),"%s/rms-lookup-%ld",base,(long)getpid())>=(int)sizeof(dir))return 2;
    (void)mkdir(base,0777);if(mkdir(dir,0777)){perror("isolated RMS directory");return 2;}
    if(nj_port_headless(&port)!=NJ_OK)return 2;
    port.rms_dir=dir;
    vm=nj_vm_create(&port,4u*1024u*1024u);if(!vm)return 2;
    normal(vm);guards(vm);fixture(vm,!nj_has_exception(vm),"first VM final state");nj_vm_destroy(vm);
    legacy_file(dir);vm=nj_vm_create(&port,4u*1024u*1024u);if(!vm)return 2;legacy(vm,0);nj_vm_destroy(vm);
    vm=nj_vm_create(&port,4u*1024u*1024u);if(!vm)return 2;legacy(vm,1);nj_vm_destroy(vm);
    legacy_prefix(dir);nj_port_shutdown(&port);
    printf("test_rms_lookup: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
