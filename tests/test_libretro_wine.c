/*
 * tests/test_libretro_wine.c -- Win32 mock libretro frontend for the
 * nano_j2me libretro core, meant to be compiled with mingw-w64 and executed
 * under wine on a Linux build host (tools/check_libretro_windows.sh does
 * both).
 *
 * This is the Windows counterpart of tests/test_libretro_frontend.c: it
 * LoadLibraryA()s the core DLL and drives the core the way RetroArch would
 * on Windows (set_environment, callbacks, init, get_system_info,
 * load_game, retro_run frames, unload_game, deinit).  It exists to prove
 * that the PE DLL is not just linkable but actually runs with the Win32
 * filesystem/thread layer, produces video and audio, and can be loaded by a
 * separate process.
 *
 * Build (from the repository root, on a Linux host):
 *   x86_64-w64-mingw32-gcc -std=c99 -O2 -Wall -Wextra \
 *      -I src/port/libretro -o build/test_libretro_wine.exe \
 *      tests/test_libretro_wine.c
 *   WINEDEBUG=-all wine build/test_libretro_wine.exe \
 *      --core build/libretro-dist/windows-x86_64/nano_j2me_libretro.dll \
 *      --jar  tests/fixtures/loopdemo.jar --frames 120 --save-dir build/wine-save
 *
 * Usage:
 *   test_libretro_wine.exe --core PATH --jar PATH [options]
 *
 * Options:
 *   --core PATH        libretro core DLL (required)
 *   --jar PATH         JAR passed to retro_load_game (required)
 *   --frames N         retro_run frames to run, >= 60 (default 120)
 *   --save-dir PATH    directory reported as save/system directory
 *                      (default "."; must already exist)
 *   --visual-required  fail unless at least one frame contains a non-black
 *                      pixel
 *   --audio-required   fail unless at least one nonzero audio sample was
 *                      produced
 *   --font MODE        auto (default), system, or builtin
 *   --expect-font MODE require loaded, builtin, or fallback font log
 *   --help, -h         this help
 *
 * Files named like tests/fixtures/loopdemo.jar get --visual-required and
 * files named like tests/fixtures/audiodemo.jar get --audio-required
 * automatically; the explicit flags override that. loopdemo also requires
 * an actual savestate roundtrip, corruption rejection and continued frames.
 *
 * Only kernel32 is (explicitly) linked: LoadLibraryA/GetProcAddress and the
 * console CRT are all the harness needs, which keeps the test honest about
 * the core's own dependencies.
 *
 * Exit codes: 0 pass, 1 check failure, 2 usage/setup error.
 */

#include <windows.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_OPTIONS 64
#define OPT_KEY_MAX 128
#define OPT_VALUE_MAX 256
#define LOG_LINE_LIMIT 500

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

struct config
{
   char core_path[PATH_MAX];
   char jar_path[PATH_MAX];
   char save_dir[PATH_MAX];
   unsigned frames;
   int visual_required;
   int audio_required;
   int visual_required_set;
   int audio_required_set;
   int state_required;
   const char *font_mode;
   const char *expect_font;
};

static struct config cfg = {
   "", "", ".", 120u, 0, 0, 0, 0, 0, "auto", ""
};

/* ------------------------------------------------------------------ */
/* Core options (minimal store so GET_VARIABLE can answer)             */
/* ------------------------------------------------------------------ */

struct option_def
{
   char key[OPT_KEY_MAX];
   char value[OPT_VALUE_MAX];
};

static struct option_def g_options[MAX_OPTIONS];
static int g_option_count;

static void store_option(const char *key, const char *default_value)
{
   struct option_def *opt;
   if (!key || !key[0] || g_option_count >= MAX_OPTIONS)
      return;
   opt = &g_options[g_option_count++];
   memset(opt, 0, sizeof(*opt));
   snprintf(opt->key, sizeof(opt->key), "%s", key);
   if (default_value)
      snprintf(opt->value, sizeof(opt->value), "%s", default_value);
}

static void register_options_v2(const struct retro_core_options_v2 *opts)
{
   const struct retro_core_option_v2_definition *def;
   if (!opts || !opts->definitions)
      return;
   for (def = opts->definitions; def->key; def++)
   {
      const char *defval = def->default_value;
      if ((!defval || !defval[0]) && def->values[0].value)
         defval = def->values[0].value;
      store_option(def->key, defval);
   }
}

static void register_options_v1(const struct retro_core_option_definition *defs)
{
   if (!defs)
      return;
   for (; defs->key; defs++)
   {
      const char *defval = defs->default_value;
      if ((!defval || !defval[0]) && defs->values[0].value)
         defval = defs->values[0].value;
      store_option(defs->key, defval);
   }
}

/* Legacy format: value is "Description; option1|option2|...". */
static void register_legacy_variables(const struct retro_variable *vars)
{
   if (!vars)
      return;
   for (; vars->key; vars++)
   {
      char buffer[OPT_VALUE_MAX];
      char *sep, *tok, *save = NULL;
      const char *p;

      if (!vars->value)
         continue;
      sep = strchr(vars->value, ';');
      if (!sep)
         continue;
      p = sep + 1;
      while (*p == ' ')
         p++;
      snprintf(buffer, sizeof(buffer), "%s", p);
      tok = strtok_s(buffer, "|", &save);
      store_option(vars->key, tok ? tok : p);
   }
}

static const char *find_option_value(const char *key)
{
   int i;
   for (i = 0; i < g_option_count; i++)
      if (strcmp(g_options[i].key, key) == 0)
         return g_options[i].value;
   return NULL;
}

/* ------------------------------------------------------------------ */
/* Session statistics / environment bookkeeping                        */
/* ------------------------------------------------------------------ */

struct env_stats
{
   unsigned rotation;
   unsigned rotation_calls;
   unsigned pixel_format;
   unsigned pixel_format_calls;
   unsigned av_info_calls;
   unsigned log_interface_calls;
   unsigned save_dir_queries;
   unsigned system_dir_queries;
   unsigned get_variable_queries;
   unsigned get_variable_misses;
   unsigned legacy_variables_calls;
   unsigned core_options_v2_calls;
   unsigned set_support_no_game;
   unsigned performance_level;
   unsigned language;
   unsigned can_dupe_queries;
   unsigned set_controller_info;
   unsigned unknown_commands;
};

struct session_stats
{
   unsigned run_calls;
   unsigned video_calls;
   unsigned video_nonnull;
   unsigned video_null;
   unsigned video_pitch_bad;
   unsigned long long max_nonblack;
   unsigned long long last_nonblack;
   uint32_t last_frame_hash;
   unsigned audio_sample_calls;
   unsigned audio_batch_calls;
   unsigned long long audio_frames;
   unsigned long long audio_nonzero;
   int peak;
   unsigned input_polls;
   unsigned log_lines;
   unsigned font_loaded;
   unsigned font_builtin;
   unsigned font_fallback;
   int shutdown_requested;
};

static struct env_stats g_env;
static struct session_stats g_ses;
static struct retro_system_av_info g_av_override;
static int g_have_av_override;
static const char *g_core_path;
static const char *g_jar_path;
static const char *g_save_dir;
static int g_failures;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void fail_check(int ok, const char *fmt, ...)
{
   va_list ap;
   printf("[check] %s: ", ok ? "PASS" : "FAIL");
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
   printf("\n");
   if (!ok)
      g_failures++;
}

static void note(const char *fmt, ...)
{
   va_list ap;
   printf("[harness] ");
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
   printf("\n");
}

static const char *base_name(const char *path)
{
   const char *slash = strrchr(path, '/');
   const char *bslash = strrchr(path, '\\');
   const char *name = path;
   if (slash && slash + 1 > name)
      name = slash + 1;
   if (bslash && bslash + 1 > name)
      name = bslash + 1;
   return name;
}

static int file_exists(const char *path)
{
   DWORD attrs = GetFileAttributesA(path);
   return attrs != INVALID_FILE_ATTRIBUTES &&
          !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static void print_win32_error(const char *what)
{
   DWORD err = GetLastError();
   char *msg = NULL;
   DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                 FORMAT_MESSAGE_IGNORE_INSERTS;
   if (FormatMessageA(flags, NULL, err, 0, (LPSTR)&msg, 0, NULL) && msg)
   {
      size_t len = strlen(msg);
      while (len > 0 && (msg[len - 1] == '\r' || msg[len - 1] == '\n'))
         msg[--len] = '\0';
      printf("[harness] %s failed: Win32 error %lu: %s\n", what,
             (unsigned long)err, msg);
      LocalFree(msg);
   }
   else
      printf("[harness] %s failed: Win32 error %lu\n", what,
             (unsigned long)err);
}

/* ------------------------------------------------------------------ */
/* Core log callback                                                  */
/* ------------------------------------------------------------------ */

static void RETRO_CALLCONV h_log(enum retro_log_level level,
                                 const char *fmt, ...)
{
   static const char *const level_names[] = {
      "debug", "info", "warn", "error"
   };
   const char *level_name = "log";
   char buffer[2048];
   va_list ap;

   if (!fmt)
      return;
   va_start(ap, fmt);
   vsnprintf(buffer, sizeof(buffer), fmt, ap);
   va_end(ap);

   g_ses.log_lines++;
   if (strstr(buffer, "font: loaded "))
      g_ses.font_loaded++;
   if (strstr(buffer, "font: using built-in OFL bitmap fonts"))
      g_ses.font_builtin++;
   if (strstr(buffer, "font: no supported external font; using built-in OFL bitmap fonts"))
      g_ses.font_fallback++;
   if (level >= 0 && level <= RETRO_LOG_ERROR)
      level_name = level_names[(int)level];

   if (g_ses.log_lines <= LOG_LINE_LIMIT)
   {
      printf("[core][%s] %s", level_name, buffer);
      if (!buffer[0] || buffer[strlen(buffer) - 1] != '\n')
         printf("\n");
   }
   else if (g_ses.log_lines == LOG_LINE_LIMIT + 1)
      printf("[core] (further core log lines suppressed)\n");
}

/* ------------------------------------------------------------------ */
/* Environment callback                                               */
/* ------------------------------------------------------------------ */

static bool RETRO_CALLCONV h_environment(unsigned cmd, void *data)
{
   switch (cmd & 0xffffu)
   {
      case (RETRO_ENVIRONMENT_SET_ROTATION & 0xffffu):
         if (!data)
            return false;
         g_env.rotation = *(const unsigned *)data;
         g_env.rotation_calls++;
         return true;

      case (RETRO_ENVIRONMENT_GET_OVERSCAN & 0xffffu):
         if (data)
            *(bool *)data = false;
         return true;

      case (RETRO_ENVIRONMENT_GET_CAN_DUPE & 0xffffu):
         g_env.can_dupe_queries++;
         if (data)
            *(bool *)data = true;
         return true;

      case (RETRO_ENVIRONMENT_SET_MESSAGE & 0xffffu):
      {
         const struct retro_message *msg = (const struct retro_message *)data;
         if (msg && msg->msg)
            note("SET_MESSAGE from core: %s", msg->msg);
         return true;
      }

      case (RETRO_ENVIRONMENT_SHUTDOWN & 0xffffu):
         g_ses.shutdown_requested = 1;
         note("core requested SHUTDOWN");
         return true;

      case (RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL & 0xffffu):
         if (!data)
            return false;
         g_env.performance_level = *(const unsigned *)data;
         return true;

      case (RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY & 0xffffu):
         if (!data)
            return false;
         g_env.system_dir_queries++;
         *(const char **)data = g_save_dir;
         return true;

      case (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY & 0xffffu):
         if (!data)
            return false;
         g_env.save_dir_queries++;
         *(const char **)data = g_save_dir;
         return true;

      case (RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY & 0xffffu):
         if (data)
            *(const char **)data = g_save_dir;
         return true;

      case (RETRO_ENVIRONMENT_SET_PIXEL_FORMAT & 0xffffu):
         if (!data)
            return false;
         g_env.pixel_format = *(const enum retro_pixel_format *)data;
         g_env.pixel_format_calls++;
         return true;

      case (RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS & 0xffffu):
         return data != NULL;

      case (RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK & 0xffffu):
      {
         const struct retro_keyboard_callback *kb =
            (const struct retro_keyboard_callback *)data;
         return kb != NULL && kb->callback != NULL;
      }

      case (RETRO_ENVIRONMENT_GET_VARIABLE & 0xffffu):
      {
         struct retro_variable *var = (struct retro_variable *)data;
         const char *value;
         if (!var || !var->key)
            return false;
         g_env.get_variable_queries++;
         value = strcmp(var->key, "nano_j2me_font") == 0 ?
             cfg.font_mode : find_option_value(var->key);
         if (!value)
            g_env.get_variable_misses++;
         var->value = value;
         /* A frontend supports the call even when the key is unknown. */
         return true;
      }

      case (RETRO_ENVIRONMENT_SET_VARIABLES & 0xffffu):
         g_env.legacy_variables_calls++;
         register_legacy_variables((const struct retro_variable *)data);
         return true;

      case (RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE & 0xffffu):
         if (data)
            *(bool *)data = false;
         return true;

      case (RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME & 0xffffu):
         if (!data)
            return false;
         g_env.set_support_no_game = *(const bool *)data ? 1u : 0u;
         return true;

      case (RETRO_ENVIRONMENT_GET_LIBRETRO_PATH & 0xffffu):
         if (!data)
            return false;
         *(const char **)data = g_core_path;
         return true;

      case (RETRO_ENVIRONMENT_GET_LOG_INTERFACE & 0xffffu):
      {
         struct retro_log_callback *cb = (struct retro_log_callback *)data;
         if (!cb)
            return false;
         g_env.log_interface_calls++;
         cb->log = h_log;
         return true;
      }

      case (RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO & 0xffffu):
         if (!data)
            return false;
         g_env.av_info_calls++;
         g_av_override = *(const struct retro_system_av_info *)data;
         g_have_av_override = 1;
         return true;

      case (RETRO_ENVIRONMENT_SET_GEOMETRY & 0xffffu):
         return data != NULL;

      case (RETRO_ENVIRONMENT_GET_LANGUAGE & 0xffffu):
         if (data)
            *(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
         g_env.language = RETRO_LANGUAGE_ENGLISH;
         return true;

      case (RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION & 0xffffu):
         if (data)
            *(unsigned *)data = 2;
         return true;

      case (RETRO_ENVIRONMENT_SET_CORE_OPTIONS & 0xffffu):
         register_options_v1(
            (const struct retro_core_option_definition *)data);
         return true;

      case (RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL & 0xffffu):
      {
         const struct retro_core_options_intl *intl =
            (const struct retro_core_options_intl *)data;
         register_options_v1(intl ? intl->us : NULL);
         return true;
      }

      case (RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2 & 0xffffu):
         g_env.core_options_v2_calls++;
         register_options_v2(
            (const struct retro_core_options_v2 *)data);
         return true;

      case (RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL & 0xffffu):
      {
         const struct retro_core_options_v2_intl *intl =
            (const struct retro_core_options_v2_intl *)data;
         g_env.core_options_v2_calls++;
         register_options_v2(intl ? intl->us : NULL);
         return true;
      }

      case (RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY & 0xffffu):
         return true;

      case (RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION & 0xffffu):
         if (data)
            *(unsigned *)data = 1;
         return true;

      case (RETRO_ENVIRONMENT_SET_MESSAGE_EXT & 0xffffu):
      {
         const struct retro_message_ext *msg =
            (const struct retro_message_ext *)data;
         if (msg && msg->msg)
            note("SET_MESSAGE_EXT from core: %s", msg->msg);
         return true;
      }

      case (RETRO_ENVIRONMENT_SET_CONTROLLER_INFO & 0xffffu):
         g_env.set_controller_info++;
         return true;

      /* Known optional interfaces this minimal frontend does not provide.
       * The core must tolerate a false return for these. */
      case (RETRO_ENVIRONMENT_SET_HW_RENDER & 0xffffu):
      case (RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK & 0xffffu):
      case (RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK & 0xffffu):
      case (RETRO_ENVIRONMENT_GET_PERF_INTERFACE & 0xffffu):
      case (RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO & 0xffffu):
      case (RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER & 0xffffu):
      case (RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION & 0xffffu):
      case (RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE & 0xffffu):
      case (RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE & 0xffffu):
      case (RETRO_ENVIRONMENT_GET_VFS_INTERFACE & 0xffffu):
         return false;

      case (RETRO_ENVIRONMENT_GET_FASTFORWARDING & 0xffffu):
         if (data)
            *(bool *)data = false;
         return true;

      case (RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE & 0xffffu):
         if (data)
            *(float *)data = 60.0f;
         return true;

      case (RETRO_ENVIRONMENT_GET_INPUT_BITMASKS & 0xffffu):
         return false;

      case (RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE & 0xffffu):
         if (data)
            *(unsigned *)data = 3u; /* video + audio enabled */
         return true;

      default:
         g_env.unknown_commands++;
         printf("[env] unsupported environment command %u (returning false)\n",
                cmd);
         return false;
   }
}

/* ------------------------------------------------------------------ */
/* Video / audio / input callbacks                                    */
/* ------------------------------------------------------------------ */

static void RETRO_CALLCONV h_video(const void *data, unsigned width,
                                   unsigned height, size_t pitch)
{
   unsigned bytes_per_pixel = g_env.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888
                                 ? 4u
                                 : 2u;
   g_ses.video_calls++;
   if (!data)
   {
      g_ses.video_null++;
      return;
   }
   if (width == 0 || height == 0 || width > 4096 || height > 4096)
   {
      g_ses.video_pitch_bad++;
      return;
   }
   g_ses.video_nonnull++;
   if (pitch < (size_t)width * bytes_per_pixel)
   {
      g_ses.video_pitch_bad++;
      return;
   }

   {
      const unsigned char *frame = (const unsigned char *)data;
      unsigned long long nonblack = 0;
      uint32_t hash = UINT32_C(2166136261);
      unsigned y, x;
      for (y = 0; y < height; y++)
      {
         const unsigned char *row = frame + (size_t)y * pitch;
         /* Ignore row padding; hash just the frontend's visible frame bytes. */
         for (x = 0; x < width * bytes_per_pixel; x++)
            hash = (hash ^ row[x]) * UINT32_C(16777619);
         if (bytes_per_pixel == 4)
         {
            for (x = 0; x + 3 < width * 4u; x += 4)
            {
               unsigned pixel = (unsigned)row[x] | ((unsigned)row[x + 1] << 8) |
                                ((unsigned)row[x + 2] << 16);
               if (pixel)
                  nonblack++;
            }
         }
         else
         {
            for (x = 0; x + 1 < width * 2u; x += 2)
            {
               unsigned pixel = (unsigned)row[x] | ((unsigned)row[x + 1] << 8);
               if (pixel)
                  nonblack++;
            }
         }
      }
      g_ses.last_nonblack = nonblack;
      g_ses.last_frame_hash = hash;
      if (nonblack > g_ses.max_nonblack)
         g_ses.max_nonblack = nonblack;
   }
}

static void RETRO_CALLCONV h_audio_sample(int16_t left, int16_t right)
{
   int peak;
   g_ses.audio_sample_calls++;
   g_ses.audio_frames++;
   if (left || right)
      g_ses.audio_nonzero++;
   peak = left < 0 ? -(int)left : (int)left;
   if (peak > g_ses.peak)
      g_ses.peak = peak;
   peak = right < 0 ? -(int)right : (int)right;
   if (peak > g_ses.peak)
      g_ses.peak = peak;
}

static size_t RETRO_CALLCONV h_audio_batch(const int16_t *data, size_t frames)
{
   size_t i;
   g_ses.audio_batch_calls++;
   if (!data)
      return frames;
   g_ses.audio_frames += frames;
   for (i = 0; i < frames * 2; i++)
   {
      int peak;
      if (data[i])
         g_ses.audio_nonzero++;
      peak = data[i] < 0 ? -(int)data[i] : (int)data[i];
      if (peak > g_ses.peak)
         g_ses.peak = peak;
   }
   return frames;
}

static void RETRO_CALLCONV h_input_poll(void)
{
   g_ses.input_polls++;
}

static int16_t RETRO_CALLCONV h_input_state(unsigned port, unsigned device,
                                            unsigned index, unsigned id)
{
   (void)port;
   (void)device;
   (void)index;
   (void)id;
   return 0;
}

/* ------------------------------------------------------------------ */
/* Core entry points                                                   */
/* ------------------------------------------------------------------ */

struct core_api
{
   void (RETRO_CALLCONV *retro_set_environment)(retro_environment_t);
   void (RETRO_CALLCONV *retro_set_video_refresh)(retro_video_refresh_t);
   void (RETRO_CALLCONV *retro_set_audio_sample)(retro_audio_sample_t);
   void (RETRO_CALLCONV *retro_set_audio_sample_batch)(
      retro_audio_sample_batch_t);
   void (RETRO_CALLCONV *retro_set_input_poll)(retro_input_poll_t);
   void (RETRO_CALLCONV *retro_set_input_state)(retro_input_state_t);
   void (RETRO_CALLCONV *retro_init)(void);
   void (RETRO_CALLCONV *retro_deinit)(void);
   unsigned (RETRO_CALLCONV *retro_api_version)(void);
   void (RETRO_CALLCONV *retro_get_system_info)(struct retro_system_info *);
   void (RETRO_CALLCONV *retro_get_system_av_info)(
      struct retro_system_av_info *);
   unsigned (RETRO_CALLCONV *retro_get_region)(void);
   bool (RETRO_CALLCONV *retro_load_game)(const struct retro_game_info *);
   void (RETRO_CALLCONV *retro_run)(void);
   void (RETRO_CALLCONV *retro_unload_game)(void);
   size_t (RETRO_CALLCONV *retro_serialize_size)(void);
   bool (RETRO_CALLCONV *retro_serialize)(void *, size_t);
   bool (RETRO_CALLCONV *retro_unserialize)(const void *, size_t);
   void (RETRO_CALLCONV *retro_reset)(void);
   void (RETRO_CALLCONV *retro_set_controller_port_device)(unsigned, unsigned);
   void (RETRO_CALLCONV *retro_cheat_reset)(void);
   void (RETRO_CALLCONV *retro_cheat_set)(unsigned, bool, const char *);
   bool (RETRO_CALLCONV *retro_load_game_special)(unsigned,
      const struct retro_game_info *, size_t);
   void *(RETRO_CALLCONV *retro_get_memory_data)(unsigned);
   size_t (RETRO_CALLCONV *retro_get_memory_size)(unsigned);
};

static struct core_api api;
static HMODULE g_core_module;

static int resolve_required(HMODULE mod)
{
   int missing = 0;
#define RESOLVE_REQ(field, symbol) \
   do { \
      FARPROC proc_ = GetProcAddress(mod, symbol); \
      memcpy(&api.field, &proc_, sizeof(proc_)); \
      if (!api.field) \
      { \
         printf("[harness] missing required export: %s\n", symbol); \
         missing++; \
      } \
   } while (0)

   RESOLVE_REQ(retro_set_environment, "retro_set_environment");
   RESOLVE_REQ(retro_set_video_refresh, "retro_set_video_refresh");
   RESOLVE_REQ(retro_set_audio_sample, "retro_set_audio_sample");
   RESOLVE_REQ(retro_set_audio_sample_batch, "retro_set_audio_sample_batch");
   RESOLVE_REQ(retro_set_input_poll, "retro_set_input_poll");
   RESOLVE_REQ(retro_set_input_state, "retro_set_input_state");
   RESOLVE_REQ(retro_init, "retro_init");
   RESOLVE_REQ(retro_deinit, "retro_deinit");
   RESOLVE_REQ(retro_api_version, "retro_api_version");
   RESOLVE_REQ(retro_get_system_info, "retro_get_system_info");
   RESOLVE_REQ(retro_get_system_av_info, "retro_get_system_av_info");
   RESOLVE_REQ(retro_get_region, "retro_get_region");
   RESOLVE_REQ(retro_load_game, "retro_load_game");
   RESOLVE_REQ(retro_run, "retro_run");
   RESOLVE_REQ(retro_unload_game, "retro_unload_game");
   RESOLVE_REQ(retro_serialize_size, "retro_serialize_size");
   RESOLVE_REQ(retro_serialize, "retro_serialize");
   RESOLVE_REQ(retro_unserialize, "retro_unserialize");
   RESOLVE_REQ(retro_reset, "retro_reset");
   RESOLVE_REQ(retro_set_controller_port_device, "retro_set_controller_port_device");
   RESOLVE_REQ(retro_cheat_reset, "retro_cheat_reset");
   RESOLVE_REQ(retro_cheat_set, "retro_cheat_set");
   RESOLVE_REQ(retro_load_game_special, "retro_load_game_special");
   RESOLVE_REQ(retro_get_memory_data, "retro_get_memory_data");
   RESOLVE_REQ(retro_get_memory_size, "retro_get_memory_size");
#undef RESOLVE_REQ
   return missing;
}

/* Exercise real DLL state handling, not only presence of the ABI stubs. */
static void run_frames(unsigned count)
{
   unsigned i;
   for (i = 0; i < count; i++)
   {
      api.retro_run();
      g_ses.run_calls++;
   }
}

static void check_state_roundtrip(void)
{
   size_t size = api.retro_serialize_size();
   unsigned char *buffer;
   unsigned video_before, input_before;
   int ok;

   fail_check(size >= 8, "savestate has a nonzero usable size (%zu bytes)", size);
   if (size < 8)
      return;
   buffer = (unsigned char *)malloc(size);
   fail_check(buffer != NULL, "allocated %zu-byte savestate buffer", size);
   if (!buffer)
      return;
   memset(buffer, 0xAA, size);
   ok = api.retro_serialize(buffer, size);
   fail_check(ok, "retro_serialize writes a real savestate (%zu bytes)", size);
   if (!ok)
   {
      free(buffer);
      return;
   }

   run_frames(4); /* Advance the MIDlet before rewinding it. */
   fail_check(api.retro_serialize_size() == size,
              "savestate size remains stable after additional frames");
   ok = api.retro_unserialize(buffer, size);
   fail_check(ok, "retro_unserialize restores the savestate");
   if (ok)
   {
      video_before = g_ses.video_nonnull;
      input_before = g_ses.input_polls;
      run_frames(8);
      fail_check(g_ses.video_nonnull > video_before &&
                 g_ses.input_polls > input_before,
                 "video and input callbacks continue after valid restore");

      /* The API size is an upper bound, so the tail may be unused padding.
       * Corrupt the actual version field rather than an ignored padding byte. */
      buffer[4] ^= 0xFF;
      ok = api.retro_unserialize(buffer, size);
      fail_check(!ok, "retro_unserialize rejects a corrupted state version");
      video_before = g_ses.video_nonnull;
      input_before = g_ses.input_polls;
      run_frames(8);
      fail_check(g_ses.video_nonnull > video_before &&
                 g_ses.input_polls > input_before,
                 "video and input callbacks continue after corrupt-state rejection");
   }
   free(buffer);
}

/* ------------------------------------------------------------------ */
/* Usage                                                              */
/* ------------------------------------------------------------------ */

static void usage(FILE *out, const char *argv0)
{
   fprintf(out,
      "Usage: %s --core CORE.dll --jar GAME.jar [options]\n"
      "\n"
      "Options:\n"
      "  --core PATH        libretro core DLL (required)\n"
      "  --jar PATH         JAR passed to retro_load_game (required)\n"
      "  --frames N         retro_run frames, >= 60 (default 120)\n"
      "  --save-dir PATH    save/system directory (default \".\")\n"
      "  --visual-required  fail unless a frame has a non-black pixel\n"
      "  --audio-required   fail unless a nonzero audio sample is produced\n"
      "  --state-required   require save/restore, reject corruption, continue frames\n"
      "  --font MODE        auto (default), system, or builtin\n"
      "  --expect-font MODE require loaded, builtin, or fallback font log\n"
      "  --help, -h         this help\n"
      "\n"
      "Exit codes: 0 pass, 1 check failure, 2 usage/setup error.\n",
      argv0);
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
   int i;
   int visual_required = 0;
   int audio_required = 0;
   int have_visual_flag = 0;
   int have_audio_flag = 0;
   const char *jar_name;
   unsigned frames;
   HMODULE mod;
   struct retro_system_info sysinfo;
   struct retro_system_av_info avinfo;
   struct retro_game_info game;
   unsigned api_version;
   unsigned region;
   int load_ok;


   for (i = 1; i < argc; i++)
   {
      if (strcmp(argv[i], "--core") == 0 && i + 1 < argc)
         snprintf(cfg.core_path, sizeof(cfg.core_path), "%s", argv[++i]);
      else if (strcmp(argv[i], "--jar") == 0 && i + 1 < argc)
         snprintf(cfg.jar_path, sizeof(cfg.jar_path), "%s", argv[++i]);
      else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
      {
         char *end = NULL;
         long value = strtol(argv[++i], &end, 10);
         if (!end || *end || value < 0 || value > 1000000)
         {
            fprintf(stderr, "error: invalid --frames value: %s\n", argv[i]);
            return 2;
         }
         cfg.frames = (unsigned)value;
      }
      else if (strcmp(argv[i], "--save-dir") == 0 && i + 1 < argc)
         snprintf(cfg.save_dir, sizeof(cfg.save_dir), "%s", argv[++i]);
      else if (strcmp(argv[i], "--visual-required") == 0)
      {
         visual_required = 1;
         have_visual_flag = 1;
      }
      else if (strcmp(argv[i], "--audio-required") == 0)
      {
         audio_required = 1;
         have_audio_flag = 1;
      }
      else if (strcmp(argv[i], "--font") == 0 && i + 1 < argc)
          cfg.font_mode = argv[++i];
       else if (strcmp(argv[i], "--expect-font") == 0 && i + 1 < argc)
          cfg.expect_font = argv[++i];
       else if (strcmp(argv[i], "--state-required") == 0)
         cfg.state_required = 1;
      else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
      {
         usage(stdout, argv[0]);
         return 0;
      }
      else
      {
         fprintf(stderr, "error: unknown argument: %s\n", argv[i]);
         usage(stderr, argv[0]);
         return 2;
      }
   }

   if (strcmp(cfg.font_mode, "auto") != 0 &&
       strcmp(cfg.font_mode, "system") != 0 &&
       strcmp(cfg.font_mode, "builtin") != 0)
   {
      fprintf(stderr, "error: --font must be auto, system, or builtin\n");
      return 2;
   }
   if (cfg.expect_font[0] && strcmp(cfg.expect_font, "loaded") != 0 &&
       strcmp(cfg.expect_font, "builtin") != 0 &&
       strcmp(cfg.expect_font, "fallback") != 0)
   {
      fprintf(stderr, "error: --expect-font must be loaded, builtin, or fallback\n");
      return 2;
   }

   if (!cfg.core_path[0])
   {
      fprintf(stderr, "error: --core is required\n");
      usage(stderr, argv[0]);
      return 2;
   }
   if (!cfg.jar_path[0])
   {
      fprintf(stderr, "error: --jar is required\n");
      usage(stderr, argv[0]);
      return 2;
   }
   if (cfg.frames < 60)
   {
      fprintf(stderr, "error: --frames must be at least 60\n");
      return 2;
   }
   if (!file_exists(cfg.core_path))
   {
      fprintf(stderr, "error: core not found: %s\n", cfg.core_path);
      return 2;
   }
   if (!file_exists(cfg.jar_path))
   {
      fprintf(stderr, "error: jar not found: %s\n", cfg.jar_path);
      return 2;
   }
   if (cfg.save_dir[0] && !file_exists(cfg.save_dir))
   {
      DWORD attrs = GetFileAttributesA(cfg.save_dir);
      if (attrs == INVALID_FILE_ATTRIBUTES ||
          !(attrs & FILE_ATTRIBUTE_DIRECTORY))
      {
         fprintf(stderr, "error: --save-dir is not a directory: %s\n",
                 cfg.save_dir);
         return 2;
      }
   }

   jar_name = base_name(cfg.jar_path);
   if (!have_visual_flag && !have_audio_flag)
   {
      if (strstr(jar_name, "loopdemo") != NULL)
         visual_required = 1;
      else if (strstr(jar_name, "audiodemo") != NULL)
         audio_required = 1;
   }

   if (strstr(jar_name, "loopdemo") != NULL)
      cfg.state_required = 1;

   frames = cfg.frames;
   g_core_path = cfg.core_path;
   g_jar_path = cfg.jar_path;
   g_save_dir = cfg.save_dir;
   memset(&api, 0, sizeof(api));
   memset(&g_env, 0, sizeof(g_env));
   memset(&g_ses, 0, sizeof(g_ses));

   printf("[harness] nano_j2me libretro core -- Win32 wine smoke test\n");
   printf("[harness] core     : %s\n", cfg.core_path);
   printf("[harness] jar      : %s\n", cfg.jar_path);
   printf("[harness] save dir : %s\n", cfg.save_dir);
   printf("[harness] font     : %s (expected log: %s)\n", cfg.font_mode,
          cfg.expect_font[0] ? cfg.expect_font : "loaded or builtin fallback");
   printf("[harness] frames   : %u (visual-required=%d audio-required=%d "
          "state-required=%d)\n", frames, visual_required, audio_required,
          cfg.state_required);

   mod = LoadLibraryA(cfg.core_path);
   if (!mod)
   {
      print_win32_error("LoadLibraryA(core)");
      printf("[result] FAIL\n");
      return 1;
   }
   g_core_module = mod;
   note("LoadLibraryA succeeded");

   if (resolve_required(mod) != 0)
   {
      printf("[result] FAIL\n");
      FreeLibrary(mod);
      return 1;
   }
   note("all required entry points resolved");

   /* Wire the frontend the way RetroArch does. */
   api.retro_set_environment(h_environment);
   api.retro_set_video_refresh(h_video);
   api.retro_set_audio_sample(h_audio_sample);
   api.retro_set_audio_sample_batch(h_audio_batch);
   api.retro_set_input_poll(h_input_poll);
   api.retro_set_input_state(h_input_state);

   api.retro_init();
   note("retro_init returned");

   api_version = api.retro_api_version();
   fail_check(api_version == RETRO_API_VERSION,
              "retro_api_version() = %u (expected %u)",
              api_version, RETRO_API_VERSION);

   memset(&sysinfo, 0, sizeof(sysinfo));
   api.retro_get_system_info(&sysinfo);
   printf("[harness] system info: library_name=%s library_version=%s\n",
          sysinfo.library_name ? sysinfo.library_name : "(null)",
          sysinfo.library_version ? sysinfo.library_version : "(null)");
   printf("[harness] valid_extensions=%s need_fullpath=%d block_extract=%d\n",
          sysinfo.valid_extensions ? sysinfo.valid_extensions : "(null)",
          sysinfo.need_fullpath ? 1 : 0, sysinfo.block_extract ? 1 : 0);
   fail_check(sysinfo.library_name != NULL && sysinfo.library_name[0] != '\0',
              "core reports a library name");
   fail_check(sysinfo.valid_extensions != NULL &&
              strstr(sysinfo.valid_extensions, "jar") != NULL,
              "valid_extensions includes jar");
   fail_check(sysinfo.need_fullpath != 0,
              "need_fullpath is true (core loads from a path)");

   region = api.retro_get_region();
   printf("[harness] retro_get_region() = %u (%s)\n", region,
          region == RETRO_REGION_NTSC ? "NTSC" : "PAL/other");

   memset(&avinfo, 0, sizeof(avinfo));
   api.retro_get_system_av_info(&avinfo);
   printf("[harness] av_info before load: %ux%u max %ux%u fps=%.3f "
          "sample_rate=%.1f\n",
          avinfo.geometry.base_width, avinfo.geometry.base_height,
          avinfo.geometry.max_width, avinfo.geometry.max_height,
          avinfo.timing.fps, avinfo.timing.sample_rate);

   memset(&game, 0, sizeof(game));
   game.path = cfg.jar_path;
   load_ok = api.retro_load_game(&game) ? 1 : 0;
   fail_check(load_ok != 0, "retro_load_game(%s)", jar_name);
   if (!load_ok)
   {
      api.retro_deinit();
      printf("[summary] load failed; no frames were run\n");
      printf("[result] FAIL\n");
      FreeLibrary(mod);
      return 1;
   }

   api.retro_get_system_av_info(&avinfo);
   printf("[harness] av_info after load: %ux%u max %ux%u fps=%.3f "
          "sample_rate=%.1f\n",
          avinfo.geometry.base_width, avinfo.geometry.base_height,
          avinfo.geometry.max_width, avinfo.geometry.max_height,
          avinfo.timing.fps, avinfo.timing.sample_rate);
   fail_check(avinfo.geometry.base_width > 0 &&
              avinfo.geometry.base_height > 0 &&
              avinfo.geometry.max_width >= avinfo.geometry.base_width &&
              avinfo.geometry.max_height >= avinfo.geometry.base_height,
              "av_info geometry is sane (%ux%u)",
              avinfo.geometry.base_width, avinfo.geometry.base_height);
   fail_check(avinfo.timing.fps > 0.0 && avinfo.timing.sample_rate > 0.0,
              "av_info timing is sane (fps=%.3f, sample_rate=%.1f)",
              avinfo.timing.fps, avinfo.timing.sample_rate);
   fail_check(g_env.pixel_format_calls > 0 &&
              (g_env.pixel_format == RETRO_PIXEL_FORMAT_RGB565 ||
               g_env.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888),
              "core selected a supported pixel format (%u)",
              g_env.pixel_format_calls > 0 ? g_env.pixel_format : 99u);
   fail_check(g_env.log_interface_calls > 0,
              "core requested the log interface");
   fail_check(g_env.save_dir_queries > 0 || g_env.system_dir_queries > 0,
              "core requested a save/system directory");
   fail_check(g_env.core_options_v2_calls > 0 ||
              g_env.legacy_variables_calls > 0,
              "core registered its options (%d found)",
              g_option_count);

   if (strcmp(cfg.expect_font, "loaded") == 0)
      fail_check(g_ses.font_loaded > 0 && !g_ses.font_builtin && !g_ses.font_fallback,
                 "external font loaded without bitmap fallback");
   else if (strcmp(cfg.expect_font, "builtin") == 0)
      fail_check(g_ses.font_builtin > 0 && !g_ses.font_loaded && !g_ses.font_fallback,
                 "explicit builtin font mode used bitmap fonts");
   else if (strcmp(cfg.expect_font, "fallback") == 0)
      fail_check(g_ses.font_fallback > 0 && !g_ses.font_loaded && !g_ses.font_builtin,
                 "unavailable external font fell back to bitmap fonts");
   else
      fail_check(g_ses.font_loaded + g_ses.font_builtin + g_ses.font_fallback > 0,
                 "font selection logged a load or bitmap fallback");

   api.retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
   run_frames(frames);
   if (cfg.state_required)
      check_state_roundtrip();

   api.retro_unload_game();
   note("retro_unload_game returned");
   api.retro_deinit();
   note("retro_deinit returned");
   FreeLibrary(mod);
   mod = NULL;
   note("FreeLibrary returned");

   printf("[summary] frames=%u run_calls=%u shutdown_requested=%d\n",
          frames, g_ses.run_calls, g_ses.shutdown_requested ? 1 : 0);
   printf("[summary] video: calls=%u nonnull=%u null=%u pitch_bad=%u "
          "max_nonblack=%llu last_nonblack=%llu\n",
          g_ses.video_calls, g_ses.video_nonnull, g_ses.video_null,
          g_ses.video_pitch_bad, g_ses.max_nonblack, g_ses.last_nonblack);
   printf("[summary] last visible frame FNV1a=%08lx\n",
          (unsigned long)g_ses.last_frame_hash);
   printf("[summary] audio: sample_calls=%u batch_calls=%u frames=%llu "
          "nonzero=%llu peak=%d\n",
          g_ses.audio_sample_calls, g_ses.audio_batch_calls,
          g_ses.audio_frames, g_ses.audio_nonzero, g_ses.peak);
   printf("[summary] input polls=%u, core log lines=%u, options=%d, "
          "unknown env commands=%u\n",
          g_ses.input_polls, g_ses.log_lines, g_option_count,
          g_env.unknown_commands);

   fail_check(g_ses.run_calls >= frames,
              "ran %u retro_run frames (expected %u)",
              g_ses.run_calls, frames);
   fail_check(g_ses.video_calls > 0, "video callback was called (%u times)",
              g_ses.video_calls);
   fail_check(g_ses.video_nonnull > 0,
              "video callback received non-NULL frame data (%u frames)",
              g_ses.video_nonnull);
   fail_check(g_ses.video_pitch_bad == 0,
              "video pitch was valid for every frame (%u bad)",
              g_ses.video_pitch_bad);
   if (visual_required)
      fail_check(g_ses.max_nonblack > 0,
                 "at least one frame is non-black (max_nonblack=%llu)",
                 g_ses.max_nonblack);
   fail_check(g_ses.input_polls > 0,
              "core polled input (%u polls)", g_ses.input_polls);
   if (audio_required)
      fail_check(g_ses.audio_nonzero > 0,
                 "at least one nonzero audio sample was produced "
                 "(nonzero=%llu peak=%d)",
                 g_ses.audio_nonzero, g_ses.peak);
   else
      printf("[harness] audio: %llu nonzero samples, peak=%d (not required)\n",
             g_ses.audio_nonzero, g_ses.peak);

   printf("[summary] %d check(s) failed\n", g_failures);
   printf("[result] %s\n", g_failures == 0 ? "PASS" : "FAIL");
   return g_failures == 0 ? 0 : 1;
}
