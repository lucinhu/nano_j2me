/* LocalDevice.getProperty must be callable by actual Java invokestatic.
 *
 * The embedded, hand-assembled Java 45.3 class has no native methods or JAR
 * dependencies. Its only method is equivalent to:
 *   package test;
 *   public class BluetoothPropertyCalls {
 *     public static String query(String key) {
 *       return javax.bluetooth.LocalDevice.getProperty(key);
 *     }
 *   }
 * Code: aload_0; invokestatic #12; areturn. CP #12 is a Methodref to the
 * real builtin LocalDevice, not a test native. Calling query through the VM
 * therefore exercises the interpreter's staticness check and native arguments.
 *
 * --expect-old-icce is a separate sensitivity check for a real pre-fix archive:
 * it succeeds ONLY when this bytecode raises IncompatibleClassChangeError.
 * The default mode tests the corrected contract, using fake port hooks only;
 * it never initializes a host Bluetooth adapter or opens a connection.
 */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOCAL "javax/bluetooth/LocalDevice"
#define PROBE "test/BluetoothPropertyCalls"
#define DESC "(Ljava/lang/String;)Ljava/lang/String;"
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static unsigned checks, failures, queries;
static const char *group;

#define CHECK(cond, message) do { \
    ++checks; \
    if (!(cond)) { \
        ++failures; \
        fprintf(stderr, "FAIL Bluetooth property %s line %d: %s\n", \
                group, __LINE__, message); \
    } \
} while (0)

static const nj_u1 probe_class[] = {
    0xca, 0xfe, 0xba, 0xbe, 0x00, 0x03, 0x00, 0x2d, 0x00, 0x0d, 0x01, 0x00, 0x1b, 0x74, 0x65, 0x73,
    0x74, 0x2f, 0x42, 0x6c, 0x75, 0x65, 0x74, 0x6f, 0x6f, 0x74, 0x68, 0x50, 0x72, 0x6f, 0x70, 0x65,
    0x72, 0x74, 0x79, 0x43, 0x61, 0x6c, 0x6c, 0x73, 0x07, 0x00, 0x01, 0x01, 0x00, 0x10, 0x6a, 0x61,
    0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x4f, 0x62, 0x6a, 0x65, 0x63, 0x74, 0x07, 0x00,
    0x03, 0x01, 0x00, 0x05, 0x71, 0x75, 0x65, 0x72, 0x79, 0x01, 0x00, 0x26, 0x28, 0x4c, 0x6a, 0x61,
    0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x53, 0x74, 0x72, 0x69, 0x6e, 0x67, 0x3b, 0x29,
    0x4c, 0x6a, 0x61, 0x76, 0x61, 0x2f, 0x6c, 0x61, 0x6e, 0x67, 0x2f, 0x53, 0x74, 0x72, 0x69, 0x6e,
    0x67, 0x3b, 0x01, 0x00, 0x04, 0x43, 0x6f, 0x64, 0x65, 0x01, 0x00, 0x1b, 0x6a, 0x61, 0x76, 0x61,
    0x78, 0x2f, 0x62, 0x6c, 0x75, 0x65, 0x74, 0x6f, 0x6f, 0x74, 0x68, 0x2f, 0x4c, 0x6f, 0x63, 0x61,
    0x6c, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65, 0x07, 0x00, 0x08, 0x01, 0x00, 0x0b, 0x67, 0x65, 0x74,
    0x50, 0x72, 0x6f, 0x70, 0x65, 0x72, 0x74, 0x79, 0x0c, 0x00, 0x0a, 0x00, 0x06, 0x0a, 0x00, 0x09,
    0x00, 0x0b, 0x00, 0x21, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x09,
    0x00, 0x05, 0x00, 0x06, 0x00, 0x01, 0x00, 0x07, 0x00, 0x00, 0x00, 0x11, 0x00, 0x01, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x05, 0x2a, 0xb8, 0x00, 0x0c, 0xb0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

typedef struct radio {
    unsigned calls;
    int available;
} radio;

static nj_status fake_local(void *ud, nj_bt_local *out)
{
    radio *r = (radio *)ud;
    ++r->calls;
    if (!r->available) return NJ_ERR_UNSUPPORTED;
    memset(out, 0, sizeof(*out));
    return NJ_OK;
}

static nj_vm *create(const nj_port *port)
{
    nj_vm *vm = nj_vm_create(port, 512u * 1024u);
    nj_class *cls;
    nj_method *m = NULL;
    nj_u2 i;
    if (!vm) {
        fprintf(stderr, "Bluetooth property fixture: VM creation failed\n");
        exit(2);
    }
    cls = nj_class_load(vm, PROBE, probe_class, sizeof(probe_class));
    CHECK(cls != NULL, "load Java 45.3 probe");
    if (!cls) {
        nj_vm_destroy(vm);
        exit(2);
    }
    /* nj_method_lookup is instance-only. Inspect this declared static wrapper
     * directly; class loading keeps its original classfile bytecode intact. */
    for (i = 0u; i < cls->method_count; ++i) {
        if (strcmp(cls->methods[i].name, "query") == 0 &&
            strcmp(cls->methods[i].desc, DESC) == 0) {
            m = &cls->methods[i];
            break;
        }
    }
    CHECK(m && (m->flags & NJ_ACC_STATIC) != 0u && !m->native &&
          m->code_len == 5u && m->code &&
          memcmp(m->code, "\x2a\xb8\x00\x0c\xb0", 5u) == 0,
          "probe must execute actual invokestatic bytecode");
    return vm;
}

static nj_status query(nj_vm *vm, nj_ref key, nj_value *ret)
{
    nj_value arg;
    arg.r = key;
    ret->r = NULL;
    ++queries;
    return nj_call_static(vm, PROBE, "query", DESC, &arg, ret);
}

static void expect(nj_vm *vm, radio *r, nj_ref key, const char *value,
                   unsigned hook_calls)
{
    unsigned before = r->calls;
    nj_value ret;
    nj_status status = query(vm, key, &ret);
    CHECK(status == NJ_OK, "Java invokestatic query must succeed");
    CHECK(vm->exception == NULL, "query must not throw without a radio");
    if (status == NJ_OK && !vm->exception) {
        CHECK(value ? nj_string_equals_utf8(ret.r, value) : ret.r == NULL,
              "property value must match the supported/unknown answer");
    }
    CHECK(r->calls - before == hook_calls, "radio hook only for known properties");
    vm->exception = NULL;
}

static nj_ref text(nj_vm *vm, const char *s)
{
    nj_ref str = nj_string_new(vm, s);
    if (!str) {
        fprintf(stderr, "Bluetooth property fixture: String creation failed\n");
        exit(2);
    }
    return str;
}

static void test_contract(int mode)
{
    static const char *const names[] = {
        "bluetooth.sd.attr.retrievable.max",
        "bluetooth.connected.devices.max",
        "bluetooth.sd.trans.max"
    };
    static const char *const values[] = { "64", "7", "1" };
    static const char *const unknown[] = {
        "", "unknown.property", "bluetooth.api.version",
        "bluetooth.master.switch", "bluetooth.l2cap.receiveMTU.max",
        "Bluetooth.sd.trans.max", "bluetooth.sd.trans.max ",
        "bluetooth.sd.attr.retrievable.max.extra"
    };
    nj_port port;
    radio r;
    nj_vm *vm;
    nj_class *local;
    nj_method *property;
    size_t i, j;
    char long_key[256];
    nj_u2 embedded[64];
    nj_ref key;
    memset(&port, 0, sizeof(port));
    memset(&r, 0, sizeof(r));
    r.available = mode == 3;
    port.ud = &r;
    if (mode >= 2) port.bt_local = fake_local;
    group = mode == 0 ? "no port" : mode == 1 ? "no hook" :
            mode == 2 ? "unavailable radio" : "available fake radio";
    vm = create(mode == 0 ? NULL : &port);
    local = nj_class_lookup(vm, LOCAL);
    property = NULL;
    if (local) {
        for (i = 0u; i < local->method_count; ++i) {
            nj_method *m = &local->methods[i];
            if (strcmp(m->name, "getProperty") == 0 && strcmp(m->desc, DESC) == 0) {
                property = m;
                break;
            }
        }
    }
    CHECK(property && (property->flags & NJ_ACC_STATIC), "getProperty is static");
    CHECK(property && property->arg_slots == 1u && property->max_locals == 2u &&
          property->max_stack == 0u,
          "static property keeps legacy two-local snapshot ABI");
    expect(vm, &r, NULL, NULL, 0u); /* Preserve this runtime's permissive null. */
    for (i = 0; i < COUNT(unknown); ++i)
        expect(vm, &r, text(vm, unknown[i]), NULL, 0u);
    for (i = 0; i < COUNT(names); ++i) {
        expect(vm, &r, text(vm, names[i]), mode == 3 ? values[i] : NULL,
               mode >= 2 ? 1u : 0u);
        /* C-string conversion would incorrectly alias name + U+0000 + 'x'. */
        for (j = 0; names[i][j]; ++j) embedded[j] = (nj_u2)names[i][j];
        embedded[j++] = 0;
        embedded[j++] = 'x';
        key = nj_new_string_utf16(vm, embedded, (int)j);
        CHECK(key != NULL, "construct embedded-NUL Java key");
        if (key) expect(vm, &r, key, NULL, 0u);
    }
    memset(long_key, 'x', sizeof(long_key) - 1u);
    long_key[sizeof(long_key) - 1u] = '\0';
    expect(vm, &r, text(vm, long_key), NULL, 0u);
    CHECK(vm->exception == NULL, "all property queries leave the VM usable");
    nj_vm_destroy(vm);
}

static void test_frame_padding(void)
{
    nj_vm *vm = nj_vm_create(NULL, 512u * 1024u);
    nj_thread *th;
    nj_method m;
    nj_value *base;
    nj_u2 depth;
    nj_status status;
    const nj_s8 token = INT64_C(0x1234567812345678);
    group = "complete local slot initialization";
    CHECK(vm != NULL, "create VM for reused frame slots");
    if (!vm) return;
    th = nj_thread_main(vm);
    CHECK(th && th->stack_end - th->top >= 7,
          "frame padding fixture has real thread stack capacity");
    if (!th || th->stack_end - th->top < 7) {
        nj_vm_destroy(vm);
        return;
    }
    memset(&m, 0, sizeof(m));
    m.name = "padding";
    m.max_locals = 3u;
    m.arg_slots = 1u;
    m.flags = NJ_ACC_STATIC | NJ_ACC_NATIVE;
    base = th->top;
    depth = th->nframes;
    base[0].j = token;
    memset(base + 1, 0xa5, 2u * sizeof(*base));
    status = nj_frame_push(vm, th, &m, base, 1u);
    CHECK(status == NJ_OK, "push a native frame over poisoned unused locals");
    if (status == NJ_OK) {
        CHECK(base[0].j == token, "frame setup preserves complete argument slot");
        CHECK(base[1].j == 0, "first unused local has all 64 bits cleared");
        CHECK(base[2].j == 0, "second unused local has all 64 bits cleared");
        CHECK(th->frames[depth].stack == base + 3,
              "operand stack starts beyond declared locals");
        nj_frame_pop(vm, th);
        CHECK(th->nframes == depth && th->top == base,
              "pop restores caller depth and stack allocation");
    }
    nj_vm_destroy(vm);
}

static int old_icce(void)
{
    /* The bare CLDC library does not register ICCE. Supply this zero-member
     * exception class only in the old-code harness, so nj_exception_new can
     * instantiate the exact type instead of its generic Throwable fallback.
     * The interpreter must still reach its real staticness THROW unaided. */
    static const nj_builtin_class icce_def = {
        "java/lang/IncompatibleClassChangeError", "java/lang/Error",
        NJ_ACC_PUBLIC, NULL, 0u, NULL, 0u
    };
    nj_class *icce;
    nj_vm *vm;
    nj_value ret;
    nj_status status;
    const char *exception;
    int matched;
    group = "old archive sensitivity";
    vm = create(NULL);
    icce = nj_class_lookup(vm, icce_def.name);
    if (!icce) icce = nj_class_create_builtin(vm, &icce_def);
    CHECK(icce && nj_class_lookup(vm, icce_def.name) == icce,
          "prepare exact ICCE type for real interpreter throw");
    if (!icce || failures) {
        nj_vm_destroy(vm);
        return 2;
    }
    status = query(vm, text(vm, "unknown.property"), &ret);
    exception = vm->exception && vm->exception->cls ?
                vm->exception->cls->name : "<none>";
    matched = status != NJ_OK &&
              strcmp(exception, "java/lang/IncompatibleClassChangeError") == 0;
    CHECK(matched, "pre-fix bytecode must raise IncompatibleClassChangeError");
    printf("old invokestatic sensitivity: status=%d exception=%s matched=%d\n",
           (int)status, exception, matched);
    nj_vm_destroy(vm);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    int mode;
    if (argc == 2 && strcmp(argv[1], "--expect-old-icce") == 0)
        return old_icce();
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--expect-old-icce]\n", argv[0]);
        return 2;
    }
    for (mode = 0; mode < 4; ++mode) test_contract(mode);
    test_frame_padding();
    printf("Bluetooth property: %u Java queries, %u checks, %u failures\n",
           queries, checks, failures);
    return failures ? 1 : 0;
}
