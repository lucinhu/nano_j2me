/*
 * Small non-validating SAX reader for JSR 172 games. Streaming input, no DOM,
 * DTDs or external entities. UTF-8 and UTF-16 are decoded strictly. Every parse
 * owns its state, and Attributes snapshots live in the Java heap, so callbacks
 * may retain them, collect garbage, or parse another document recursively.
 */
#include "nj_midp.h"

#if NJ_ENABLE_SAX
#define FACTORY "javax/xml/parsers/SAXParserFactory"
#define PARSER  "javax/xml/parsers/SAXParser"
#define HANDLER "org/xml/sax/helpers/DefaultHandler"
#define ATTRS   "org/xml/sax/helpers/AttributesImpl"
#define IFACE   "org/xml/sax/Attributes"
#define SAXERR  "org/xml/sax/SAXParseException"
#define CONFIGERR "javax/xml/parsers/ParserConfigurationException"
#define STR "Ljava/lang/String;"
#define START_DESC "(" STR STR STR "L" IFACE ";)V"
#define END_DESC "(" STR STR STR ")V"
#define PARSE_DESC "(Ljava/io/InputStream;L" HANDLER ";)V"
#define COUNT(a) ((nj_u2)(sizeof(a) / sizeof((a)[0])))
#define FN(name) NJ_NATIVE_FN(name)

/* Hard resource limits are configurable independently of the Java heap. Text
 * is delivered in chunks; only an individual name/value needs a growable token. */
#ifndef NJ_SAX_MAX_DEPTH
#define NJ_SAX_MAX_DEPTH 64u
#endif
#ifndef NJ_SAX_MAX_ATTRIBUTES
#define NJ_SAX_MAX_ATTRIBUTES 128u
#endif
#ifndef NJ_SAX_MAX_TOKEN
#define NJ_SAX_MAX_TOKEN 65536u
#endif
#ifndef NJ_SAX_MAX_CHARS
#define NJ_SAX_MAX_CHARS 1048576u
#endif

typedef struct sax_reader {
    nj_vm *vm;
    nj_ref stream, handler, bytes, empty;
    nj_u4 pos, end, chars, depth;
    nj_u1 prefix[4];
    unsigned prefix_pos, prefix_len;
    int encoding;                    /* 0 UTF-8, 1 UTF-16LE, 2 UTF-16BE */
    int ascii, skip_lf, loaded;
    nj_s4 look;
    nj_u2 text[512];
    nj_u4 text_len;
} sax_reader;

typedef struct sax_token {
    nj_u2 *data;
    nj_u4 len, cap;
} sax_token;

static int sax_fail(sax_reader *p, const char *message)
{
    if (!nj_has_exception(p->vm)) (void)nj_throw(p->vm, SAXERR, message);
    return 0;
}

static int sax_space(nj_s4 c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int sax_xml_char(nj_u4 c)
{
    return c == 9u || c == 10u || c == 13u ||
           (c >= 0x20u && c <= 0xd7ffu) ||
           (c >= 0xe000u && c <= 0xfffdu) ||
           (c >= 0x10000u && c <= 0x10ffffu);
}

static int sax_name_start(nj_s4 c)
{
    return c == ':' || c == '_' || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || (c >= 0xc0 && c <= 0xd6) ||
           (c >= 0xd8 && c <= 0xf6) || (c >= 0xf8 && c <= 0x2ff) ||
           (c >= 0x370 && c <= 0x37d) || (c >= 0x37f && c <= 0x1fff) ||
           (c >= 0x200c && c <= 0x200d) || (c >= 0x2070 && c <= 0x218f) ||
           (c >= 0x2c00 && c <= 0x2fef) || (c >= 0x3001 && c <= 0xd7ff) ||
           (c >= 0xf900 && c <= 0xfdcf) || (c >= 0xfdf0 && c <= 0xfffd) ||
           (c >= 0x10000 && c <= 0xeffff);
}

static int sax_name_char(nj_s4 c)
{
    return sax_name_start(c) || c == '-' || c == '.' ||
           (c >= '0' && c <= '9') || c == 0xb7 ||
           (c >= 0x300 && c <= 0x36f) || (c >= 0x203f && c <= 0x2040);
}

static int sax_byte_raw(sax_reader *p)
{
    nj_value a[3], r;
    if (nj_has_exception(p->vm)) return -2;
    if (p->pos == p->end) {
        a[0].r = p->bytes; a[1].i = 0; a[2].i = 4096;
        if (nj_call(p->vm, p->stream, "java/io/InputStream", "read", "([BII)I",
                    a, &r) != NJ_OK) return -2;
        if (r.i == -1) return -1;
        if (r.i < -1 || r.i > 4096) {
            (void)nj_throw(p->vm, "java/io/IOException", "invalid stream read count");
            return -2;
        }
        if (r.i == 0) {
            if (nj_call(p->vm, p->stream, "java/io/InputStream", "read", "()I",
                        NULL, &r) != NJ_OK) return -2;
            if (r.i >= -1 && r.i <= 255) return r.i;
            (void)nj_throw(p->vm, "java/io/IOException", "invalid stream byte");
            return -2;
        }
        p->pos = 0; p->end = (nj_u4)r.i;
    }
    return ((nj_u1 *)nj_array_data(p->bytes))[p->pos++];
}

static int sax_byte(sax_reader *p)
{
    if (p->prefix_pos < p->prefix_len) return p->prefix[p->prefix_pos++];
    return sax_byte_raw(p);
}

static int sax_encoding(sax_reader *p)
{
    int c;
    while (p->prefix_len < 4u) {
        c = sax_byte_raw(p);
        if (c == -2) return 0;
        if (c < 0) break;
        p->prefix[p->prefix_len++] = (nj_u1)c;
    }
    if (p->prefix_len >= 2u && p->prefix[0] == 0xff && p->prefix[1] == 0xfe) {
        p->encoding = 1; p->prefix_pos = 2;
    } else if (p->prefix_len >= 2u && p->prefix[0] == 0xfe && p->prefix[1] == 0xff) {
        p->encoding = 2; p->prefix_pos = 2;
    } else if (p->prefix_len >= 3u && p->prefix[0] == 0xef &&
               p->prefix[1] == 0xbb && p->prefix[2] == 0xbf) {
        p->prefix_pos = 3;
    } else if (p->prefix_len >= 4u && p->prefix[0] == '<' &&
               p->prefix[1] == 0 && p->prefix[2] == '?' && p->prefix[3] == 0) {
        p->encoding = 1;
    } else if (p->prefix_len >= 4u && p->prefix[0] == 0 &&
               p->prefix[1] == '<' && p->prefix[2] == 0 && p->prefix[3] == '?') {
        p->encoding = 2;
    }
    return 1;
}

static nj_s4 sax_decode(sax_reader *p)
{
    int a = sax_byte(p), b, n, i;
    nj_u4 c, low, min;
    if (a < 0) return a;
    if (p->encoding) {
        b = sax_byte(p);
        if (b < 0) { sax_fail(p, "truncated UTF-16 code unit"); return -2; }
        c = p->encoding == 1 ? (nj_u4)(a | (b << 8)) : (nj_u4)((a << 8) | b);
        if (c >= 0xd800u && c <= 0xdbffu) {
            a = sax_byte(p);
            if (a < 0) { sax_fail(p, "truncated UTF-16 surrogate"); return -2; }
            b = sax_byte(p);
            if (b < 0) { sax_fail(p, "truncated UTF-16 surrogate"); return -2; }
            low = p->encoding == 1 ? (nj_u4)(a | (b << 8)) : (nj_u4)((a << 8) | b);
            if (low < 0xdc00u || low > 0xdfffu) {
                sax_fail(p, "invalid UTF-16 surrogate pair"); return -2;
            }
            c = 0x10000u + ((c - 0xd800u) << 10) + low - 0xdc00u;
        }
    } else {
        c = (nj_u4)a;
        if (a >= 0x80) {
            if (a >= 0xc2 && a <= 0xdf) { n = 1; c &= 0x1fu; min = 0x80u; }
            else if (a >= 0xe0 && a <= 0xef) { n = 2; c &= 0xfu; min = 0x800u; }
            else if (a >= 0xf0 && a <= 0xf4) { n = 3; c &= 7u; min = 0x10000u; }
            else { sax_fail(p, "invalid UTF-8 leading byte"); return -2; }
            for (i = 0; i < n; i++) {
                b = sax_byte(p);
                if (b < 0x80 || b > 0xbf) {
                    sax_fail(p, "invalid UTF-8 continuation"); return -2;
                }
                c = (c << 6) | ((nj_u4)b & 0x3fu);
            }
            if (c < min) { sax_fail(p, "overlong UTF-8 sequence"); return -2; }
        }
    }
    if (!sax_xml_char(c) || (p->ascii && c > 127u)) {
        sax_fail(p, "invalid XML character or encoding"); return -2;
    }
    if (++p->chars > NJ_SAX_MAX_CHARS) {
        sax_fail(p, "XML document limit exceeded"); return -2;
    }
    return (nj_s4)c;
}

static nj_s4 sax_peek(sax_reader *p)
{
    nj_s4 c;
    if (p->loaded) return p->look;
    do {
        c = sax_decode(p);
        if (p->skip_lf) { p->skip_lf = 0; if (c == '\n') continue; }
        break;
    } while (1);
    if (c == '\r') { p->skip_lf = 1; c = '\n'; }
    p->look = c; p->loaded = 1;
    return c;
}

static nj_s4 sax_take(sax_reader *p)
{
    nj_s4 c = sax_peek(p);
    p->loaded = 0;
    return c;
}

static int sax_expect(sax_reader *p, const char *literal)
{
    while (*literal) {
        if (sax_take(p) != (nj_u1)*literal++) return sax_fail(p, "unexpected XML syntax");
    }
    return 1;
}

static int sax_skip_space(sax_reader *p)
{
    int n = 0;
    while (sax_space(sax_peek(p))) { (void)sax_take(p); n = 1; }
    return n;
}

static int sax_push(sax_reader *p, sax_token *t, nj_u4 c)
{
    nj_u4 need = t->len + (c >= 0x10000u ? 2u : 1u);
    if (need > NJ_SAX_MAX_TOKEN) return sax_fail(p, "XML token limit exceeded");
    if (need > t->cap) {
        nj_u4 cap = t->cap ? t->cap * 2u : 64u;
        nj_u2 *next;
        if (cap > NJ_SAX_MAX_TOKEN) cap = NJ_SAX_MAX_TOKEN;
        next = (nj_u2 *)nj_host_alloc(p->vm, (nj_size)cap * sizeof(nj_u2));
        if (!next) { (void)nj_throw(p->vm, "java/lang/OutOfMemoryError", NULL); return 0; }
        if (t->len) memcpy(next, t->data, t->len * sizeof(nj_u2));
        nj_host_free(p->vm, t->data); t->data = next; t->cap = cap;
    }
    if (c >= 0x10000u) {
        c -= 0x10000u; t->data[t->len++] = (nj_u2)(0xd800u + (c >> 10));
        t->data[t->len++] = (nj_u2)(0xdc00u + (c & 0x3ffu));
    } else t->data[t->len++] = (nj_u2)c;
    return 1;
}

static nj_ref sax_token_string(sax_reader *p, sax_token *t)
{
    nj_ref s = nj_string_new_chars(p->vm, t->data, t->len);
    nj_host_free(p->vm, t->data); t->data = NULL; t->len = t->cap = 0;
    return s;
}

static nj_ref sax_name(sax_reader *p)
{
    sax_token t = { NULL, 0, 0 };
    if (!sax_name_start(sax_peek(p))) { sax_fail(p, "XML name expected"); return NULL; }
    do {
        if (!sax_push(p, &t, (nj_u4)sax_take(p))) {
            nj_host_free(p->vm, t.data); return NULL;
        }
    } while (sax_name_char(sax_peek(p)));
    return sax_token_string(p, &t);
}

static int sax_equal(nj_ref a, nj_ref b)
{
    return a && b && nj_string_length(a) == nj_string_length(b) &&
           memcmp(nj_string_data(a), nj_string_data(b),
                  nj_string_length(a) * sizeof(nj_u2)) == 0;
}

static nj_s4 sax_entity(sax_reader *p)
{
    nj_u4 c = 0, base = 10, digits = 0;
    int d;
    char name[6]; unsigned len = 0;
    if (sax_take(p) != '&') return -2;
    if (sax_peek(p) == '#') {
        (void)sax_take(p);
        if (sax_peek(p) == 'x') { base = 16; (void)sax_take(p); }
        while (sax_peek(p) != ';') {
            nj_s4 v = sax_take(p);
            if (v >= '0' && v <= '9') d = (int)(v - '0');
            else if (base == 16u && v >= 'a' && v <= 'f') d = (int)(v - 'a') + 10;
            else if (base == 16u && v >= 'A' && v <= 'F') d = (int)(v - 'A') + 10;
            else { sax_fail(p, "invalid character reference"); return -2; }
            if (c > (0x10ffffu - (nj_u4)d) / base) {
                sax_fail(p, "character reference overflow"); return -2;
            }
            c = c * base + (nj_u4)d; digits++;
        }
        (void)sax_take(p);
        if (!digits || !sax_xml_char(c)) { sax_fail(p, "invalid character reference"); return -2; }
        return (nj_s4)c;
    }
    while (sax_peek(p) != ';' && len < sizeof(name) - 1u) {
        nj_s4 v = sax_take(p);
        if (v < 'a' || v > 'z') { sax_fail(p, "invalid entity reference"); return -2; }
        name[len++] = (char)v;
    }
    name[len] = 0;
    if (sax_take(p) != ';') { sax_fail(p, "invalid entity reference"); return -2; }
    if (strcmp(name, "amp") == 0) return '&';
    if (strcmp(name, "lt") == 0) return '<';
    if (strcmp(name, "gt") == 0) return '>';
    if (strcmp(name, "apos") == 0) return '\'';
    if (strcmp(name, "quot") == 0) return '"';
    sax_fail(p, "unknown entity (DTDs are not supported)"); return -2;
}

static nj_ref sax_value(sax_reader *p, int declaration)
{
    sax_token t = { NULL, 0, 0 };
    nj_s4 quote = sax_take(p), c;
    if (quote != '\'' && quote != '"') { sax_fail(p, "quoted attribute expected"); return NULL; }
    while ((c = sax_peek(p)) != quote) {
        if (c < 0 || c == '<' || (declaration && c == '&')) {
            sax_fail(p, "invalid attribute value"); goto fail;
        }
        if (c == '&') c = sax_entity(p);
        else { (void)sax_take(p); if (sax_space(c)) c = ' '; }
        if (c < 0 || !sax_push(p, &t, (nj_u4)c)) goto fail;
    }
    (void)sax_take(p);
    return sax_token_string(p, &t);
fail:
    nj_host_free(p->vm, t.data);
    return NULL;
}

static nj_field *sax_entries(nj_vm *vm)
{
    return nj_field_lookup(vm, ATTRS, "entries", "[Ljava/lang/String;", 0);
}

static nj_field *sax_length(nj_vm *vm)
{
    return nj_field_lookup(vm, ATTRS, "length", "I", 0);
}

static int sax_add_attr(sax_reader *p, nj_ref obj, nj_ref name, nj_ref value)
{
    nj_field *f = sax_entries(p->vm), *n = sax_length(p->vm);
    nj_ref entries = nj_field_read_ref(p->vm, obj, f), next;
    nj_u4 i, count = (nj_u4)nj_field_read_int(p->vm, obj, n);
    if (count >= NJ_SAX_MAX_ATTRIBUTES) return sax_fail(p, "XML attribute limit exceeded");
    for (i = 0; i < count; i++) {
        if (sax_equal(nj_array_get_ref(entries, i * 2u), name))
            return sax_fail(p, "duplicate XML attribute");
    }
    if (!entries || (count + 1u) * 2u > nj_array_length(entries)) {
        nj_u4 cap = count ? count * 4u : 16u;
        next = nj_object_array_new(p->vm, "java/lang/String", cap);
        if (!next) return 0;
        for (i = 0; i < count * 2u; i++)
            nj_array_set_ref(next, i, nj_array_get_ref(entries, i));
        nj_field_write_ref(p->vm, obj, f, next); entries = next;
    }
    nj_array_set_ref(entries, count * 2u, name);
    nj_array_set_ref(entries, count * 2u + 1u, value);
    nj_field_write_int(p->vm, obj, n, (nj_s4)(count + 1u));
    return 1;
}

static int sax_callback(sax_reader *p, const char *name, const char *desc, nj_value *a)
{
    nj_value ret;
    return nj_call(p->vm, p->handler, HANDLER, name, desc, a, &ret) == NJ_OK;
}

static int sax_flush(sax_reader *p)
{
    nj_u4 mark;
    nj_value a[3];
    int ok;
    if (!p->text_len) return 1;
    mark = nj_root_mark(p->vm);
    a[0].r = nj_root(p->vm, nj_char_array_new(p->vm, p->text_len));
    if (!a[0].r) return 0;
    memcpy(nj_array_data(a[0].r), p->text, p->text_len * sizeof(nj_u2));
    a[1].i = 0; a[2].i = (nj_s4)p->text_len; p->text_len = 0;
    ok = sax_callback(p, "characters", "([CII)V", a);
    nj_root_release(p->vm, mark);
    return ok;
}

static int sax_text(sax_reader *p, nj_u4 c)
{
    if (p->text_len + (c >= 0x10000u ? 2u : 1u) > COUNT(p->text)) {
        if (!sax_flush(p)) return 0;
    }
    if (c >= 0x10000u) {
        c -= 0x10000u; p->text[p->text_len++] = (nj_u2)(0xd800u + (c >> 10));
        p->text[p->text_len++] = (nj_u2)(0xdc00u + (c & 0x3ffu));
    } else p->text[p->text_len++] = (nj_u2)c;
    return 1;
}

static int sax_comment(sax_reader *p)
{
    nj_s4 c;
    if (!sax_expect(p, "--")) return 0;
    while ((c = sax_take(p)) >= 0) {
        if (c == '-' && sax_peek(p) == '-') {
            (void)sax_take(p);
            return sax_take(p) == '>' ? 1 : sax_fail(p, "double hyphen inside XML comment");
        }
    }
    return sax_fail(p, "unterminated XML comment");
}

static int sax_cdata(sax_reader *p)
{
    nj_u4 brackets = 0;
    nj_s4 c;
    if (!sax_expect(p, "[CDATA[")) return 0;
    while ((c = sax_take(p)) >= 0) {
        if (c == ']') { brackets++; continue; }
        if (c == '>' && brackets >= 2u) {
            brackets -= 2u;
            while (brackets) { if (!sax_text(p, ']')) return 0; brackets--; }
            return 1;
        }
        while (brackets) { if (!sax_text(p, ']')) return 0; brackets--; }
        if (!sax_text(p, (nj_u4)c)) return 0;
    }
    return sax_fail(p, "unterminated CDATA");
}

static int sax_case(nj_ref s, const char *literal)
{
    nj_u4 i, len = nj_string_length(s);
    if (len != strlen(literal)) return 0;
    for (i = 0; i < len; i++) {
        nj_u2 c = nj_string_data(s)[i];
        if (c >= 'A' && c <= 'Z') c = (nj_u2)(c + ('a' - 'A'));
        if (c != (nj_u2)(nj_u1)literal[i]) return 0;
    }
    return 1;
}

/* Called after '<?'. Only the XML declaration has attribute-like syntax. */
static int sax_pi(sax_reader *p, int initial)
{
    nj_u4 mark = nj_root_mark(p->vm), part = 0;
    nj_ref target = nj_root(p->vm, sax_name(p));
    sax_token data = { NULL, 0, 0 };
    int ok = 0, decl;
    if (!target) goto done;
    decl = sax_case(target, "xml");
    if (decl && (!initial || !nj_string_equals_utf8(target, "xml"))) {
        sax_fail(p, "XML declaration must be first"); goto done;
    }
    if (!decl) {
        nj_s4 c;
        if (!sax_skip_space(p) && sax_peek(p) != '?') {
            sax_fail(p, "processing instruction separator expected"); goto done;
        }
        while ((c = sax_take(p)) >= 0) {
            if (c == '?' && sax_peek(p) == '>') {
                nj_value a[2];
                (void)sax_take(p);
                a[0].r = target;
                a[1].r = nj_root(p->vm, sax_token_string(p, &data));
                if (a[1].r)
                    ok = sax_callback(p, "processingInstruction", "(" STR STR ")V", a);
                goto done;
            }
            if (!sax_push(p, &data, (nj_u4)c)) goto done;
        }
        sax_fail(p, "unterminated processing instruction"); goto done;
    }
    while (sax_skip_space(p)) {
        nj_u4 item = nj_root_mark(p->vm);
        nj_ref name, value;
        if (sax_peek(p) == '?') break;
        name = nj_root(p->vm, sax_name(p));
        if (!name) goto done;
        (void)sax_skip_space(p);
        if (!sax_expect(p, "=")) goto done;
        (void)sax_skip_space(p);
        value = nj_root(p->vm, sax_value(p, 1));
        if (!value) goto done;
        if (part == 0u && nj_string_equals_utf8(name, "version") &&
            nj_string_equals_utf8(value, "1.0")) part = 1;
        else if (part == 1u && nj_string_equals_utf8(name, "encoding")) {
            if ((sax_case(value, "utf-8") && p->encoding == 0) ||
                (sax_case(value, "utf-16") && p->encoding != 0) ||
                (sax_case(value, "utf-16le") && p->encoding == 1) ||
                (sax_case(value, "utf-16be") && p->encoding == 2)) { /* matched */ }
            else if (sax_case(value, "us-ascii") && p->encoding == 0) p->ascii = 1;
            else { sax_fail(p, "unsupported or conflicting XML encoding"); goto done; }
            part = 2;
        } else if ((part == 1u || part == 2u) &&
                   nj_string_equals_utf8(name, "standalone") &&
                   (nj_string_equals_utf8(value, "yes") || nj_string_equals_utf8(value, "no"))) part = 3;
        else { sax_fail(p, "invalid XML declaration"); goto done; }
        nj_root_release(p->vm, item);
    }
    if (!part) { sax_fail(p, "XML version 1.0 required"); goto done; }
    ok = sax_expect(p, "?>");
done:
    if (data.data) nj_host_free(p->vm, data.data);
    nj_root_release(p->vm, mark);
    return ok;
}

/* '<' has already been consumed; recursion is bounded before entering a child. */
static int sax_element(sax_reader *p)
{
    nj_u4 mark = nj_root_mark(p->vm);
    nj_ref name = NULL, attrs;
    nj_value a[4];
    int ok = 0, empty = 0, ws;
    nj_s4 c;
    unsigned closing_brackets = 0;
    if (p->depth >= NJ_SAX_MAX_DEPTH) return sax_fail(p, "XML nesting limit exceeded");
    p->depth++;
    name = nj_root(p->vm, sax_name(p));
    if (!name) goto done;
    attrs = nj_root(p->vm, nj_object_new(p->vm, ATTRS));
    if (!attrs) goto done;
    for (;;) {
        nj_u4 item;
        nj_ref key, value;
        ws = sax_skip_space(p); c = sax_peek(p);
        if (c == '>') { (void)sax_take(p); break; }
        if (c == '/') { if (!sax_expect(p, "/>")) goto done; empty = 1; break; }
        if (!ws) { sax_fail(p, "attribute separator expected"); goto done; }
        item = nj_root_mark(p->vm);
        key = nj_root(p->vm, sax_name(p));
        if (!key) goto done;
        (void)sax_skip_space(p);
        if (!sax_expect(p, "=")) goto done;
        (void)sax_skip_space(p);
        value = nj_root(p->vm, sax_value(p, 0));
        if (!value || !sax_add_attr(p, attrs, key, value)) goto done;
        nj_root_release(p->vm, item);
    }
    a[0].r = p->empty; a[1].r = p->empty; a[2].r = name; a[3].r = attrs;
    if (!sax_callback(p, "startElement", START_DESC, a)) goto done;
    /* The callback may retain attrs; never recycle its array for another tag. */
    if (!empty) for (;;) {
        c = sax_peek(p);
        if (c < 0) { sax_fail(p, "unterminated XML element"); goto done; }
        if (c == '<') {
            if (!sax_flush(p)) goto done;
            closing_brackets = 0;
            (void)sax_take(p); c = sax_peek(p);
            if (c == '/') {
                nj_ref end;
                nj_u4 item = nj_root_mark(p->vm);
                (void)sax_take(p); end = nj_root(p->vm, sax_name(p));
                if (!end) goto done;
                if (!sax_equal(name, end)) { sax_fail(p, "mismatched XML end tag"); goto done; }
                (void)sax_skip_space(p);
                if (!sax_expect(p, ">")) goto done;
                nj_root_release(p->vm, item); break;
            }
            if (c == '!') {
                (void)sax_take(p); c = sax_peek(p);
                if (c == '-') { if (!sax_comment(p)) goto done; }
                else if (c == '[') { if (!sax_cdata(p)) goto done; }
                else { sax_fail(p, "DTD declarations are not supported"); goto done; }
            } else if (c == '?') {
                (void)sax_take(p); if (!sax_pi(p, 0)) goto done;
            } else if (!sax_element(p)) goto done;
        } else {
            if (c == '&') { c = sax_entity(p); closing_brackets = 0; }
            else {
                (void)sax_take(p);
                if (c == '>' && closing_brackets >= 2u) {
                    sax_fail(p, "CDATA terminator outside CDATA"); goto done;
                }
                closing_brackets = c == ']' ? closing_brackets + 1u : 0u;
            }
            if (c < 0 || !sax_text(p, (nj_u4)c)) goto done;
        }
    }
    if (!sax_flush(p)) goto done;
    a[0].r = p->empty; a[1].r = p->empty; a[2].r = name;
    ok = sax_callback(p, "endElement", END_DESC, a);
done:
    p->depth--;
    nj_root_release(p->vm, mark);
    return ok;
}

FN(sax_parse)
{
    sax_reader p;
    nj_u4 mark = nj_root_mark(vm);
    int seen = 0, initial = 1, ok = 0;
    nj_s4 c;
    memset(&p, 0, sizeof(p)); p.vm = vm;
    p.stream = NJ_ARG_REF(args, 0); p.handler = NJ_ARG_REF(args, 1);
    if (!p.stream || !p.handler)
        return nj_throw(vm, "java/lang/IllegalArgumentException", "null stream or handler");
    if (!nj_root(vm, p.stream) || !nj_root(vm, p.handler)) goto done;
    p.bytes = nj_root(vm, nj_byte_array_new(vm, 4096));
    if (!p.bytes) goto done;
    p.empty = nj_root(vm, nj_string_new(vm, ""));
    if (!p.empty || !sax_encoding(&p) || !sax_callback(&p, "startDocument", "()V", NULL)) goto done;
    while ((c = sax_peek(&p)) >= 0) {
        if (sax_space(c)) { (void)sax_take(&p); initial = 0; continue; }
        if (c != '<') { sax_fail(&p, "text outside XML root element"); goto done; }
        (void)sax_take(&p); c = sax_peek(&p);
        if (c == '?') { (void)sax_take(&p); if (!sax_pi(&p, initial)) goto done; }
        else if (c == '!') {
            (void)sax_take(&p);
            if (sax_peek(&p) != '-') { sax_fail(&p, "DTD declarations are not supported"); goto done; }
            if (!sax_comment(&p)) goto done;
        } else {
            if (seen) { sax_fail(&p, "multiple XML root elements"); goto done; }
            if (!sax_element(&p)) goto done;
            seen = 1;
        }
        initial = 0;
    }
    if (c == -2) goto done;
    if (!seen) { sax_fail(&p, "XML root element required"); goto done; }
    ok = sax_callback(&p, "endDocument", "()V", NULL);
done:
    nj_root_release(vm, mark);
    return ok ? NJ_OK : NJ_ERR;
}

FN(sax_noop) { return NJ_OK; }
FN(sax_false) { ret->i = 0; return NJ_OK; }
FN(sax_factory) { ret->r = nj_object_new(vm, FACTORY); return ret->r ? NJ_OK : NJ_ERR; }
FN(sax_new_parser)
{
    nj_ref self = NJ_THIS(args);
    nj_field *ns = nj_field_lookup(vm, FACTORY, "namespaceAware", "Z", 0);
    nj_field *val = nj_field_lookup(vm, FACTORY, "validating", "Z", 0);
    if (nj_field_read_int(vm, self, ns) || nj_field_read_int(vm, self, val))
        return nj_throw(vm, CONFIGERR, "namespace processing and validation are not supported");
    ret->r = nj_object_new(vm, PARSER); return ret->r ? NJ_OK : NJ_ERR;
}
FN(sax_set_ns)
{
    nj_field_write_int(vm, NJ_THIS(args), nj_field_lookup(vm, FACTORY, "namespaceAware", "Z", 0),
                       NJ_ARG_INT(args, 0) != 0); return NJ_OK;
}
FN(sax_get_ns)
{
    ret->i = nj_field_read_int(vm, NJ_THIS(args), nj_field_lookup(vm, FACTORY, "namespaceAware", "Z", 0));
    return NJ_OK;
}
FN(sax_set_val)
{
    nj_field_write_int(vm, NJ_THIS(args), nj_field_lookup(vm, FACTORY, "validating", "Z", 0),
                       NJ_ARG_INT(args, 0) != 0); return NJ_OK;
}
FN(sax_get_val)
{
    ret->i = nj_field_read_int(vm, NJ_THIS(args), nj_field_lookup(vm, FACTORY, "validating", "Z", 0));
    return NJ_OK;
}
FN(sax_attr_length)
{
    ret->i = nj_field_read_int(vm, NJ_THIS(args), sax_length(vm)); return NJ_OK;
}
static nj_ref sax_attr_at(nj_vm *vm, nj_ref self, nj_s4 index, int value)
{
    nj_s4 count = nj_field_read_int(vm, self, sax_length(vm));
    nj_ref entries = nj_field_read_ref(vm, self, sax_entries(vm));
    return index < 0 || index >= count ? NULL : nj_array_get_ref(entries, (nj_u4)index * 2u + (value ? 1u : 0u));
}
static nj_s4 sax_attr_find(nj_vm *vm, nj_ref self, nj_ref name)
{
    nj_s4 i, count = nj_field_read_int(vm, self, sax_length(vm));
    for (i = 0; i < count; i++) if (sax_equal(sax_attr_at(vm, self, i, 0), name)) return i;
    return -1;
}
FN(sax_attr_name) { ret->r = sax_attr_at(vm, NJ_THIS(args), NJ_ARG_INT(args, 0), 0); return NJ_OK; }
FN(sax_attr_value_at) { ret->r = sax_attr_at(vm, NJ_THIS(args), NJ_ARG_INT(args, 0), 1); return NJ_OK; }
FN(sax_attr_value)
{
    nj_s4 i = sax_attr_find(vm, NJ_THIS(args), NJ_ARG_REF(args, 0));
    ret->r = sax_attr_at(vm, NJ_THIS(args), i, 1); return NJ_OK;
}
FN(sax_attr_index) { ret->i = sax_attr_find(vm, NJ_THIS(args), NJ_ARG_REF(args, 0)); return NJ_OK; }
FN(sax_attr_empty)
{
    ret->r = sax_attr_at(vm, NJ_THIS(args), NJ_ARG_INT(args, 0), 0) ? nj_string_new(vm, "") : NULL;
    return ret->r || !nj_has_exception(vm) ? NJ_OK : NJ_ERR;
}
FN(sax_attr_type_at)
{
    ret->r = sax_attr_at(vm, NJ_THIS(args), NJ_ARG_INT(args, 0), 0) ? nj_string_new(vm, "CDATA") : NULL;
    return ret->r || !nj_has_exception(vm) ? NJ_OK : NJ_ERR;
}
FN(sax_attr_type)
{
    ret->r = sax_attr_find(vm, NJ_THIS(args), NJ_ARG_REF(args, 0)) >= 0 ? nj_string_new(vm, "CDATA") : NULL;
    return ret->r || !nj_has_exception(vm) ? NJ_OK : NJ_ERR;
}
/* Namespace processing is disabled: SAX2 namespace-name lookups are absent. */
FN(sax_attr_ns_value) { ret->r = NULL; return NJ_OK; }
FN(sax_attr_ns_index) { ret->i = -1; return NJ_OK; }

#define M(n,d,f) { n,d,f }
#define F(n,d) { n,d,0 }
static const nj_builtin_field factory_fields[] = { F("namespaceAware", "Z"), F("validating", "Z") };
static const nj_builtin_field attrs_fields[] = { F("entries", "[Ljava/lang/String;"), F("length", "I") };
#define ATTR_METHODS(flag) \
    M("getLength", "()I", flag), \
    M("getQName", "(I)" STR, flag), M("getURI", "(I)" STR, flag), \
    M("getLocalName", "(I)" STR, flag), M("getValue", "(I)" STR, flag), \
    M("getValue", "(" STR ")" STR, flag), M("getValue", "(" STR STR ")" STR, flag), \
    M("getType", "(I)" STR, flag), M("getType", "(" STR ")" STR, flag), \
    M("getType", "(" STR STR ")" STR, flag), \
    M("getIndex", "(" STR ")I", flag), M("getIndex", "(" STR STR ")I", flag)
static const nj_builtin_method attr_iface[] = { ATTR_METHODS(NJ_ACC_PUBLIC | NJ_ACC_ABSTRACT) };
static const nj_builtin_method attr_methods[] = { M("<init>", "()V", 0), ATTR_METHODS(0) };
static const nj_builtin_method factory_methods[] = {
    M("<init>", "()V", 0), M("newInstance", "()L" FACTORY ";", NJ_ACC_STATIC),
    M("newSAXParser", "()L" PARSER ";", 0),
    M("setNamespaceAware", "(Z)V", 0), M("isNamespaceAware", "()Z", 0),
    M("setValidating", "(Z)V", 0), M("isValidating", "()Z", 0)
};
static const nj_builtin_method parser_methods[] = {
    M("<init>", "()V", 0), M("parse", PARSE_DESC, 0),
    M("isNamespaceAware", "()Z", 0), M("isValidating", "()Z", 0)
};
static const nj_builtin_method handler_methods[] = {
    M("<init>", "()V", 0), M("startDocument", "()V", 0), M("endDocument", "()V", 0),
    M("startElement", START_DESC, 0), M("endElement", END_DESC, 0),
    M("characters", "([CII)V", 0), M("ignorableWhitespace", "([CII)V", 0),
    M("processingInstruction", "(" STR STR ")V", 0),
    M("startPrefixMapping", "(" STR STR ")V", 0), M("endPrefixMapping", "(" STR ")V", 0),
    M("skippedEntity", "(" STR ")V", 0),
    M("warning", "(Lorg/xml/sax/SAXParseException;)V", 0),
    M("error", "(Lorg/xml/sax/SAXParseException;)V", 0),
    M("fatalError", "(Lorg/xml/sax/SAXParseException;)V", 0)
};
#define CLASS(n,s,f,fields,methods) { n,s,f,fields,COUNT(fields),methods,COUNT(methods) }
const nj_builtin_class nj_lib_sax_classes[] = {
    CLASS(FACTORY, "java/lang/Object", NJ_ACC_PUBLIC, factory_fields, factory_methods),
    { PARSER, "java/lang/Object", NJ_ACC_PUBLIC, NULL, 0, parser_methods, COUNT(parser_methods) },
    { HANDLER, "java/lang/Object", NJ_ACC_PUBLIC, NULL, 0, handler_methods, COUNT(handler_methods) },
    { IFACE, "java/lang/Object", NJ_ACC_PUBLIC | NJ_ACC_INTERFACE | NJ_ACC_ABSTRACT,
      NULL, 0, attr_iface, COUNT(attr_iface) },
    CLASS(ATTRS, "java/lang/Object", NJ_ACC_PUBLIC, attrs_fields, attr_methods),
    { "org/xml/sax/SAXException", "java/lang/Exception", NJ_ACC_PUBLIC, NULL, 0, NULL, 0 },
    { SAXERR, "org/xml/sax/SAXException", NJ_ACC_PUBLIC, NULL, 0, NULL, 0 },
    { CONFIGERR, "java/lang/Exception", NJ_ACC_PUBLIC, NULL, 0, NULL, 0 }
};
const int nj_lib_sax_class_count = COUNT(nj_lib_sax_classes);
#define N(c,n,d,fn) { c,n,d,fn }
#define H(n,d) N(HANDLER,n,d,sax_noop)
const nj_native_entry nj_lib_sax_table[] = {
    N(FACTORY,"<init>","()V",sax_noop), N(FACTORY,"newInstance","()L" FACTORY ";",sax_factory),
    N(FACTORY,"newSAXParser","()L" PARSER ";",sax_new_parser),
    N(FACTORY,"setNamespaceAware","(Z)V",sax_set_ns), N(FACTORY,"isNamespaceAware","()Z",sax_get_ns),
    N(FACTORY,"setValidating","(Z)V",sax_set_val), N(FACTORY,"isValidating","()Z",sax_get_val),
    N(PARSER,"<init>","()V",sax_noop), N(PARSER,"parse",PARSE_DESC,sax_parse),
    N(PARSER,"isNamespaceAware","()Z",sax_false), N(PARSER,"isValidating","()Z",sax_false),
    H("<init>","()V"), H("startDocument","()V"), H("endDocument","()V"),
    H("startElement",START_DESC), H("endElement",END_DESC), H("characters","([CII)V"),
    H("ignorableWhitespace","([CII)V"), H("processingInstruction","(" STR STR ")V"),
    H("startPrefixMapping","(" STR STR ")V"), H("endPrefixMapping","(" STR ")V"),
    H("skippedEntity","(" STR ")V"), H("warning","(Lorg/xml/sax/SAXParseException;)V"),
    H("error","(Lorg/xml/sax/SAXParseException;)V"), H("fatalError","(Lorg/xml/sax/SAXParseException;)V"),
    N(ATTRS,"<init>","()V",sax_noop), N(ATTRS,"getLength","()I",sax_attr_length),
    N(ATTRS,"getQName","(I)" STR,sax_attr_name), N(ATTRS,"getURI","(I)" STR,sax_attr_empty),
    N(ATTRS,"getLocalName","(I)" STR,sax_attr_empty), N(ATTRS,"getValue","(I)" STR,sax_attr_value_at),
    N(ATTRS,"getValue","(" STR ")" STR,sax_attr_value), N(ATTRS,"getValue","(" STR STR ")" STR,sax_attr_ns_value),
    N(ATTRS,"getType","(I)" STR,sax_attr_type_at), N(ATTRS,"getType","(" STR ")" STR,sax_attr_type),
    N(ATTRS,"getType","(" STR STR ")" STR,sax_attr_ns_value),
    N(ATTRS,"getIndex","(" STR ")I",sax_attr_index), N(ATTRS,"getIndex","(" STR STR ")I",sax_attr_ns_index)
};
const int nj_lib_sax_count = COUNT(nj_lib_sax_table);

/* Builtin descriptors do not encode interface lists. Install this relation once
 * after linking, rather than lying about Attributes being a concrete class. */
nj_status nj_lib_sax_init(nj_vm *vm)
{
    nj_class *c = nj_class_resolve(vm, ATTRS), *iface = nj_class_resolve(vm, IFACE);
    if (!c || !iface) return NJ_ERR;
    if (c->ninterfaces) return NJ_OK;
    c->interfaces = (nj_class **)nj_host_alloc(vm, sizeof(nj_class *));
    if (!c->interfaces) return NJ_ERR_NOMEM;
    c->interfaces[0] = iface; c->ninterfaces = 1;
    return NJ_OK;
}
#else
const nj_builtin_class nj_lib_sax_classes[] = { { NULL, NULL, 0, NULL, 0, NULL, 0 } };
const int nj_lib_sax_class_count = 0;
const nj_native_entry nj_lib_sax_table[] = { { NULL, NULL, NULL, NULL } };
const int nj_lib_sax_count = 0;
nj_status nj_lib_sax_init(nj_vm *vm) { (void)vm; return NJ_OK; }
#endif
