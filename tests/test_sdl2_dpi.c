/* Real SDL renderer + event regression, including the production window loops.
 * Link with nj2me and the same SDL headers/libs as test_port_binds. Including the
 * two adapters lets the test inspect rectangles without exposing test-only API.
 * Default: dummy video. NJ_DPI_TEST_VIDEO=x11/wayland uses an existing display.
 * NJ_DPI_TEST_SHOTS=<existing directory> saves renderer (not framebuffer) BMPs.
 * Settings saves are isolated in a temporary XDG directory and removed on exit.
 */
#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>
#include <unistd.h>
#include "../src/port/posix/nj_port_sdl2.c"
#include "../src/port/posix/nj_sdl2_client.c"

static int dpi_checks, dpi_failures;
#define DPI_CHECK(expr, text) do { \
    dpi_checks++; \
    if (!(expr)) { dpi_failures++; \
        fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, text, SDL_GetError()); \
    } \
} while (0)

static void dpi_env_clear(void)
{
    SDL_setenv("NJ2ME_UI_SCALE", "", 1);
    SDL_setenv("GDK_SCALE", "", 1);
    SDL_setenv("GDK_DPI_SCALE", "", 1);
    SDL_setenv("QT_SCALE_FACTOR", "", 1);
    SDL_setenv("NJ_SDL2_UI_SCALE", "0", 1);
}

static void dpi_mouse(SDL_Window *win, int x, int y, Uint32 type)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    e.button.windowID = SDL_GetWindowID(win);
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = x;
    e.button.y = y;
    DPI_CHECK(SDL_PushEvent(&e) == 1, "mouse event enters actual SDL queue");
}

static void dpi_finger(SDL_Window *win, float x, float y, Uint32 type)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    e.tfinger.windowID = SDL_GetWindowID(win);
    e.tfinger.touchId = 42;
    e.tfinger.fingerId = 1;
    e.tfinger.x = x;
    e.tfinger.y = y;
    DPI_CHECK(SDL_PushEvent(&e) == 1, "touch event enters actual SDL queue");
}

static void dpi_window_event(SDL_Window *win, Uint8 kind)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_WINDOWEVENT;
    e.window.windowID = SDL_GetWindowID(win);
    e.window.event = kind;
    DPI_CHECK(SDL_PushEvent(&e) == 1, "window event enters actual SDL queue");
}

static void dpi_key(SDL_Keycode key)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = key;
    DPI_CHECK(SDL_PushEvent(&e) == 1, "key event enters actual SDL queue");
}

static void dpi_shot(SDL_Renderer *renderer, const char *name)
{
    const char *dir = SDL_getenv("NJ_DPI_TEST_SHOTS");
    SDL_Surface *image;
    int w = 0, h = 0;
    char path[1024];
    if (!dir || !*dir) return;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    image = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    DPI_CHECK(image != NULL, "allocate renderer capture");
    if (!image) return;
    DPI_CHECK(SDL_RenderReadPixels(renderer, NULL, image->format->format,
                                   image->pixels, image->pitch) == 0,
              "read actual SDL renderer output");
    SDL_snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
    DPI_CHECK(SDL_SaveBMP(image, path) == 0, "write renderer screenshot");
    SDL_FreeSurface(image);
}

static void dpi_client_pump(nj_sdl_client *c, nj_client_ui *ui,
                            nj_client_settings *settings)
{
    char run[1024] = {0};
    DPI_CHECK(client_launcher_events(c, ui, settings, run, sizeof(run)) == 0,
              "launcher event loop continues");
    client_draw(c, ui, settings);
}

static void dpi_test_launcher(void)
{
    nj_client_settings settings;
    nj_library lib;
    nj_client_ui *ui;
    nj_sdl_client c;
    int i;
    static const int scales[] = {100, 125, 150, 200, 300};
    nj_client_settings_default(&settings);
    memset(&lib, 0, sizeof(lib));
    settings.ui_scale = 100;
    ui = nj_client_ui_new(&settings, &lib);
    DPI_CHECK(ui != NULL, "create real launcher model");
    if (!ui) return;
    if (!client_launcher_open(&c, 720, 540, ui, &settings)) {
        DPI_CHECK(0, "open real launcher window");
        nj_client_ui_free(ui);
        return;
    }
    dpi_client_pump(&c, ui, &settings);
    DPI_CHECK(c.width == 720 && c.height == 540, "100% preserves old logical window size");
    DPI_CHECK((SDL_GetWindowFlags(c.window) & SDL_WINDOW_ALLOW_HIGHDPI) != 0,
              "launcher enables high DPI drawable");
    dpi_shot(c.renderer, "launcher-100");
    for (i = 0; i < (int)(sizeof(scales) / sizeof(scales[0])); i++) {
        int x, y, expected_w, expected_h;
        char name[64];
        /* A small logical window fits all test scales on the dummy display. */
        settings.ui_scale = 100;
        dpi_client_pump(&c, ui, &settings);
        SDL_SetWindowSize(c.window, 360, 240);
        dpi_window_event(c.window, SDL_WINDOWEVENT_SIZE_CHANGED);
        dpi_client_pump(&c, ui, &settings);
        settings.ui_scale = scales[i];
        dpi_client_pump(&c, ui, &settings);
        expected_w = nj_sdl2_ui_extent(360, c.dpi.window_x);
        expected_h = nj_sdl2_ui_extent(240, c.dpi.window_y);
        nj_sdl2_limit_window(c.window, &expected_w, &expected_h);
        printf("launcher percent=%d cached=%d logical=%dx%d window=%dx%d drawable=%dx%d factor=%.2f expected=%dx%d mode=%d\n",
               scales[i], c.ui_percent, c.width, c.height, c.dpi.window_w,
               c.dpi.window_h, c.dpi.output_w, c.dpi.output_h, c.dpi.window_x,
               expected_w, expected_h, nj_client_ui_mode(ui));
        DPI_CHECK(abs(c.dpi.window_w - expected_w) <= 1 &&
                  abs(c.dpi.window_h - expected_h) <= 1,
                  "manual scale changes window immediately within usable bounds");
        DPI_CHECK(settings.launcher_w == c.width && settings.launcher_h == c.height,
                  "persisted window dimensions are logical UI units");
        /* Settings button centre in the real footer, then logical -> OS point. */
        x = (int)((c.width - 138.0) * c.dpi.window_w / c.width);
        y = (int)((c.height - 20.0) * c.dpi.window_h / c.height);
        dpi_mouse(c.window, x, y, SDL_MOUSEBUTTONDOWN);
        dpi_mouse(c.window, x, y, SDL_MOUSEBUTTONUP);
        dpi_client_pump(&c, ui, &settings);
        DPI_CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_SETTINGS,
                  "scaled mouse click hits the displayed Settings button");
        SDL_snprintf(name, sizeof(name), "launcher-settings-%d", scales[i]);
        dpi_shot(c.renderer, name);
        nj_client_ui_char(ui, 's');
        client_draw(&c, ui, &settings);
        dpi_finger(c.window, (c.width - 138.0f) / c.width,
                   (c.height - 20.0f) / c.height, SDL_FINGERDOWN);
        dpi_finger(c.window, (c.width - 138.0f) / c.width,
                   (c.height - 20.0f) / c.height, SDL_FINGERUP);
        dpi_client_pump(&c, ui, &settings);
        DPI_CHECK(nj_client_ui_mode(ui) == NJ_CLIENT_MODE_SETTINGS,
                  "normalized touch hits the same Settings button");
        nj_client_ui_char(ui, 's');
    }
    /* Capture the normal logical launcher at desktop percentages as well. */
    settings.ui_scale = 100;
    dpi_client_pump(&c, ui, &settings);
    SDL_SetWindowSize(c.window, 720, 540);
    dpi_window_event(c.window, SDL_WINDOWEVENT_SIZE_CHANGED);
    dpi_client_pump(&c, ui, &settings);
    dpi_shot(c.renderer, "launcher-normal-100");
    settings.ui_scale = 150;
    dpi_client_pump(&c, ui, &settings);
    dpi_shot(c.renderer, "launcher-normal-150");
    settings.ui_scale = 200;
    dpi_client_pump(&c, ui, &settings);
    dpi_shot(c.renderer, "launcher-normal-200");
    printf("launcher backend=%s DPI=%.2f logical=%dx%d window=%dx%d drawable=%dx%d\n",
           SDL_GetCurrentVideoDriver(), c.dpi.pixels, c.width, c.height,
           c.dpi.window_w, c.dpi.window_h, c.dpi.output_w, c.dpi.output_h);
    /* Change the actual settings row with queued SDL keys, including scrolling. */
    settings.ui_scale = 100;
    dpi_client_pump(&c, ui, &settings);
    dpi_key(SDLK_s);
    dpi_key(SDLK_END);
    dpi_key(SDLK_UP);
    dpi_key(SDLK_RIGHT);
    dpi_client_pump(&c, ui, &settings);
    dpi_client_pump(&c, ui, &settings);
    DPI_CHECK(settings.ui_scale == 125 && c.ui_percent == 125,
              "UI scale settings row takes effect during the SDL event loop");
    dpi_shot(c.renderer, "launcher-settings-live-125");
    nj_client_ui_char(ui, 's');

    settings.ui_scale = 102;
    dpi_client_pump(&c, ui, &settings);
    DPI_CHECK(c.dpi.pixels == 1.02, "explicit 102% is not rounded to automatic 100%");
    settings.ui_scale = 200;
    SDL_setenv("NJ2ME_UI_SCALE", "1.02", 1);
    c.dpi_dirty = 1;
    dpi_client_pump(&c, ui, &settings);
    DPI_CHECK(c.dpi.pixels == 1.02, "explicit 1.02 factor is not rounded away");
    SDL_setenv("NJ2ME_UI_SCALE", "1.5", 1);
    c.dpi_dirty = 1;
    dpi_client_pump(&c, ui, &settings);
    DPI_CHECK(c.dpi.pixels == 1.5, "explicit factor override precedes saved percent");
    SDL_setenv("NJ2ME_UI_SCALE", "", 1);
    SDL_setenv("GDK_SCALE", "2", 1);
    settings.ui_scale = 0;
    dpi_client_pump(&c, ui, &settings);
    DPI_CHECK(c.dpi.pixels >= 2.0, "automatic desktop scale follows GDK_SCALE");
    dpi_shot(c.renderer, "launcher-auto-gdk-200");
    SDL_setenv("GDK_SCALE", "1", 1);
    dpi_window_event(c.window, SDL_WINDOWEVENT_MOVED);
    dpi_client_pump(&c, ui, &settings);
    dpi_client_pump(&c, ui, &settings);
    DPI_CHECK(c.dpi.pixels >= 1.0 && c.dpi.pixels < 2.0,
              "moving the window re-reads desktop DPI");
    client_launcher_close(&c);
    nj_client_ui_free(ui);
    dpi_env_clear();
}

static void dpi_drain(nj_port *port)
{
    nj_event ev;
    int i;
    for (i = 0; i < 256 && port->input_poll(port->ud, &ev); i++) {}
}

static void dpi_test_game(void)
{
    nj_port port;
    sdl_state *s;
    int w = 0, h = 0, stride = 0, button100, x, y;
    void *pixels = NULL;
    nj_event event;
    SDL_setenv("NJ_SDL2_UI_SCALE", "100", 1);
    SDL_setenv("NJ_SDL2_KEYS", "1", 1);
    SDL_setenv("NJ_SDL2_GAMEPAD", "0", 1);
    SDL_setenv("NJ_SDL2_ACCEL", "0", 1);
    DPI_CHECK(nj_port_sdl2(&port, "DPI regression") == NJ_OK, "create SDL game port");
    port.width = 240;
    port.height = 320;
    if (port.display_init(port.ud, &w, &h, &stride, &pixels) < 0) {
        DPI_CHECK(0, "open game renderer");
        port.shutdown(port.ud);
        return;
    }
    s = (sdl_state *)port.ud;
    SDL_SetWindowSize(s->window, 960, 720);
    dpi_window_event(s->window, SDL_WINDOWEVENT_SIZE_CHANGED);
    memset(pixels, 0x80, (size_t)stride * h);
    port.display_present(port.ud, 0, 0, w, h);
    dpi_drain(&port);
    button100 = s->btn[NJ_SDL2_BTN_UP].w;
    dpi_shot(s->renderer, "game-overlay-100");
    SDL_setenv("NJ_SDL2_UI_SCALE", "200", 1);
    dpi_window_event(s->window, SDL_WINDOWEVENT_MOVED);
    port.display_present(port.ud, 0, 0, w, h);
    DPI_CHECK(w == 240 && h == 320 && s->width == 240 && s->height == 320,
              "UI DPI leaves Java framebuffer resolution unchanged");
    DPI_CHECK(s->game_dst.w == 240 * s->scale && s->game_dst.h == 320 * s->scale,
              "game rendering stays at an integer pixel scale");
    DPI_CHECK(s->btn[NJ_SDL2_BTN_UP].w == 2 * button100,
              "overlay buttons double at 200% on the same drawable");
    dpi_shot(s->renderer, "game-overlay-200");
    x = s->speed_btn.x + s->speed_btn.w / 2;
    y = s->speed_btn.y + s->speed_btn.h / 2;
    dpi_mouse(s->window, nj_sdl2_coord(x, s->dpi.output_w, s->dpi.window_w),
              nj_sdl2_coord(y, s->dpi.output_h, s->dpi.window_h), SDL_MOUSEBUTTONDOWN);
    dpi_mouse(s->window, nj_sdl2_coord(x, s->dpi.output_w, s->dpi.window_w),
              nj_sdl2_coord(y, s->dpi.output_h, s->dpi.window_h), SDL_MOUSEBUTTONUP);
    port.display_present(port.ud, 0, 0, w, h);
    DPI_CHECK(s->speed_percent == 150, "scaled mouse hits the speed badge");
    DPI_CHECK(!port.input_poll(port.ud, &event), "overlay click sends no Java pointer");
    x = s->btn[NJ_SDL2_BTN_UP].x + s->btn[NJ_SDL2_BTN_UP].w / 2;
    y = s->btn[NJ_SDL2_BTN_UP].y + s->btn[NJ_SDL2_BTN_UP].h / 2;
    dpi_finger(s->window, (x + 0.5f) / s->dpi.output_w,
               (y + 0.5f) / s->dpi.output_h, SDL_FINGERDOWN);
    dpi_finger(s->window, (x + 0.5f) / s->dpi.output_w,
               (y + 0.5f) / s->dpi.output_h, SDL_FINGERUP);
    port.display_present(port.ud, 0, 0, w, h);
    DPI_CHECK(port.input_poll(port.ud, &event) && event.type == NJ_EV_KEY_DOWN &&
              event.key == NJ_KEY_UP, "touch hits scaled overlay Up key");
    DPI_CHECK(port.input_poll(port.ud, &event) && event.type == NJ_EV_KEY_UP &&
              event.key == NJ_KEY_UP, "touch releases scaled overlay Up key");
    dpi_drain(&port);
    s->keys_visible = 0;
    port.display_present(port.ud, 0, 0, w, h);
    x = s->game_dst.x + 120 * s->scale;
    y = s->game_dst.y + 160 * s->scale;
    dpi_mouse(s->window, nj_sdl2_coord(x, s->dpi.output_w, s->dpi.window_w),
              nj_sdl2_coord(y, s->dpi.output_h, s->dpi.window_h), SDL_MOUSEBUTTONDOWN);
    dpi_mouse(s->window, nj_sdl2_coord(x, s->dpi.output_w, s->dpi.window_w),
              nj_sdl2_coord(y, s->dpi.output_h, s->dpi.window_h), SDL_MOUSEBUTTONUP);
    port.display_present(port.ud, 0, 0, w, h);
    DPI_CHECK(port.input_poll(port.ud, &event) && event.type == NJ_EV_POINTER_DOWN &&
              abs(event.x - 120) <= 1 && abs(event.y - 160) <= 1,
              "mouse maps drawable centre back to Java device pixels once");
    DPI_CHECK(port.input_poll(port.ud, &event) && event.type == NJ_EV_POINTER_UP,
              "Java pointer release follows scaled press");
    dpi_finger(s->window, (x + 0.5f) / s->dpi.output_w,
               (y + 0.5f) / s->dpi.output_h, SDL_FINGERDOWN);
    dpi_finger(s->window, (x + 0.5f) / s->dpi.output_w,
               (y + 0.5f) / s->dpi.output_h, SDL_FINGERUP);
    port.display_present(port.ud, 0, 0, w, h);
    DPI_CHECK(port.input_poll(port.ud, &event) && event.type == NJ_EV_POINTER_DOWN &&
              abs(event.x - 120) <= 1 && abs(event.y - 160) <= 1,
              "normalized touch maps to the same Java device pixels");
    dpi_drain(&port);
    s->keys_visible = 1;
    SDL_SetWindowSize(s->window, 360, 240);
    SDL_setenv("NJ_SDL2_UI_SCALE", "300", 1);
    dpi_window_event(s->window, SDL_WINDOWEVENT_SIZE_CHANGED);
    port.display_present(port.ud, 0, 0, w, h);
    {
        int i;
        for (i = 0; i < s->btn_n; i++) {
            SDL_Rect b = s->btn[i];
            DPI_CHECK(b.x >= 0 && b.y >= 0 &&
                      b.x + b.w <= s->dpi.output_w &&
                      b.y + b.h <= s->dpi.output_h,
                      "300% overlay remains reachable in a small window");
        }
    }
    x = s->btn[NJ_SDL2_BTN_UP].x + s->btn[NJ_SDL2_BTN_UP].w / 2;
    y = s->btn[NJ_SDL2_BTN_UP].y + s->btn[NJ_SDL2_BTN_UP].h / 2;
    dpi_mouse(s->window, nj_sdl2_coord(x, s->dpi.output_w, s->dpi.window_w),
              nj_sdl2_coord(y, s->dpi.output_h, s->dpi.window_h), SDL_MOUSEBUTTONDOWN);
    dpi_mouse(s->window, nj_sdl2_coord(x, s->dpi.output_w, s->dpi.window_w),
              nj_sdl2_coord(y, s->dpi.output_h, s->dpi.window_h), SDL_MOUSEBUTTONUP);
    port.display_present(port.ud, 0, 0, w, h);
    DPI_CHECK(port.input_poll(port.ud, &event) && event.type == NJ_EV_KEY_DOWN &&
              event.key == NJ_KEY_UP, "small-window 300% Up key is clickable");
    dpi_shot(s->renderer, "game-small-overlay-300");
    printf("game DPI=%.2f window=%dx%d drawable=%dx%d Java=%dx%d integer=%d\n",
           s->dpi.pixels, s->dpi.window_w, s->dpi.window_h,
           s->dpi.output_w, s->dpi.output_h, w, h, s->scale);
    port.shutdown(port.ud);
}

int main(void)
{
    const char *video_env = SDL_getenv("NJ_DPI_TEST_VIDEO");
    char video[32];
    char isolated[] = "/tmp/nj2me-dpi-XXXXXX";
    char config[256];
    char subdir[256];
    SDL_snprintf(video, sizeof(video), "%s", video_env && *video_env ? video_env : "dummy");
    if (!mkdtemp(isolated)) return 1;
    SDL_setenv("XDG_CONFIG_HOME", isolated, 1);
    SDL_setenv("XDG_DATA_HOME", isolated, 1);
    SDL_setenv("SDL_VIDEODRIVER", video, 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    dpi_env_clear();
    DPI_CHECK(nj_sdl2_factor("NaN") == 0.0 && nj_sdl2_factor("2oops") == 0.0,
              "malformed scale overrides fall back safely");
    dpi_test_launcher();
    dpi_test_game();
    SDL_snprintf(config, sizeof(config), "%s/nano_j2me/client.conf", isolated);
    SDL_snprintf(subdir, sizeof(subdir), "%s/nano_j2me", isolated);
    unlink(config);
    rmdir(subdir);
    rmdir(isolated);
    printf("SDL DPI regression: %d checks, %d failures (%s)\n",
           dpi_checks, dpi_failures, video);
    return dpi_failures ? 1 : 0;
}
