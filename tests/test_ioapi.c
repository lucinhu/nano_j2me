/*
 * nano_j2me - tests/test_ioapi.c
 *
 * The java.io members a CLDC/MIDP reference declares and this runtime used to
 * answer with NoSuchMethodError or UnsatisfiedLinkError: PrintStream's
 * formatting overloads, checkError()/setError() and write(byte[],int,int), and
 * the character streams Reader, Writer, InputStreamReader and
 * OutputStreamWriter.  tools/api_audit.py lists exactly these; this file is
 * the same list expressed as calls.
 *
 *   1. PrintStream.print/println for every shape the audit named.  The values
 *      are checked by capturing the port log, because that is where System.out
 *      writes: seeing "1234567890123" arrive proves the overload goes through
 *      the runtime's own String.valueOf and print(String), not through a
 *      formatter of its own.  println's extra log call (the line break) is
 *      counted too, so print and println cannot be swapped silently.
 *   2. PrintStream(OutputStream) - write(byte[],int,int) has to reach the
 *      stream the constructor was handed, or a title writing to a file writes
 *      nowhere at all.
 *   3. checkError() and setError(): checkError() flushes first and then
 *      answers what setError() recorded.
 *   4. A round trip through InputStreamReader over a ByteArrayInputStream and
 *      through OutputStreamWriter into a ByteArrayOutputStream - the pair a
 *      title uses to read a text resource and write a text file.
 *   5. Reader and Writer themselves: this runtime registers them as concrete
 *      rows whose stream slot the subclasses inherit, so a title that builds
 *      one over a stream can read and write through it.
 *   6. The Java-level fixture in tests/ioapi/: DataInput.readFloat and
 *      DataOutput.writeFloat through an interface reference, Reader.lock and
 *      the two protected Reader constructors, and inherited Reader defaults
 *      (read(), skip(), and ready()'s false default).  These run real bytecode
 *      because the bug was in constant-pool resolution, which C-level
 *      nj_call() does not exercise.
 *
 * Usage:  test_ioapi
 * Exit code 0 on PASS, non-zero otherwise.
 */
#include "nj2me/nj.h"

#include "nj_internal.h"

#include "ioapi/ioapi_fixture.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Assertion machinery (the idiom tests/test_lib.c uses)                */
/* ------------------------------------------------------------------ */
static int g_pass;
static int g_fail;

#define OK(cond)                                                              \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

#define OK_MSG(cond, ...)                                                     \
    do {                                                                      \
        if (cond) {                                                           \
            g_pass++;                                                         \
        } else {                                                              \
            g_fail++;                                                         \
            fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);   \
            fprintf(stderr, __VA_ARGS__);                                     \
            fputc('\n', stderr);                                              \
        }                                                                     \
    } while (0)

/* ------------------------------------------------------------------ */
/* The class names and the port log capture                            */
/* ------------------------------------------------------------------ */
#define PS      "java/io/PrintStream"
#define READER  "java/io/Reader"
#define WRITER  "java/io/Writer"
#define ISR     "java/io/InputStreamReader"
#define OSW     "java/io/OutputStreamWriter"
#define BAIS    "java/io/ByteArrayInputStream"
#define DIS     "java/io/DataInputStream"
#define IS      "java/io/InputStream"
#define BAOS    "java/io/ByteArrayOutputStream"

#define CAP_MAX 4096
static char g_cap[CAP_MAX];     /* every non-empty log message, concatenated */
static nj_u4 g_cap_len;
static int  g_cap_blank;        /* empty messages: one per println() */

/*
 * The headless port logs with fputs(msg) + fputc('\n'), so a message is a line
 * of console output and an empty message is a bare newline - which is what
 * PrintStream.println() emits after the text.  Keeping the two apart lets the
 * text be asserted exactly and the line breaks be counted.
 */
static void cap_log(void *user, int level, const char *msg)
{
    (void)user;
    (void)level;
    if (!msg || !*msg) {
        g_cap_blank++;
        return;
    }
    while (*msg && g_cap_len + 1u < (nj_u4)CAP_MAX) {
        g_cap[g_cap_len++] = *msg++;
    }
    g_cap[g_cap_len] = '\0';
}

static void cap_reset(void)
{
    g_cap[0] = '\0';
    g_cap_len = 0u;
    g_cap_blank = 0;
}

static int cap_is(const char *expect)
{
    return strcmp(g_cap, expect ? expect : "") == 0;
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
static nj_value vref(nj_ref r)
{
    nj_value v;
    v.bits = 0u;
    v.r = r;
    return v;
}

static nj_value vint(nj_s4 i)
{
    nj_value v;
    v.bits = 0u;
    v.i = i;
    return v;
}

static nj_value vlong(nj_s8 j)
{
    nj_value v;
    v.bits = 0u;
    v.j = j;
    return v;
}

static nj_value vfloat(float f)
{
    nj_value v;
    v.bits = 0u;
    v.f = f;
    return v;
}

static nj_value vdouble(double d)
{
    nj_value v;
    v.bits = 0u;
    v.d = d;
    return v;
}

/* Assert that the VM came out of a step with no exception pending. */
static void clean(nj_vm *vm, const char *where)
{
    if (nj_has_exception(vm)) {
        const char *d = nj_exception_describe(vm);
        g_fail++;
        fprintf(stderr, "FAIL %s: unexpected exception: %s\n", where,
                d ? d : "(no description)");
        nj_clear_exception(vm);
    } else {
        g_pass++;
    }
}

/* Allocate `cls` and run its constructor.  Returns NULL on failure. */
static nj_ref construct(nj_vm *vm, const char *cls, const char *desc,
                        nj_value *args, const char *where)
{
    nj_ref o = nj_object_new(vm, cls);

    if (!o) {
        g_fail++;
        fprintf(stderr, "FAIL %s: cannot allocate %s\n", where, cls);
        return NULL;
    }
    if (nj_call_special(vm, o, cls, "<init>", desc, args, NULL) != NJ_OK) {
        const char *d = nj_exception_describe(vm);
        g_fail++;
        fprintf(stderr, "FAIL %s: %s%s threw: %s\n", where, cls, desc,
                d ? d : "(no description)");
        nj_clear_exception(vm);
        return NULL;
    }
    g_pass++;
    return o;
}

/* A ByteArrayInputStream over a fresh byte[] holding `data`. */
static nj_ref bais_over(nj_vm *vm, const char *data)
{
    nj_ref arr = nj_byte_array_new(vm, (nj_u4)strlen(data));
    nj_value a[2];
    nj_u4 i;

    if (!arr) {
        g_fail++;
        return NULL;
    }
    for (i = 0u; i < (nj_u4)strlen(data); i++) {
        nj_array_set_int(arr, i, (nj_s4)(unsigned char)data[i]);
    }
    a[0] = vref(arr);
    return construct(vm, BAIS, "([B)V", a, "ByteArrayInputStream(byte[])");
}

/* The text a stream has collected, as a C string. */
static int stream_text_is(nj_vm *vm, nj_ref stream, const char *cls,
                          const char *where, const char *expect)
{
    nj_ref s = nj_call_ref(vm, stream, cls, "toString",
                           "()Ljava/lang/String;", NULL);
    char got[64];
    nj_u4 n;
    nj_u4 i;
    int same;

    if (!s) {
        g_fail++;
        fprintf(stderr, "FAIL %s: %s.toString() threw\n", where, cls);
        nj_clear_exception(vm);
        return 0;
    }
    n = nj_string_length(s);
    for (i = 0u; i < n && i < (nj_u4)sizeof(got) - 1u; i++) {
        got[i] = (char)(nj_string_data(s)[i] & 0xFFu);
    }
    got[i] = '\0';
    same = nj_string_equals_utf8(s, expect);
    OK_MSG(same, "%s: got \"%s\", want \"%s\"", where, got, expect);
    return same;
}

/* A fresh char[] holding `text`. */
static nj_ref chars_of(nj_vm *vm, const char *text)
{
    nj_ref a = nj_char_array_new(vm, (nj_u4)strlen(text));
    nj_u4 i;

    for (i = 0u; a && i < (nj_u4)strlen(text); i++) {
        nj_array_set_int(a, i, (nj_s4)(unsigned char)text[i]);
    }
    return a;
}

/* ------------------------------------------------------------------ */
/* 1: PrintStream formatting, observed through the port log            */
/* ------------------------------------------------------------------ */
/*
 * A PrintStream made with nj_object_new and no constructor is System.out: no
 * wrapped stream, so every shape writes to the port log.  That is the path a
 * title's debug output takes, and it is the only place these values can be
 * read back from.
 */
static void test_printstream_formats(nj_vm *vm)
{
    nj_ref ps = nj_object_new(vm, PS);
    nj_ref obj = nj_string_new(vm, "obj");
    nj_ref chars = chars_of(vm, "hi");
    nj_value a[4];
    nj_value ret;

    OK_MSG(ps != NULL, "PrintStream allocation");
    if (!ps) {
        return;
    }
    cap_reset();

    a[0] = vint(1);
    OK(nj_call(vm, ps, PS, "print", "(Z)V", a, NULL) == NJ_OK);
    a[0] = vlong((nj_s8)1234567890123LL);
    OK(nj_call(vm, ps, PS, "print", "(J)V", a, NULL) == NJ_OK);
    a[0] = vfloat(2.5f);
    OK(nj_call(vm, ps, PS, "print", "(F)V", a, NULL) == NJ_OK);
    a[0] = vdouble(1.5);
    OK(nj_call(vm, ps, PS, "print", "(D)V", a, NULL) == NJ_OK);
    a[0] = vref(chars);
    OK(nj_call(vm, ps, PS, "print", "([C)V", a, NULL) == NJ_OK);
    a[0] = vref(obj);
    OK(nj_call(vm, ps, PS, "print", "(Ljava/lang/Object;)V", a, NULL) == NJ_OK);
    a[0] = vref(NULL);
    OK_MSG(nj_call(vm, ps, PS, "print", "(Ljava/lang/Object;)V", a, NULL)
               == NJ_OK,
           "print((Object)null)");
    clean(vm, "PrintStream.print overloads");

    OK_MSG(cap_is("true12345678901232.51.5hiobjnull"),
           "print(Z)(J)(F)(D)([C)(Object): got \"%s\"", g_cap);
    OK_MSG(g_cap_blank == 0, "print must not end a line (%d blank logs)",
           g_cap_blank);

    /* println: the same values, each followed by the one empty log message
     * that is PrintStream.println's line break. */
    cap_reset();
    a[0] = vint('X');
    OK(nj_call(vm, ps, PS, "println", "(C)V", a, NULL) == NJ_OK);
    a[0] = vlong((nj_s8)-7);
    OK(nj_call(vm, ps, PS, "println", "(J)V", a, NULL) == NJ_OK);
    a[0] = vfloat(0.5f);
    OK(nj_call(vm, ps, PS, "println", "(F)V", a, NULL) == NJ_OK);
    a[0] = vdouble(3.25);
    OK(nj_call(vm, ps, PS, "println", "(D)V", a, NULL) == NJ_OK);
    a[0] = vref(chars_of(vm, "yz"));
    OK(nj_call(vm, ps, PS, "println", "([C)V", a, NULL) == NJ_OK);
    clean(vm, "PrintStream.println overloads");

    OK_MSG(cap_is("X-70.53.25yz"),
           "println(C)(J)(F)(D)([C): got \"%s\"", g_cap);
    OK_MSG(g_cap_blank == 5, "println must end every line (%d blank logs)",
           g_cap_blank);

    /* Non-ASCII text reaches the log as UTF-8, not as one '?' per rune: a
     * title printing its own Chinese diagnostic used to arrive as "????",
     * which made a caught exception's log unreadable exactly when it
     * mattered.  The 4-byte row pins the surrogate-pair path too. */
    cap_reset();
    a[0] = vref(nj_string_new(vm, "\u6388\u6743\u8fc7\u671f"));
    OK(nj_call(vm, ps, PS, "println", "(Ljava/lang/String;)V", a, NULL)
       == NJ_OK);
    {
        nj_u2 pair[2];

        pair[0] = 0xD83Du;
        pair[1] = 0xDE00u;
        a[0] = vref(nj_string_new_chars(vm, pair, 2));
    }
    OK(nj_call(vm, ps, PS, "println", "(Ljava/lang/String;)V", a, NULL)
       == NJ_OK);
    clean(vm, "PrintStream.println(String) with non-ASCII text");
    OK_MSG(cap_is("\346\216\210\346\235\203\350\277\207\346\234\237"
                  "\360\237\230\200"),
           "println must log UTF-8 bytes, got \"%s\"", g_cap);

    /* The range form of write() on a stream-less PrintStream goes through
     * write(int), the byte-at-a-time path its println has always used. */
    cap_reset();
    {
        nj_ref bytes = nj_byte_array_new(vm, 3u);
        nj_array_set_int(bytes, 0, 'A');
        nj_array_set_int(bytes, 1, 'B');
        nj_array_set_int(bytes, 2, 'C');
        a[0] = vref(bytes);
        a[1] = vint(1);
        a[2] = vint(2);
        OK(nj_call(vm, ps, PS, "write", "([BII)V", a, NULL) == NJ_OK);
        clean(vm, "PrintStream.write(byte[],int,int) with no stream");
        OK_MSG(cap_is("BC"), "write([BII) with no stream: got \"%s\"", g_cap);
    }

    /* checkError() has to say false on a stream that never failed, and the
     * result is a boolean, not a rest of the call. */
    cap_reset();
    ret.i = -1;
    OK(nj_call(vm, ps, PS, "checkError", "()Z", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 0, "checkError() on a fresh PrintStream returned %d",
           (int)ret.i);
    clean(vm, "PrintStream.checkError()");
}

/* ------------------------------------------------------------------ */
/* 2 + 3: a PrintStream over a stream, and its error flag              */
/* ------------------------------------------------------------------ */
static void test_printstream_stream(nj_vm *vm)
{
    nj_ref baos = construct(vm, BAOS, "()V", NULL, "ByteArrayOutputStream()");
    nj_ref ps;
    nj_value a[3];
    nj_value ret;

    OK(baos != NULL);
    if (!baos) {
        return;
    }
    a[0] = vref(baos);
    ps = construct(vm, PS, "(Ljava/io/OutputStream;)V", a,
                   "PrintStream(OutputStream)");
    OK(ps != NULL);
    if (!ps) {
        return;
    }
    clean(vm, "new PrintStream(baos)");

    /* write(byte[],int,int) is the member whose whole job is to reach the
     * stream the constructor was handed. */
    {
        nj_ref bytes = nj_byte_array_new(vm, 3u);
        nj_array_set_int(bytes, 0, 'A');
        nj_array_set_int(bytes, 1, 'B');
        nj_array_set_int(bytes, 2, 'C');
        a[0] = vref(bytes);
        a[1] = vint(1);
        a[2] = vint(2);
        OK(nj_call(vm, ps, PS, "write", "([BII)V", a, NULL) == NJ_OK);
        clean(vm, "PrintStream.write([BII) over a stream");
    }
    (void)stream_text_is(vm, baos, BAOS, "write([BII) reached the stream",
                         "BC");

    /* The formatting overloads on a wrapped stream: every byte a PrintStream
     * prints goes to the stream it was constructed with, and the line break a
     * println adds goes there too - the port log is only the fallback for the
     * stream-less System.out. */
    a[0] = vlong((nj_s8)5);
    OK(nj_call(vm, ps, PS, "print", "(J)V", a, NULL) == NJ_OK);
    a[0] = vdouble(2.5);
    OK(nj_call(vm, ps, PS, "print", "(D)V", a, NULL) == NJ_OK);
    a[0] = vref(chars_of(vm, "zz"));
    OK(nj_call(vm, ps, PS, "println", "([C)V", a, NULL) == NJ_OK);
    clean(vm, "formatting on a wrapped PrintStream");
    (void)stream_text_is(vm, baos, BAOS, "wrapped print/println", "BC52.5zz\n");

    /* 3: checkError() is false until a write fails, and setError() is how a
     * subclass or a failing write records that it did. */
    ret.i = -1;
    OK(nj_call(vm, ps, PS, "checkError", "()Z", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 0, "checkError() should be false, got %d", (int)ret.i);
    clean(vm, "checkError() before setError()");

    OK(nj_call(vm, ps, PS, "setError", "()V", NULL, NULL) == NJ_OK);
    clean(vm, "setError()");

    ret.i = 0;
    OK(nj_call(vm, ps, PS, "checkError", "()Z", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 1, "checkError() should be true after setError(), got %d",
           (int)ret.i);
    clean(vm, "checkError() after setError()");

    /* A protected member is still a member: the record above is per object,
     * so System.out's own flag must not have moved. */
    {
        nj_ref sysout = nj_object_new(vm, PS);
        ret.i = -1;
        OK(nj_call(vm, sysout, PS, "checkError", "()Z", NULL, &ret) == NJ_OK);
        OK_MSG(ret.i == 0, "setError() leaked to another PrintStream (%d)",
               (int)ret.i);
        clean(vm, "checkError() on a second PrintStream");
    }
}

/* ------------------------------------------------------------------ */
/* 4 + 5: Reader, InputStreamReader, Writer, OutputStreamWriter        */
/* ------------------------------------------------------------------ */
static void test_reader(nj_vm *vm)
{
    nj_ref bais = bais_over(vm, "hello world");
    nj_ref isr;
    nj_ref chars = nj_char_array_new(vm, 8u);
    nj_value a[4];
    nj_value ret;

    OK(bais != NULL);
    if (!bais || !chars) {
        return;
    }
    a[0] = vref(bais);
    isr = construct(vm, ISR, "(Ljava/io/InputStream;)V", a,
                    "InputStreamReader(InputStream)");
    OK(isr != NULL);
    if (!isr) {
        return;
    }
    clean(vm, "new InputStreamReader(bais)");

    /* read(): one character, decoded from the byte stream. */
    ret.i = 0;
    OK(nj_call(vm, isr, ISR, "read", "()I", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 'h', "read() returned %d, want 'h'", (int)ret.i);
    clean(vm, "InputStreamReader.read()");

    /* ready(): the underlying stream still has bytes. */
    ret.i = 0;
    OK(nj_call(vm, isr, ISR, "ready", "()Z", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 1, "ready() returned %d with 10 bytes left", (int)ret.i);
    clean(vm, "InputStreamReader.ready()");

    /* read(char[],int,int): fills the range and reports the count. */
    a[0] = vref(chars);
    a[1] = vint(0);
    a[2] = vint(5);
    ret.i = 0;
    OK(nj_call(vm, isr, ISR, "read", "([CII)I", a, &ret) == NJ_OK);
    OK_MSG(ret.i == 5, "read([CII) returned %d, want 5", (int)ret.i);
    OK_MSG(nj_array_get_int(chars, 0) == 'e' && nj_array_get_int(chars, 4) == ' ',
           "read([CII) filled \"%c%c...\" want \"e... \"",
           (char)nj_array_get_int(chars, 0),
           (char)nj_array_get_int(chars, 4));
    clean(vm, "InputStreamReader.read([CII)");

    /* skip(long) is inherited from Reader and discards through read([CII). */
    a[0] = vlong((nj_s8)1);
    ret.i = 0;
    OK(nj_call(vm, isr, ISR, "skip", "(J)J", a, &ret) == NJ_OK);
    OK_MSG(ret.i == 1, "skip(1) returned %d", (int)ret.i);
    clean(vm, "InputStreamReader.skip(long)");

    ret.i = 0;
    OK(nj_call(vm, isr, ISR, "read", "()I", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 'o', "read() after skip returned %d, want 'o'",
           (int)ret.i);
    clean(vm, "InputStreamReader.read() after skip");

    /* The rest, then end of stream: -1, not 0. */
    a[0] = vref(chars);
    a[1] = vint(0);
    a[2] = vint(8);
    ret.i = 0;
    OK(nj_call(vm, isr, ISR, "read", "([CII)I", a, &ret) == NJ_OK);
    OK_MSG(ret.i == 3, "tail read returned %d, want 3", (int)ret.i);
    ret.i = 0;
    OK(nj_call(vm, isr, ISR, "read", "()I", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == -1, "read() at end of stream returned %d", (int)ret.i);
    ret.i = 1;
    OK(nj_call(vm, isr, ISR, "ready", "()Z", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 0, "ready() returned %d at end of stream", (int)ret.i);
    clean(vm, "InputStreamReader at end of stream");

    /* mark/markSupported/reset: a byte stream remembers nothing, and the
     * specification's answer for reset() is an IOException. */
    ret.i = 1;
    OK(nj_call(vm, isr, ISR, "markSupported", "()Z", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 0, "markSupported() returned %d", (int)ret.i);
    OK(nj_call(vm, isr, ISR, "mark", "(I)V", NULL, NULL) == NJ_OK);
    clean(vm, "markSupported()/mark()");

    OK(nj_call(vm, isr, ISR, "reset", "()V", NULL, NULL) != NJ_OK);
    OK_MSG(nj_has_exception(vm), "reset() must raise IOException");
    nj_clear_exception(vm);

    OK(nj_call(vm, isr, ISR, "close", "()V", NULL, NULL) == NJ_OK);
    clean(vm, "InputStreamReader.close()");

    /* The two-argument constructor takes an encoding name; the runtime serves
     * the identity encoding it advertises, so the bytes come back unchanged. */
    {
        nj_ref bais2 = bais_over(vm, "enc");
        nj_ref isr2;
        a[0] = vref(bais2);
        a[1] = vref(nj_string_new(vm, "ISO-8859-1"));
        isr2 = construct(vm, ISR, "(Ljava/io/InputStream;Ljava/lang/String;)V",
                         a, "InputStreamReader(InputStream,String)");
        if (isr2) {
            a[0] = vref(chars);
            a[1] = vint(0);
            a[2] = vint(3);
            ret.i = 0;
            OK(nj_call(vm, isr2, ISR, "read", "([CII)I", a, &ret) == NJ_OK);
            OK_MSG(ret.i == 3, "named-encoding read returned %d", (int)ret.i);
            clean(vm, "InputStreamReader(InputStream,String).read([CII)");
        }
    }
}

/*
 * Reader is abstract in the reference and a concrete row here.  A title cannot
 * name it as a type to build, but the runtime has to be able to read through
 * one - that is the row the four abstract methods were missing from.
 */
static void test_reader_base(nj_vm *vm)
{
    nj_ref bais = bais_over(vm, "abc");
    nj_ref rd = nj_object_new(vm, READER);
    nj_ref chars = nj_char_array_new(vm, 4u);
    nj_value a[4];
    nj_value ret;

    OK(rd != NULL);
    if (!rd || !chars) {
        return;
    }
    a[0] = vref(bais);
    OK(nj_call_special(vm, rd, READER, "<init>", "(Ljava/lang/Object;)V", a,
                       NULL) == NJ_OK);
    clean(vm, "new Reader(stream)");

    a[0] = vref(chars);
    a[1] = vint(0);
    a[2] = vint(4);
    ret.i = 0;
    OK(nj_call(vm, rd, READER, "read", "([CII)I", a, &ret) == NJ_OK);
    OK_MSG(ret.i == 3, "Reader.read([CII) returned %d, want 3", (int)ret.i);
    OK_MSG(nj_array_get_int(chars, 0) == 'a'
               && nj_array_get_int(chars, 2) == 'c',
           "Reader.read([CII) filled \"%c%c%c\"",
           (char)nj_array_get_int(chars, 0),
           (char)nj_array_get_int(chars, 1),
           (char)nj_array_get_int(chars, 2));
    clean(vm, "Reader.read([CII)");

    ret.i = 0;
    OK(nj_call(vm, rd, READER, "read", "()I", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == -1, "Reader.read() at end returned %d", (int)ret.i);
    clean(vm, "Reader.read()");

    ret.i = 1;
    OK(nj_call(vm, rd, READER, "ready", "()Z", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 0, "Reader.ready() at end returned %d", (int)ret.i);
    ret.i = 1;
    OK(nj_call(vm, rd, READER, "markSupported", "()Z", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 0, "Reader.markSupported() returned %d", (int)ret.i);
    OK(nj_call(vm, rd, READER, "close", "()V", NULL, NULL) == NJ_OK);
    clean(vm, "Reader.close()");
}

static void test_writer(nj_vm *vm)
{
    nj_ref baos = construct(vm, BAOS, "()V", NULL, "ByteArrayOutputStream()");
    nj_ref osw;
    nj_value a[4];

    OK(baos != NULL);
    if (!baos) {
        return;
    }
    a[0] = vref(baos);
    osw = construct(vm, OSW, "(Ljava/io/OutputStream;)V", a,
                    "OutputStreamWriter(OutputStream)");
    OK(osw != NULL);
    if (!osw) {
        return;
    }
    clean(vm, "new OutputStreamWriter(baos)");

    a[0] = vint('A');
    OK(nj_call(vm, osw, OSW, "write", "(I)V", a, NULL) == NJ_OK);
    clean(vm, "OutputStreamWriter.write(int)");
    (void)stream_text_is(vm, baos, BAOS, "after write(int)", "A");
    a[0] = vref(chars_of(vm, "BCDEF"));
    a[1] = vint(1);
    a[2] = vint(3);
    OK(nj_call(vm, osw, OSW, "write", "([CII)V", a, NULL) == NJ_OK);
    /* off=1 skips the 'B', len=3 stops before 'F': "CDE", not "BCDEF". */
    (void)stream_text_is(vm, baos, BAOS, "after write([CII)", "ACDE");
    a[0] = vref(nj_string_new(vm, "hi"));
    OK(nj_call(vm, osw, OSW, "write", "(Ljava/lang/String;)V", a, NULL)
       == NJ_OK);
    a[0] = vref(nj_string_new(vm, "XYZ"));
    a[1] = vint(1);
    a[2] = vint(2);
    OK(nj_call(vm, osw, OSW, "write", "(Ljava/lang/String;II)V", a, NULL)
       == NJ_OK);
    clean(vm, "OutputStreamWriter write overloads");

    OK(nj_call(vm, osw, OSW, "flush", "()V", NULL, NULL) == NJ_OK);
    clean(vm, "OutputStreamWriter.flush()");
    (void)stream_text_is(vm, baos, BAOS, "OutputStreamWriter wrote",
                         "ACDEhiYZ");

    /* The small configuration preserves the old writer's low-byte mapping. */
    {
        nj_ref ch = nj_char_array_new(vm, 1);
        ((nj_u2 *)nj_array_data(ch))[0] = 0x0142u;
        a[0] = vint(0x0141);
        OK(nj_call(vm, osw, OSW, "write", "(I)V", a, NULL) == NJ_OK);
        a[0] = vref(ch); a[1] = vint(0); a[2] = vint(1);
        OK(nj_call(vm, osw, OSW, "write", "([CII)V", a, NULL) == NJ_OK);
#if NJ_ENABLE_STREAM_UTF8
        (void)stream_text_is(vm, baos, BAOS, "unmappable Latin-1", "ACDEhiYZ??");
#else
        (void)stream_text_is(vm, baos, BAOS, "legacy low-byte writer", "ACDEhiYZAB");
#endif
    }

    /* The named-encoding constructor, and close() on top of it. */
    {
        nj_ref baos2 = nj_object_new(vm, BAOS);
        nj_ref osw2;
        OK(nj_call_special(vm, baos2, BAOS, "<init>", "()V", NULL, NULL)
           == NJ_OK);
        a[0] = vref(baos2);
        a[1] = vref(nj_string_new(vm, "ISO-8859-1"));
        osw2 = construct(vm, OSW, "(L" "java/io/OutputStream"
                               ";Ljava/lang/String;)V", a,
                         "OutputStreamWriter(OutputStream,String)");
        if (osw2) {
            a[0] = vref(nj_string_new(vm, "enc"));
            OK(nj_call(vm, osw2, OSW, "write", "(Ljava/lang/String;)V", a, NULL)
               == NJ_OK);
            OK(nj_call(vm, osw2, OSW, "close", "()V", NULL, NULL) == NJ_OK);
            clean(vm, "OutputStreamWriter(OutputStream,String)");
            (void)stream_text_is(vm, baos2, BAOS, "named-encoding writer",
                                 "enc");
        }
    }
}

static void test_writer_base(nj_vm *vm)
{
    nj_ref baos = construct(vm, BAOS, "()V", NULL, "ByteArrayOutputStream()");
    nj_ref wr = nj_object_new(vm, WRITER);
    nj_value a[4];

    OK(wr != NULL);
    if (!wr || !baos) {
        return;
    }
    a[0] = vref(baos);
    OK(nj_call_special(vm, wr, WRITER, "<init>", "(Ljava/lang/Object;)V", a,
                       NULL) == NJ_OK);
    clean(vm, "new Writer(stream)");

    a[0] = vint('o');
    OK(nj_call(vm, wr, WRITER, "write", "(I)V", a, NULL) == NJ_OK);
    a[0] = vref(nj_string_new(vm, "kay"));
    OK(nj_call(vm, wr, WRITER, "write", "(Ljava/lang/String;)V", a, NULL)
       == NJ_OK);
    a[0] = vref(nj_string_new(vm, "XkY"));
    a[1] = vint(1);
    a[2] = vint(1);
    OK(nj_call(vm, wr, WRITER, "write", "(Ljava/lang/String;II)V", a, NULL)
       == NJ_OK);
    OK(nj_call(vm, wr, WRITER, "flush", "()V", NULL, NULL) == NJ_OK);
    clean(vm, "Writer write overloads");
    (void)stream_text_is(vm, baos, BAOS, "Writer wrote", "okayk");
}

/* ------------------------------------------------------------------ */
/* 7: DataInputStream mark/reset/markSupported forward to the stream  */
/* ------------------------------------------------------------------ */
/*
 * FilterInputStream forwards all three, and a title that rewinds a data
 * stream depends on it.  实况足球经理2009_10609's resource unpacker (d.c)
 * reads a packed container, fills its offset table, reset()s back to the
 * start and skipBytes()s to the requested entry.  With reset() falling
 * through to InputStream's no-op the entry began 44 bytes - the container
 * header - too far in, d.E read entry sizes that did not fit the buffer it
 * allocated, its string table stayed null and the title painted a flat
 * frame forever.
 */
static void test_datainput_mark_reset(nj_vm *vm)
{
    nj_ref in = bais_over(vm, "ABCDEFGH");
    nj_value a[2];
    nj_value ret;
    nj_ref dis;

    OK_MSG(in != NULL, "ByteArrayInputStream for the rewind test");
    if (!in) {
        return;
    }
    a[0] = vref(in);
    dis = construct(vm, DIS, "(Ljava/io/InputStream;)V", a,
                    "DataInputStream(InputStream)");
    if (!dis) {
        return;
    }

    OK(nj_call(vm, dis, DIS, "readByte", "()B", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 65, "readByte() returned %d, want 'A'", (int)ret.i);

    OK(nj_call(vm, dis, DIS, "markSupported", "()Z", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 1, "markSupported() must forward to the byte stream, got %d",
           (int)ret.i);

    /* reset() with no mark() rewinds to the start of the stream. */
    OK(nj_call(vm, dis, DIS, "reset", "()V", NULL, NULL) == NJ_OK);
    OK(nj_call(vm, dis, DIS, "readByte", "()B", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 65, "readByte() after reset() returned %d, want 'A'",
           (int)ret.i);

    /* An explicit mark() is where reset() returns to, and skipBytes() must
     * not move the mark - the unpacker marks, skips to the entry and resets
     * for the next one. */
    OK(nj_call(vm, dis, DIS, "readByte", "()B", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 66, "readByte() returned %d, want 'B'", (int)ret.i);
    a[0] = vint(0);
    OK(nj_call(vm, dis, DIS, "mark", "(I)V", a, NULL) == NJ_OK);
    a[0] = vint(5);
    OK(nj_call(vm, dis, DIS, "skipBytes", "(I)I", a, &ret) == NJ_OK);
    OK_MSG(ret.i == 5, "skipBytes(5) returned %d", (int)ret.i);
    OK(nj_call(vm, dis, DIS, "reset", "()V", NULL, NULL) == NJ_OK);
    OK(nj_call(vm, dis, DIS, "readByte", "()B", NULL, &ret) == NJ_OK);
    OK_MSG(ret.i == 67, "readByte() after mark()/skipBytes()/reset() returned %d, want 'C'",
           (int)ret.i);

    /* A short skip stops at the end of the stream, it does not run past it. */
    a[0] = vint(100);
    OK(nj_call(vm, dis, DIS, "skipBytes", "(I)I", a, &ret) == NJ_OK);
    OK_MSG(ret.i == 5, "skipBytes(100) near the end returned %d, want 5",
           (int)ret.i);

    /* A stream that does not implement marking must say so through reset(),
     * not silently do nothing: that is what the CLDC InputStream does. */
    {
        nj_ref plain = nj_object_new(vm, IS);

        OK_MSG(plain != NULL, "java/io/InputStream allocation");
        if (plain) {
            OK(nj_call(vm, plain, IS, "reset", "()V", NULL, NULL) != NJ_OK);
            OK_MSG(nj_has_exception(vm), "reset() must raise IOException");
            nj_clear_exception(vm);
        }
    }
}


/*
 * ------------------------------------------------------------------ */
/* Java-level fixture: constant-pool resolution and the Reader lock     */
/* ------------------------------------------------------------------ */
/*
 * C-level nj_call() takes a class name and looks the method up by name.  The
 * bugs the fixture covers are in constant-pool resolution instead:
 *
 *   - `invokeinterface java/io/DataInput.readFloat:()F` resolves the method
 *     against the *interface* that declares it, not against the receiver.
 *   - `getfield java/io/Reader.lock:Ljava/lang/Object;` resolves a protected
 *     field declared by the superclass.
 *
 * So the fixture is real Java bytecode (tests/ioapi/*.java, compiled with
 * javac --release 8 and embedded in ioapi_fixture.h).  The want values are a
 * desktop HotSpot run of the same sources; tests/ioapi/README.md reproduces
 * that oracle.
 */
#define IOAPI_PROBE "ioapi/IoApiProbe"
#define IOAPI_LOCKR "ioapi/LockReader"
#define IOAPI_RANGE "ioapi/RangeOnlyReader"
#define IOAPI_ENCODING "ioapi/StreamEncoding"

/* Permit the formal fixture to link against a saved pre-feature archive for
 * fail-before evidence. The public configuration supplies this in new builds. */
#ifndef NJ_ENABLE_STREAM_UTF8
#define NJ_ENABLE_STREAM_UTF8 1
#endif

struct ioapi_case {
    const char *tag;        /* label IoApiDesktop prints for this value */
    const char *cls;
    const char *name;
    nj_s4 want;
};

static const struct ioapi_case ioapi_probe_cases[] = {
    { "io.f_read_known",     IOAPI_PROBE, "readFloatBits",        1069547520 },
    { "io.f_read_negzero",   IOAPI_PROBE, "readFloatNegZeroBits", -2147483648 },
    { "io.f_read_posinf",    IOAPI_PROBE, "readFloatPosInfBits",   2139095040 },
    { "io.f_read_nan",       IOAPI_PROBE, "readFloatNanBits",      2143289344 },
    { "io.f_read_eof",       IOAPI_PROBE, "readFloatEof",                31 },
    { "io.f_read_trunc",     IOAPI_PROBE, "readFloatTruncatedEof",       34 },
    { "io.d_read_pi",        IOAPI_PROBE, "readDoublePiBits",      340593891 },
    { "io.d_read_negzero",   IOAPI_PROBE, "readDoubleNegZeroBits", -2147483648 },
    { "io.d_read_neginf",    IOAPI_PROBE, "readDoubleNegInfBits",   -1048576 },
    { "io.d_read_eof",       IOAPI_PROBE, "readDoubleEof",               37 },
    { "io.o_write_floats",   IOAPI_PROBE, "writeFloatBytes",      1472412883 },
    { "io.o_write_float_len", IOAPI_PROBE, "writeFloatLength",          12 },
    { "io.o_write_doubles",  IOAPI_PROBE, "writeDoubleBytes",    -2051675269 },
    { "io.o_write_chars",    IOAPI_PROBE, "writeCharsBytes",       15762444 },
    { "io.roundtrip",        IOAPI_PROBE, "roundTripBits",       -1789919232 }
};

static const struct ioapi_case ioapi_lock_cases[] = {
    { "io.r_lock_self",      IOAPI_LOCKR, "ctorNoArgSelf",          1 },
    { "io.r_lock_explicit",  IOAPI_LOCKR, "ctorExplicitObject",     1 },
    { "io.r_lock_null",      IOAPI_LOCKR, "ctorNullRejected",      41 },
    { "io.r_sync_read",      IOAPI_LOCKR, "syncRead",         6382081 },
    { "io.r_read_range",     IOAPI_LOCKR, "readRange",       33579363 }
};

static const struct ioapi_case ioapi_range_cases[] = {
    /* A subclass that overrides only read(char[],int,int) must get working
     * inherited read(), skip() and ready(); before the lock fix, read() and
     * ready() treated the lock object as the stream.  Kept last because the
     * broken read() recursed until the VM stack died. */
    { "io.r_inherited_read",  IOAPI_RANGE, "inheritedRead",       81 },
    { "io.r_inherited_read2", IOAPI_RANGE, "inheritedReadTwice", 20735 },
    { "io.r_inherited_skip",  IOAPI_RANGE, "inheritedSkip",          1 },
    /* 200 = the inherited CLDC ready() default returned false; 201 = true,
     * 40 = it threw.  InputStreamReader keeps its real available() probe. */
    { "io.r_inherited_ready", IOAPI_RANGE, "inheritedReady",       200 }
};

/* Generated fixture returns complete sequence digests, not a round trip:
 * matching encoder/decoder bugs cannot cancel each other. Desktop oracle:
 * java -Dfile.encoding=ISO-8859-1 -cp /tmp/ioapi ioapi.IoApiDesktop */
static const struct ioapi_case ioapi_encoding_cases[] = {
    { "io.enc.default_read", IOAPI_ENCODING, "defaultLatin1Read", 490609510 },
    { "io.enc.default_write", IOAPI_ENCODING, "defaultLatin1Write", 490609510 },
    { "io.enc.latin1_alias_read", IOAPI_ENCODING, "latin1AliasesRead", 2062192150 },
    { "io.enc.latin1_alias_write", IOAPI_ENCODING, "latin1AliasesWrite", 2062192150 },
    { "io.enc.unknown_reader", IOAPI_ENCODING, "unknownReader", 3 },
    { "io.enc.unknown_writer", IOAPI_ENCODING, "unknownWriter", 3 },
    { "io.enc.null_reader", IOAPI_ENCODING, "nullReader", 1 },
    { "io.enc.null_writer", IOAPI_ENCODING, "nullWriter", 1 },
    { "io.enc.closed_reader", IOAPI_ENCODING, "closedReader", 15 },
    { "io.enc.closed_writer", IOAPI_ENCODING, "closedWriter", 31 },
#if NJ_ENABLE_STREAM_UTF8
    { "io.enc.utf8_alias", IOAPI_ENCODING, "utf8Aliases", -369754991 },
    { "io.enc.utf8_read", IOAPI_ENCODING, "utf8Read", 1319727115 },
    { "io.enc.split_supplementary", IOAPI_ENCODING, "splitSupplementary", 1388573965 },
    { "io.enc.ready_pending_low", IOAPI_ENCODING, "readyPendingLow", 125085354 },
    { "io.enc.skip_pending_low", IOAPI_ENCODING, "skipPendingLow", 1664843219 },
    { "io.enc.utf8_write", IOAPI_ENCODING, "utf8WriteOverloads", 1895888707 },
    { "io.enc.writer_cross_calls", IOAPI_ENCODING, "writerCrossCalls", 577745536 },
    { "io.enc.close_dangling_high", IOAPI_ENCODING, "closeDanglingHigh", 18446 },
    { "io.enc.closed_empty_string", IOAPI_ENCODING, "closedEmptyStringWrite", 1 },
    { "io.enc.failed_close_writer", IOAPI_ENCODING, "failedCloseWriter", 8191 },
    /* Local callback contract: one pair plus one reentrant 'A', in any byte
     * order. Desktop buffering gives 1, so this is not a HotSpot oracle row. */
    { "io.enc.local_reentrant_pair", IOAPI_ENCODING, "writerReentrantPendingPair", 7 },
    { "io.enc.write_bad_surrogates", IOAPI_ENCODING, "malformedSurrogatesWrite", 1188236842 },
    { "io.enc.ascii_alias", IOAPI_ENCODING, "asciiAliases", -2141736295 },
    { "io.enc.ascii_read", IOAPI_ENCODING, "asciiRead", 1312801457 },
    { "io.enc.ascii_write", IOAPI_ENCODING, "asciiWrite", -1035939219 },
    { "io.enc.invalid_lead", IOAPI_ENCODING, "invalidLead", 65515248 },
    { "io.enc.overlong_two", IOAPI_ENCODING, "overlongTwo", 65515248 },
    { "io.enc.overlong_three", IOAPI_ENCODING, "overlongThree", 2033002261 },
    { "io.enc.overlong_four", IOAPI_ENCODING, "overlongFour", -1399409776 },
    { "io.enc.truncated_two", IOAPI_ENCODING, "truncatedTwo", 66060 },
    { "io.enc.truncated_three", IOAPI_ENCODING, "truncatedThree", 66060 },
    { "io.enc.truncated_four", IOAPI_ENCODING, "truncatedFour", 66060 },
    { "io.enc.bad_cont_two", IOAPI_ENCODING, "badContinuationTwo", 63485837 },
    { "io.enc.bad_cont_three", IOAPI_ENCODING, "badContinuationThree", 63485837 },
    { "io.enc.bad_cont_four", IOAPI_ENCODING, "badContinuationFour", 63485837 },
    { "io.enc.encoded_surrogates", IOAPI_ENCODING, "encodedSurrogates", 65515248 },
    { "io.enc.above_limit", IOAPI_ENCODING, "aboveUnicodeLimit", -1399409776 },
    { "io.enc.modified_nul", IOAPI_ENCODING, "modifiedNulRejected", 2030970738 }
#else
    /* These rows express the disabled-feature contract, not desktop behavior:
     * UTF-8 and ASCII raise UnsupportedEncodingException for both streams. */
    { "io.enc.disabled_utf8", IOAPI_ENCODING, "disabledUtf8Rejected", 15 },
    { "io.enc.disabled_ascii", IOAPI_ENCODING, "disabledAsciiRejected", 15 }
#endif
};

static void run_ioapi_cases(nj_vm *vm, const char *cls, const nj_u1 *bytes,
                            nj_u4 len, const struct ioapi_case *cases,
                            unsigned count)
{
    unsigned i;

    if (!nj_class_load(vm, cls, bytes, len)) {
        const char *d = nj_exception_describe(vm);
        g_fail++;
        fprintf(stderr, "FAIL %s: the fixture class is not loadable: %s\n",
                cls, d ? d : "(no description)");
        nj_clear_exception(vm);
        return;
    }
    for (i = 0u; i < count; i++) {
        const struct ioapi_case *c = &cases[i];
        nj_value ret;
        nj_status st;

        ret.i = 0;
        st = nj_call_static(vm, c->cls, c->name, "()I", NULL, &ret);
        if (st != NJ_OK || nj_has_exception(vm)) {
            const char *d = nj_exception_describe(vm);
            g_fail++;
            fprintf(stderr, "FAIL %s: %s.%s threw: %s\n", c->tag, c->cls,
                    c->name, d ? d : "(no description)");
            nj_clear_exception(vm);
            continue;
        }
        OK_MSG(ret.i == c->want, "%s = %d, want %d (Java fixture contract)",
               c->tag, (int)ret.i, (int)c->want);
    }
}

static void test_ioapi_java(nj_vm *vm)
{
    run_ioapi_cases(vm, IOAPI_PROBE, ioapi_IoApiProbe_class,
                    (nj_u4)sizeof(ioapi_IoApiProbe_class), ioapi_probe_cases,
                    (unsigned)(sizeof(ioapi_probe_cases)
                               / sizeof(ioapi_probe_cases[0])));
    run_ioapi_cases(vm, IOAPI_LOCKR, ioapi_LockReader_class,
                    (nj_u4)sizeof(ioapi_LockReader_class), ioapi_lock_cases,
                    (unsigned)(sizeof(ioapi_lock_cases)
                               / sizeof(ioapi_lock_cases[0])));
    run_ioapi_cases(vm, IOAPI_RANGE, ioapi_RangeOnlyReader_class,
                    (nj_u4)sizeof(ioapi_RangeOnlyReader_class),
                    ioapi_range_cases,
                    (unsigned)(sizeof(ioapi_range_cases)
                               / sizeof(ioapi_range_cases[0])));
    OK(nj_class_load(vm, "ioapi/StreamEncoding$WriterOutput",
                     ioapi_StreamEncoding_WriterOutput_class,
                     (nj_u4)sizeof(ioapi_StreamEncoding_WriterOutput_class)) != NULL);
    run_ioapi_cases(vm, IOAPI_ENCODING, ioapi_StreamEncoding_class,
                    (nj_u4)sizeof(ioapi_StreamEncoding_class),
                    ioapi_encoding_cases,
                    (unsigned)(sizeof(ioapi_encoding_cases)
                               / sizeof(ioapi_encoding_cases[0])));
}

int main(void)
{
    nj_port port;
    nj_vm *vm;

    printf("== nano_j2me java.io reference-member test ==\n");

    memset(&port, 0, sizeof(port));
    if (nj_port_headless(&port) != NJ_OK) {
        fprintf(stderr, "cannot start the headless port\n");
        return 2;
    }
    /* Read what System.out writes instead of letting it reach stderr. */
    port.log = cap_log;
    port.user = NULL;

    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    if (!vm) {
        fprintf(stderr, "cannot create the VM\n");
        nj_port_shutdown(&port);
        return 2;
    }

    test_printstream_formats(vm);
    test_printstream_stream(vm);
    test_reader(vm);
    test_reader_base(vm);
    test_writer(vm);
    test_writer_base(vm);
    test_datainput_mark_reset(vm);
    test_ioapi_java(vm);

    nj_vm_destroy(vm);
    nj_port_shutdown(&port);

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
