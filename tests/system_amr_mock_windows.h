/* SPDX-License-Identifier: MIT
 * Test interception only; uses real Windows SDK ABI, not a replacement SDK.
 */
#ifndef NJ_SYSTEM_AMR_MOCK_WINDOWS_H
#define NJ_SYSTEM_AMR_MOCK_WINDOWS_H
HMODULE WINAPI nj_test_win_load(LPCWSTR, HANDLE, DWORD);
FARPROC WINAPI nj_test_win_symbol(HMODULE, LPCSTR);
BOOL WINAPI nj_test_win_unload(HMODULE);
DWORD WINAPI nj_test_win_ticks(void);
#define LoadLibraryExW nj_test_win_load
#define GetProcAddress nj_test_win_symbol
#define FreeLibrary nj_test_win_unload
#define GetTickCount nj_test_win_ticks
#endif
