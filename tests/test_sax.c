/* Java-facing SAX regression tests; no generated Java/class fixtures needed. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NJ_ENABLE_SAX
#define NJ_ENABLE_SAX 0
#endif
#if NJ_ENABLE_SAX
#define FACTORY "javax/xml/parsers/SAXParserFactory"
#define PARSER "javax/xml/parsers/SAXParser"
#define HANDLER "org/xml/sax/helpers/DefaultHandler"
#define ATTR "org/xml/sax/Attributes"
#define FIXTURE "test/SAXHandler"
#define STR "Ljava/lang/String;"
#define ELEMENT "(" STR STR STR "L" ATTR ";)V"
#define END_ELEMENT "(" STR STR STR ")V"
#define PARSE "(Ljava/io/InputStream;L" HANDLER ";)V"
static int checks, failures;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

typedef struct capture {
    char events[1024];
    nj_u2 text[1024];
    unsigned used, depth, starts, ends;
    int mode, reentered;
    nj_ref handler;
} capture;
static capture *active;
static nj_ref parser, sentinel, later_sentinel;
static unsigned surrogate_reads;
static nj_field *retained;
enum { ATTRS = 1, STRESS = 2, REENTER = 4, THROW = 8 };

static nj_value ref(nj_ref r) { nj_value v; v.r = r; return v; }
static nj_value integer(int i) { nj_value v; v.r = NULL; v.i = i; return v; }
static nj_value call(nj_vm *vm, nj_ref o, const char *cls, const char *name,
                     const char *desc, nj_value *a)
{
    nj_value r;
    nj_status st;
    r.r = NULL;
    st = o ? nj_call(vm, o, cls, name, desc, a, &r)
           : nj_call_static(vm, cls, name, desc, a, &r);
    CHECK(st == NJ_OK, "%s.%s%s: %s", cls, name, desc,
          nj_exception_describe(vm) ? nj_exception_describe(vm) : "call failed");
    if (st != NJ_OK) nj_clear_exception(vm);
    return r;
}
static void event(const char *s)
{
    size_t n = strlen(active->events), len = strlen(s);
    CHECK(n + len < sizeof(active->events), "callback event capacity");
    if (n + len < sizeof(active->events)) memcpy(active->events + n, s, len + 1u);
}
static void collect(nj_vm *vm)
{
    nj_u4 mark = nj_root_mark(vm);
    int i;
    if (active->mode & STRESS) {
        for (i = 0; i < 96; i++)
            CHECK(nj_root(vm, nj_string_new(vm, "temporary root")) != NULL,
                  "grow temporary roots during callback");
    }
    (void)nj_gc_collect(vm);
    nj_root_release(vm, mark);
}
static int equals_utf8(nj_ref s, const char *want)
{
    char text[4096];
    if (!s) return want == NULL;
    nj_string_to_utf8(s, text, sizeof(text));
    return want && strcmp(text, want) == 0;
}
static void attr_string(nj_vm *vm, nj_ref attrs, const char *method,
                        const char *desc, nj_value *a, const char *want)
{
    nj_ref r = call(vm, attrs, ATTR, method, desc, a).r;
    CHECK(want ? r && equals_utf8(r, want) : r == NULL,
          "Attributes.%s expected %s", method, want ? want : "null");
}
static void check_attrs(nj_vm *vm, nj_ref attrs)
{
    nj_value i = integer(0), q;
    nj_u4 mark = nj_root_mark(vm);
    CHECK(attrs != NULL, "startElement provides Attributes");
    if (!attrs) return;
    q = ref(nj_root(vm, nj_string_new(vm, "p:a")));
    CHECK(nj_class_is_assignable(attrs->cls, nj_class_lookup(vm, ATTR)),
          "AttributesImpl implements the Attributes interface");
    CHECK(strcmp(attrs->cls->name, "org/xml/sax/helpers/AttributesImpl") == 0,
          "callback exposes AttributesImpl");
    CHECK(call(vm, attrs, ATTR, "getLength", "()I", NULL).i == 3, "three attributes");
    i.i = call(vm, attrs, ATTR, "getIndex", "(" STR ")I", &q).i;
    CHECK(i.i >= 0 && i.i < 3, "qualified name has an attribute index");
    attr_string(vm, attrs, "getValue", "(" STR ")" STR, &q, "中文");
    attr_string(vm, attrs, "getValue", "(I)" STR, &i, "中文");
    attr_string(vm, attrs, "getQName", "(I)" STR, &i, "p:a");
    attr_string(vm, attrs, "getURI", "(I)" STR, &i, "");
    attr_string(vm, attrs, "getLocalName", "(I)" STR, &i, "");
    attr_string(vm, attrs, "getType", "(I)" STR, &i, "CDATA");
    attr_string(vm, attrs, "getType", "(" STR ")" STR, &q, "CDATA");
    q.r = nj_root(vm, nj_string_new(vm, "id"));
    attr_string(vm, attrs, "getValue", "(" STR ")" STR, &q, "a b c d \t\n\r&中");
    q.r = nj_root(vm, nj_string_new(vm, "missing"));
    CHECK(call(vm, attrs, ATTR, "getIndex", "(" STR ")I", &q).i == -1,
          "absent attribute index");
    attr_string(vm, attrs, "getValue", "(" STR ")" STR, &q, NULL);
    i.i = -1;
    attr_string(vm, attrs, "getValue", "(I)" STR, &i, NULL);
    i.i = 3;
    attr_string(vm, attrs, "getQName", "(I)" STR, &i, NULL);
    nj_root_release(vm, mark);
}
static nj_status parse_bytes(nj_vm *vm, const unsigned char *data, unsigned len,
                             capture *out, int mode);

static nj_status start_document(nj_vm *vm, nj_thread *th, nj_value *a, nj_value *r)
{
    (void)th; (void)a; (void)r;
    collect(vm);
    CHECK(active->events[0] == '\0', "startDocument dispatched first");
    event("{");
    return NJ_OK;
}
static nj_status end_document(nj_vm *vm, nj_thread *th, nj_value *a, nj_value *r)
{
    (void)th; (void)a; (void)r;
    collect(vm);
    CHECK(active->depth == 0, "balanced elements before endDocument");
    event("}");
    return NJ_OK;
}
static nj_status start_element(nj_vm *vm, nj_thread *th, nj_value *a, nj_value *r)
{
    char name[128];
    nj_ref attrs = a[3].r;
    (void)th; (void)r;
    collect(vm);
    CHECK(equals_utf8(a[0].r, "") && equals_utf8(a[1].r, ""),
          "namespace-disabled startElement URI/localName are empty");
    nj_string_to_utf8(a[2].r, name, sizeof(name));
    event("<"); event(name); event(">");
    if (active->starts++ == 0) {
        nj_field_write_ref(vm, NJ_THIS(a), retained, attrs);
        if (active->mode & ATTRS) check_attrs(vm, attrs);
        if (active->mode & REENTER) {
            capture inner;
            nj_u4 mark = nj_root_mark(vm);
            CHECK(parse_bytes(vm, (const unsigned char *)"<inner x='1'/>", 14,
                              &inner, STRESS) == NJ_OK, "same-parser callback reentry");
            CHECK(strcmp(inner.events, "{<inner></inner>}") == 0,
                  "reentrant parser callback sequence");
            active->reentered++;
            nj_root_release(vm, mark);
            collect(vm);
            CHECK(equals_utf8(a[2].r, name), "outer callback arguments survive reentry");
        }
    }
    active->depth++;
    return NJ_OK;
}
static nj_status end_element(nj_vm *vm, nj_thread *th, nj_value *a, nj_value *r)
{
    char name[128];
    (void)th; (void)r;
    collect(vm);
    CHECK(equals_utf8(a[0].r, "") && equals_utf8(a[1].r, ""),
          "namespace-disabled endElement URI/localName are empty");
    CHECK(active->depth > 0, "endElement follows startElement");
    if (active->depth) active->depth--;
    active->ends++;
    nj_string_to_utf8(a[2].r, name, sizeof(name));
    event("</"); event(name); event(">");
    return NJ_OK;
}
static nj_status characters(nj_vm *vm, nj_thread *th, nj_value *a, nj_value *r)
{
    int i, off = a[1].i, len = a[2].i;
    (void)th; (void)r;
    collect(vm);
    if (active->mode & THROW) return nj_vm_throw_ref(vm, sentinel);
    CHECK(off >= 0 && len >= 0 && (nj_u4)(off + len) <= nj_array_length(a[0].r),
          "characters receives a valid char[] slice");
    CHECK(active->used + (unsigned)len <= 1024u, "text capture capacity");
    for (i = 0; i < len && active->used < 1024u; i++)
        active->text[active->used++] = (nj_u2)nj_array_get_int(a[0].r, (nj_u4)(off + i));
    return NJ_OK;
}
static nj_status processing_instruction(nj_vm *vm, nj_thread *th, nj_value *a, nj_value *r)
{
    (void)th; (void)r;
    collect(vm);
    CHECK(equals_utf8(a[0].r, "pre") || equals_utf8(a[0].r, "inside"),
          "processingInstruction target survives callback GC");
    CHECK(equals_utf8(a[1].r, "ok"), "processingInstruction data excludes separator");
    event("<?");
    return NJ_OK;
}
static nj_status stream_read(nj_vm *vm, nj_thread *th, nj_value *a, nj_value *r)
{
    (void)th; (void)a; (void)r;
    return nj_vm_throw_ref(vm, sentinel);
}
static nj_status surrogate_stream_read(nj_vm *vm, nj_thread *th, nj_value *a, nj_value *r)
{
    static const unsigned char prefix[] = {0xff,0xfe,'<',0,'r',0,'>',0,0,0xd8};
    (void)th;
    surrogate_reads++;
    if (surrogate_reads == 1) {
        memcpy(nj_array_data(a[0].r), prefix, sizeof(prefix));
        r->i = (nj_s4)sizeof(prefix);
        return NJ_OK;
    }
    return nj_vm_throw_ref(vm, surrogate_reads == 2 ? sentinel : later_sentinel);
}
static int fixtures(nj_vm *vm)
{
    static const nj_builtin_method methods[] = {
        {"startDocument", "()V", NJ_ACC_PUBLIC}, {"endDocument", "()V", NJ_ACC_PUBLIC},
        {"startElement", ELEMENT, NJ_ACC_PUBLIC}, {"endElement", END_ELEMENT, NJ_ACC_PUBLIC},
        {"characters", "([CII)V", NJ_ACC_PUBLIC},
        {"processingInstruction", "(" STR STR ")V", NJ_ACC_PUBLIC}
    };
    static const nj_builtin_field fields[] = {{"retained", "L" ATTR ";", NJ_ACC_PUBLIC}};
    static const nj_builtin_class handler = {
        FIXTURE, HANDLER, NJ_ACC_PUBLIC, fields, 1, methods, 6
    };
    static const nj_builtin_method read_method[] = {{"read", "()I", NJ_ACC_PUBLIC}};
    static const nj_builtin_class stream = {
        "test/FailingStream", "java/io/InputStream", NJ_ACC_PUBLIC, NULL, 0, read_method, 1
    };
    static const nj_builtin_method bulk_read[] = {{"read", "([BII)I", NJ_ACC_PUBLIC}};
    static const nj_builtin_class short_stream = {
        "test/SurrogateStream", "java/io/InputStream", NJ_ACC_PUBLIC, NULL, 0, bulk_read, 1
    };
    nj_class *h = nj_class_create_builtin(vm, &handler);
    nj_class *s = nj_class_create_builtin(vm, &stream);
    nj_class *u = nj_class_create_builtin(vm, &short_stream);
    CHECK(h && s && u, "create builtin subclass fixtures");
    if (!h || !s || !u) return 0;
    u->methods[0].native = surrogate_stream_read;
    h->methods[0].native = start_document; h->methods[1].native = end_document;
    h->methods[2].native = start_element; h->methods[3].native = end_element;
    h->methods[4].native = characters;
    h->methods[5].native = processing_instruction; s->methods[0].native = stream_read;
    retained = nj_field_lookup(vm, FIXTURE, "retained", "L" ATTR ";", 0);
    CHECK(retained != NULL, "retained attribute Java field");
    return retained != NULL;
}
static nj_status parse_stream(nj_vm *vm, nj_ref stream, nj_ref handler)
{
    nj_value a[2];
    nj_u4 roots = vm->nroots;
    nj_status st;
    a[0] = ref(stream); a[1] = ref(handler);
    st = nj_call(vm, parser, PARSER, "parse", PARSE, a, NULL);
    CHECK(vm->nroots == roots, "parse releases temporary roots on success and failure");
    return st;
}
static nj_status parse_bytes(nj_vm *vm, const unsigned char *data, unsigned len,
                             capture *out, int mode)
{
    capture *saved = active;
    nj_u4 mark;
    nj_ref bytes, stream;
    nj_value a;
    nj_status st;
    memset(out, 0, sizeof(*out)); out->mode = mode;
    out->handler = nj_root(vm, nj_object_new(vm, FIXTURE));
    CHECK(out->handler != NULL, "handler allocation");
    if (!out->handler) return NJ_ERR;
    call(vm, out->handler, HANDLER, "<init>", "()V", NULL);
    mark = nj_root_mark(vm);
    bytes = nj_root(vm, nj_byte_array_new(vm, len));
    stream = nj_root(vm, nj_object_new(vm, "java/io/ByteArrayInputStream"));
    CHECK(bytes && stream, "stream allocation");
    if (!bytes || !stream) { nj_root_release(vm, mark); return NJ_ERR; }
    memcpy(nj_array_data(bytes), data, len);
    a = ref(bytes);
    call(vm, stream, "java/io/ByteArrayInputStream", "<init>", "([B)V", &a);
    active = out;
    st = parse_stream(vm, stream, out->handler);
    active = saved;
    nj_root_release(vm, mark);
    return st;
}
static void text_is(nj_vm *vm, capture *c, const char *want)
{
    nj_ref s = nj_string_new_chars(vm, c->text, (int)c->used);
    CHECK(s && equals_utf8(s, want), "decoded/normalized characters: %s", want);
}
static void thrown(nj_vm *vm, nj_status st, const char *cls, nj_ref exact)
{
    nj_ref e = nj_take_exception(vm);
    CHECK(st != NJ_OK && e != NULL, "parse throws %s", cls);
    CHECK(e && nj_class_is_assignable(e->cls, nj_class_lookup(vm, cls)),
          "exception assignable to %s", cls);
    if (exact) CHECK(e == exact, "original exception identity propagates");
}
static void valid_documents(nj_vm *vm)
{
    static const char xml[] = "<?xml version='1.0' encoding='UTF-8'?>\n<?pre ok?>"
        "<r id='a\tb\r\nc\rd\n&#x9;&#10;&#13;&amp;&#x4E2D;' p:a='中文' xmlns:p='urn:p'>"
        "ASCII中文&amp;&lt;&gt;&quot;&apos;&#65;&#x4E2D;\r\nX\rY\n"
        "<!--ignored--><![CDATA[<raw>&中文\r\n]]><?inside ok?>"
        "<p:c n='next'/><b><c/></b></r><!--after-->";
    capture c;
    nj_ref attrs;
    nj_u4 mark = nj_root_mark(vm);
    int endian;
    CHECK(parse_bytes(vm, (const unsigned char *)xml, sizeof(xml) - 1u, &c,
                      ATTRS | STRESS | REENTER) == NJ_OK, "UTF8 document parses");
    CHECK(!nj_has_exception(vm), "successful parse has no pending exception");
    CHECK(strcmp(c.events, "{<?<r><?<p:c></p:c><b><c></c></b></r>}") == 0,
          "nested/selfclosing callback order: %s", c.events);
    CHECK(c.starts == 4 && c.ends == 4 && c.reentered == 1, "virtual callbacks and reentry");
    text_is(vm, &c, "ASCII中文&<>\"'A中\nX\nY\n<raw>&中文\n");
    (void)nj_gc_collect(vm);
    attrs = nj_field_read_ref(vm, c.handler, retained);
    CHECK(attrs && nj_heap_owns_object(vm, attrs), "retained Attributes survives callbacks and GC");
    if (attrs) check_attrs(vm, attrs);
    nj_root_release(vm, mark);
    for (endian = 0; endian < 2; endian++) {
        static const nj_u2 units[] = {'<','r','>',0x4e2d,0x6587,'&','#','6','5',';','<','/','r','>'};
        unsigned char bytes[2 + 2 * sizeof(units) / sizeof(units[0])];
        unsigned i;
        bytes[0] = endian ? 0xfe : 0xff; bytes[1] = endian ? 0xff : 0xfe;
        for (i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
            bytes[2 + i * 2 + (endian ? 1 : 0)] = (unsigned char)units[i];
            bytes[2 + i * 2 + (endian ? 0 : 1)] = (unsigned char)(units[i] >> 8);
        }
        CHECK(parse_bytes(vm, bytes, sizeof(bytes), &c, 0) == NJ_OK,
              "UTF16 %s BOM parses", endian ? "BE" : "LE");
        CHECK(strcmp(c.events, "{<r></r>}") == 0, "UTF16 matching end tag");
        text_is(vm, &c, "中文A");
    }
    CHECK(parse_bytes(vm, (const unsigned char *)"<r>&#x1F600;</r>", 16, &c, 0) == NJ_OK,
          "supplementary numeric character reference");
    CHECK(c.used == 2 && c.text[0] == 0xd83d && c.text[1] == 0xde00,
          "numeric reference emits a UTF16 surrogate pair");
}
static void invalid_documents(nj_vm *vm)
{
    static const char *bad[] = { "<a></b>", "<a><b></a>", "<a>",
        "<a x='1' x='2'/>", "<a>&unknown;</a>", "<!DOCTYPE a><a/>",
        "<!DOCTYPE a [<!ENTITY x 'y'>]><a>&x;</a>", "x<a/>", "<a/>x", "<a/><b/>",
        "<a>&#0;</a>", "<a>&#xD800;</a>", "<a>&#x110000;</a>",
        "<a>\xc0\xaf</a>", "<a>\xe4\xb8</a>", "<a>\xed\xa0\x80</a>" };
    static const unsigned char surrogate[] = {0xff,0xfe,'<',0,'a',0,'>',0,
        0,0xd8,'<',0,'/',0,'a',0,'>',0};
    capture c;
    unsigned i;
    CHECK(nj_class_is_subclass_of(nj_class_lookup(vm, "org/xml/sax/SAXParseException"),
                                 nj_class_lookup(vm, "org/xml/sax/SAXException")),
          "SAXParseException extends SAXException");
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        int before = failures;
        thrown(vm, parse_bytes(vm, (const unsigned char *)bad[i], (unsigned)strlen(bad[i]), &c, 0),
               "org/xml/sax/SAXException", NULL);
        if (failures != before) fprintf(stderr, "  malformed XML case %u: %s\n", i, bad[i]);
    }
    thrown(vm, parse_bytes(vm, surrogate, sizeof(surrogate), &c, 0),
           "org/xml/sax/SAXException", NULL);
    sentinel = nj_root(vm, nj_exception_new(vm, "java/lang/IllegalStateException", "callback"));
    thrown(vm, parse_bytes(vm, (const unsigned char *)"<r>text</r>", 11, &c, THROW),
           "java/lang/IllegalStateException", sentinel);
    CHECK(c.ends == 0 && !strchr(c.events, '}'), "callback exception aborts remaining callbacks");
    sentinel = nj_root(vm, nj_exception_new(vm, "java/io/IOException", "stream"));
    c.events[0] = '\0'; c.used = c.depth = c.starts = c.ends = 0; c.mode = 0;
    active = &c;
    thrown(vm, parse_stream(vm, nj_root(vm, nj_object_new(vm, "test/FailingStream")), c.handler),
           "java/io/IOException", sentinel);
    later_sentinel = nj_root(vm, nj_exception_new(vm, "java/io/IOException", "later read"));
    surrogate_reads = 0;
    thrown(vm, parse_stream(vm, nj_root(vm, nj_object_new(vm, "test/SurrogateStream")), c.handler),
           "java/io/IOException", sentinel);
    CHECK(surrogate_reads == 2, "UTF-16 surrogate decoding stops at the first failed read");
    thrown(vm, parse_stream(vm, NULL, c.handler), "java/lang/IllegalArgumentException", NULL);
    thrown(vm, parse_stream(vm, nj_root(vm, nj_object_new(vm, "test/FailingStream")), NULL),
           "java/lang/IllegalArgumentException", NULL);
    active = NULL;
    CHECK(parse_bytes(vm, (const unsigned char *)"<ok/>", 5, &c, STRESS) == NJ_OK,
          "parser reusable after errors");
}
static void parser_limits(nj_vm *vm)
{
    capture c;
    char nested[512], attrs[2048];
    char *large;
    size_t n = 0;
    unsigned i;
    for (i = 0; i < 65; i++) { memcpy(nested + n, "<a>", 3); n += 3; }
    for (i = 0; i < 65; i++) { memcpy(nested + n, "</a>", 4); n += 4; }
    thrown(vm, parse_bytes(vm, (const unsigned char *)nested, (unsigned)n, &c, 0),
           "org/xml/sax/SAXException", NULL);
    n = (size_t)sprintf(attrs, "<a");
    for (i = 0; i < 129; i++) n += (size_t)sprintf(attrs + n, " x%u='v'", i);
    memcpy(attrs + n, "/>", 2); n += 2;
    thrown(vm, parse_bytes(vm, (const unsigned char *)attrs, (unsigned)n, &c, 0),
           "org/xml/sax/SAXException", NULL);
    large = (char *)malloc(1048600u);
    CHECK(large != NULL, "limit fixture allocation");
    if (!large) return;
    memcpy(large, "<a x='", 6); memset(large + 6, 'v', 65537u);
    memcpy(large + 65543u, "'/>", 3);
    thrown(vm, parse_bytes(vm, (const unsigned char *)large, 65546u, &c, 0),
           "org/xml/sax/SAXException", NULL);
    /* A long comment exercises the document budget without text/token growth. */
    memcpy(large, "<!--", 4); memset(large + 4, 'x', 1048576u);
    memcpy(large + 1048580u, "--><a/>", 7);
    thrown(vm, parse_bytes(vm, (const unsigned char *)large, 1048587u, &c, 0),
           "org/xml/sax/SAXException", NULL);
    free(large);
}
static void factory_flags(nj_vm *vm, nj_ref factory)
{
    nj_value a = integer(1), result;
    CHECK(!call(vm, factory, FACTORY, "isNamespaceAware", "()Z", NULL).i &&
          !call(vm, factory, FACTORY, "isValidating", "()Z", NULL).i,
          "factory defaults to namespace-disabled and non-validating");
    call(vm, factory, FACTORY, "setNamespaceAware", "(Z)V", &a);
    CHECK(call(vm, factory, FACTORY, "isNamespaceAware", "()Z", NULL).i,
          "namespace flag is retained");
    thrown(vm, nj_call(vm, factory, FACTORY, "newSAXParser", "()L" PARSER ";", NULL, &result),
           "javax/xml/parsers/ParserConfigurationException", NULL);
    a = integer(0); call(vm, factory, FACTORY, "setNamespaceAware", "(Z)V", &a);
    a = integer(1); call(vm, factory, FACTORY, "setValidating", "(Z)V", &a);
    CHECK(call(vm, factory, FACTORY, "isValidating", "()Z", NULL).i,
          "validation flag is retained");
    thrown(vm, nj_call(vm, factory, FACTORY, "newSAXParser", "()L" PARSER ";", NULL, &result),
           "javax/xml/parsers/ParserConfigurationException", NULL);
    a = integer(0); call(vm, factory, FACTORY, "setValidating", "(Z)V", &a);
    CHECK(!call(vm, parser, PARSER, "isNamespaceAware", "()Z", NULL).i &&
          !call(vm, parser, PARSER, "isValidating", "()Z", NULL).i,
          "created parser reports its supported mode");
    CHECK(call(vm, factory, FACTORY, "newSAXParser", "()L" PARSER ";", NULL).r != NULL,
          "factory can create a parser after flags reset");
}
int main(void)
{
    nj_port port;
    nj_vm *vm;
    nj_ref factory;
    if (nj_port_headless(&port) != NJ_OK) return 2;
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) { nj_port_shutdown(&port); return 2; }
    factory = nj_root(vm, call(vm, NULL, FACTORY, "newInstance", "()L" FACTORY ";", NULL).r);
    CHECK(factory != NULL, "SAXParserFactory.newInstance()");
    parser = factory ? nj_root(vm, call(vm, factory, FACTORY, "newSAXParser", "()L" PARSER ";", NULL).r) : NULL;
    CHECK(parser != NULL, "factory.newSAXParser()");
    if (parser && fixtures(vm)) {
        factory_flags(vm, factory);
        valid_documents(vm); invalid_documents(vm); parser_limits(vm);
    }
    nj_vm_destroy(vm);
    nj_port_shutdown(&port);
    printf("SAX: %d assertions, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
#else
int main(void)
{
    nj_port port;
    nj_vm *vm;
    int absent;
    if (nj_port_headless(&port) != NJ_OK) return 2;
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) { nj_port_shutdown(&port); return 2; }
    absent = nj_class_lookup(vm, "javax/xml/parsers/SAXParserFactory") == NULL &&
             nj_class_lookup(vm, "org/xml/sax/helpers/DefaultHandler") == NULL &&
             nj_class_lookup(vm, "org/xml/sax/Attributes") == NULL;
    nj_vm_destroy(vm); nj_port_shutdown(&port);
    puts(absent ? "SAX disabled: classes absent" : "SAX disabled: unexpected class registered");
    return absent ? 0 : 1;
}
#endif
