/*
 * tests/test_libretro_frontend.c -- strict mock libretro frontend for the
 * nano_j2me libretro core.
 *
 * This program dlopen()s the core and drives it the way RetroArch would:
 * set_environment, callbacks, init, load_game, run frames, serialize checks,
 * unload_game, deinit.  It asserts the core's ABI/behaviour contract so the
 * core can be validated on a machine without RetroArch installed.
 *
 * Build (from the repository root):
 *   cc -std=c99 -O2 -Wall -Wextra -I src/port/libretro \
 *      -o build/test_libretro_frontend tests/test_libretro_frontend.c -ldl
 *
 * Usage:
 *   build/test_libretro_frontend --core CORE.so --jar GAME.jar [options]
 *
 * Options:
 *   --core PATH        libretro core shared object (required)
 *   --jar PATH         game file passed to retro_load_game (required)
 *   --frames N         frames to run per cycle (default 180)
 *   --pace-us N        real-time pause before each retro_run in microseconds
 *                      (default 0 = maximally tight loop).  The nano_j2me core
 *                      advances its J2ME VM on a helper thread; --pace-us 16667
 *                      models a RetroArch 60 Hz loop, while the tight default
 *                      is the stricter, faster starvation check.
 *   --audio-required   fail unless the core emits at least one nonzero sample
 *   --input-check      drive joypad/keyboard/pointer and require the core to poll
 *   --expect-load-fail fail unless retro_load_game rejects the --jar file
 *   --cycle            load/run/unload twice in the same process
 *   --rotation N       expect SET_ROTATION for N degrees (0/90/180/270) or the
 *                      raw rotation code (0..3); N>3 must be a multiple of 90
 *   --set k=v          core option override, repeatable (frontend value)
 *   --save-dir PATH    directory reported as system/save directory
 *   --visual-required  enforce non-black + animated frame content
 *   --static-ok        with --visual-required, accept a static (non-animated) frame
 *   --min-nonblack N   non-black pixel threshold for --visual-required (default 1)
 *   --timeout N        watchdog seconds (default 30, 0 disables)
 *   -h, --help         this help
 *
 * Exit codes: 0 pass, 1 check failure, 2 usage/setup error, 3 core load error,
 *             4 watchdog timeout, 5 fatal signal from the core.
 *
 * Notes on deliberate leniency (echoed to the Lead):
 *   * Frame content is only *enforced* with --visual-required.  tests/fixtures/
 *     alphademo.jar has a single MIDlet that paints black and calls
 *     notifyDestroyed() during load, so a correct core presents black frames;
 *     tools/check_libretro_frontend.sh enforces content with loopdemo.jar and
 *     uses alphademo as a present/load smoke test.
 *   * Audio size is reported (expected ~735 stereo frames per 60 Hz run) but
 *     not asserted: the libretro API permits variable-size and silent batches.
 *     --audio-required only demands at least one nonzero sample.
 *   * Pointer input is exercised but never required; a core may map touch
 *     through joypad buttons instead.
 *   * retro_get_region and the cheat/memory/controller-port entry points are
 *     reported when absent, not failed: RetroArch may require some of them
 *     (retro_get_region in particular) even though the core ABI does not.
 *   * Frames are not paced by default (--pace-us 0): a tight retro_run loop is
 *     the stricter check for the core's helper-thread frame production.  Use
 *     --pace-us 16667 to model a real RetroArch 60 Hz display loop.
 */

#define _XOPEN_SOURCE 700

#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "libretro.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_OVERRIDES 64
#define MAX_OPTIONS 64
#define MAX_STORED_OPTION_VALUES 24
#define OPT_KEY_MAX 128
#define OPT_VALUE_MAX 256
#define MAX_MISSING_KEYS 8
#define LOG_LINE_LIMIT 2000

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

struct config
{
   char core_path[PATH_MAX];
   char jar_path[PATH_MAX];
   char save_dir[PATH_MAX];
   unsigned frames;
   unsigned pace_us;
   int audio_required;
   int input_check;
   int expect_load_fail;
   int cycle;
   int rotation_expect_set;
   unsigned rotation_expect_code;
   int visual_required;
   int static_ok;
   unsigned min_nonblack;
   int timeout_s;
};

static struct config cfg = {
   "", "", "", 180u, 0u, 0, 0, 0, 0, 0, 0u, 0, 0, 1u, 30
};

/* ------------------------------------------------------------------ */
/* Core option / override storage                                      */
/* ------------------------------------------------------------------ */

struct option_override
{
   char key[OPT_KEY_MAX];
   char value[OPT_VALUE_MAX];
};

static struct option_override g_overrides[MAX_OVERRIDES];
static int g_override_count;

struct option_def
{
   char key[OPT_KEY_MAX];
   char default_value[OPT_VALUE_MAX];
   char values[MAX_STORED_OPTION_VALUES][OPT_VALUE_MAX];
   int n_values_stored;
   int n_values_total;
};

static struct option_def g_options[MAX_OPTIONS];
static int g_option_count;

/* ------------------------------------------------------------------ */
/* Environment observations                                            */
/* ------------------------------------------------------------------ */

struct env_state
{
   int pixel_format_set;
   enum retro_pixel_format pixel_format;
   int pixel_format_calls;

   int rotation_set;
   unsigned rotation;
   int rotation_calls;

   int keyboard_cb_set;
   retro_keyboard_event_t keyboard_cb;

   int input_descriptor_count;

   int support_no_game_set;
   bool support_no_game;

   int performance_level_set;
   unsigned performance_level;

   int core_options_version_calls;
   int core_options_v2_calls;
   int core_options_v1_calls;
   int legacy_variables_calls;

   int set_geometry_calls;
   int set_av_info_calls;
   struct retro_system_av_info av_info_override;
   struct retro_game_geometry geometry_override;

   int shutdown_requested;
   int log_interface_calls;
   int log_lines;
   int message_count;

   unsigned long long get_variable_queries;
   unsigned long long get_variable_hits;
   unsigned long long get_variable_misses;
   char missing_keys[MAX_MISSING_KEYS][OPT_KEY_MAX];
   int missing_key_count;

   unsigned long long set_rotation_invalid;
};

/* ------------------------------------------------------------------ */
/* Per-game-session statistics                                         */
/* ------------------------------------------------------------------ */

struct session_stats
{
   unsigned long long runs;

   unsigned long long video_calls;
   unsigned long long video_null;
   unsigned long long video_pitch_bad;
   unsigned long long video_geometry_bad;

   uint64_t *frame_hashes;
   size_t frame_hash_count;
   size_t frame_hash_cap;

   unsigned long long nonblack_max;
   unsigned long long nonblack_min;
   int nonblack_seen;
   unsigned long long frames_with_content;

   unsigned long long audio_batch_calls;
   unsigned long long audio_sample_calls;
   unsigned long long audio_frames;
   unsigned long long audio_nonzero;
   unsigned long long audio_null_batches;
   unsigned long long audio_oversized_batches;
   unsigned long long runs_without_audio;
   int audio_peak;

   unsigned long long poll_calls;
   unsigned long long input_state_calls;
   unsigned long long input_joypad_queries;
   unsigned long long input_pressed_queries;
   unsigned long long input_pointer_queries;
   unsigned long long input_keyboard_queries;
   unsigned long long pressed_frames;
   unsigned long long keyboard_events;

   size_t serialize_size_at_load;
};

/* ------------------------------------------------------------------ */
/* Core API table                                                      */
/* ------------------------------------------------------------------ */

struct core_api
{
   void (*set_environment)(retro_environment_t cb);
   void (*set_video_refresh)(retro_video_refresh_t cb);
   void (*set_audio_sample)(retro_audio_sample_t cb);
   void (*set_audio_sample_batch)(retro_audio_sample_batch_t cb);
   void (*set_input_poll)(retro_input_poll_t cb);
   void (*set_input_state)(retro_input_state_t cb);

   void (*init)(void);
   void (*deinit)(void);
   unsigned (*api_version)(void);
   void (*get_system_info)(struct retro_system_info *info);
   void (*get_system_av_info)(struct retro_system_av_info *info);
   void (*set_controller_port_device)(unsigned port, unsigned device);
   void (*reset)(void);
   void (*run)(void);
   size_t (*serialize_size)(void);
   bool (*serialize)(void *data, size_t size);
   bool (*unserialize)(const void *data, size_t size);
   void (*cheat_reset)(void);
   void (*cheat_set)(unsigned index, bool enabled, const char *code);
   bool (*load_game)(const struct retro_game_info *game);
   bool (*load_game_special)(unsigned type,
                             const struct retro_game_info *info, size_t num_info);
   void (*unload_game)(void);
   unsigned (*get_region)(void);
   void *(*get_memory_data)(unsigned id);
   size_t (*get_memory_size)(unsigned id);
};

static struct core_api api;

static struct
{
   void *lib;
   char core_abs[PATH_MAX];
   char jar_abs[PATH_MAX];
   char *save_dir;
   int save_dir_owned;
   struct env_state env;
   struct session_stats ses;
   int have_geometry;
   unsigned expected_width;
   unsigned expected_height;
   unsigned long long total_runs;
   unsigned long long total_video_calls;
   unsigned long long total_audio_frames;
   unsigned long long total_audio_nonzero;
} g;

/* Input state driven by the harness. */
static unsigned g_pad_state;
static int g_pointer_pressed;
static unsigned char g_key_down[RETROK_LAST + 1];

/* ------------------------------------------------------------------ */
/* Output helpers                                                      */
/* ------------------------------------------------------------------ */

static int g_checks_run;
static int g_checks_failed;
static int g_checks_skipped;

static void note(const char *fmt, ...)
{
   va_list ap;
   printf("[info] ");
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
   printf("\n");
}

static void skip_check(const char *name, const char *fmt, ...)
{
   va_list ap;
   g_checks_skipped++;
   printf("SKIP %s: ", name);
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
   printf("\n");
}

static int check(const char *name, int ok, const char *fmt, ...)
{
   va_list ap;
   g_checks_run++;
   if (!ok)
      g_checks_failed++;
   printf("%s %s: ", ok ? "PASS" : "FAIL", name);
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
   printf("\n");
   return ok;
}

/* ------------------------------------------------------------------ */
/* Watchdog / fatal signal handlers                                    */
/* ------------------------------------------------------------------ */

static void on_alarm(int sig)
{
   static const char msg[] =
      "FAIL watchdog: core did not finish within the harness time limit\n";
   (void)sig;
   if (write(STDERR_FILENO, msg, sizeof(msg) - 1) < 0) {
      /* nothing sensible to do in a signal handler */
   }
   _exit(4);
}

static void on_fatal_signal(int sig)
{
   static const char msg[] = "FAIL core_crashed: core raised a fatal signal\n";
   (void)sig;
   if (write(STDERR_FILENO, msg, sizeof(msg) - 1) < 0) {
      /* nothing sensible to do in a signal handler */
   }
   _exit(5);
}

static void install_handlers(void)
{
   struct sigaction sa;

   memset(&sa, 0, sizeof(sa));
   sa.sa_handler = on_alarm;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = 0;
   sigaction(SIGALRM, &sa, NULL);

   sa.sa_handler = on_fatal_signal;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = SA_RESETHAND;
   sigaction(SIGSEGV, &sa, NULL);
   sigaction(SIGABRT, &sa, NULL);
   sigaction(SIGFPE, &sa, NULL);
   sigaction(SIGBUS, &sa, NULL);

   if (cfg.timeout_s > 0)
      alarm((unsigned)cfg.timeout_s);
}

/* ------------------------------------------------------------------ */
/* Option / override helpers                                           */
/* ------------------------------------------------------------------ */

static void trim_option_value(char *dst, size_t dst_size, const char *src)
{
   size_t i;
   if (!dst || dst_size == 0)
      return;
   dst[0] = '\0';
   if (!src)
      return;
   while (*src == ' ')
      src++;
   for (i = 0; i + 1 < dst_size && src[i]; i++)
      dst[i] = src[i];
   dst[i] = '\0';
   while (i > 0 && (dst[i - 1] == ' ' || dst[i - 1] == '\t'))
      dst[--i] = '\0';
}

static void print_option(const struct option_def *opt)
{
   int i;
   printf("[coreopt] key=%s default=%s values=", opt->key, opt->default_value);
   for (i = 0; i < opt->n_values_stored; i++)
      printf("%s%s", i ? "," : "", opt->values[i]);
   if (opt->n_values_total > opt->n_values_stored)
      printf(",...");
   printf("\n");
}

static void store_option(const char *key, const char *default_value,
                         const char *const *values, int n_values)
{
   struct option_def *opt;
   int i;

   if (!key || !key[0] || g_option_count >= MAX_OPTIONS)
      return;

   opt = &g_options[g_option_count++];
   memset(opt, 0, sizeof(*opt));
   snprintf(opt->key, sizeof(opt->key), "%s", key);
   trim_option_value(opt->default_value, sizeof(opt->default_value),
                     default_value);
   if (!opt->default_value[0] && n_values > 0 && values && values[0])
      trim_option_value(opt->default_value, sizeof(opt->default_value),
                        values[0]);
   for (i = 0; i < n_values && i < MAX_STORED_OPTION_VALUES; i++)
      trim_option_value(opt->values[i], sizeof(opt->values[i]), values[i]);
   opt->n_values_stored = n_values < MAX_STORED_OPTION_VALUES
                             ? n_values
                             : MAX_STORED_OPTION_VALUES;
   opt->n_values_total = n_values;
   print_option(opt);
}

static void register_options_v2(const struct retro_core_options_v2 *opts)
{
   const struct retro_core_option_v2_definition *def;

   g_option_count = 0;
   if (!opts || !opts->definitions)
   {
      note("SET_CORE_OPTIONS_V2: core cleared all options");
      return;
   }
   for (def = opts->definitions; def->key; def++)
   {
      const char *values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
      int n = 0;
      while (n < RETRO_NUM_CORE_OPTION_VALUES_MAX &&
             def->values[n].value)
         values[n] = def->values[n].value, n++;
      store_option(def->key, def->default_value, values, n);
   }
}

static void register_options_v1(const struct retro_core_option_definition *defs)
{
   g_option_count = 0;
   if (!defs)
   {
      note("SET_CORE_OPTIONS: core cleared all options");
      return;
   }
   for (; defs->key; defs++)
   {
      const char *values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
      int n = 0;
      while (n < RETRO_NUM_CORE_OPTION_VALUES_MAX &&
             defs->values[n].value)
         values[n] = defs->values[n].value, n++;
      store_option(defs->key, defs->default_value, values, n);
   }
}

/* Legacy SET_VARIABLES: value is "Description; value1|value2|...". */
static void register_legacy_variables(const struct retro_variable *vars)
{
   g_option_count = 0;
   if (!vars)
      return;
   for (; vars->key; vars++)
   {
      const char *values[RETRO_NUM_CORE_OPTION_VALUES_MAX];
      int n = 0;
      const char *p = vars->value ? strchr(vars->value, ';') : NULL;
      char joined[OPT_VALUE_MAX];
      char *tok;
      char *save = NULL;

      if (!p)
         continue;
      p++;
      while (*p == ' ')
         p++;
      snprintf(joined, sizeof(joined), "%s", p);
      for (tok = strtok_r(joined, "|", &save);
           tok && n < RETRO_NUM_CORE_OPTION_VALUES_MAX;
           tok = strtok_r(NULL, "|", &save))
         values[n++] = tok;
      store_option(vars->key, n > 0 ? values[0] : NULL, values, n);
   }
}

static const char *find_override(const char *key)
{
   int i;
   for (i = 0; i < g_override_count; i++)
      if (strcmp(g_overrides[i].key, key) == 0)
         return g_overrides[i].value;
   return NULL;
}

static const char *find_option_default(const char *key)
{
   int i;
   for (i = 0; i < g_option_count; i++)
      if (strcmp(g_options[i].key, key) == 0)
         return g_options[i].default_value;
   return NULL;
}

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* ------------------------------------------------------------------ */

static void RETRO_CALLCONV h_log(enum retro_log_level level,
                                 const char *fmt, ...);

/* ------------------------------------------------------------------ */
/* Environment callback                                                */
/* ------------------------------------------------------------------ */

#define MAX_UNKNOWN_COMMANDS 32
static unsigned g_unknown_commands[MAX_UNKNOWN_COMMANDS];
static int g_unknown_command_count;

static void note_unknown_command(unsigned cmd)
{
   int i;
   for (i = 0; i < g_unknown_command_count; i++)
      if (g_unknown_commands[i] == cmd)
         return;
   if (g_unknown_command_count < MAX_UNKNOWN_COMMANDS)
      g_unknown_commands[g_unknown_command_count++] = cmd;
   printf("[env] unsupported environment command %u (returning false)\n",
          cmd);
}

static bool RETRO_CALLCONV h_environment(unsigned cmd, void *data)
{
   switch (cmd & 0xffffu)
   {
      case (RETRO_ENVIRONMENT_SET_ROTATION & 0xffffu):
         if (!data)
            return false;
         g.env.rotation_set = 1;
         g.env.rotation_calls++;
         g.env.rotation = *(const unsigned *)data;
         if (g.env.rotation > 3)
            g.env.set_rotation_invalid++;
         return true;

      case (RETRO_ENVIRONMENT_GET_OVERSCAN & 0xffffu):
         if (data)
            *(bool *)data = false;
         return true;

      case (RETRO_ENVIRONMENT_GET_CAN_DUPE & 0xffffu):
         if (data)
            *(bool *)data = true;
         return true;

      case (RETRO_ENVIRONMENT_SET_MESSAGE & 0xffffu):
      {
         const struct retro_message *msg = (const struct retro_message *)data;
         g.env.message_count++;
         if (msg && msg->msg)
            printf("[env] SET_MESSAGE (%u frames): %s\n", msg->frames,
                   msg->msg);
         return true;
      }

      case (RETRO_ENVIRONMENT_SHUTDOWN & 0xffffu):
         g.env.shutdown_requested = 1;
         note("core requested SHUTDOWN");
         return true;

      case (RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL & 0xffffu):
         if (!data)
            return false;
         g.env.performance_level_set = 1;
         g.env.performance_level = *(const unsigned *)data;
         return true;

      case (RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY & 0xffffu):
         if (!data)
            return false;
         *(const char **)data = g.save_dir;
         return true;

      case (RETRO_ENVIRONMENT_SET_PIXEL_FORMAT & 0xffffu):
         if (!data)
            return false;
         g.env.pixel_format_set = 1;
         g.env.pixel_format_calls++;
         g.env.pixel_format = *(const enum retro_pixel_format *)data;
         return true;

      case (RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS & 0xffffu):
      {
         const struct retro_input_descriptor *desc =
            (const struct retro_input_descriptor *)data;
         int n = 0;
         if (!desc)
            return false;
         for (; desc->description; desc++)
         {
            if (n < 24)
               printf("[env] input descriptor: port=%u device=%u index=%u "
                      "id=%u desc=%s\n",
                      desc->port, desc->device, desc->index, desc->id,
                      desc->description);
            n++;
         }
         g.env.input_descriptor_count = n;
         return true;
      }

      case (RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK & 0xffffu):
      {
         const struct retro_keyboard_callback *kb =
            (const struct retro_keyboard_callback *)data;
         if (!kb || !kb->callback)
            return false;
         g.env.keyboard_cb = kb->callback;
         g.env.keyboard_cb_set = 1;
         return true;
      }

      case (RETRO_ENVIRONMENT_GET_VARIABLE & 0xffffu):
      {
         struct retro_variable *var = (struct retro_variable *)data;
         const char *value;
         if (!var || !var->key)
            return false;
         g.env.get_variable_queries++;
         value = find_override(var->key);
         if (!value)
         {
            value = find_option_default(var->key);
            if (!value)
            {
               g.env.get_variable_misses++;
               if (g.env.missing_key_count < MAX_MISSING_KEYS)
               {
                  snprintf(g.env.missing_keys[g.env.missing_key_count],
                           OPT_KEY_MAX, "%s", var->key);
                  g.env.missing_key_count++;
               }
            }
         }
         if (value)
            g.env.get_variable_hits++;
         var->value = value;
         /* A frontend supports the call even when the key is unknown. */
         return true;
      }

      case (RETRO_ENVIRONMENT_SET_VARIABLES & 0xffffu):
         g.env.legacy_variables_calls++;
         register_legacy_variables(
            (const struct retro_variable *)data);
         return true;

      case (RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE & 0xffffu):
         if (data)
            *(bool *)data = false;
         return true;

      case (RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME & 0xffffu):
         if (!data)
            return false;
         g.env.support_no_game_set = 1;
         g.env.support_no_game = *(const bool *)data;
         return true;

      case (RETRO_ENVIRONMENT_GET_LIBRETRO_PATH & 0xffffu):
         if (!data)
            return false;
         *(const char **)data = g.core_abs;
         return true;

      case (RETRO_ENVIRONMENT_GET_LOG_INTERFACE & 0xffffu):
      {
         struct retro_log_callback *cb = (struct retro_log_callback *)data;
         if (!cb)
            return false;
         g.env.log_interface_calls++;
         cb->log = h_log;
         return true;
      }

      case (RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY & 0xffffu):
         if (data)
            *(const char **)data = g.save_dir;
         return true;

      case (RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY & 0xffffu):
         if (!data)
            return false;
         *(const char **)data = g.save_dir;
         return true;

      case (RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO & 0xffffu):
         if (!data)
            return false;
         g.env.set_av_info_calls++;
         g.env.av_info_override =
            *(const struct retro_system_av_info *)data;
         return true;

      case (RETRO_ENVIRONMENT_SET_GEOMETRY & 0xffffu):
         if (!data)
            return false;
         g.env.set_geometry_calls++;
         g.env.geometry_override = *(const struct retro_game_geometry *)data;
         return true;

      case (RETRO_ENVIRONMENT_GET_LANGUAGE & 0xffffu):
         if (data)
            *(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
         return true;

      case (RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION & 0xffffu):
         if (data)
            *(unsigned *)data = 2;
         g.env.core_options_version_calls++;
         return true;

      case (RETRO_ENVIRONMENT_SET_CORE_OPTIONS & 0xffffu):
         g.env.core_options_v1_calls++;
         register_options_v1(
            (const struct retro_core_option_definition *)data);
         return true;

      case (RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL & 0xffffu):
      {
         const struct retro_core_options_intl *intl =
            (const struct retro_core_options_intl *)data;
         g.env.core_options_v1_calls++;
         register_options_v1(intl ? intl->us : NULL);
         return true;
      }

      case (RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2 & 0xffffu):
         g.env.core_options_v2_calls++;
         register_options_v2(
            (const struct retro_core_options_v2 *)data);
         return true;

      case (RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL & 0xffffu):
      {
         const struct retro_core_options_v2_intl *intl =
            (const struct retro_core_options_v2_intl *)data;
         g.env.core_options_v2_calls++;
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
         g.env.message_count++;
         if (msg && msg->msg)
            printf("[env] SET_MESSAGE_EXT: %s\n", msg->msg);
         return true;
      }

      case (RETRO_ENVIRONMENT_SET_CONTROLLER_INFO & 0xffffu):
         return true;

      /* Known commands this simple frontend does not provide.  Cores must
       * tolerate a false return value for these optional interfaces. */
      case (RETRO_ENVIRONMENT_SET_HW_RENDER & 0xffffu):
      case 4:  /* reserved */
      case 5:  /* reserved */
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
         /* false => per-button values in retro_input_state_t (we do that). */
         return false;

      case (RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE & 0xffffu):
         if (data)
            *(unsigned *)data = 3u; /* video + audio enabled */
         return true;

      default:
         note_unknown_command(cmd);
         return false;
   }
}

/* ------------------------------------------------------------------ */
/* Core log callback                                                   */
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

   g.env.log_lines++;
   if (level >= 0 && level <= RETRO_LOG_ERROR)
      level_name = level_names[(int)level];

   if (g.env.log_lines <= LOG_LINE_LIMIT)
   {
      printf("[core][%s] %s", level_name, buffer);
      if (!buffer[0] || buffer[strlen(buffer) - 1] != '\n')
         printf("\n");
   }
   else if (g.env.log_lines == LOG_LINE_LIMIT + 1)
      printf("[core] (further core log lines suppressed)\n");
}

/* ------------------------------------------------------------------ */
/* Video / audio / input callbacks                                     */
/* ------------------------------------------------------------------ */

static uint64_t fnv1a(uint64_t hash, unsigned char byte)
{
   hash ^= byte;
   hash *= 1099511628211ULL;
   return hash;
}

static void RETRO_CALLCONV h_video(const void *data, unsigned width,
                                   unsigned height, size_t pitch)
{
   struct session_stats *s = &g.ses;

   s->video_calls++;

   if (!data)
   {
      s->video_null++;
      if (s->video_null == 1)
         printf("[video] retro_run presented a NULL frame (frame duping is "
                "allowed by libretro but the core contract for this test is "
                "non-NULL)\n");
      return;
   }
   if (width == 0 || height == 0 || width > 4096 || height > 4096)
   {
      s->video_geometry_bad++;
      return;
   }
   if (pitch < (size_t)width * 2)
   {
      s->video_pitch_bad++;
      if (s->video_pitch_bad == 1)
         printf("[video] pitch %zu is smaller than width*2=%u\n", pitch,
                width * 2);
      return;
   }
   if (pitch != (size_t)width * 2)
   {
      s->video_pitch_bad++;
      if (s->video_pitch_bad == 1)
         printf("[video] pitch %zu != width*2=%u (RGB565 frames must be "
                "tightly packed per row)\n", pitch, width * 2);
   }
   if (g.have_geometry &&
       (width != g.expected_width || height != g.expected_height))
   {
      s->video_geometry_bad++;
      if (s->video_geometry_bad == 1)
         printf("[video] frame %ux%u does not match av_info %ux%u\n", width,
                height, g.expected_width, g.expected_height);
   }

   {
      const unsigned char *frame = (const unsigned char *)data;
      uint64_t hash = 1469598103934665603ULL;
      unsigned long long nonblack = 0;
      unsigned y, x, row_bytes = width * 2;

      hash = fnv1a(hash, (unsigned char)(width & 0xff));
      hash = fnv1a(hash, (unsigned char)(height & 0xff));

      for (y = 0; y < height; y++)
      {
         const unsigned char *row = frame + (size_t)y * pitch;
         for (x = 0; x < row_bytes; x += 2)
         {
            unsigned char lo = row[x];
            unsigned char hi = row[x + 1];
            if (lo | hi)
               nonblack++;
            hash = fnv1a(hash, lo);
            hash = fnv1a(hash, hi);
         }
      }

      if (!s->nonblack_seen || nonblack < s->nonblack_min)
         s->nonblack_min = nonblack;
      if (!s->nonblack_seen || nonblack > s->nonblack_max)
         s->nonblack_max = nonblack;
      s->nonblack_seen = 1;
      if (nonblack >= cfg.min_nonblack)
         s->frames_with_content++;

      if (s->frame_hash_count < s->frame_hash_cap)
         s->frame_hashes[s->frame_hash_count++] = hash;
   }
}

static void RETRO_CALLCONV h_audio_sample(int16_t left, int16_t right)
{
   struct session_stats *s = &g.ses;
   int peak;

   s->audio_sample_calls++;
   s->audio_frames++;
   if (left || right)
      s->audio_nonzero++;
   peak = left < 0 ? -(int)left : (int)left;
   if (peak > s->audio_peak)
      s->audio_peak = peak;
   peak = right < 0 ? -(int)right : (int)right;
   if (peak > s->audio_peak)
      s->audio_peak = peak;
}

static size_t RETRO_CALLCONV h_audio_batch(const int16_t *data,
                                           size_t frames)
{
   struct session_stats *s = &g.ses;
   size_t i;

   s->audio_batch_calls++;
   if (!data)
   {
      if (frames)
         s->audio_null_batches++;
      return frames;
   }
   if (frames > 44100)
      s->audio_oversized_batches++;
   s->audio_frames += frames;
   for (i = 0; i < frames * 2; i++)
   {
      int16_t sample = data[i];
      int peak;
      if (sample)
         s->audio_nonzero++;
      peak = sample < 0 ? -(int)sample : (int)sample;
      if (peak > s->audio_peak)
         s->audio_peak = peak;
   }
   return frames;
}

static void RETRO_CALLCONV h_input_poll(void)
{
   g.ses.poll_calls++;
}

static int16_t RETRO_CALLCONV h_input_state(unsigned port, unsigned device,
                                            unsigned index, unsigned id)
{
   struct session_stats *s = &g.ses;

   (void)index;
   s->input_state_calls++;
   if (port != 0)
      return 0;

   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
         s->input_joypad_queries++;
         if (g_pad_state)
            s->input_pressed_queries++;
         if (id > 15)
            return 0;
         return (g_pad_state & (1u << id)) ? 1 : 0;

      case RETRO_DEVICE_POINTER:
         s->input_pointer_queries++;
         switch (id)
         {
            case RETRO_DEVICE_ID_POINTER_X:
               /* Centered, "absolute" coordinates in the -32768..32767 range
                * that the legacy pointer API uses. */
               return 0;
            case RETRO_DEVICE_ID_POINTER_Y:
               return 0;
            case RETRO_DEVICE_ID_POINTER_PRESSED:
               return g_pointer_pressed ? 1 : 0;
            case RETRO_DEVICE_ID_POINTER_COUNT:
               return g_pointer_pressed ? 1 : 0;
            case RETRO_DEVICE_ID_POINTER_IS_OFFSCREEN:
               return 0;
            default:
               return 0;
         }

      case RETRO_DEVICE_KEYBOARD:
         s->input_keyboard_queries++;
         if (id <= RETROK_LAST)
            return g_key_down[id] ? 1 : 0;
         return 0;

      default:
         return 0;
   }
}

/* ------------------------------------------------------------------ */
/* dlsym helpers                                                       */
/* ------------------------------------------------------------------ */

static int resolve_required(const char *name, void *target)
{
   void *sym = dlsym(g.lib, name);
   if (!sym)
   {
      check("core_symbol", 0, "missing required symbol %s (%s)", name,
            dlerror());
      return 0;
   }
   memcpy(target, &sym, sizeof(sym));
   return 1;
}

static void resolve_optional(const char *name, void *target)
{
   void *sym = dlsym(g.lib, name);
   if (sym)
      memcpy(target, &sym, sizeof(sym));
}

static int load_core_symbols(void)
{
   int missing = 0;

   missing += !resolve_required("retro_set_environment",
                                &api.set_environment);
   missing += !resolve_required("retro_set_video_refresh",
                                &api.set_video_refresh);
   missing += !resolve_required("retro_set_audio_sample",
                                &api.set_audio_sample);
   missing += !resolve_required("retro_set_audio_sample_batch",
                                &api.set_audio_sample_batch);
   missing += !resolve_required("retro_set_input_poll",
                                &api.set_input_poll);
   missing += !resolve_required("retro_set_input_state",
                                &api.set_input_state);
   missing += !resolve_required("retro_init", &api.init);
   missing += !resolve_required("retro_deinit", &api.deinit);
   missing += !resolve_required("retro_api_version", &api.api_version);
   missing += !resolve_required("retro_get_system_info",
                                &api.get_system_info);
   missing += !resolve_required("retro_get_system_av_info",
                                &api.get_system_av_info);
   missing += !resolve_required("retro_reset", &api.reset);
   missing += !resolve_required("retro_run", &api.run);
   missing += !resolve_required("retro_serialize_size",
                                &api.serialize_size);
   missing += !resolve_required("retro_serialize", &api.serialize);
   missing += !resolve_required("retro_unserialize", &api.unserialize);
   missing += !resolve_required("retro_load_game", &api.load_game);
   missing += !resolve_required("retro_unload_game", &api.unload_game);

   resolve_optional("retro_set_controller_port_device",
                    &api.set_controller_port_device);
   resolve_optional("retro_cheat_reset", &api.cheat_reset);
   resolve_optional("retro_cheat_set", &api.cheat_set);
   resolve_optional("retro_load_game_special", &api.load_game_special);
   resolve_optional("retro_get_region", &api.get_region);
   resolve_optional("retro_get_memory_data", &api.get_memory_data);
   resolve_optional("retro_get_memory_size", &api.get_memory_size);

   {
      static const char *optional_names[] = {
         "retro_set_controller_port_device", "retro_cheat_reset",
         "retro_cheat_set", "retro_load_game_special", "retro_get_region",
         "retro_get_memory_data", "retro_get_memory_size", NULL
      };
      const char *absent[8];
      int absent_count = 0, i;
      for (i = 0; optional_names[i]; i++)
         if (!dlsym(g.lib, optional_names[i]))
            absent[absent_count++] = optional_names[i];
      if (absent_count > 0)
      {
         note("optional libretro symbols absent (%d); RetroArch may still "
              "require some (e.g. retro_get_region):", absent_count);
         for (i = 0; i < absent_count; i++)
            note("  missing optional symbol %s", absent[i]);
      }
   }

   return check("required_symbols", missing == 0,
                "%d required libretro symbol(s) missing", missing);
}

/* ------------------------------------------------------------------ */
/* Session helpers                                                     */
/* ------------------------------------------------------------------ */

static void session_reset(void)
{
   struct session_stats *s = &g.ses;
   size_t cap = (size_t)cfg.frames + 64u;

   free(s->frame_hashes);
   memset(s, 0, sizeof(*s));
   s->nonblack_min = ~0ULL;
   s->frame_hashes = (uint64_t *)calloc(cap, sizeof(uint64_t));
   if (s->frame_hashes)
      s->frame_hash_cap = cap;
   else
      note("out of memory allocating frame hash buffer");

   g_pad_state = 0;
   g_pointer_pressed = 0;
   memset(g_key_down, 0, sizeof(g_key_down));
}

static void session_teardown(void)
{
   free(g.ses.frame_hashes);
   g.ses.frame_hashes = NULL;
   g.ses.frame_hash_cap = 0;
   g.ses.frame_hash_count = 0;
}

static int cmp_u64(const void *a, const void *b)
{
   uint64_t x = *(const uint64_t *)a;
   uint64_t y = *(const uint64_t *)b;
   return (x < y) ? -1 : (x > y);
}

static int count_distinct(uint64_t *values, size_t count)
{
   size_t i;
   int distinct = 1;

   if (count == 0)
      return 0;
   if (count == 1)
      return 1;
   qsort(values, count, sizeof(*values), cmp_u64);
   for (i = 1; i < count; i++)
      if (values[i] != values[i - 1])
         distinct++;
   return distinct;
}

static void press_key(unsigned code, bool down)
{
   if (code > RETROK_LAST)
      return;
   if (!g.env.keyboard_cb)
      return;
   g.env.keyboard_cb(down, code, 0, 0);
   g_key_down[code] = down ? 1 : 0;
   g.ses.keyboard_events++;
}

static void release_all_keys(void)
{
   unsigned code;
   for (code = 0; code <= RETROK_LAST; code++)
      if (g_key_down[code])
         press_key(code, false);
}

/* Input schedule used by --input-check.  The state is applied before each
 * retro_run() so the core polls it from inside the run like RetroArch does. */
static void input_apply(unsigned frame, unsigned total)
{
   static const unsigned joy_ids[] = {
      RETRO_DEVICE_ID_JOYPAD_B,     RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_DOWN,  RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT
   };
   static const unsigned key_codes[] = {
      RETROK_RETURN, RETROK_ESCAPE, RETROK_LEFT, RETROK_RIGHT,
      RETROK_UP,     RETROK_DOWN,   RETROK_0,    RETROK_1,
      RETROK_2,      RETROK_9
   };
   unsigned start, k, slot;

   if (!cfg.input_check)
      return;
   start = total > 8 ? total / 4 : 0;
   if (frame < start)
      return;
   k = frame - start;
   if (k >= 60)
   {
      g_pad_state = 0;
      g_pointer_pressed = 0;
      return;
   }

   slot = k / 3;
   if (slot < 8)
   {
      g_pad_state = 1u << joy_ids[slot];
      g.ses.pressed_frames++;
   }
   else
      g_pad_state = 0;

   g_pointer_pressed = (k >= 24 && k < 30) ? 1 : 0;

   if (k < 20)
   {
      unsigned index = k / 2;
      press_key(key_codes[index], (k % 2) == 0);
   }
}

/* Pause between retro_run() calls.  A real libretro frontend calls retro_run
 * once per display frame; nano_j2me's J2ME VM runs on a helper thread, so an
 * unpaced tight loop mostly measures how fast the main thread can spin.
 * --pace-us 0 keeps the old maximally tight behaviour. */
static void sleep_us(unsigned usec)
{
   struct timespec req, rem;

   if (!usec)
      return;
   req.tv_sec = (time_t)(usec / 1000000u);
   req.tv_nsec = (long)(usec % 1000000u) * 1000L;
   while (nanosleep(&req, &rem) == -1 && errno == EINTR)
      req = rem;
}

static void run_frames(unsigned count)
{
   unsigned i;

   for (i = 0; i < count; i++)
   {
      unsigned long long audio_mark;
      input_apply(i, count);
      if (cfg.pace_us)
         sleep_us(cfg.pace_us);
      audio_mark = g.ses.audio_batch_calls + g.ses.audio_sample_calls;
      api.run();
      g.ses.runs++;
      if (g.ses.audio_batch_calls + g.ses.audio_sample_calls == audio_mark)
         g.ses.runs_without_audio++;
   }

   g_pad_state = 0;
   g_pointer_pressed = 0;
   release_all_keys();
}

/* ------------------------------------------------------------------ */
/* Checks                                                              */
/* ------------------------------------------------------------------ */

static void print_env_summary(void)
{
   note("env summary: pixel_format_set=%d(%d) rotation=%d(%u) keyboard_cb=%d "
        "input_descriptors=%d support_no_game=%d perf_level=%d "
        "core_options_v2=%d variable_queries=%llu hits=%llu misses=%llu "
        "set_geometry=%d set_av_info=%d shutdown=%d log_lines=%d",
        g.env.pixel_format_set, (int)g.env.pixel_format,
        g.env.rotation_set, g.env.rotation, g.env.keyboard_cb_set,
        g.env.input_descriptor_count, g.env.support_no_game_set,
        g.env.performance_level_set, g.env.core_options_v2_calls,
        g.env.get_variable_queries, g.env.get_variable_hits,
        g.env.get_variable_misses, g.env.set_geometry_calls,
        g.env.set_av_info_calls, g.env.shutdown_requested, g.env.log_lines);

   if (g.env.missing_key_count > 0)
   {
      int i;
      printf("[info] core queried undefined options:");
      for (i = 0; i < g.env.missing_key_count; i++)
         printf(" %s", g.env.missing_keys[i]);
      printf("%s\n", g.env.missing_key_count == MAX_MISSING_KEYS ? " ..." : "");
   }
}

static void check_after_load(int cycle)
{
   struct retro_system_av_info av;
   const struct retro_game_geometry *geom;
   int portrait, landscape_ok;

   memset(&av, 0, sizeof(av));
   api.get_system_av_info(&av);

   g.expected_width = av.geometry.base_width;
   g.expected_height = av.geometry.base_height;
   g.have_geometry = (g.expected_width > 0 && g.expected_height > 0);

   printf("[info] av_info: base=%ux%u max=%ux%u fps=%.6f sample_rate=%.3f "
          "aspect_ratio=%.6f\n",
          av.geometry.base_width, av.geometry.base_height,
          av.geometry.max_width, av.geometry.max_height,
          av.timing.fps, av.timing.sample_rate, av.geometry.aspect_ratio);

   portrait = (av.geometry.base_width == 240 &&
               av.geometry.base_height == 320);
   landscape_ok = cfg.rotation_expect_set &&
                  (cfg.rotation_expect_code == 1 ||
                   cfg.rotation_expect_code == 3) &&
                  av.geometry.base_width == 320 &&
                  av.geometry.base_height == 240;

   check("av_base_geometry_240x320", portrait || landscape_ok,
         "base=%ux%u expected 240x320 (320x240 accepted when the core "
         "internally rotates for --rotation 90/270)",
         av.geometry.base_width, av.geometry.base_height);

   check("av_max_geometry_at_least_base",
         av.geometry.max_width >= av.geometry.base_width &&
            av.geometry.max_height >= av.geometry.base_height,
         "max=%ux%u base=%ux%u", av.geometry.max_width,
         av.geometry.max_height, av.geometry.base_width,
         av.geometry.base_height);

   check("av_fps_positive", av.timing.fps > 0.0, "fps=%.6f",
         av.timing.fps);
   if (av.timing.fps > 0.0 && (av.timing.fps < 59.0 || av.timing.fps > 61.0))
      note("av_info fps=%.3f is not ~60.0, which the nano_j2me core is "
           "designed to report", av.timing.fps);

   check("av_sample_rate_44100", av.timing.sample_rate == 44100.0,
         "sample_rate=%.3f expected 44100", av.timing.sample_rate);

   check("env_pixel_format_rgb565",
         g.env.pixel_format_set &&
            g.env.pixel_format == RETRO_PIXEL_FORMAT_RGB565,
         "SET_PIXEL_FORMAT called=%d value=%d (RGB565=%d)",
         g.env.pixel_format_set, (int)g.env.pixel_format,
         (int)RETRO_PIXEL_FORMAT_RGB565);

   if (g.env.set_geometry_calls || g.env.set_av_info_calls)
   {
      geom = &g.env.geometry_override;
      note("core adjusted geometry at runtime: set_geometry=%d set_av_info=%d "
           "last=%ux%u",
           g.env.set_geometry_calls, g.env.set_av_info_calls,
           geom->base_width, geom->base_height);
      g.expected_width = av.geometry.base_width;
      g.expected_height = av.geometry.base_height;
      g.have_geometry = (g.expected_width > 0 && g.expected_height > 0);
   }

   if (g.env.rotation_set)
      check("env_rotation_valid", g.env.rotation <= 3,
            "SET_ROTATION value=%u (must be 0..3)", g.env.rotation);

   if (cfg.rotation_expect_set)
   {
      if (cfg.rotation_expect_code == 0)
      {
         if (g.env.rotation_set)
            check("env_set_rotation_value", g.env.rotation == 0,
                  "SET_ROTATION value=%u expected 0", g.env.rotation);
         else
            note("--rotation 0: core did not call SET_ROTATION (acceptable; "
                 "no rotation requested)");
      }
      else if (!g.env.rotation_set &&
               av.geometry.base_width == 320 &&
               av.geometry.base_height == 240)
      {
         /* Some cores implement --rotation by swapping the reported output
          * geometry themselves instead of asking the frontend to rotate.  That
          * is a legitimate alternative to SET_ROTATION, so accept it. */
         note("--rotation %u: core did not call SET_ROTATION but reports "
              "rotated 320x240 geometry (internal rotation accepted)",
              cfg.rotation_expect_code * 90u);
      }
      else
      {
         check("env_set_rotation_called", g.env.rotation_set,
               "expected SET_ROTATION(%u) for %u degrees",
               cfg.rotation_expect_code, cfg.rotation_expect_code * 90u);
         if (g.env.rotation_set)
            check("env_set_rotation_value",
                  g.env.rotation == cfg.rotation_expect_code,
                  "SET_ROTATION value=%u expected %u (%u degrees)",
                  g.env.rotation, cfg.rotation_expect_code,
                  cfg.rotation_expect_code * 90u);
      }
   }

   if (cycle == 1)
      print_env_summary();
}

static void check_session(int cycle)
{
   struct session_stats *s = &g.ses;
   unsigned long long total_pixels =
      (unsigned long long)g.expected_width * g.expected_height;

   printf("[info] session %d: runs=%llu video_calls=%llu\n", cycle, s->runs,
          s->video_calls);

   check("video_one_frame_per_run", s->runs == s->video_calls,
         "runs=%llu video_calls=%llu (exactly one non-NULL presentation per "
         "retro_run expected)",
         s->runs, s->video_calls);

   check("video_frame_nonnull", s->video_null == 0,
         "%llu NULL frame callback(s)", s->video_null);

   check("video_pitch_width_times_2", s->video_pitch_bad == 0,
         "%llu frame(s) with pitch != width*2 for RGB565", s->video_pitch_bad);

   if (s->video_calls > 0 && g.have_geometry)
      check("video_frame_geometry", s->video_geometry_bad == 0,
            "%llu frame(s) did not match av_info %ux%u",
            s->video_geometry_bad, g.expected_width, g.expected_height);

   if (s->runs > 0 && s->video_calls > 0)
   {
      int distinct = count_distinct(s->frame_hashes, s->frame_hash_count);

      note("content: non-black pixels per frame in [%llu, %llu] of %llu; "
           "frames_with_content=%llu/%llu; distinct_frame_hashes=%d/%zu",
           s->nonblack_min == ~0ULL ? 0ULL : s->nonblack_min,
           s->nonblack_max, total_pixels, s->frames_with_content, s->runs,
           distinct, s->frame_hash_count);

      if (cfg.visual_required)
      {
         check("visual_nonblack", s->nonblack_max >= cfg.min_nonblack,
               "max non-black pixels=%llu (threshold %u, frame is %llu px)",
               s->nonblack_max, cfg.min_nonblack, total_pixels);
         if (cfg.static_ok)
            skip_check("visual_animation", "static frames accepted "
                                           "(--static-ok)");
         else
            check("visual_animation", distinct >= 2,
                  "distinct sampled frame hashes=%d (need >=2 for animated "
                  "content)",
                  distinct);
      }
      else
         note("visual content checks not enforced; pass --visual-required "
              "(and --static-ok to allow static frames)");
   }
   else if (cfg.visual_required)
      skip_check("visual_checks", "no frames were run (frames=%u, runs=%llu)",
                 cfg.frames, s->runs);

   printf("[info] audio: batch_calls=%llu sample_calls=%llu frames=%llu "
          "nonzero_samples=%llu peak=%d\n",
          s->audio_batch_calls, s->audio_sample_calls, s->audio_frames,
          s->audio_nonzero, s->audio_peak);
   {
      unsigned long long expected_frames = s->runs * 735ULL;
      if (s->runs > 0)
         note("audio frame budget: %llu emitted for %llu runs (expected ~%llu "
              "at 44100 Hz / 60 fps; not asserted because the API allows "
              "variable batch sizes)",
              s->audio_frames, s->runs, expected_frames);
      if (s->runs_without_audio > 0)
         note("%llu retro_run() call(s) emitted no audio (allowed: audio may "
              "be silence)",
              s->runs_without_audio);
   }

   if (cfg.audio_required)
   {
      check("audio_callback_called",
            s->audio_batch_calls + s->audio_sample_calls > 0,
            "batch_calls=%llu sample_calls=%llu", s->audio_batch_calls,
            s->audio_sample_calls);
      check("audio_has_nonzero_samples", s->audio_nonzero > 0,
            "nonzero_samples=%llu peak=%d (audiodemo plays a 200000-byte WAV "
            "during MIDlet start)",
            s->audio_nonzero, s->audio_peak);
   }

   if (cfg.input_check)
   {
      printf("[info] input: input_state_calls=%llu joypad_queries=%llu "
             "pressed_queries=%llu pointer_queries=%llu keyboard_queries=%llu "
             "keyboard_events=%llu pressed_frames=%llu poll_calls=%llu\n",
             s->input_state_calls, s->input_joypad_queries,
             s->input_pressed_queries, s->input_pointer_queries,
             s->input_keyboard_queries, s->keyboard_events,
             s->pressed_frames, s->poll_calls);
      check("input_pressed_state_polled", s->input_pressed_queries > 0,
            "the core must poll retro_input_state while a button is held "
            "(pressed_queries=%llu)",
            s->input_pressed_queries);
      check("input_keyboard_callback_registered",
            g.env.keyboard_cb_set && s->keyboard_events > 0,
            "SET_KEYBOARD_CALLBACK registered=%d keyboard_events=%llu",
            g.env.keyboard_cb_set, s->keyboard_events);
      if (g.env.input_descriptor_count == 0)
         note("core did not register input descriptors (cosmetic only)");
   }
}

static void check_serialize_roundtrip(size_t size_at_load)
{
   size_t size;
   unsigned char *buffer;
   int ok;

   size = api.serialize_size();
   if (size == 0)
   {
      check("serialize_size_stable", 0,
            "serialize_size() dropped from %zu to 0", size_at_load);
      return;
   }
   printf("[info] serialize roundtrip with %zu bytes\n", size);
   buffer = (unsigned char *)malloc(size);
   if (!buffer)
   {
      check("serialize_roundtrip", 0, "out of memory (%zu bytes)", size);
      return;
   }
   ok = api.serialize(buffer, size);
   check("serialize_writes_state", ok, "retro_serialize(%zu) returned %d",
         size, ok);
   if (ok)
   {
      /* Change emulated state before restoring the snapshot. */
      run_frames(2);
      ok = api.unserialize(buffer, size);
      check("unserialize_restores_state", ok,
            "retro_unserialize(%zu) returned %d", size, ok);
      if (ok)
         run_frames(1);
   }
   free(buffer);
}

/* Runs load -> frames -> (serialize roundtrip) -> unload -> checks.
 * Returns 1 on success, 0 when load_game() failed (game was not loaded). */
static int run_game_cycle(int cycle)
{
   struct retro_game_info game;
   struct session_stats *s = &g.ses;
   size_t serialize_size;
   int loaded;

   session_reset();

   memset(&game, 0, sizeof(game));
   game.path = g.jar_abs;

   printf("\n[info] === cycle %d: retro_load_game(%s) ===\n", cycle,
          g.jar_abs);
   loaded = api.load_game(&game);
   check("load_game", loaded,
         "cycle=%d path=%s (need_fullpath; core reads the file itself)",
         cycle, g.jar_abs);
   if (!loaded)
      return 0;

   if (api.set_controller_port_device)
      api.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

   check_after_load(cycle);

   serialize_size = api.serialize_size();
   s->serialize_size_at_load = serialize_size;
   note("serialize_size=%zu (%s)", serialize_size,
        serialize_size ? "save states supported"
                       : "save states unsupported");

   run_frames(cfg.frames);

   if (serialize_size == 0)
   {
      unsigned char buffer[64];
      int ok;
      memset(buffer, 0xAA, sizeof(buffer));
      ok = api.serialize(buffer, sizeof(buffer));
      check("serialize_unsupported_returns_false", !ok,
            "serialize_size()==0 but retro_serialize() returned %d", ok);
   }
   else if (cfg.cycle)
      check_serialize_roundtrip(serialize_size);
   else
      note("serialize roundtrip only exercised in --cycle mode");

   api.unload_game();
   check_session(cycle);

   g.total_runs += s->runs;
   g.total_video_calls += s->video_calls;
   g.total_audio_frames += s->audio_frames;
   g.total_audio_nonzero += s->audio_nonzero;
   return 1;
}

static void run_load_fail_check(void)
{
   struct retro_game_info game;
   int loaded;

   memset(&game, 0, sizeof(game));
   game.path = g.jar_abs;

   printf("\n[info] === expect-load-fail: retro_load_game(%s) ===\n",
          g.jar_abs);
   loaded = api.load_game(&game);
   check("load_game_rejects_invalid_input", !loaded,
         "retro_load_game() returned %d for a non-JAR file (expected false)",
         loaded);
   if (loaded)
      api.unload_game();
}

/* ------------------------------------------------------------------ */
/* Command line                                                        */
/* ------------------------------------------------------------------ */

static void usage(FILE *out, const char *argv0)
{
   fprintf(out,
           "usage: %s --core PATH --jar PATH [options]\n"
           "\n"
           "mock libretro frontend conformance harness for nano_j2me.\n"
           "\n"
           "  --core PATH        libretro core shared object (required)\n"
           "  --jar PATH         game file passed to retro_load_game "
           "(required)\n"
           "  --frames N         frames to run per cycle (default 180)\n"
           "  --pace-us N        real-time pause before each retro_run in us\n"
           "                     (default 0 = tight loop; 16667 = 60 Hz)\n"
           "  --audio-required   fail unless at least one nonzero audio "
           "sample is emitted\n"
           "  --input-check      drive joypad/keyboard/pointer and require the "
           "core to poll\n"
           "  --expect-load-fail fail unless retro_load_game rejects --jar\n"
           "  --cycle            load/run/unload twice in the same process\n"
           "  --rotation N       expect SET_ROTATION for N degrees "
           "(0/90/180/270) or raw code 0..3\n"
           "  --set k=v          core option override, repeatable\n"
           "  --save-dir PATH    directory reported as system/save directory\n"
           "  --visual-required  enforce non-black and animated frame content\n"
           "  --static-ok        with --visual-required, accept a static frame\n"
           "  --min-nonblack N   non-black pixel threshold (default 1)\n"
           "  --timeout N        watchdog seconds (default 30, 0 disables)\n"
           "  -h, --help         this help\n",
           argv0);
}

static int parse_uint(const char *text, unsigned *out)
{
   char *end = NULL;
   unsigned long value;

   errno = 0;
   value = strtoul(text, &end, 0);
   if (errno || !end || *end || end == text || value > 0xffffffffUL)
      return 0;
   *out = (unsigned)value;
   return 1;
}

static int add_override(const char *assignment)
{
   const char *eq;
   size_t key_len;

   eq = strchr(assignment, '=');
   if (!eq || eq == assignment)
      return 0;
   key_len = (size_t)(eq - assignment);
   if (key_len >= sizeof(g_overrides[0].key))
      return 0;
   if (g_override_count >= MAX_OVERRIDES)
      return 0;
   memset(&g_overrides[g_override_count], 0,
          sizeof(g_overrides[g_override_count]));
   memcpy(g_overrides[g_override_count].key, assignment, key_len);
   trim_option_value(g_overrides[g_override_count].value,
                     sizeof(g_overrides[g_override_count].value), eq + 1);
   g_override_count++;
   return 1;
}

static int parse_args(int argc, char **argv)
{
   int i;

   for (i = 1; i < argc; i++)
   {
      const char *arg = argv[i];
      if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0)
      {
         usage(stdout, argv[0]);
         exit(0);
      }
      else if (strcmp(arg, "--core") == 0 && i + 1 < argc)
         snprintf(cfg.core_path, sizeof(cfg.core_path), "%s", argv[++i]);
      else if (strcmp(arg, "--jar") == 0 && i + 1 < argc)
         snprintf(cfg.jar_path, sizeof(cfg.jar_path), "%s", argv[++i]);
      else if (strcmp(arg, "--frames") == 0 && i + 1 < argc)
      {
         if (!parse_uint(argv[++i], &cfg.frames) || cfg.frames > 100000)
            goto bad_value;
      }
      else if (strcmp(arg, "--pace-us") == 0 && i + 1 < argc)
      {
         if (!parse_uint(argv[++i], &cfg.pace_us) || cfg.pace_us > 1000000u)
            goto bad_value;
      }
      else if (strcmp(arg, "--no-pace") == 0)
         cfg.pace_us = 0;
      else if (strcmp(arg, "--audio-required") == 0)
         cfg.audio_required = 1;
      else if (strcmp(arg, "--input-check") == 0)
         cfg.input_check = 1;
      else if (strcmp(arg, "--expect-load-fail") == 0)
         cfg.expect_load_fail = 1;
      else if (strcmp(arg, "--cycle") == 0)
         cfg.cycle = 1;
      else if (strcmp(arg, "--rotation") == 0 && i + 1 < argc)
      {
         unsigned value;
         if (!parse_uint(argv[++i], &value))
            goto bad_value;
         if (value <= 3)
            cfg.rotation_expect_code = value;
         else if (value % 90 == 0 && value / 90 <= 3)
            cfg.rotation_expect_code = value / 90;
         else
            goto bad_value;
         cfg.rotation_expect_set = 1;
      }
      else if (strcmp(arg, "--set") == 0 && i + 1 < argc)
      {
         if (!add_override(argv[++i]))
            goto bad_value;
      }
      else if (strcmp(arg, "--save-dir") == 0 && i + 1 < argc)
         snprintf(cfg.save_dir, sizeof(cfg.save_dir), "%s", argv[++i]);
      else if (strcmp(arg, "--visual-required") == 0)
         cfg.visual_required = 1;
      else if (strcmp(arg, "--static-ok") == 0)
         cfg.static_ok = 1;
      else if (strcmp(arg, "--min-nonblack") == 0 && i + 1 < argc)
      {
         if (!parse_uint(argv[++i], &cfg.min_nonblack))
            goto bad_value;
         if (cfg.min_nonblack == 0)
            cfg.min_nonblack = 1;
      }
      else if (strcmp(arg, "--timeout") == 0 && i + 1 < argc)
      {
         unsigned value;
         if (!parse_uint(argv[++i], &value))
            goto bad_value;
         cfg.timeout_s = (int)value;
      }
      else
      {
         fprintf(stderr, "%s: unknown or incomplete option '%s'\n", argv[0],
                 arg);
         usage(stderr, argv[0]);
         return 0;
      }
      continue;

   bad_value:
      fprintf(stderr, "%s: invalid value for '%s'\n", argv[0], arg);
      usage(stderr, argv[0]);
      return 0;
   }

   if (!cfg.core_path[0] || !cfg.jar_path[0])
   {
      fprintf(stderr, "%s: --core and --jar are required\n", argv[0]);
      usage(stderr, argv[0]);
      return 0;
   }
   return 1;
}

/* ------------------------------------------------------------------ */
/* Setup / teardown                                                    */
/* ------------------------------------------------------------------ */

static int path_is_file(const char *path)
{
   return access(path, R_OK) == 0;
}

static void resolve_path(const char *input, char *output, size_t output_size)
{
   char resolved[PATH_MAX];
   if (realpath(input, resolved))
      snprintf(output, output_size, "%s", resolved);
   else
      snprintf(output, output_size, "%s", input);
}

static int setup_save_dir(void)
{
   struct stat st;

   if (cfg.save_dir[0])
   {
      if (stat(cfg.save_dir, &st) == 0)
      {
         if (!S_ISDIR(st.st_mode))
         {
            fprintf(stderr, "error: --save-dir '%s' is not a directory\n",
                    cfg.save_dir);
            return 0;
         }
      }
      else
      {
         if (mkdir(cfg.save_dir, 0700) != 0)
         {
            fprintf(stderr, "error: cannot create --save-dir '%s': %s\n",
                    cfg.save_dir, strerror(errno));
            return 0;
         }
      }
      g.save_dir = strdup(cfg.save_dir);
      g.save_dir_owned = 0;
   }
   else
   {
      char template_path[] = "/tmp/nano_j2me_frontend_XXXXXX";
      char *created = mkdtemp(template_path);
      if (!created)
      {
         fprintf(stderr, "error: cannot create temp directory: %s\n",
                 strerror(errno));
         return 0;
      }
      g.save_dir = strdup(created);
      g.save_dir_owned = 1;
   }
   if (!g.save_dir)
   {
      fprintf(stderr, "error: out of memory\n");
      return 0;
   }
   return 1;
}

static void cleanup_save_dir(void)
{
   if (g.save_dir_owned && g.save_dir)
   {
      char command[PATH_MAX + 32];
      if (snprintf(command, sizeof(command), "rm -rf -- '%s'", g.save_dir) <
          (int)sizeof(command))
      {
         if (system(command) != 0)
            fprintf(stderr, "warning: could not remove %s\n", g.save_dir);
      }
   }
   free(g.save_dir);
   g.save_dir = NULL;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
   int cycles;
   int cycle;
   int completed = 0;

   setvbuf(stdout, NULL, _IOLBF, 0);
   memset(&g, 0, sizeof(g));
   g.env.pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;

   if (!parse_args(argc, argv))
      return 2;

   printf("[nano_j2me libretro mock frontend harness]\n");
   resolve_path(cfg.core_path, g.core_abs, sizeof(g.core_abs));
   resolve_path(cfg.jar_path, g.jar_abs, sizeof(g.jar_abs));

   if (!path_is_file(g.jar_abs))
   {
      fprintf(stderr, "error: --jar '%s' is not a readable file\n",
              g.jar_abs);
      return 2;
   }
   if (!path_is_file(g.core_abs))
   {
      fprintf(stderr, "FAIL core_dlopen: core '%s' is not a readable file\n",
              g.core_abs);
      return 3;
   }
   if (!setup_save_dir())
      return 2;

   printf("[info] core=%s\n", g.core_abs);
   printf("[info] jar=%s\n", g.jar_abs);
   printf("[info] save_dir=%s\n", g.save_dir);
   printf("[info] frames_per_cycle=%u pace_us=%u cycles=%s input_check=%s "
          "audio_required=%s visual_required=%s expect_load_fail=%s\n",
          cfg.frames, cfg.pace_us, cfg.cycle ? "yes" : "no",
          cfg.input_check ? "yes" : "no", cfg.audio_required ? "yes" : "no",
          cfg.visual_required ? "yes" : "no",
          cfg.expect_load_fail ? "yes" : "no");
   if (g_override_count > 0)
   {
      int i;
      printf("[info] core option overrides:");
      for (i = 0; i < g_override_count; i++)
         printf(" %s=%s", g_overrides[i].key, g_overrides[i].value);
      printf("\n");
   }

   install_handlers();
   if (cfg.timeout_s > 0)
      note("watchdog armed for %d seconds", cfg.timeout_s);

   g.lib = dlopen(g.core_abs, RTLD_NOW | RTLD_LOCAL);
   if (!g.lib)
   {
      printf("FAIL core_dlopen: %s\n", dlerror());
      cleanup_save_dir();
      return 3;
   }
   if (!load_core_symbols())
   {
      dlclose(g.lib);
      printf("\n[result] FAIL (core ABI symbols missing)\n");
      cleanup_save_dir();
      return 3;
   }

   api.set_environment(h_environment);
   api.set_video_refresh(h_video);
   api.set_audio_sample(h_audio_sample);
   api.set_audio_sample_batch(h_audio_batch);
   api.set_input_poll(h_input_poll);
   api.set_input_state(h_input_state);

   api.init();

   check("api_version", api.api_version() == RETRO_API_VERSION,
         "retro_api_version()=%u expected %d", api.api_version(),
         RETRO_API_VERSION);

   {
      struct retro_system_info info;
      memset(&info, 0, sizeof(info));
      api.get_system_info(&info);
      printf("[info] system_info: name='%s' version='%s' extensions='%s' "
             "need_fullpath=%d block_extract=%d\n",
             info.library_name ? info.library_name : "(null)",
             info.library_version ? info.library_version : "(null)",
             info.valid_extensions ? info.valid_extensions : "(null)",
             info.need_fullpath, info.block_extract);

      check("sysinfo_library_name",
            info.library_name && info.library_name[0],
            "library_name is %s", info.library_name ? "set" : "missing");
      check("sysinfo_need_fullpath", info.need_fullpath,
            "need_fullpath=%d expected true", info.need_fullpath);
      check("sysinfo_valid_extensions_has_jar",
            info.valid_extensions &&
               (strstr(info.valid_extensions, "jar") != NULL),
            "valid_extensions='%s' expected to contain 'jar'",
            info.valid_extensions ? info.valid_extensions : "(null)");
      check("sysinfo_block_extract", info.block_extract,
            "block_extract=%d expected true", info.block_extract);
   }

   if (cfg.expect_load_fail)
      run_load_fail_check();
   else
   {
      cycles = cfg.cycle ? 2 : 1;
      for (cycle = 1; cycle <= cycles; cycle++)
      {
         if (!run_game_cycle(cycle))
         {
            note("cycle %d failed to load; skipping remaining cycles", cycle);
            break;
         }
         completed++;
      }
      if (completed < cycles)
         note("only %d of %d load cycle(s) completed", completed, cycles);
   }

   api.deinit();
   dlclose(g.lib);
   g.lib = NULL;
   session_teardown();

   printf("\n[info] totals: frames=%llu video_calls=%llu audio_frames=%llu "
          "audio_nonzero=%llu core_log_lines=%d\n",
          g.total_runs, g.total_video_calls, g.total_audio_frames,
          g.total_audio_nonzero, g.env.log_lines);
   printf("[summary] checks=%d failed=%d skipped=%d\n", g_checks_run,
          g_checks_failed, g_checks_skipped);

   if (g_checks_failed > 0)
   {
      printf("[result] FAIL (%d check(s) failed)\n", g_checks_failed);
      cleanup_save_dir();
      return 1;
   }
   printf("[result] PASS\n");
   cleanup_save_dir();
   return 0;
}
