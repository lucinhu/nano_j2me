/* Character-stream encoders/decoders.  Included by nj_lib_io.c after the
 * unencoded Reader/Writer defaults; only the two concrete stream classes use
 * these natives.  In particular a user Reader subclass needs no stream state.
 * No read-ahead buffer: UTF-8 needs just a pending low surrogate or one byte
 * rejected as a continuation.  The writer retains a high surrogate until the
 * next write (flush is not end-of-input). */
enum { CS_LATIN1 = 0, CS_UTF8 = 1, CS_ASCII = 2 };
#if NJ_ENABLE_STREAM_UTF8
static const nj_builtin_field cs_fields[] = {
    { "csState", "I", NJ_ACC_PRIVATE }
};
#define CS_FIELDS cs_fields
#define CS_NFIELDS 1u
/* mode: 2 bits; pending surrogate: 16 bits; pushback byte + 1: 9 bits. */
#define CS_PENDING_MASK (0xffffu << 2)
#define CS_PUSH_MASK (0x1ffu << 18)
static nj_u4 cs_state(nj_vm *vm, nj_ref self, const char *cls)
{
    nj_field *f = nj_field_lookup(vm, cls, "csState", "I", 0);
    return f ? (nj_u4)nj_field_read_int(vm, self, f) : 0u;
}
static void cs_set_state(nj_vm *vm, nj_ref self, const char *cls, nj_u4 state)
{
    nj_field *f = nj_field_lookup(vm, cls, "csState", "I", 0);
    if (f) nj_field_write_int(vm, self, f, (nj_s4)state);
}
#else
#define CS_FIELDS NULL
#define CS_NFIELDS 0u
#define cs_state(vm, self, cls) 0u
#define cs_set_state(vm, self, cls, state) ((void)(state))
#endif

static int cs_name_is(nj_ref name, const char *alias)
{
    const nj_u2 *chars = nj_string_chars(name);
    nj_u4 i, n = nj_string_len(name);
    for (i = 0; i < n; ++i) {
        unsigned int a = chars[i], b = (unsigned char)alias[i];
        if (!b) return 0;
        if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
        if (b >= 'a' && b <= 'z') b -= 'a' - 'A';
        if (a != b) return 0;
    }
    return alias[n] == '\0';
}

static int cs_encoding(nj_vm *vm, nj_ref name)
{
    static const char *const latin[] = {
        "ISO-8859-1", "ISO8859-1", "ISO8859_1", "ISO_8859-1", "ISO_8859_1",
        "8859_1", "8859-1", "ISO-IR-100", "LATIN1", "LATIN-1", "L1",
        "CP819", "IBM819", "819", "CSISOLATIN1"
    };
    nj_u4 i;
    if (!name) {
        (void)nj_throw(vm, "java/lang/NullPointerException", "encoding is null");
        return -1;
    }
    for (i = 0; i < sizeof(latin) / sizeof(latin[0]); ++i)
        if (cs_name_is(name, latin[i])) return CS_LATIN1;
#if NJ_ENABLE_STREAM_UTF8
    if (cs_name_is(name, "UTF-8") || cs_name_is(name, "UTF8") ||
        cs_name_is(name, "UTF_8") || cs_name_is(name, "UNICODE-1-1-UTF-8"))
        return CS_UTF8;
    {
        static const char *const ascii[] = {
            "ASCII", "US-ASCII", "USASCII", "ISO646-US", "ISO-IR-6", "646",
            "IBM367", "CP367", "CSASCII", "ANSI_X3.4-1968", "ANSI_X3.4-1986"
        };
        for (i = 0; i < sizeof(ascii) / sizeof(ascii[0]); ++i)
            if (cs_name_is(name, ascii[i])) return CS_ASCII;
    }
#endif
    (void)nj_throw(vm, "java/io/UnsupportedEncodingException",
                   "unsupported character-stream encoding");
    return -1;
}

NJFN(isr_init_encoding)
{
    int kind;
    nj_status st;
    if (!NJ_ARG_REF(args, 0))
        return nj_throw(vm, "java/lang/NullPointerException", "stream is null");
    kind = cs_encoding(vm, NJ_ARG_REF(args, 1));
    if (kind < 0) return NJ_ERR;
    st = isr_init(vm, th, args, ret);
    if (st == NJ_OK) cs_set_state(vm, NJ_THIS(args), NJ_C_ISR, (nj_u4)kind);
    return st;
}

NJFN(osw_init_encoding)
{
    int kind;
    nj_status st;
    if (!NJ_ARG_REF(args, 0))
        return nj_throw(vm, "java/lang/NullPointerException", "stream is null");
    kind = cs_encoding(vm, NJ_ARG_REF(args, 1));
    if (kind < 0) return NJ_ERR;
    st = osw_init(vm, th, args, ret);
    if (st == NJ_OK) cs_set_state(vm, NJ_THIS(args), NJ_C_OSW, (nj_u4)kind);
    return st;
}

static nj_status cs_byte(nj_vm *vm, nj_ref in, nj_u4 *state, nj_s4 *byte)
{
    nj_value r;
#if NJ_ENABLE_STREAM_UTF8
    nj_u4 pushed = *state >> 18;
    if (pushed) {
        *state &= ~CS_PUSH_MASK;
        *byte = (nj_s4)(pushed - 1u);
        return NJ_OK;
    }
#else
    (void)state;
#endif
    r.i = -1;
    if (nj_call(vm, in, NJ_C_ISTREAM, "read", "()I", NULL, &r) != NJ_OK)
        return NJ_ERR;
    *byte = r.i;
    return NJ_OK;
}

static nj_status cs_read(nj_vm *vm, nj_ref in, nj_u4 *state, nj_s4 *value)
{
    nj_s4 b;
#if NJ_ENABLE_STREAM_UTF8
    nj_u4 mode = *state & 3u, cp, count, i;
    if (*state & CS_PENDING_MASK) {
        *value = (nj_s4)((*state >> 2) & 0xffffu);
        *state &= ~CS_PENDING_MASK;
        return NJ_OK;
    }
#endif
    if (cs_byte(vm, in, state, &b) != NJ_OK) return NJ_ERR;
    *value = b;
#if NJ_ENABLE_STREAM_UTF8
    if (b < 0 || b < 128 || mode == CS_LATIN1) return NJ_OK;
    *value = 0xfffd;
    if (mode == CS_ASCII) return NJ_OK;
    if (b >= 0xc2 && b <= 0xdf) { cp = (nj_u4)b & 31u; count = 2u; }
    else if (b >= 0xe0 && b <= 0xef) { cp = (nj_u4)b & 15u; count = 3u; }
    else if (b >= 0xf0 && b <= 0xf4) { cp = (nj_u4)b & 7u; count = 4u; }
    else return NJ_OK;
    for (i = 1u; i < count; ++i) {
        nj_s4 next;
        if (cs_byte(vm, in, state, &next) != NJ_OK) return NJ_ERR;
        if (next < 0) return NJ_OK; /* truncated prefix: one replacement */
        if ((next & 0xc0) != 0x80 ||
            (i == 1u && ((b == 0xe0 && next < 0xa0) ||
                         (b == 0xf0 && next < 0x90) ||
                         (b == 0xf4 && next >= 0x90)))) {
            /* The next character's first byte must not disappear. */
            *state = (*state & ~CS_PUSH_MASK) | ((nj_u4)(next + 1) << 18);
            return NJ_OK;
        }
        cp = (cp << 6) | ((nj_u4)next & 63u);
    }
    if (cp >= 0xd800u && cp <= 0xdfffu) return NJ_OK;
    if (cp > 0xffffu) {
        cp -= 0x10000u;
        *state = (*state & ~CS_PENDING_MASK) | ((0xdc00u + (cp & 1023u)) << 2);
        *value = (nj_s4)(0xd800u + (cp >> 10));
    } else *value = (nj_s4)cp;
#endif
    return NJ_OK;
}

NJFN(isr_encoded_read)
{
    nj_ref self = NJ_THIS(args);
    nj_ref in = strm_field(vm, self, NJ_C_READER, "lock", "Ljava/lang/Object;");
    nj_u4 state = cs_state(vm, self, NJ_C_ISR);
    nj_status st;
    if (!in) return nj_throw(vm, "java/io/IOException", "stream is closed");
    st = cs_read(vm, in, &state, &ret->i);
    cs_set_state(vm, self, NJ_C_ISR, state);
    return st;
}

NJFN(isr_encoded_range)
{
    nj_ref self = NJ_THIS(args), chars = NJ_ARG_REF(args, 0);
    nj_ref in = strm_field(vm, self, NJ_C_READER, "lock", "Ljava/lang/Object;");
    nj_s4 off = NJ_ARG_INT(args, 1), len = NJ_ARG_INT(args, 2), i = 0, ch;
    nj_u4 state = cs_state(vm, self, NJ_C_ISR);
    nj_status st = NJ_OK;
    if (!in) return nj_throw(vm, "java/io/IOException", "stream is closed");
    if (!chars) return nj_throw(vm, "java/lang/NullPointerException", "buffer is null");
    if (off < 0 || len < 0 || (nj_u4)off > chars->length ||
        (nj_u4)len > chars->length - (nj_u4)off)
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    while (i < len) {
        st = cs_read(vm, in, &state, &ch);
        if (st != NJ_OK || ch < 0) break;
        ((nj_u2 *)chars->u.bytes)[off + i++] = (nj_u2)ch;
    }
    cs_set_state(vm, self, NJ_C_ISR, state);
    ret->i = (i == 0 && len > 0) ? -1 : i;
    return st;
}

NJFN(isr_encoded_ready)
{
    nj_ref self = NJ_THIS(args);
    nj_ref in = strm_field(vm, self, NJ_C_READER, "lock", "Ljava/lang/Object;");
    if (!in) return nj_throw(vm, "java/io/IOException", "stream is closed");
    if (cs_state(vm, self, NJ_C_ISR) >> 2) { ret->i = 1; return NJ_OK; }
    return isr_ready(vm, th, args, ret);
}

NJFN(isr_stream_close)
{
    nj_ref self = NJ_THIS(args);
    nj_field *f = nj_field_lookup(vm, NJ_C_READER, "lock", "Ljava/lang/Object;", 0);
    nj_ref in = f ? nj_field_read_ref(vm, self, f) : NULL;
    nj_status st = NJ_OK;
    if (in) {
        st = nj_call(vm, in, NJ_C_ISTREAM, "close", "()V", NULL, NULL);
        nj_field_write_ref(vm, self, f, NULL);
        cs_set_state(vm, self, NJ_C_ISR, 0u);
    }
    return st;
}

static nj_status cs_write_byte(nj_vm *vm, nj_ref out, nj_u4 byte)
{
    nj_value a[1];
    a[0].i = (nj_s4)byte;
    return nj_call(vm, out, NJ_C_OSTREAM, "write", "(I)V", a, NULL);
}

static nj_status cs_write(nj_vm *vm, nj_ref self, nj_ref out, nj_u4 ch)
{
#if NJ_ENABLE_STREAM_UTF8
    nj_u4 state = cs_state(vm, self, NJ_C_OSW);
    nj_u4 mode = state & 3u, high = (state >> 2) & 0xffffu;
    if (high) {
        state &= ~CS_PENDING_MASK;
        /* Publish the consumed surrogate before a Java write callback can
         * reenter this writer. A nested write must not consume it again. */
        cs_set_state(vm, self, NJ_C_OSW, state);
        if (ch >= 0xdc00u && ch <= 0xdfffu)
            ch = 0x10000u + ((high - 0xd800u) << 10) + ch - 0xdc00u;
        else if (cs_write_byte(vm, out, '?') != NJ_OK) return NJ_ERR;
    }
    if (ch >= 0xd800u && ch <= 0xdbffu) {
        cs_set_state(vm, self, NJ_C_OSW, (state & ~CS_PENDING_MASK) | (ch << 2));
        return NJ_OK;
    }
    if (ch >= 0xdc00u && ch <= 0xdfffu) ch = '?';
    if (mode == CS_UTF8) {
        if (ch >= 0x10000u) {
            if (cs_write_byte(vm, out, 0xf0u | (ch >> 18)) != NJ_OK ||
                cs_write_byte(vm, out, 0x80u | ((ch >> 12) & 63u)) != NJ_OK)
                return NJ_ERR;
        } else if (ch >= 0x800u) {
            if (cs_write_byte(vm, out, 0xe0u | (ch >> 12)) != NJ_OK) return NJ_ERR;
        }
        if (ch >= 0x80u) {
            if (cs_write_byte(vm, out, (ch >= 0x800u ? 0x80u : 0xc0u) |
                               ((ch >> 6) & 63u)) != NJ_OK) return NJ_ERR;
            return cs_write_byte(vm, out, 0x80u | (ch & 63u));
        }
        return cs_write_byte(vm, out, ch);
    }
    if (mode == CS_ASCII && ch > 127u) ch = '?';
    return cs_write_byte(vm, out, ch <= 255u ? ch : '?');
#else
    (void)self;
    /* The footprint option retains the original low-byte writer semantics. */
    return cs_write_byte(vm, out, ch & 255u);
#endif
}

NJFN(osw_encoded_write)
{
    nj_ref self = NJ_THIS(args);
    nj_ref out = strm_field(vm, self, NJ_C_WRITER, "wrOut", "Ljava/io/OutputStream;");
    if (!out) return nj_throw(vm, "java/io/IOException", "stream is closed");
    return cs_write(vm, self, out, (nj_u2)NJ_ARG_INT(args, 0));
}

NJFN(osw_encoded_range)
{
    nj_ref self = NJ_THIS(args), chars = NJ_ARG_REF(args, 0);
    nj_ref out = strm_field(vm, self, NJ_C_WRITER, "wrOut", "Ljava/io/OutputStream;");
    nj_s4 off = NJ_ARG_INT(args, 1), len = NJ_ARG_INT(args, 2), i;
    nj_status st = NJ_OK;
    if (!out) return nj_throw(vm, "java/io/IOException", "stream is closed");
    if (!chars) return nj_throw(vm, "java/lang/NullPointerException", "buffer is null");
    if (off < 0 || len < 0 || (nj_u4)off > chars->length ||
        (nj_u4)len > chars->length - (nj_u4)off)
        return nj_throw(vm, "java/lang/IndexOutOfBoundsException", NULL);
    for (i = 0; i < len; ++i) {
        st = cs_write(vm, self, out, ((nj_u2 *)chars->u.bytes)[off + i]);
        if (st != NJ_OK) break;
    }
    return st;
}

NJFN(osw_stream_flush)
{
    nj_ref out = strm_field(vm, NJ_THIS(args), NJ_C_WRITER, "wrOut",
                           "Ljava/io/OutputStream;");
    if (!out) return nj_throw(vm, "java/io/IOException", "stream is closed");
    return nj_call(vm, out, NJ_C_OSTREAM, "flush", "()V", NULL, NULL);
}

NJFN(osw_stream_close)
{
    nj_ref self = NJ_THIS(args);
    nj_field *f = nj_field_lookup(vm, NJ_C_WRITER, "wrOut", "Ljava/io/OutputStream;", 0);
    nj_ref out = f ? nj_field_read_ref(vm, self, f) : NULL;
    nj_status st = NJ_OK, close_st;
    nj_ref first_exception;
    nj_u4 roots, state;
    if (!out) return NJ_OK;
    roots = nj_root_mark(vm);
    nj_root(vm, out);
    state = cs_state(vm, self, NJ_C_OSW);
    /* Closing is final even when replacement, flush or close fails. Publish
     * that state before invoking user code, and root the detached stream. */
    nj_field_write_ref(vm, self, f, NULL);
    cs_set_state(vm, self, NJ_C_OSW, 0u);
#if NJ_ENABLE_STREAM_UTF8
    if (state & CS_PENDING_MASK) st = cs_write_byte(vm, out, '?');
#else
    (void)state;
#endif
    if (st == NJ_OK)
        st = nj_call(vm, out, NJ_C_OSTREAM, "flush", "()V", NULL, NULL);
    first_exception = nj_root(vm, vm->exception);
    vm->exception = NULL;
    /* Always attempt the wrapped close, retaining the first failure. */
    close_st = nj_call(vm, out, NJ_C_OSTREAM, "close", "()V", NULL, NULL);
    if (st != NJ_OK) vm->exception = first_exception;
    else st = close_st;
    nj_root_release(vm, roots);
    return st;
}
