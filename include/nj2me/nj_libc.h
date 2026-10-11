/*
 * The C library surface the runtime actually uses.
 *
 * nano_j2me is written to run without a hosted C library, but that claim was
 * not true: with NJ_USE_LIBC=0 half the sources did not compile, because the
 * includes that provided memcpy and friends were behind the same switch.  A
 * freestanding port was left to guess which declarations to supply.
 *
 * There are exactly nine functions, and this is the whole list.  With
 * NJ_USE_LIBC=1 they come from the hosted headers.  With NJ_USE_LIBC=0 they are
 * declared here and the PORT must provide them - ESP-IDF and newlib already do,
 * and a bare-metal port supplies its own.  Everything else the runtime needs
 * (allocation, time, file access) goes through nj_port callbacks, never libc.
 *
 * If you are porting and get an undefined reference from this list, that is the
 * gap to fill; if you get one from anywhere else, it is a bug in the runtime.
 */
#ifndef NJ_LIBC_H
#define NJ_LIBC_H

#include "nj2me/nj_config.h"

#if NJ_USE_LIBC
#  include <string.h>
#  include <stdlib.h>
#  include <stdio.h>
#  include <math.h>
#else
#  include <stddef.h>   /* size_t, and nothing else */

# ifdef __cplusplus
extern "C" {
#  endif

/* <string.h> */
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strchr(const char *s, int c);
int    tolower(int c);
int    toupper(int c);

/*
 * <math.h>, for java.lang.Math.
 *
 * These are not optional if the MIDlet uses Math at all, but they are the only
 * part of the list a port can plausibly choose to leave out: NJ_ENABLE_FLOAT=0
 * compiles java.lang.Math down to the integer operations and drops every one of
 * them.  A port with an FPU but no libm (or one that wants to save the ~20 KB)
 * can therefore take that switch instead of providing them.
 *
 * newlib - and so ESP-IDF - already has all of these.
 */
double fmod(double x, double y);
double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double exp(double x);
double log(double x);
double log10(double x);
double pow(double x, double y);
double ceil(double x);
double floor(double x);
double cbrt(double x);

#  ifdef __cplusplus
}
#  endif
#endif /* NJ_USE_LIBC */

#endif /* NJ_LIBC_H */
