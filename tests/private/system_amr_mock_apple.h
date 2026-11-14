/* SPDX-License-Identifier: MIT */
#ifndef NJ_SYSTEM_AMR_MOCK_APPLE_H
#define NJ_SYSTEM_AMR_MOCK_APPLE_H
#include "../../src/port/common/nj_system_amr_apple_abi.h"
void *nj_test_apple_dlopen(const char *, int);
void *nj_test_apple_dlsym(void *, const char *);
int nj_test_apple_dlclose(void *);
#define dlopen nj_test_apple_dlopen
#define dlsym nj_test_apple_dlsym
#define dlclose nj_test_apple_dlclose
#endif
