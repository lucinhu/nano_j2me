/*
 * nano_j2me - nj_class.c
 *
 * Java class file parser, class table, linker and constant-pool resolver.
 *
 * The parser is a straightforward one-pass reader over the class bytes with
 * bounds checking on every field.  Game JARs are untrusted input, so a
 * malformed class must produce a clean error and never a wild read.
 *
 * Class bytes are kept alive for the lifetime of the class because the
 * constant pool is resolved *lazily* (a J2ME JAR may legitimately reference
 * optional APIs that this runtime does not implement; failing only when the
 * code path is actually reached is far friendlier than refusing to load).
 */
#include "nj_internal.h"

#if NJ_USE_LIBC
#  include <string.h>
#endif

#define NJ_CP_OFF(x) ((x) >> 4)
#define NJ_CP_TAG(x) ((x) & 0xFu)

#define NJ_CLASS_MAGIC 0xCAFEBABEu

/* ------------------------------------------------------------------ */
/* Bounds-checked byte reader                                          */
/* ------------------------------------------------------------------ */
typedef struct nj_rd {
    const nj_u1 *p;
    nj_u4        len;
    nj_u4        off;
    int          err;
} nj_rd;

static void rd_init(nj_rd *r, const nj_u1 *p, nj_u4 len)
{
    r->p = p;
    r->len = len;
    r->off = 0u;
    r->err = 0;
}

static int rd_need(nj_rd *r, nj_u4 n)
{
    if (r->err || n > r->len || r->off > r->len - n) {
        r->err = 1;
        return 0;
    }
    return 1;
}

static nj_u1 rd_u1(nj_rd *r)
{
    if (!rd_need(r, 1u)) {
        return 0u;
    }
    return r->p[r->off++];
}

static nj_u2 rd_u2(nj_rd *r)
{
    nj_u2 v;
    if (!rd_need(r, 2u)) {
        return 0u;
    }
    v = (nj_u2)(((nj_u2)r->p[r->off] << 8) | (nj_u2)r->p[r->off + 1u]);
    r->off += 2u;
    return v;
}

static nj_u4 rd_u4(nj_rd *r)
{
    nj_u4 v;
    if (!rd_need(r, 4u)) {
        return 0u;
    }
    v = ((nj_u4)r->p[r->off] << 24) | ((nj_u4)r->p[r->off + 1u] << 16) |
        ((nj_u4)r->p[r->off + 2u] << 8) | (nj_u4)r->p[r->off + 3u];
    r->off += 4u;
    return v;
}

static void rd_skip(nj_rd *r, nj_u4 n)
{
    if (rd_need(r, n)) {
        r->off += n;
    }
}

static const nj_u1 *rd_ptr(nj_rd *r, nj_u4 n)
{
    const nj_u1 *q;
    if (!rd_need(r, n)) {
        return NULL;
    }
    q = r->p + r->off;
    r->off += n;
    return q;
}

/* ------------------------------------------------------------------ */
/* Descriptor helpers                                                  */
/* ------------------------------------------------------------------ */
nj_u1 nj_desc_ptype(const char *desc, nj_u1 *width)
{
    nj_u1 w = 1u;
    nj_u1 t = NJ_PT_OBJECT;

    if (!desc || !*desc) {
        if (width) { *width = 1u; }
        return NJ_PT_VOID;
    }
    switch (*desc) {
    case 'V': t = NJ_PT_VOID;    break;
    case 'Z': t = NJ_PT_BOOLEAN; break;
    case 'C': t = NJ_PT_CHAR;    break;
    case 'B': t = NJ_PT_BYTE;    break;
    case 'S': t = NJ_PT_SHORT;   break;
    case 'I': t = NJ_PT_INT;     break;
    case 'J': t = NJ_PT_LONG;    w = 2u; break;
    case 'F': t = NJ_PT_FLOAT;   break;
    case 'D': t = NJ_PT_DOUBLE;  w = 2u; break;
    case 'L':
    case '[': t = NJ_PT_OBJECT;  break;
    default:  t = NJ_PT_OBJECT;  break;
    }
    if (width) { *width = w; }
    return t;
}

nj_u2 nj_desc_arg_slots(const char *desc)
{
    nj_u2 slots = 0u;
    const char *p = desc;

    if (!p || *p != '(') {
        return 0u;
    }
    p++;
    while (*p && *p != ')') {
        if (*p == '[') {
            while (*p == '[') { p++; }
            if (*p == 'L') {
                while (*p && *p != ';') { p++; }
            }
            if (*p) { p++; }        /* consume ';' or the element type char */
            slots++;
        } else if (*p == 'L') {
            while (*p && *p != ';') { p++; }
            if (*p) { p++; }
            slots++;
        } else if (*p == 'J' || *p == 'D') {
            slots = (nj_u2)(slots + 2u);
            p++;
        } else {
            slots++;
            p++;
        }
    }
    return slots;
}

nj_u1 nj_desc_ret_ptype(const char *desc)
{
    const char *p = desc;
    if (!p || *p != '(') {
        return NJ_PT_VOID;
    }
    while (*p && *p != ')') { p++; }
    if (*p != ')') {
        return NJ_PT_VOID;
    }
    p++;
    if (*p == '[') {
        return NJ_PT_OBJECT;
    }
    {
        nj_u1 w = 1u;
        return nj_desc_ptype(p, &w);
    }
}

/* ------------------------------------------------------------------ */
/* Class table                                                         */
/* ------------------------------------------------------------------ */
static nj_u4 name_hash(const char *s)
{
    nj_u4 h = 2166136261u;
    while (s && *s) {
        h ^= (nj_u1)*s++;
        h *= 16777619u;
    }
    return h;
}

/* Normalise a dotted name ("java.lang.String") into internal form. */
static void normalise_name(const char *in, char *out, nj_u4 cap)
{
    nj_u4 i = 0u;
    if (!cap) {
        return;
    }
    while (in && *in && i + 1u < cap) {
        out[i++] = (*in == '.') ? '/' : *in;
        in++;
    }
    out[i] = '\0';
}

nj_class *nj_class_find(nj_vm *vm, const char *internal_name)
{
    nj_u4 h;
    nj_class *c;
    char buf[256];

    if (!vm || !internal_name) {
        return NULL;
    }
    if (strchr(internal_name, '.')) {
        normalise_name(internal_name, buf, (nj_u4)sizeof(buf));
    } else {
        nj_u4 i = 0u;
        while (internal_name[i] && i + 1u < (nj_u4)sizeof(buf)) {
            buf[i] = internal_name[i];
            i++;
        }
        buf[i] = '\0';
    }

    h = name_hash(buf) & (NJ_CLASS_HASH - 1u);
    for (c = vm->class_head[h]; c; c = c->next_hash) {
        if (strcmp(c->name, buf) == 0) {
            return c;
        }
    }
    return NULL;
}

static nj_class *class_new(nj_vm *vm, const char *internal_name)
{
    nj_class *c;
    nj_size nlen;

    if (vm->nclasses >= (nj_u2)NJ_MAX_CLASSES) {
        return NULL;
    }
    c = (nj_class *)nj_host_alloc(vm, sizeof(nj_class));
    if (!c) {
        return NULL;
    }
    nlen = (nj_size)strlen(internal_name) + 1u;
    c->name = (const char *)nj_host_alloc(vm, nlen);
    if (!c->name) {
        nj_host_free(vm, c);
        return NULL;
    }
    memcpy((char *)(uintptr_t)c->name, internal_name, nlen);

    c->id = vm->next_class_id++;
    c->state = 0u;
    c->super = NULL;
    c->next_hash = vm->class_head[name_hash(c->name) & (NJ_CLASS_HASH - 1u)];
    vm->class_head[name_hash(c->name) & (NJ_CLASS_HASH - 1u)] = c;

    if (vm->nclasses < (nj_u2)NJ_MAX_CLASSES) {
        vm->classes[vm->nclasses++] = c;
    }
    return c;
}

const char *nj_intern_cstr(nj_vm *vm, const char *s)
{
    /* Class bytes and member names outlive every reference to them, so the
     * "interned" string is simply the input.  Kept as a function so the
     * indirection can change later without touching callers. */
    (void)vm;
    return s;
}

/* ------------------------------------------------------------------ */
/* Constant pool helpers                                               */
/* ------------------------------------------------------------------ */
const char *nj_cp_utf8(nj_class *c, nj_u2 index)
{
    if (!c || !c->cp_utf8 || index == 0u || index >= c->cp_count) {
        return NULL;
    }
    return c->cp_utf8[index];
}

static nj_u4 cp_off(nj_class *c, nj_u2 index)
{
    if (!c || !c->cp_info || index == 0u || index >= c->cp_count) {
        return 0u;
    }
    return NJ_CP_OFF(c->cp_info[index]);
}

static nj_u2 cp_u2(nj_class *c, nj_u4 off)
{
    if (off == 0u || off + 2u > c->data_len) {
        return 0u;
    }
    return (nj_u2)(((nj_u2)c->data[off] << 8) | (nj_u2)c->data[off + 1u]);
}

/* Class name referenced by a CONSTANT_Class entry. */
const char *nj_cp_class_name(nj_class *c, nj_u2 class_index)
{
    nj_u4 off = cp_off(c, class_index);
    nj_u2 name_idx;
    if (off == 0u || NJ_CP_TAG(c->cp_info[class_index]) != NJ_CP_CLASS) {
        return NULL;
    }
    name_idx = cp_u2(c, off);
    return nj_cp_utf8(c, name_idx);
}

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */
static nj_status parse_constant_pool(nj_vm *vm, nj_class *c, nj_rd *r)
{
    nj_u2 count = rd_u2(r);
    nj_u2 i;
    nj_u4 off;
    const char **utfs;
    nj_u4 *info;

    if (r->err || count < 1u) {
        return NJ_ERR_FORMAT;
    }
    c->cp_count = count;
    info = (nj_u4 *)nj_host_alloc(vm, (nj_size)count * sizeof(nj_u4));
    utfs = (const char **)nj_host_alloc(vm, (nj_size)count * sizeof(char *));
    if (!info || !utfs) {
        nj_host_free(vm, info);
        nj_host_free(vm, utfs);
        return NJ_ERR_NOMEM;
    }
    c->cp_info = info;
    c->cp_utf8 = utfs;

    for (i = 1u; i < count; i++) {
        nj_u1 tag = rd_u1(r);
        nj_u2 cur = i;   /* the entry's own index: long/double advance i below */
        off = r->off;    /* points just past the tag: the entry payload */
        if (r->err) {
            return NJ_ERR_FORMAT;
        }
        switch (tag) {
        case NJ_CP_UTF8: {
            nj_u2 len = rd_u2(r);
            const nj_u1 *bytes = rd_ptr(r, len);
            char *s;
            if (!bytes) {
                return NJ_ERR_FORMAT;
            }
            s = (char *)nj_host_alloc(vm, (nj_size)len + 1u);
            if (!s) {
                return NJ_ERR_NOMEM;
            }
            memcpy(s, bytes, len);
            s[len] = '\0';
            utfs[i] = s;
            break;
        }
        case NJ_CP_INTEGER:
        case NJ_CP_FLOAT:
            rd_skip(r, 4u);
            break;
        case NJ_CP_LONG:
        case NJ_CP_DOUBLE:
            rd_skip(r, 8u);
            i++;    /* 8-byte constants occupy two pool slots */
            break;
        case NJ_CP_CLASS:
        case NJ_CP_STRING:
            rd_skip(r, 2u);
            break;
        case NJ_CP_FIELDREF:
        case NJ_CP_METHODREF:
        case NJ_CP_IFACEMETHODREF:
        case NJ_CP_NAMEANDTYPE:
            rd_skip(r, 4u);
            break;
        default:
            return NJ_ERR_FORMAT;
        }
        if (r->err) {
            return NJ_ERR_FORMAT;
        }
        info[cur] = (off << 4) | (nj_u4)tag;
    }
    return NJ_OK;
}

static nj_status parse_fields(nj_vm *vm, nj_class *c, nj_rd *r)
{
    nj_u2 count = rd_u2(r);
    nj_u2 i;

    if (r->err) {
        return NJ_ERR_FORMAT;
    }
    if (count == 0u) {
        return NJ_OK;
    }
    c->fields = (nj_field *)nj_host_alloc(vm, (nj_size)count * sizeof(nj_field));
    if (!c->fields) {
        return NJ_ERR_NOMEM;
    }
    for (i = 0u; i < count; i++) {
        nj_field *f = &c->fields[i];
        nj_u2 name_idx, desc_idx, attr_count, a;
        nj_u1 w = 1u;

        f->flags = rd_u2(r);
        name_idx = rd_u2(r);
        desc_idx = rd_u2(r);
        attr_count = rd_u2(r);
        if (r->err) {
            return NJ_ERR_FORMAT;
        }
        f->name = nj_cp_utf8(c, name_idx);
        f->desc = nj_cp_utf8(c, desc_idx);
        f->owner = c;
        f->ptype = nj_desc_ptype(f->desc, &w);
        f->width = w;
        f->offset = 0u;
        if (!f->name || !f->desc) {
            return NJ_ERR_FORMAT;
        }
        for (a = 0u; a < attr_count; a++) {
            nj_u2 an = rd_u2(r);
            nj_u4 alen = rd_u4(r);
            (void)an;
            rd_skip(r, alen);
            if (r->err) {
                return NJ_ERR_FORMAT;
            }
        }
        c->field_count++;
    }
    return NJ_OK;
}

static nj_status parse_methods(nj_vm *vm, nj_class *c, nj_rd *r)
{
    nj_u2 count = rd_u2(r);
    nj_u2 i;

    if (r->err) {
        return NJ_ERR_FORMAT;
    }
    if (count == 0u) {
        return NJ_OK;
    }
    c->methods = (nj_method *)nj_host_alloc(vm, (nj_size)count * sizeof(nj_method));
    if (!c->methods) {
        return NJ_ERR_NOMEM;
    }
    for (i = 0u; i < count; i++) {
        nj_method *m = &c->methods[i];
        nj_u2 name_idx, desc_idx, attr_count, a;

        memset(m, 0, sizeof(*m));
        m->flags = rd_u2(r);
        name_idx = rd_u2(r);
        desc_idx = rd_u2(r);
        attr_count = rd_u2(r);
        if (r->err) {
            return NJ_ERR_FORMAT;
        }
        m->name = nj_cp_utf8(c, name_idx);
        m->desc = nj_cp_utf8(c, desc_idx);
        m->owner = c;
        m->arg_slots = nj_desc_arg_slots(m->desc);
        m->ret_ptype = nj_desc_ret_ptype(m->desc);
        if (!m->name || !m->desc) {
            return NJ_ERR_FORMAT;
        }

        for (a = 0u; a < attr_count; a++) {
            nj_u2 an = rd_u2(r);
            nj_u4 alen = rd_u4(r);
            const char *aname;
            nj_u4 next;

            if (r->err) {
                return NJ_ERR_FORMAT;
            }
            next = r->off + alen;
            if (next > r->len || next < r->off) {
                return NJ_ERR_FORMAT;
            }
            aname = nj_cp_utf8(c, an);
            if (aname && strcmp(aname, "Code") == 0) {
                nj_u2 ex_count, ea;
                nj_u4 code_len;
                const nj_u1 *code;

                m->max_stack = rd_u2(r);
                m->max_locals = rd_u2(r);
                code_len = rd_u4(r);
                code = rd_ptr(r, code_len);
                if (!code || r->err) {
                    return NJ_ERR_FORMAT;
                }
                m->code = code;
                m->code_len = code_len;

                ex_count = rd_u2(r);
                if (r->err) {
                    return NJ_ERR_FORMAT;
                }
                if (ex_count > 0u) {
                    nj_handler *hs = (nj_handler *)nj_host_alloc(
                        vm, (nj_size)ex_count * sizeof(nj_handler));
                    if (!hs) {
                        return NJ_ERR_NOMEM;
                    }
                    for (ea = 0u; ea < ex_count; ea++) {
                        hs[ea].start_pc = rd_u2(r);
                        hs[ea].end_pc = rd_u2(r);
                        hs[ea].handler_pc = rd_u2(r);
                        hs[ea].catch_cp = rd_u2(r);
                    }
                    m->handlers = hs;
                    m->handler_count = ex_count;
                }
                /* Nested Code attributes are irrelevant here. */
            }
            r->off = next;
            if (r->err) {
                return NJ_ERR_FORMAT;
            }
        }
        c->method_count++;
    }
    return NJ_OK;
}

nj_class *nj_class_load(nj_vm *vm, const char *internal_name,
                        const nj_u1 *bytes, nj_u4 len)
{
    nj_class *c;
    nj_rd r;
    nj_u4 magic;
    nj_u1 *copy;
    nj_status st;

    if (!vm || !internal_name || !bytes || len < 16u) {
        return NULL;
    }
    c = nj_class_find(vm, internal_name);
    if (c && c->state >= NJ_CLASS_LOADED) {
        return c;
    }
    if (!c) {
        c = class_new(vm, internal_name);
        if (!c) {
            return NULL;
        }
    }

    copy = (nj_u1 *)nj_host_alloc(vm, len);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, bytes, len);
    c->data = copy;
    c->data_len = len;

    rd_init(&r, copy, len);
    magic = rd_u4(&r);
    if (magic != NJ_CLASS_MAGIC || r.err) {
        return NULL;
    }
    rd_skip(&r, 4u);            /* minor + major version */

    if (parse_constant_pool(vm, c, &r) != NJ_OK) {
        return NULL;
    }
    c->data_off = 0u;

    c->flags = rd_u2(&r);
    c->this_cp = rd_u2(&r);
    c->super_cp = rd_u2(&r);
    if (r.err) {
        return NULL;
    }

    {
        nj_u2 icount = rd_u2(&r);
        nj_u2 k;
        if (r.err || icount > 256u) {
            return NULL;
        }
        c->ninterfaces = icount;
        if (icount > 0u) {
            c->interfaces = (nj_class **)nj_host_alloc(
                vm, (nj_size)icount * sizeof(nj_class *));
            c->iface_cp = (nj_u2 *)nj_host_alloc(
                vm, (nj_size)icount * sizeof(nj_u2));
            if (!c->interfaces || !c->iface_cp) {
                return NULL;
            }
            for (k = 0u; k < icount; k++) {
                c->interfaces[k] = NULL;   /* resolved at link time */
                c->iface_cp[k] = rd_u2(&r);
            }
        }
    }

    st = parse_fields(vm, c, &r);
    if (st != NJ_OK) {
        return NULL;
    }
    st = parse_methods(vm, c, &r);
    if (st != NJ_OK) {
        return NULL;
    }

    /* class attributes: skipped in full */
    {
        nj_u2 acount = rd_u2(&r);
        nj_u2 a;
        for (a = 0u; a < acount; a++) {
            rd_skip(&r, 2u);
            {
                nj_u4 alen = rd_u4(&r);
                rd_skip(&r, alen);
            }
        }
        if (r.err) {
            return NULL;
        }
    }

    c->state = NJ_CLASS_LOADED;
    return c;
}

/* ================================================================== */
/* PART 2: class table resolution, linking, member lookup, CP resolver */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* Array classes and primitive mirrors                                 */
/* ------------------------------------------------------------------ */
typedef struct nj_primdef {
    const char *name;   /* as it appears in a descriptor */
    nj_u1       kind;   /* NJ_ARRAY_T_* */
    nj_u1       prim;   /* nj_ptype */
} nj_primdef;

static const nj_primdef nj_prims[] = {
    { "Z", NJ_ARRAY_T_BOOLEAN, NJ_PT_BOOLEAN },
    { "B", NJ_ARRAY_T_BYTE,    NJ_PT_BYTE    },
    { "C", NJ_ARRAY_T_CHAR,    NJ_PT_CHAR    },
    { "S", NJ_ARRAY_T_SHORT,   NJ_PT_SHORT   },
    { "I", NJ_ARRAY_T_INT,     NJ_PT_INT     },
    { "J", NJ_ARRAY_T_LONG,    NJ_PT_LONG    },
    { "F", NJ_ARRAY_T_FLOAT,   NJ_PT_FLOAT   },
    { "D", NJ_ARRAY_T_DOUBLE,  NJ_PT_DOUBLE  }
};
#define NJ_NPRIMS ((int)(sizeof(nj_prims) / sizeof(nj_prims[0])))

/* The nj_class standing in for a primitive type (used by Class mirrors). */
static nj_class *primitive_class(nj_vm *vm, const char *desc)
{
    int i;
    for (i = 0; i < NJ_NPRIMS; i++) {
        if (nj_prims[i].name[0] == desc[0]) {
            char full[16];
            nj_class *c;
            full[0] = '\0';
            switch (nj_prims[i].prim) {
            case NJ_PT_BOOLEAN: memcpy(full, "boolean", 8u); break;
            case NJ_PT_BYTE:    memcpy(full, "byte", 5u);    break;
            case NJ_PT_CHAR:    memcpy(full, "char", 5u);    break;
            case NJ_PT_SHORT:   memcpy(full, "short", 6u);   break;
            case NJ_PT_INT:     memcpy(full, "int", 4u);     break;
            case NJ_PT_LONG:    memcpy(full, "long", 5u);    break;
            case NJ_PT_FLOAT:   memcpy(full, "float", 6u);   break;
            default:            memcpy(full, "double", 7u);  break;
            }
            c = nj_class_find(vm, full);
            if (!c) {
                c = class_new(vm, full);
                if (!c) {
                    return NULL;
                }
                c->prim = nj_prims[i].prim;
                c->instance_size = (nj_u2)sizeof(nj_object);
                c->state = NJ_CLASS_LINKED;
            }
            return c;
        }
    }
    return NULL;
}

/*
 * Build (or fetch) the nj_class for an array whose descriptor is `desc`
 * (e.g. "[I", "[[Ljava/lang/String;").
 */
static nj_class *array_class(nj_vm *vm, const char *desc)
{
    nj_class *c = nj_class_find(vm, desc);
    nj_class *comp;
    const char *elem = desc + 1;

    if (c && c->is_array) {
        return c;
    }
    c = class_new(vm, desc);
    if (!c) {
        return NULL;
    }
    c->is_array = 1u;
    c->flags = NJ_ACC_PUBLIC | NJ_ACC_FINAL;

    if (*elem == '[') {
        comp = array_class(vm, elem);
    } else if (*elem == 'L') {
        char buf[256];
        nj_u4 n = 0u;
        const char *p = elem + 1;
        while (*p && *p != ';' && n + 1u < (nj_u4)sizeof(buf)) {
            buf[n++] = *p++;
        }
        buf[n] = '\0';
        comp = nj_class_resolve(vm, buf);
    } else {
        int i;
        comp = NULL;
        for (i = 0; i < NJ_NPRIMS; i++) {
            if (nj_prims[i].name[0] == *elem) {
                c->array_elem = nj_prims[i].kind;
                comp = primitive_class(vm, elem);
                break;
            }
        }
    }
    c->component_cls = comp;
    c->super = nj_class_resolve(vm, "java/lang/Object");
    c->instance_size = (nj_u2)sizeof(nj_object);
    c->state = NJ_CLASS_LINKED;
    return c;
}

/* ------------------------------------------------------------------ */
/* Loading from the class source                                        */
/* ------------------------------------------------------------------ */
nj_class *nj_class_resolve(nj_vm *vm, const char *internal_name)
{
    nj_class *c;
    char buf[256];

    if (!vm || !internal_name || !*internal_name) {
        return NULL;
    }
    if (internal_name[0] == '[') {
        return array_class(vm, internal_name);
    }

    normalise_name(internal_name, buf, (nj_u4)sizeof(buf));
    c = nj_class_find(vm, buf);
    if (c) {
        return c;
    }
    if (!vm->source || !vm->source->load) {
        return NULL;
    }
    {
        nj_u1 *bytes = NULL;
        nj_u4 len = 0u;
        if (vm->source->load(vm, vm->source->ud, buf, &bytes, &len) != NJ_OK) {
            return NULL;
        }
        if (!bytes || len == 0u) {
            nj_host_free(vm, bytes);
            return NULL;
        }
        /* Pre-register the name so recursive references terminate. */
        c = class_new(vm, buf);
        if (!c) {
            nj_host_free(vm, bytes);
            return NULL;
        }
        c->state = 0u;
        c = nj_class_load(vm, buf, bytes, len);
        nj_host_free(vm, bytes);
        if (!c) {
            return NULL;
        }
    }
    return c;
}

nj_class *nj_class_find_or_create_array(nj_vm *vm, nj_class *elem, int dims)
{
    char buf[300];
    nj_u4 n = 0u;
    int d;

    if (!vm || !elem || dims <= 0 || dims > NJ_MAX_DIMENSIONS) {
        return NULL;
    }
    for (d = 0; d < dims && n + 2u < (nj_u4)sizeof(buf); d++) {
        buf[n++] = '[';
    }
    if (elem->prim != 0u) {
        int i;
        for (i = 0; i < NJ_NPRIMS; i++) {
            if (nj_prims[i].prim == elem->prim) {
                buf[n++] = nj_prims[i].name[0];
                break;
            }
        }
    } else {
        nj_u4 k = 0u;
        buf[n++] = 'L';
        while (elem->name[k] && n + 2u < (nj_u4)sizeof(buf)) {
            buf[n++] = elem->name[k++];
        }
        buf[n++] = ';';
    }
    buf[n] = '\0';
    return array_class(vm, buf);
}

/* ------------------------------------------------------------------ */
/* Linking                                                             */
/* ------------------------------------------------------------------ */
#define NJ_ALIGN_UP(v, a) (((v) + ((a) - 1u)) & ~((a) - 1u))

static nj_u4 field_align(const nj_field *f)
{
    switch (f->ptype) {
    case NJ_PT_BOOLEAN:
    case NJ_PT_BYTE:   return 1u;
    case NJ_PT_CHAR:
    case NJ_PT_SHORT:  return 2u;
    case NJ_PT_LONG:
    case NJ_PT_DOUBLE: return 8u;
    default:           return (nj_u4)sizeof(nj_ref);
    }
}

static nj_u4 field_bytes(const nj_field *f)
{
    switch (f->ptype) {
    case NJ_PT_BOOLEAN:
    case NJ_PT_BYTE:   return 1u;
    case NJ_PT_CHAR:
    case NJ_PT_SHORT:  return 2u;
    case NJ_PT_LONG:
    case NJ_PT_DOUBLE: return 8u;
    default:           return (nj_u4)sizeof(nj_ref);
    }
}

/* The real linker.  Call it through nj_class_link() below, which turns a
 * failure into a describable exception - see the comment there. */
static nj_status class_link_inner(nj_vm *vm, nj_class *c)
{
    nj_u2 i;
    nj_u2 base;

    if (!vm || !c) {
        return NJ_ERR;
    }
    /* Clear the previous attempt's reason, so a stale name is never reported
     * against the class that failed this time. */
    vm->link_cls = NULL;
    vm->link_missing = NULL;
    if (c->state >= NJ_CLASS_LINKED) {
        return NJ_OK;
    }
    if (c->state < NJ_CLASS_LOADED) {
        return NJ_ERR_STATE;
    }

    /* ---- superclass ---- */
    if (c->super_cp != 0u) {
        const char *sn = nj_cp_class_name(c, c->super_cp);
        if (!sn) {
            nj_logf(vm, 3, "link %s: malformed superclass reference", c->name);
            return NJ_ERR_FORMAT;
        }
        c->super = nj_class_resolve(vm, sn);
        if (!c->super) {
            /* The most common bring-up failure: say exactly what is missing
             * rather than leaving the caller to guess. */
            nj_logf(vm, 3, "link %s: superclass not found: %s", c->name, sn);
            vm->link_cls = c->name;
            vm->link_missing = sn;
            return NJ_ERR;
        }
        if (nj_class_link(vm, c->super) != NJ_OK) {
            return NJ_ERR;
        }
    } else if (c->super_name != NULL) {
        c->super = nj_class_resolve(vm, c->super_name);
        if (!c->super) {
            vm->link_cls = c->name;
            vm->link_missing = c->super_name;
            return NJ_ERR;
        }
        if (nj_class_link(vm, c->super) != NJ_OK) {
            return NJ_ERR;
        }
    } else if (strcmp(c->name, "java/lang/Object") != 0 &&
               (c->flags & NJ_ACC_INTERFACE) == 0u && !c->is_array) {
        c->super = nj_class_resolve(vm, "java/lang/Object");
        if (!c->super) {
            return NJ_ERR;
        }
        if (nj_class_link(vm, c->super) != NJ_OK) {
            return NJ_ERR;
        }
    }

    /* ---- interfaces ---- */
    for (i = 0u; i < c->ninterfaces; i++) {
        const char *in = nj_cp_class_name(c, c->iface_cp[i]);
        if (!in) {
            nj_logf(vm, 3, "link %s: malformed interface reference", c->name);
            return NJ_ERR_FORMAT;
        }
        c->interfaces[i] = nj_class_resolve(vm, in);
        if (!c->interfaces[i]) {
            nj_logf(vm, 3, "link %s: interface this runtime does not provide: "
                          "%s", c->name, in);
            vm->link_cls = c->name;
            vm->link_missing = in;
            return NJ_ERR;
        }
        (void)nj_class_link(vm, c->interfaces[i]);
    }

    /* ---- instance layout ---- */
    base = c->super ? c->super->instance_size : (nj_u2)sizeof(nj_object);
    base = (nj_u2)NJ_ALIGN_UP(base, (nj_u4)sizeof(nj_ref));

    for (i = 0u; i < c->field_count; i++) {
        nj_field *f = &c->fields[i];
        if ((f->flags & NJ_ACC_STATIC) != 0u) {
            nj_u4 need = (nj_u4)f->width;
            if ((nj_u4)vm->nstatics + need > (nj_u4)NJ_MAX_STATICS) {
                return NJ_ERR_NOMEM;
            }
            f->offset = (nj_u2)vm->nstatics;
            for (need = 0u; need < (nj_u4)f->width; need++) {
                vm->statics[vm->nstatics].bits = 0u;
                vm->nstatics++;
            }
        } else {
            nj_u4 a = field_align(f);
            nj_u4 b = field_bytes(f);
            base = (nj_u2)NJ_ALIGN_UP(base, a);
            if ((nj_u4)base + b > 65500u) {
                return NJ_ERR_NOMEM;   /* absurd instance layout */
            }
            f->offset = base;
            base = (nj_u2)(base + b);
        }
    }
    c->instance_size = (nj_u2)NJ_ALIGN_UP(base, 16u);
    if (c->instance_size < (nj_u2)sizeof(nj_object)) {
        c->instance_size = (nj_u2)sizeof(nj_object);
    }

    c->state = NJ_CLASS_LINKED;

    /* Common classes: cache the pointer so hot paths avoid the hash. */
    if (!vm->cls_object && strcmp(c->name, "java/lang/Object") == 0) {
        vm->cls_object = c;
    } else if (!vm->cls_string && strcmp(c->name, "java/lang/String") == 0) {
        vm->cls_string = c;
    } else if (!vm->cls_class && strcmp(c->name, "java/lang/Class") == 0) {
        vm->cls_class = c;
    } else if (!vm->cls_throwable && strcmp(c->name, "java/lang/Throwable") == 0) {
        vm->cls_throwable = c;
    }
    return NJ_OK;
}

/*
 * Append `s` to buf[*n], never overrunning.  A freestanding build has no
 * snprintf to borrow, and this is the whole of the formatting below.
 */
static void append_str(char *buf, nj_u4 *n, nj_u4 cap, const char *s)
{
    while (s != NULL && *s != '\0' && *n + 1u < cap) {
        buf[(*n)++] = *s++;
    }
}

/*
 * Link a class, and on failure raise an exception that says what was missing.
 *
 * This is the single point where the reason is still known: class_link_inner()
 * records it in vm->link_cls / vm->link_missing as it gives up.  Without this
 * wrapper every caller turned a link failure into a bare
 * `NoClassDefFoundError: <the class that failed>` - which names the class that
 * is demonstrably present in the JAR and never the name that is actually
 * absent.  nj_throw() keeps the first exception, so the plain NoClassDefFound
 * errors raised further out become no-ops rather than overwriting this one.
 */
nj_status nj_class_link(nj_vm *vm, nj_class *c)
{
    nj_status st = class_link_inner(vm, c);

    if (st != NJ_OK && vm != NULL && !vm->exception && vm->link_cls != NULL) {
        char buf[320];
        nj_u4 n = 0u;

        append_str(buf, &n, sizeof(buf), vm->link_cls);
        if (vm->link_missing != NULL) {
            append_str(buf, &n, sizeof(buf), ": missing ");
            append_str(buf, &n, sizeof(buf), vm->link_missing);
            append_str(buf, &n, sizeof(buf),
                       " (this runtime does not provide it)");
        }
        buf[n] = '\0';
        (void)nj_throw(vm, "java/lang/NoClassDefFoundError", buf);
    }
    return st;
}

nj_status nj_class_ensure_refs(nj_vm *vm, nj_class *c)
{
    return nj_class_link(vm, c);
}

/* ------------------------------------------------------------------ */
/* Hierarchy helpers                                                   */
/* ------------------------------------------------------------------ */
int nj_class_is_subclass_of(nj_class *c, nj_class *maybe_super)
{
    nj_class *k;
    if (!c || !maybe_super) {
        return 0;
    }
    for (k = c; k; k = k->super) {
        if (k == maybe_super) {
            return 1;
        }
    }
    return 0;
}

static int implements_interface(nj_class *c, nj_class *iface)
{
    nj_class *k;
    for (k = c; k; k = k->super) {
        nj_u2 i;
        for (i = 0u; i < k->ninterfaces; i++) {
            nj_class *ic = k->interfaces[i];
            if (ic == iface) {
                return 1;
            }
            if (ic && implements_interface(ic, iface)) {
                return 1;
            }
        }
    }
    return 0;
}

int nj_class_is_assignable(nj_class *from, nj_class *to)
{
    if (!from || !to) {
        return 0;
    }
    if (from == to) {
        return 1;
    }
    if ((to->flags & NJ_ACC_INTERFACE) != 0u) {
        return implements_interface(from, to);
    }
    /* Every array is an Object and a Cloneable/Serializable in real Java;
     * for MIDP purposes treating arrays as assignable to Object is enough. */
    return nj_class_is_subclass_of(from, to);
}

/* ------------------------------------------------------------------ */
/* Member lookup                                                       */
/* ------------------------------------------------------------------ */
nj_method *nj_method_lookup(nj_class *c, const char *name, const char *desc)
{
    nj_class *k;

    for (k = c; k; k = k->super) {
        nj_u2 i;
        for (i = 0u; i < k->method_count; i++) {
            nj_method *m = &k->methods[i];
            if ((m->flags & NJ_ACC_STATIC) != 0u) {
                continue;
            }
            if (strcmp(m->name, name) == 0 && strcmp(m->desc, desc) == 0) {
                return m;
            }
        }
    }
    /* Interface default/abstract methods: the concrete implementation always
     * lives in the class chain, so only the names are needed for the error
     * path.  Still search interfaces for the (rare) case of an interface
     * method invoked on an object whose class declares it only via an
     * abstract superclass. */
    return NULL;
}

static nj_method *method_lookup_any(nj_class *c, const char *name,
                                    const char *desc, int want_static)
{
    nj_class *k;

    for (k = c; k; k = k->super) {
        nj_u2 i;
        for (i = 0u; i < k->method_count; i++) {
            nj_method *m = &k->methods[i];
            int is_static = (m->flags & NJ_ACC_STATIC) != 0u;
            if (is_static != want_static) {
                continue;
            }
            if (strcmp(m->name, name) == 0 && strcmp(m->desc, desc) == 0) {
                return m;
            }
        }
    }
    return NULL;
}

static nj_field *field_lookup_walk(nj_class *c, const char *name,
                                   const char *desc, int want_static,
                                   int search_interfaces)
{
    nj_class *k;

    for (k = c; k; k = k->super) {
        nj_u2 i;
        for (i = 0u; i < k->field_count; i++) {
            nj_field *f = &k->fields[i];
            int is_static = (f->flags & NJ_ACC_STATIC) != 0u;
            if (is_static != want_static) {
                continue;
            }
            if (strcmp(f->name, name) != 0) {
                continue;
            }
            if (desc && strcmp(f->desc, desc) != 0) {
                continue;
            }
            return f;
        }
        if (search_interfaces) {
            nj_u2 j;
            for (j = 0u; j < k->ninterfaces; j++) {
                nj_field *f = field_lookup_walk(k->interfaces[j], name, desc,
                                                want_static, 1);
                if (f) {
                    return f;
                }
            }
        }
    }
    return NULL;
}

nj_field *nj_field_lookup_in(nj_class *c, const char *name, const char *desc,
                             int is_static)
{
    return field_lookup_walk(c, name, desc, is_static, 1);
}

/* ------------------------------------------------------------------ */
/* Constant pool resolution cache                                      */
/* ------------------------------------------------------------------ */
static nj_u4 cp_key_hash(nj_class *c, nj_u4 cp_index)
{
    nj_u4 h = (nj_u4)(((nj_size)(uintptr_t)c) >> 4);
    h ^= cp_index * 2654435761u;
    h ^= h >> 15;
    return h;
}

static nj_cpentry *cp_cache_slot(nj_vm *vm, nj_class *c, nj_u4 cp_index)
{
    nj_u4 mask = (nj_u4)vm->cp_cache_size - 1u;
    nj_u4 h = cp_key_hash(c, cp_index) & mask;
    nj_u4 probes = 0u;

    while (probes++ <= mask) {
        nj_cpentry *e = &vm->cp_cache[h];
        if (!e->used || (e->cls == c && e->cp_index == cp_index)) {
            return e;
        }
        h = (h + 1u) & mask;
    }
    return NULL;   /* table full */
}

nj_status nj_cp_cache_init(nj_vm *vm, nj_u2 size)
{
    nj_u4 i;
    vm->cp_cache = (nj_cpentry *)nj_host_alloc(
        vm, (nj_size)size * sizeof(nj_cpentry));
    if (!vm->cp_cache) {
        return NJ_ERR_NOMEM;
    }
    vm->cp_cache_size = size;
    vm->cp_cache_used = 0u;
    vm->cp_cache_limit = (nj_u2)((nj_u4)size * 7u / 10u);
    for (i = 0u; i < (nj_u4)size; i++) {
        vm->cp_cache[i].used = 0u;
    }
    return NJ_OK;
}

static void cp_cache_grow(nj_vm *vm)
{
    nj_u2 old_size = vm->cp_cache_size;
    nj_cpentry *old = vm->cp_cache;
    nj_u4 i;

    if (old_size >= 8192u) {
        return;   /* hard cap: keep resolving through the full table */
    }
    if (nj_cp_cache_init(vm, (nj_u2)(old_size * 2u)) != NJ_OK) {
        vm->cp_cache = old;   /* restore and keep going */
        vm->cp_cache_size = old_size;
        return;
    }
    vm->cp_cache_used = 0u;
    for (i = 0u; i < (nj_u4)old_size; i++) {
        if (old[i].used) {
            nj_cpentry *e = cp_cache_slot(vm, old[i].cls, old[i].cp_index);
            if (e) {
                *e = old[i];
                vm->cp_cache_used++;
            }
        }
    }
    nj_host_free(vm, old);
}

/*
 * Resolve a constant-pool entry to a field or method and cache it.
 * Handles CONSTANT_Fieldref / Methodref / InterfaceMethodref / String.
 */
nj_status nj_cp_resolve(nj_vm *vm, nj_class *c, nj_u2 index, nj_cpentry **out)
{
    nj_cpentry *e;
    nj_u4 off;
    nj_u1 tag;
    nj_u2 class_idx, nt_idx;
    const char *cls_name;
    const char *mname;
    const char *mdesc;
    nj_class *owner;

    if (!vm || !c || !out || index == 0u || index >= c->cp_count) {
        return NJ_ERR;
    }
    off = cp_off(c, index);
    if (off == 0u) {
        return NJ_ERR_FORMAT;
    }
    tag = (nj_u1)NJ_CP_TAG(c->cp_info[index]);

    e = cp_cache_slot(vm, c, index);
    if (e && e->used) {
        vm->cp_cache_hits++;
        *out = e;
        return NJ_OK;
    }
    if (!e) {
        return NJ_ERR_NOMEM;
    }
    vm->cp_cache_misses++;

    if (tag == NJ_CP_STRING) {
        nj_u2 s_idx = cp_u2(c, off);
        const char *utf = nj_cp_utf8(c, s_idx);
        nj_ref s;
        if (!utf) {
            return NJ_ERR_FORMAT;
        }
        s = nj_intern(vm, nj_new_string_utf8(vm, utf, -1));
        if (!s) {
            return NJ_ERR_NOMEM;
        }
        e->cls = c;
        e->cp_index = index;
        e->kind = NJ_LINK_STRING;
        e->slot = 0u;
        e->target = (void *)s;   /* kind == NJ_LINK_STRING: an nj_ref */
        e->recv_cls = NULL;
        e->ic_target = NULL;
        e->used = 1u;
        vm->cp_cache_used++;
        if (vm->cp_cache_used > vm->cp_cache_limit) {
            cp_cache_grow(vm);
            e = cp_cache_slot(vm, c, index);
        }
        *out = e;
        return NJ_OK;
    }

    if (tag != NJ_CP_FIELDREF && tag != NJ_CP_METHODREF &&
        tag != NJ_CP_IFACEMETHODREF) {
        return NJ_ERR_FORMAT;
    }

    class_idx = cp_u2(c, off);
    nt_idx = cp_u2(c, off + 2u);
    cls_name = nj_cp_class_name(c, class_idx);
    if (!cls_name) {
        return NJ_ERR_FORMAT;
    }
    {
        nj_u4 nt_off = cp_off(c, nt_idx);
        if (nt_off == 0u) {
            return NJ_ERR_FORMAT;
        }
        mname = nj_cp_utf8(c, cp_u2(c, nt_off));
        mdesc = nj_cp_utf8(c, cp_u2(c, nt_off + 2u));
    }
    if (!mname || !mdesc) {
        return NJ_ERR_FORMAT;
    }

    owner = nj_class_resolve(vm, cls_name);
    if (!owner) {
        vm->resolve_fail = NJ_RESOLVE_CLASS;
        vm->resolve_cls = cls_name;
        vm->resolve_name = mname;
        vm->resolve_desc = mdesc;
        return NJ_ERR;
    }
    if (nj_class_link(vm, owner) != NJ_OK) {
        vm->resolve_fail = NJ_RESOLVE_CLASS;
        vm->resolve_cls = cls_name;
        vm->resolve_name = mname;
        vm->resolve_desc = mdesc;
        return NJ_ERR;
    }

    e->cls = c;
    e->cp_index = index;
    e->recv_cls = NULL;
    e->ic_target = NULL;
    e->target = NULL;
    e->slot = 0u;

    if (tag == NJ_CP_FIELDREF) {
        nj_field *f = nj_field_lookup_in(owner, mname, mdesc, 0);
        if (!f) {
            /* Try the static variant: some obfuscators emit both. */
            f = nj_field_lookup_in(owner, mname, mdesc, 1);
            if (!f) {
                vm->resolve_fail = NJ_RESOLVE_FIELD;
                vm->resolve_cls = cls_name;
                vm->resolve_name = mname;
                vm->resolve_desc = mdesc;
                return NJ_ERR;
            }
            e->kind = NJ_LINK_FIELD_STATIC;
        } else {
            e->kind = NJ_LINK_FIELD_INST;
        }
        e->target = f;
        e->slot = f->offset;
    } else if (tag == NJ_CP_METHODREF) {
        int want_static = 0;
        nj_method *m;
        /* A Methodref may name a static method: detect it by asking for both. */
        m = method_lookup_any(owner, mname, mdesc, 0);
        if (!m) {
            m = method_lookup_any(owner, mname, mdesc, 1);
            want_static = 1;
        }
        if (!m) {
            vm->resolve_fail = NJ_RESOLVE_METHOD;
            vm->resolve_cls = cls_name;
            vm->resolve_name = mname;
            vm->resolve_desc = mdesc;
            return NJ_ERR;
        }
        e->target = m;
        e->kind = want_static ? NJ_LINK_METHOD_STATIC : NJ_LINK_METHOD_INST;
    } else {
        nj_method *m = method_lookup_any(owner, mname, mdesc, 0);
        if (!m) {
            m = method_lookup_any(owner, mname, mdesc, 1);
            if (!m) {
                vm->resolve_fail = NJ_RESOLVE_METHOD;
                vm->resolve_cls = cls_name;
                vm->resolve_name = mname;
                vm->resolve_desc = mdesc;
                return NJ_ERR;
            }
            e->kind = NJ_LINK_METHOD_STATIC;
        } else {
            e->kind = NJ_LINK_METHOD_INST;
        }
        e->target = m;
    }

    e->used = 1u;
    vm->cp_cache_used++;
    if (vm->cp_cache_used > vm->cp_cache_limit) {
        cp_cache_grow(vm);
        e = cp_cache_slot(vm, c, index);
    }
    *out = e;
    return NJ_OK;
}


/* ------------------------------------------------------------------ */
/* Constant-pool primitives (ldc / ldc_w / ldc2_w)                     */
/* ------------------------------------------------------------------ */
static nj_s4 cp_raw_s4(nj_class *c, nj_u4 off)
{
    return (nj_s4)(((nj_u4)c->data[off] << 24) | ((nj_u4)c->data[off + 1u] << 16) |
                   ((nj_u4)c->data[off + 2u] << 8) | (nj_u4)c->data[off + 3u]);
}

static nj_s8 cp_raw_s8(nj_class *c, nj_u4 off)
{
    nj_u8 v = 0u;
    int i;
    for (i = 0; i < 8; i++) {
        v = (v << 8) | (nj_u8)c->data[off + (nj_u4)i];
    }
    return (nj_s8)v;
}

/*
 * Load a constant-pool primitive.
 *
 * `strat` selects the interpretation for a CONSTANT_String entry:
 *   0 = int/float/long/double only (ldc of an int-ish constant)
 *   1 = also resolve CONSTANT_String to a java.lang.String
 *
 * Returns: 0 = primitive written to *out (wide constants use out[0], with
 * out[1] zeroed), 1 = object reference written to *out_ref, -1 = wrong kind.
 */
int nj_cp_const(nj_vm *vm, nj_class *c, nj_u2 index, int allow_string,
                nj_value *out, nj_ref *out_ref)
{
    nj_u4 off;
    nj_u1 tag;

    if (!c || index == 0u || index >= c->cp_count || !out) {
        return -1;
    }
    off = cp_off(c, index);
    if (off == 0u) {
        return -1;
    }
    tag = (nj_u1)NJ_CP_TAG(c->cp_info[index]);
    out[0].bits = 0u;
    out[1].bits = 0u;

    switch (tag) {
    case NJ_CP_INTEGER:
        out[0].i = cp_raw_s4(c, off);
        return 0;
    case NJ_CP_FLOAT: {
        nj_s4 bits = cp_raw_s4(c, off);
        memcpy(&out[0].f, &bits, 4u);
        return 0;
    }
    case NJ_CP_LONG:
        out[0].j = cp_raw_s8(c, off);
        return 0;
    case NJ_CP_DOUBLE: {
        nj_s8 bits = cp_raw_s8(c, off);
        memcpy(&out[0].d, &bits, 8u);
        return 0;
    }
    case NJ_CP_STRING:
        if (!allow_string) {
            return -1;
        }
        {
            nj_cpentry *e = NULL;
            if (nj_cp_resolve(vm, c, index, &e) != NJ_OK || !e) {
                return -1;
            }
            *out_ref = (nj_ref)e->target;
            return 1;
        }
    default:
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Builtin (C-implemented) classes                                     */
/* ------------------------------------------------------------------ */
/*
 * The class library is written in C, so java.lang.Object and friends do not
 * exist as .class files.  Each class-library module declares them as data
 * (nj_builtin_class) and this function materialises the nj_class the VM works
 * with.  Field offsets and static slots are assigned later, by
 * nj_class_link(), exactly as for a class parsed out of a JAR.
 */
nj_class *nj_class_create_builtin(nj_vm *vm, const nj_builtin_class *bc)
{
    nj_class *c;
    nj_u2 i;

    if (!vm || !bc || !bc->name) {
        return NULL;
    }
    c = nj_class_find(vm, bc->name);
    if (c && c->state >= NJ_CLASS_LOADED) {
        return c;
    }
    if (!c) {
        c = class_new(vm, bc->name);
        if (!c) {
            return NULL;
        }
    }
    c->flags = bc->flags;
    c->super_name = bc->super;

    if (bc->nfields > 0u) {
        c->fields = (nj_field *)nj_host_alloc(
            vm, (nj_size)bc->nfields * sizeof(nj_field));
        if (!c->fields) {
            return NULL;
        }
        for (i = 0u; i < bc->nfields; i++) {
            nj_field *f = &c->fields[i];
            nj_u1 w = 1u;
            memset(f, 0, sizeof(*f));
            f->name = bc->fields[i].name;
            f->desc = bc->fields[i].desc;
            f->flags = (nj_u2)(bc->fields[i].flags & ~(nj_u2)NJ_ACC_STATIC);
            f->flags = bc->fields[i].flags;
            f->owner = c;
            f->ptype = nj_desc_ptype(f->desc, &w);
            f->width = w;
            f->offset = 0u;
            f->const_cp = 0u;
            c->field_count++;
        }
    }

    if (bc->nmethods > 0u) {
        c->methods = (nj_method *)nj_host_alloc(
            vm, (nj_size)bc->nmethods * sizeof(nj_method));
        if (!c->methods) {
            return NULL;
        }
        for (i = 0u; i < bc->nmethods; i++) {
            nj_method *m = &c->methods[i];
            nj_u2 flags = bc->methods[i].flags;
            memset(m, 0, sizeof(*m));
            m->name = bc->methods[i].name;
            m->desc = bc->methods[i].desc;
            m->owner = c;
            m->arg_slots = nj_desc_arg_slots(m->desc);
            m->ret_ptype = nj_desc_ret_ptype(m->desc);
            if ((flags & NJ_ACC_ABSTRACT) != 0u) {
                m->flags = flags;
                m->code = NULL;
                m->native = NULL;
            } else {
                m->flags = (nj_u2)(flags | NJ_ACC_NATIVE);
                m->code = NULL;   /* no bytecode: this is a C function */
                m->native = NULL; /* bound on first call from the registry */
            }
            m->max_stack = 0u;
            m->max_locals = m->arg_slots;
            if ((m->flags & NJ_ACC_STATIC) == 0u) {
                m->max_locals = (nj_u2)(m->max_locals + 1u);
            }
            c->method_count++;
        }
    }

    c->state = NJ_CLASS_LOADED;
    return c;
}
