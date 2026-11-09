/* Real RMS snapshots, including a cross-version driver. Link this same file
 * against the frozen pre-change archive for --write-legacy / --reject-new;
 * no snapshot header or class table is rewritten to imitate an old writer. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#include <errno.h>
#else
#include <unistd.h>
#endif

#define RS "javax/microedition/rms/RecordStore"
#define RE "javax/microedition/rms/RecordEnumeration"
#define VECTOR "java/util/Vector"
#define RECORDS "nj2me/rms/Records"
#define ENUM "nj2me/rms/Enumeration"
#define HOLDER "test/RmsSnapshotRoots"
#define FILTER "test/RmsSnapshotFilter"
#define COMP "test/RmsSnapshotComparator"
#define OPEN "(Ljava/lang/String;Z)L" RS ";"
#define ENUMERATE "(Ljavax/microedition/rms/RecordFilter;Ljavax/microedition/rms/RecordComparator;Z)L" RE ";"
#define HEAP_BYTES (2u * 1024u * 1024u)
static int checks, failures;
static const nj_builtin_field holder_fields[] = {
    {"state", "[Ljava/lang/Object;", NJ_ACC_PUBLIC | NJ_ACC_STATIC},
    {"marker", "I", NJ_ACC_PUBLIC | NJ_ACC_STATIC}
};
static const nj_builtin_field callback_fields[] = {
    {"setting", "I", 0}, {"calls", "I", 0}
};
static const nj_builtin_method filter_methods[] = {{"matches", "([B)Z", NJ_ACC_PUBLIC}};
static const nj_builtin_method compare_methods[] = {{"compare", "([B[B)I", NJ_ACC_PUBLIC}};
static const nj_builtin_class classes[] = {
    {HOLDER,"java/lang/Object",NJ_ACC_PUBLIC,holder_fields,2,NULL,0},
    {FILTER,"java/lang/Object",NJ_ACC_PUBLIC,callback_fields,2,filter_methods,1},
    {COMP,"java/lang/Object",NJ_ACC_PUBLIC,callback_fields,2,compare_methods,1}
};
static void check(int ok, const char *why)
{
    ++checks;
    if (!ok) { ++failures; fprintf(stderr,"FAIL: %s\n",why); }
}
static void require(int ok, const char *why)
{
    if (!ok) { fprintf(stderr,"fixture failure: %s\n",why); exit(2); }
}
static nj_field *field(nj_vm *vm,const char *cls,const char *name,const char *desc,int stat)
{
    nj_field *f=nj_field_lookup(vm,cls,name,desc,stat);
    require(f!=NULL,"test field resolves"); return f;
}
static nj_s4 cb_int(nj_vm *vm,nj_ref cb,const char *name)
{ return nj_field_read_int(vm,cb,field(vm,cb->cls->name,name,"I",0)); }
static nj_status filter(nj_vm *vm,nj_thread *th,nj_value *args,nj_value *ret)
{
    nj_ref self=NJ_THIS(args),bytes=args[0].r;
    nj_field *calls=field(vm,FILTER,"calls","I",0);
    (void)th;
    nj_field_write_int(vm,self,calls,nj_field_read_int(vm,self,calls)+1);
    nj_gc_collect(vm);
    ret->i=bytes && bytes->length==1 && bytes->u.bytes[0]>=cb_int(vm,self,"setting");
    return NJ_OK;
}
static nj_status compare(nj_vm *vm,nj_thread *th,nj_value *args,nj_value *ret)
{
    nj_ref self=NJ_THIS(args),a=args[0].r,b=args[1].r;
    nj_field *calls=field(vm,COMP,"calls","I",0);
    int av=a && a->length?a->u.bytes[0]:0,bv=b && b->length?b->u.bytes[0]:0;
    (void)th;
    nj_field_write_int(vm,self,calls,nj_field_read_int(vm,self,calls)+1);
    nj_gc_collect(vm);
    ret->i=(av>bv?1:av<bv?-1:0)*cb_int(vm,self,"setting");
    return NJ_OK;
}
static void call(nj_vm *vm,nj_ref o,const char *cls,const char *name,
                 const char *desc,nj_value *a,nj_value *r)
{
    if(nj_call(vm,o,cls,name,desc,a,r)!=NJ_OK) {
        fprintf(stderr,"fixture call %s.%s%s failed: %s\n",cls,name,desc,nj_exception_describe(vm)); exit(2);
    }
}
static nj_s4 integer(nj_vm *vm,nj_ref o,const char *cls,const char *name)
{ nj_value r; call(vm,o,cls,name,"()I",NULL,&r); return r.i; }
static void root(nj_vm *vm,nj_ref ref)
{
    nj_u4 mark=nj_root_mark(vm);
    require(ref!=NULL,"test object allocates"); nj_root(vm,ref);
    require(nj_root_mark(vm)==mark+1,"test C root allocates");
}
static nj_ref state(nj_vm *vm)
{ return nj_field_read_ref(vm,NULL,field(vm,HOLDER,"state","[Ljava/lang/Object;",1)); }
static nj_ref seed(nj_vm *vm)
{
    nj_ref refs=nj_object_array_new(vm,"java/lang/Object",3);
    require(refs!=NULL,"root array allocates");
    nj_field_write_ref(vm,NULL,field(vm,HOLDER,"state","[Ljava/lang/Object;",1),refs);
    nj_field_write_int(vm,NULL,field(vm,HOLDER,"marker","I",1),12345);
    return refs;
}
typedef struct fixture { nj_vm *vm; nj_app *app; } fixture;
static fixture create(const nj_port *port,const nj_u1 *jar,nj_u4 len)
{
    fixture f; unsigned i;
    f.vm=nj_vm_create(port,HEAP_BYTES); require(f.vm!=NULL,"VM allocates");
    f.app=nj_app_open(f.vm,jar,len); require(f.app!=NULL,"source JAR attaches");
    for(i=0;i<sizeof(classes)/sizeof(classes[0]);++i) {
        nj_class *c=nj_class_create_builtin(f.vm,&classes[i]);
        require(c && nj_class_link(f.vm,c)==NJ_OK,"test class links");
        if(i==1)c->methods[0].native=filter;
        if(i==2)c->methods[0].native=compare;
    }
    return f;
}
static void dispose(fixture f)
{ nj_app_close(f.vm,f.app); nj_vm_destroy(f.vm); }
static nj_ref open_store(nj_vm *vm,const char *name)
{
    nj_value a[2],r;
    a[0].r=nj_string_new(vm,name); require(a[0].r!=NULL,"store name allocates"); a[1].i=1;
    require(nj_call_static(vm,RS,"openRecordStore",OPEN,a,&r)==NJ_OK && r.r,"store opens");
    return r.r;
}
static nj_s4 add(nj_vm *vm,nj_ref store,int value)
{
    nj_value a[3],r; nj_ref bytes=nj_byte_array_new(vm,1);
    require(bytes!=NULL,"record allocates"); bytes->u.bytes[0]=(nj_u1)value;
    a[0].r=bytes;a[1].i=0;a[2].i=1;
    call(vm,store,RS,"addRecord","([BII)I",a,&r);return r.i;
}
static void set(nj_vm *vm,nj_ref store,int id,int value)
{
    nj_value a[4]; nj_ref bytes=nj_byte_array_new(vm,1);
    require(bytes!=NULL,"set record allocates");bytes->u.bytes[0]=(nj_u1)value;
    a[0].i=id;a[1].r=bytes;a[2].i=0;a[3].i=1;
    call(vm,store,RS,"setRecord","(I[BII)V",a,NULL);
}
static void record(nj_vm *vm,nj_ref store,int id,int value)
{
    nj_value a,r; a.i=id;call(vm,store,RS,"getRecord","(I)[B",&a,&r);
    check(r.r && r.r->length==1 && r.r->u.bytes[0]==value,"restored record has original bytes");
}
static nj_ref enumeration(nj_vm *vm,nj_ref store,int keep)
{
    nj_value a[3],r; nj_ref fi=nj_object_new(vm,FILTER),co;
    root(vm,fi);co=nj_object_new(vm,COMP);root(vm,co);
    nj_field_write_int(vm,fi,field(vm,FILTER,"setting","I",0),15);
    nj_field_write_int(vm,co,field(vm,COMP,"setting","I",0),-1);
    a[0].r=fi;a[1].r=co;a[2].i=keep;
    call(vm,store,RS,"enumerateRecords",ENUMERATE,a,&r);require(r.r!=NULL,"enumeration allocates");return r.r;
}
static void fill(nj_vm *vm,nj_ref store)
{
    check(add(vm,store,30)==1,"first record id");
    check(add(vm,store,10)==2,"second record id");
    check(add(vm,store,20)==3,"third record id");
}
static nj_ref records(nj_vm *vm,nj_ref store)
{ return nj_field_read_ref(vm,store,field(vm,RS,"rsRecords","Ljava/util/Vector;",0)); }
/* This is the exact legacy object shape, without relabelling/enlarging an
 * object. It keeps the ordinary CTest independent of an external old binary. */
static nj_ref base_store(nj_vm *vm)
{
    nj_ref rs=nj_object_new(vm,RS),v,name;
    root(vm,rs);name=nj_string_new(vm,"legacy");root(vm,name);
    nj_field_write_ref(vm,rs,field(vm,RS,"rsName","Ljava/lang/String;",0),name);
    v=nj_object_new(vm,VECTOR);root(vm,v);
    nj_field_write_ref(vm,rs,field(vm,RS,"rsRecords","Ljava/util/Vector;",0),v);
    v=nj_object_new(vm,VECTOR);root(vm,v);
    nj_field_write_ref(vm,rs,field(vm,RS,"rsIds","Ljava/util/Vector;",0),v);
    nj_field_write_int(vm,rs,field(vm,RS,"rsOpen","Z",0),1);
    nj_field_write_int(vm,rs,field(vm,RS,"rsNextId","I",0),1);
    fill(vm,rs);return rs;
}
static nj_u4 hash_file(FILE *f)
{
    unsigned char b[512];size_t n,i;nj_u4 h=2166136261u;
    rewind(f);while((n=fread(b,1,sizeof(b),f))!=0)for(i=0;i<n;++i)h=(h^b[i])*16777619u;
    require(!ferror(f),"hash snapshot");rewind(f);return h;
}
static void save(nj_vm *vm,FILE *f)
{
    nj_root_release(vm,0);check(nj_vm_can_save(vm),"state has no active native/C roots");
    require(nj_vm_save(vm,f)==NJ_OK,"snapshot saves");
    require(fflush(f)==0,"snapshot flushes");rewind(f);
}
static void fresh(nj_vm *vm)
{
    nj_ref exc=NULL,midlet=nj_vm_new_midlet(vm,"FieldDemo",&exc);int i;
    check(midlet && !exc,"target can construct a fresh MIDlet after load decision");if(!midlet)return;
    check(nj_vm_start_app(vm,midlet)==NJ_OK,"fresh MIDlet starts");
    for(i=0;!nj_midp_current(vm) && i<90;++i)(void)nj_midp_frame(vm);
    check(nj_midp_current(vm) && !vm->exception,"fresh MIDlet reaches display");
}
static void shared_close(nj_vm *vm,nj_ref rs)
{
    call(vm,rs,RS,"closeRecordStore","()V",NULL,NULL);
    check(integer(vm,rs,RS,"getNumRecords")>=3,"first shared close keeps store usable");
    call(vm,rs,RS,"closeRecordStore","()V",NULL,NULL);
    check(nj_call(vm,rs,RS,"getNumRecords","()I",NULL,NULL)!=NJ_OK && vm->exception,
          "last shared close actually closes store");nj_clear_exception(vm);
}
static void sequence(nj_vm *vm,nj_ref en,const int *ids,int n)
{
    int i;check(integer(vm,en,RE,"numRecords")==n,"enumeration record count");
    call(vm,en,RE,"reset","()V",NULL,NULL);
    for(i=0;i<n;++i)check(integer(vm,en,RE,"nextRecordId")==ids[i],"filter and comparator preserve sorted ids");
}
static void legacy_loaded(nj_vm *vm,int rms)
{
    nj_ref refs=state(vm),rs,en,again,old,old_enum;
    require(refs && refs->length==3,"legacy root graph restores");
    check(nj_field_read_int(vm,NULL,field(vm,HOLDER,"marker","I",1))==12345,"legacy static marker restores");
    if(!rms){check(refs->u.refs[0] && nj_string_equals_utf8(refs->u.refs[0],"ordinary"),"ordinary state String restores");fresh(vm);return;}
    rs=refs->u.refs[0];require(rs && strcmp(rs->cls->name,RS)==0,"legacy RS really has base shape");
    old=records(vm,rs);check(old && strcmp(old->cls->name,VECTOR)==0,"legacy records really use original Vector");
    nj_gc_collect(vm);record(vm,rs,1,30);record(vm,rs,2,10);record(vm,rs,3,20);
    old_enum=refs->u.refs[1];
    if(old_enum){
        check(strcmp(old_enum->cls->name,RE)==0,"old snapshot really contains original base Enumeration");
        check(integer(vm,old_enum,RE,"nextRecordId")==3,"legacy enumeration cursor and ids restore");
    }
    en=enumeration(vm,rs,1);refs->u.refs[2]=en;
    check(strcmp(en->cls->name,ENUM)==0,"old base RS creates new live Enumeration");
    check(records(vm,rs)!=old && strcmp(records(vm,rs)->cls->name,RECORDS)==0,"old Vector lazily upgrades without replacing RS");
    nj_root_release(vm,0);nj_gc_collect(vm);
    check(add(vm,rs,25)==4,"mutate old RS after lazy attachment");
    {const int ids[]={1,4,3};sequence(vm,en,ids,3);}
    if(old_enum){
        const int ids[]={1,3};
        call(vm,old_enum,RE,"rebuild","()V",NULL,NULL);
        sequence(vm,old_enum,ids,2); /* Old binaries never retained callbacks. */
    }
    again=open_store(vm,"legacy");check(again==rs,"reopen returns same legacy RS identity");
    check(integer(vm,again,RS,"getNumRecords")==4,"shared open reads current memory records");
    shared_close(vm,rs);
}
static void roundtrip(const nj_port *port,const nj_u1 *jar,nj_u4 len,FILE *external)
{
    fixture s=create(port,jar,len),t;nj_vm *vm=s.vm;
    nj_ref refs=seed(vm),rs=open_store(vm,"modern"),live,frozen,again;FILE *f=external?external:tmpfile();
    require(f!=NULL,"snapshot stream opens");refs->u.refs[0]=rs;fill(vm,rs);
    live=enumeration(vm,rs,1);refs->u.refs[1]=live;
    frozen=enumeration(vm,rs,0);refs->u.refs[2]=frozen;
    check(integer(vm,live,RE,"nextRecordId")==1,"source live cursor advances before save");
    check(integer(vm,frozen,RE,"nextRecordId")==1,"source frozen cursor advances before save");
    again=open_store(vm,"modern");check(again==rs,"source shared-open identity");
    nj_root_release(vm,0);nj_gc_collect(vm);save(vm,f);
    if(external){dispose(s);return;}
    t=create(port,jar,len);check(nj_vm_load(t.vm,f)==NJ_OK,"new RMS snapshot restores");vm=t.vm;refs=state(vm);
    require(refs && refs->length==3,"new root graph restores");
    rs=refs->u.refs[0];live=refs->u.refs[1];frozen=refs->u.refs[2];
    refs->u.refs[1]=NULL; /* Live enumeration is now reachable only from registry. */
    nj_gc_collect(vm);
    check(nj_heap_owns_object(vm,live),"restored registry alone retains a kept enumeration across GC");
    if(!nj_heap_owns_object(vm,live))goto done;
    check(records(vm,rs) && strcmp(records(vm,rs)->cls->name,RECORDS)==0,"Records metadata survives restore and GC");
    check(integer(vm,live,RE,"nextRecordId")==3,"restored live cursor position persists");
    check(integer(vm,frozen,RE,"nextRecordId")==3,"restored frozen cursor position persists");
    {nj_value r;call(vm,live,RE,"isKeptUpdated","()Z",NULL,&r);check(r.i==1,"live keep flag restores");
     call(vm,frozen,RE,"isKeptUpdated","()Z",NULL,&r);check(r.i==0,"frozen keep flag restores");}
    record(vm,rs,1,30);record(vm,rs,2,10);record(vm,rs,3,20);
    set(vm,rs,2,40);check(add(vm,rs,25)==4,"restored record IDs continue monotonically");nj_gc_collect(vm);
    {const int ids[]={2,1,4,3},oldids[]={1,3};sequence(vm,live,ids,4);sequence(vm,frozen,oldids,2);
     call(vm,frozen,RE,"rebuild","()V",NULL,NULL);sequence(vm,frozen,ids,4);}
    /* Only the enum fields can retain these callback objects after C roots
     * were released before save. A rebuild/notification calls System.gc(). */
    call(vm,live,RE,"destroy","()V",NULL,NULL);call(vm,frozen,RE,"destroy","()V",NULL,NULL);
    nj_gc_collect(vm);
    check(!nj_heap_owns_object(vm,live),"destroy removes registry strong root and allows collection");
    check(add(vm,rs,50)==5,"destroyed registry entries do not break later mutations");
    shared_close(vm,rs);
done:
    fclose(f);dispose(t);dispose(s);
}
static void reject(const nj_port *port,const nj_u1 *jar,nj_u4 len,FILE *f)
{
    fixture t=create(port,jar,len);nj_vm *vm=t.vm;nj_ref sentinel=seed(vm);
    nj_u1 *base=vm->heap.base;nj_size bump=vm->heap.bump;nj_thread *cur=vm->cur;nj_u4 hash=hash_file(f);
    check(nj_vm_load(vm,f)==NJ_ERR_FORMAT,"old reader rejects new internal classes");
    check(vm->heap.base==base && vm->heap.bump==bump && state(vm)==sentinel,"refusal preserves arena and static root");
    check(vm->cur==cur && nj_field_read_int(vm,NULL,field(vm,HOLDER,"marker","I",1))==12345,
          "refusal preserves thread and scalar static");
    check(hash_file(f)==hash,"reader does not modify snapshot bytes");fresh(vm);dispose(t);
}
static void internal_layout(nj_vm *vm)
{
    const char *names[]={RECORDS,ENUM},*parents[]={VECTOR,RE};unsigned j;
    for(j=0;j<2;++j){
        nj_class *c=nj_class_find(vm,names[j]);nj_u2 i;
        require(c && c->super,"new internal class and parent exist");
        check(strcmp(c->super->name,parents[j])==0,"internal class extends original public base");
        check(c->method_count==0,"internal class inherits original native methods");
        for(i=0;i<c->field_count;++i){
            check(!(c->fields[i].flags & NJ_ACC_STATIC),"new metadata adds no static slots");
            check(c->fields[i].offset>=c->super->instance_size,"new metadata follows all inherited instance fields");
        }
    }
}
static void dump_abi(nj_vm *vm)
{
    const char *names[]={RS,RE,VECTOR};unsigned j;nj_u2 i;
    printf("ABI nstatics %u\n",(unsigned)vm->nstatics);
    for(j=0;j<3;++j){
        nj_class *c=nj_class_find(vm,names[j]);require(c!=NULL,"public RMS class exists");
        printf("ABI class %s %u %u %u\n",c->name,(unsigned)c->instance_size,
               (unsigned)c->field_count,(unsigned)c->method_count);
        for(i=0;i<c->field_count;++i){
            nj_field *f=&c->fields[i];
            printf("ABI field %s %s %s %u %u %u %u\n",c->name,f->name,f->desc,
                   (unsigned)f->flags,(unsigned)f->offset,(unsigned)f->ptype,(unsigned)f->width);
        }
        for(i=0;i<c->method_count;++i){
            nj_method *m=&c->methods[i];
            printf("ABI method %s %s %s %u %u %u %u %u\n",c->name,m->name,m->desc,
                   (unsigned)m->flags,(unsigned)m->max_locals,(unsigned)m->max_stack,
                   (unsigned)m->arg_slots,(unsigned)m->ret_ptype);
        }
    }
    if(nj_class_find(vm,RECORDS))internal_layout(vm);
}
int main(int argc,char **argv)
{
    FILE *jf=fopen("tests/fixtures/fielddemo.jar","rb"),*f;nj_u1 *jar;long len;
    nj_port port;char temp[1024];const char *tmp=getenv("NJ_RMS_SNAPSHOT_TMP");
    require(jf!=NULL,"fielddemo source JAR exists");
    require(fseek(jf,0,SEEK_END)==0 && (len=ftell(jf))>0 && fseek(jf,0,SEEK_SET)==0,"size source JAR");
    jar=(nj_u1*)malloc((size_t)len);require(jar!=NULL,"source JAR buffer allocates");
    require(fread(jar,1,(size_t)len,jf)==(size_t)len,"source JAR reads");fclose(jf);
#if defined(_WIN32)
    {
        unsigned i;
        if(!tmp)tmp=getenv("TEMP");
        if(!tmp)tmp=".";
        for(i=0;i<1000;++i){
            require(snprintf(temp,sizeof(temp),"%s/nj-save-rms-%u-%u",tmp,(unsigned)_getpid(),i)<(int)sizeof(temp),"temporary path fits");
            if(_mkdir(temp)==0)break;
            require(errno==EEXIST,"isolated RMS directory creates");
        }
        require(i<1000,"unique RMS directory creates");
    }
#else
    require(snprintf(temp,sizeof(temp),"%s/nj-save-rms-XXXXXX",tmp?tmp:"/tmp")<(int)sizeof(temp),"temporary path fits");
    require(mkdtemp(temp)!=NULL,"isolated RMS directory creates");
#endif
    require(nj_port_headless_ex(&port,64,64,0,NULL)==NJ_OK,"headless port opens");port.rms_dir=temp;
    if(argc==1){
        fixture s=create(&port,jar,(nj_u4)len),t;nj_ref refs=seed(s.vm);f=tmpfile();require(f!=NULL,"legacy-shaped stream");
        internal_layout(s.vm);refs->u.refs[0]=base_store(s.vm);save(s.vm,f);t=create(&port,jar,(nj_u4)len);
        check(nj_vm_load(t.vm,f)==NJ_OK,"base RS object graph roundtrips");legacy_loaded(t.vm,1);
        fclose(f);dispose(t);dispose(s);roundtrip(&port,jar,(nj_u4)len,NULL);
    }else if(argc==2 && strcmp(argv[1],"--abi")==0){
        fixture t=create(&port,jar,(nj_u4)len);dump_abi(t.vm);dispose(t);
    }else if(argc==4 && strcmp(argv[1],"--write-legacy")==0){
        fixture s=create(&port,jar,(nj_u4)len);nj_ref refs=seed(s.vm);int rms=strcmp(argv[2],"rms")==0;
        if(rms){
            refs->u.refs[0]=open_store(s.vm,"legacy");fill(s.vm,refs->u.refs[0]);
            refs->u.refs[1]=enumeration(s.vm,refs->u.refs[0],1);
            check(integer(s.vm,refs->u.refs[1],RE,"nextRecordId")==1,"real old enum cursor advances before snapshot");
        }
        else refs->u.refs[0]=nj_string_new(s.vm,"ordinary");
        f=fopen(argv[3],"wb");require(f!=NULL,"old snapshot output opens");save(s.vm,f);fclose(f);dispose(s);
    }else if(argc==4 && strcmp(argv[1],"--read-legacy")==0){
        fixture t=create(&port,jar,(nj_u4)len);f=fopen(argv[3],"rb");require(f!=NULL,"real old snapshot opens");
        check(nj_vm_load(t.vm,f)==NJ_OK,"real f56 snapshot loads into new reader");legacy_loaded(t.vm,strcmp(argv[2],"rms")==0);
        fclose(f);dispose(t);
    }else if(argc==3 && strcmp(argv[1],"--write-new-plain")==0){
        fixture s=create(&port,jar,(nj_u4)len);nj_ref refs=seed(s.vm);
        refs->u.refs[0]=nj_string_new(s.vm,"ordinary");
        f=fopen(argv[2],"wb");require(f!=NULL,"new ordinary snapshot output opens");
        save(s.vm,f);fclose(f);dispose(s);
    }else if(argc==3 && strcmp(argv[1],"--write-new")==0){
        f=fopen(argv[2],"wb");require(f!=NULL,"new snapshot output opens");roundtrip(&port,jar,(nj_u4)len,f);fclose(f);
    }else if(argc==3 && strcmp(argv[1],"--reject-new")==0){
        f=fopen(argv[2],"rb");require(f!=NULL,"new snapshot input opens");reject(&port,jar,(nj_u4)len,f);fclose(f);
    }else{fprintf(stderr,"usage: %s [--abi | --write-legacy plain|rms file | --read-legacy plain|rms file | --write-new file | --write-new-plain file | --reject-new file]\n",argv[0]);return 2;}
    nj_port_shutdown(&port);free(jar);printf("%d checks, %d failures (RMS: %s)\n",checks,failures,temp);return failures?1:0;
}
