#include "nj_fsr2.h"
#include <stdio.h>

static void disabled(char *error, size_t cap)
{
    if (error && cap) snprintf(error, cap, "FSR2 Vulkan backend disabled at build time");
}

NjFsr2 *nj_fsr2_create(int rw, int rh, int ow, int oh, char *error, size_t cap)
{
    (void)rw; (void)rh; (void)ow; (void)oh;
    disabled(error, cap);
    return NULL;
}

void nj_fsr2_destroy(NjFsr2 *ctx) { (void)ctx; }
void nj_fsr2_jitter(NjFsr2 *ctx, unsigned frame, float *x, float *y)
{
    (void)ctx; (void)frame;
    if (x) *x = 0;
    if (y) *y = 0;
}

int nj_fsr2_dispatch(NjFsr2 *ctx, const NjFsr2Frame *frame, uint32_t *out,
                     char *error, size_t cap)
{
    (void)ctx; (void)frame; (void)out;
    disabled(error, cap);
    return 0;
}
