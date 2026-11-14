/* VM-owned memory font installation: no port file callbacks are required. */
#include "nj2me/nj.h"
#include "nj_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); failures++; } } while (0)

int main(void)
{
    nj_port port;
    nj_vm *vm;
    FILE *f;
    long len;
    nj_u1 *data;
    const nj_font *font;
    const nj_u1 *glyph;
    int w = 0, h = 0;
    memset(&port, 0, sizeof port);
    vm = nj_vm_create(&port, 2u * 1024u * 1024u);
    CHECK(vm != NULL);
    if (!vm) return 1;
    CHECK(!nj_vm_use_font_data(NULL, (const nj_u1 *)"bad", 3));
    CHECK(!nj_vm_use_font_data(vm, NULL, 0));
    font = nj_fontset_get(&vm->fonts, NJ_FONT_SIZE_MEDIUM, NJ_FONT_STYLE_PLAIN);
    CHECK(!nj_vm_use_font_data(vm, (const nj_u1 *)"bad", 3));
    CHECK(nj_fontset_get(&vm->fonts, NJ_FONT_SIZE_MEDIUM, NJ_FONT_STYLE_PLAIN) == font);
    f = fopen("tests/fixtures/ttf/nj_fixture.ttf", "rb");
    CHECK(f != NULL);
    if (!f) { nj_vm_destroy(vm); return 1; }
    CHECK(fseek(f, 0, SEEK_END) == 0);
    len = ftell(f);
    CHECK(len > 0 && len < 1024 * 1024);
    rewind(f);
    if (len <= 0 || len >= 1024 * 1024) { fclose(f); nj_vm_destroy(vm); return 1; }
    data = (nj_u1 *)malloc((size_t)len);
    CHECK(data != NULL);
    if (!data) { fclose(f); nj_vm_destroy(vm); return 1; }
    CHECK(fread(data, 1, (size_t)len, f) == (size_t)len);
    fclose(f);
    CHECK(nj_vm_use_font_data(vm, data, (nj_u4)len));
    /* Force the provider to use its own copy on the first glyph lookup. */
    memset(data, 0, (size_t)len);
    free(data);
    font = nj_fontset_get(&vm->fonts, NJ_FONT_SIZE_MEDIUM, NJ_FONT_STYLE_PLAIN);
    CHECK(font != NULL && font->glyph_lookup != NULL);
    glyph = font->glyph_lookup(font->glyph_ud, 'O', &w, &h);
    CHECK(glyph != NULL && w > 0 && h > 0);
    CHECK(!nj_vm_use_font_data(vm, (const nj_u1 *)"OTTO", 4));
    CHECK(nj_fontset_get(&vm->fonts, NJ_FONT_SIZE_MEDIUM, NJ_FONT_STYLE_PLAIN) == font);
    glyph = font->glyph_lookup(font->glyph_ud, 'O', &w, &h);
    CHECK(glyph != NULL && w > 0 && h > 0);
    nj_vm_destroy(vm);
    printf("VM memory-font checks: %d failures\n", failures);
    return failures ? 1 : 0;
}
