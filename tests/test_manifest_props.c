/*
 * nano_j2me - tests/test_manifest_props.c
 *
 * MIDlet.getAppProperty() values must be trimmed the way a real AMS hands
 * them to a MIDlet.  One corpus JAR ships
 *
 *     FeeMode: 0 \r\n
 *
 * (one blank before the CR) and calls Integer.parseInt() on the value without
 * a try/catch: on an AMS that passes the blank through, the MIDlet thread dies
 * with NumberFormatException.  Reference implementations trim the value
 * (FreeJ2ME-Plus applies String.trim() to every manifest value).
 *
 * The low-level manifest reader stays byte-faithful on purpose (see
 * test_zip's "Trail-Spaces" check); the trim belongs to the AMS-facing
 * property lookup, which is nj_app_get_property().
 *
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude \
 *       -o /tmp/test_manifest_props tests/test_manifest_props.c \
 *       build/libnj2me.a -lm
 *   /tmp/test_manifest_props
 */

#include "nj2me/nj_app.h"

#include <stdio.h>
#include <string.h>

static int checks;
static int failures;

#define CHECK(cond)                                                         \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                   \
    } while (0)

/* Build an app around a raw manifest blob and look one property up. */
static void expect(const char *label, const char *manifest, const char *attr,
                   const char *want)
{
    nj_app app;
    char out[64];
    nj_status st;

    memset(&app, 0, sizeof(app));
    /* The struct's manifest fields are the whole input of the lookup. */
    app.manifest = (char *)(void *)manifest;
    app.manifest_len = (nj_u4)strlen(manifest);

    memset(out, 0x7f, sizeof(out));
    st = nj_app_get_property(&app, attr, out, sizeof(out));
    CHECK(st == NJ_OK);
    if (st != NJ_OK || strcmp(out, want) != 0) {
        failures++;
        fprintf(stderr, "  [%s] got \"%s\", want \"%s\"\n", label,
                (st == NJ_OK) ? out : "<missing>", want);
    }
}

int main(void)
{
    /* The exact shape of the corpus manifest that killed the title. */
    expect("trailing blank value",
           "Manifest-Version: 1.0\r\n"
           "FeeMode: 0 \r\n"
           "MIDlet-1: Game, /icon.png, st.STA\r\n"
           "\r\n",
           "FeeMode", "0");

    /* A tab and a blank, both trailing. */
    expect("trailing tab and blank",
           "FeeMode:\t7\t \r\n", "FeeMode", "7");

    /* Leading blanks were always trimmed by the reader; keep it that way. */
    expect("leading blanks",
           "FeeMode:    5\r\n", "FeeMode", "5");

    /* Interior blanks are data: only the ends are trimmed. */
    expect("interior blanks",
           "Title:  A  B  \r\n", "Title", "A  B");

    /* A value made only of blanks is empty, not a blank string. */
    expect("blank-only value",
           "Empty:    \r\n", "Empty", "");

    /* Continuations are concatenated first and trimmed last. */
    expect("continuation",
           "Long: a \r\n"
           " b \r\n", "Long", "a b");

    /* A neighbour's trailing blanks do not leak into the next value. */
    expect("following attribute",
           "FeeMode: 0 \r\n"
           "MIDlet-Name: Doraemon \r\n"
           "\r\n",
           "MIDlet-Name", "Doraemon");

    /* Names stay case-insensitive. */
    expect("case-insensitive name",
           "feemode: 1 \r\n", "FeeMode", "1");

    /* Truncation still NUL terminates; the trimmed result fits. */
    {
        nj_app app;
        char small[3];
        nj_status st;

        memset(&app, 0, sizeof(app));
        app.manifest = (char *)(void *)"FeeMode: 12  \r\n";
        app.manifest_len = (nj_u4)strlen("FeeMode: 12  \r\n");
        st = nj_app_get_property(&app, "FeeMode", small, sizeof(small));
        CHECK(st == NJ_OK);
        CHECK(strcmp(small, "12") == 0);
    }

    /* Missing attributes and bad arguments are still rejected. */
    {
        nj_app app;
        char out[16];

        memset(&app, 0, sizeof(app));
        app.manifest = (char *)(void *)"FeeMode: 0 \r\n";
        app.manifest_len = (nj_u4)strlen("FeeMode: 0 \r\n");
        out[0] = 'x';
        CHECK(nj_app_get_property(&app, "Nope", out, sizeof(out)) == NJ_ERR);
        CHECK(out[0] == '\0');
        CHECK(nj_app_get_property(NULL, "FeeMode", out, sizeof(out)) == NJ_ERR);
        CHECK(nj_app_get_property(&app, NULL, out, sizeof(out)) == NJ_ERR);
        CHECK(nj_app_get_property(&app, "FeeMode", NULL, sizeof(out)) == NJ_ERR);
        CHECK(nj_app_get_property(&app, "FeeMode", out, 0u) == NJ_ERR);
        app.manifest = NULL;
        CHECK(nj_app_get_property(&app, "FeeMode", out, sizeof(out)) == NJ_ERR);
    }

    printf("test_manifest_props: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
