/* A hardware integration test. Render a moving striped triangle at 96x64,
 * supplying the SDK jitter, true depth discontinuity and a -1px motion field. */
#include "nj_fsr2.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 96
#define H 64
#define OW (2 * W)
#define OH (2 * H)
static uint32_t rgb[W*H], result[OW*OH], history[OW*OH];
static float depth[W*H], motion[W*H*2];

static void rasterize(float jx, float jy, int step)
{
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        int index = y * W + x;
        float sx = x + .5f - jx - step;
        float sy = y + .5f - jy;
        int triangle = sx >= 16 && sy >= 9 && sx + sy < 72;
        rgb[index] = triangle ? (((int)sx & 2) ? 0xfff86430u : 0xffee361cu) : 0xff183682u;
        depth[index] = triangle ? .35f : .85f;
        motion[2*index] = triangle && step ? -1.f : 0.f;
        motion[2*index+1] = 0;
    }
}

static uint32_t checksum(void)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < OW*OH; i++) h = (h ^ result[i]) * 16777619u;
    return h;
}

int main(void)
{
    char error[256] = {0};
    /* Reject either axis just outside the envelope before opening Vulkan. */
    if (nj_fsr2_create(W,H,3*W+1,3*H,error,sizeof(error)) ||
        !strstr(error,"dimensions") ||
        nj_fsr2_create(W,H,3*W,3*H+1,error,sizeof(error)) ||
        !strstr(error,"dimensions")) return 1;
    NjFsr2 *ctx = nj_fsr2_create(W,H,OW,OH,error,sizeof(error));
    if (!ctx) {
        fprintf(stderr, "%s\n",error);
        return strstr(error,"No compatible Vulkan") || strstr(error,"vkCreateInstance") ? 77 : 1;
    }
    NjFsr2Frame f = {0};
    f.color_argb = rgb;
    f.depth = depth;
    f.motion_xy = motion;
    f.delta_ms = 16.667f;
    f.camera_near = .1f;
    f.camera_far = 50.f;
    f.fov_y = 1.f;
    int status = 1;
    for (int frame = 0; frame < 4; frame++) {
        int step = frame < 3 ? frame : 2;
        nj_fsr2_jitter(ctx, (unsigned)step, &f.jitter_x, &f.jitter_y);
        rasterize(f.jitter_x, f.jitter_y, step);
        f.reset = frame == 0 || frame == 3;
        if (!nj_fsr2_dispatch(ctx,&f,result,error,sizeof(error))) {
            fprintf(stderr,"dispatch %d: %s\n", frame,error);
            goto done;
        }
        uint32_t fg = result[32*OW+60], bg = result[100*OW+160];
        if (((fg >> 16) & 255) < 170 || ((bg >> 16) & 255) > 70 ||
            (bg & 255) < 95 || (bg & 255) > 170 || (bg >> 24) != 255) {
            fprintf(stderr,"invalid readback: foreground=%08" PRIx32 " background=%08" PRIx32 "\n",fg,bg);
            goto done;
        }
        printf("frame=%d reset=%d jitter=(%.4f,%.4f) %dx%d checksum=%08" PRIx32 "\n",
               frame,f.reset,f.jitter_x,f.jitter_y,OW,OH,checksum());
        if (frame == 2) memcpy(history,result,sizeof(result));
    }
    {
        unsigned different = 0;
        for (int i = 0; i < OW*OH; i++) different += history[i] != result[i];
        printf("same input, preserved versus reset history: %u/%u different pixels\n",different,OW*OH);
        if (!different) goto done;
    }
    status = 0;
done:
    nj_fsr2_destroy(ctx);
    return status;
}
