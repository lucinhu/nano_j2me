/* SPDX-License-Identifier: MIT */
#include <jni.h>
#include <stdio.h>
int nj_amr_test_main(int, char **);
JNIEXPORT jint JNICALL Java_org_nanoj2me_amrprobe_Probe_run(JNIEnv *env, jclass type, jstring path)
{
    const char *name = (*env)->GetStringUTFChars(env, path, NULL);
    int opened;
    (void)type;
    if (!name) return 2;
    opened = freopen(name, "w", stdout) != NULL;
    if (opened) opened = freopen(name, "a", stderr) != NULL;
    (*env)->ReleaseStringUTFChars(env, path, name);
    if (!opened) return 3;
    return nj_amr_test_main(0, NULL);
}
