#ifndef NJ_TEST_ANDROID_LOG_H
#define NJ_TEST_ANDROID_LOG_H
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
int __android_log_print(int priority, const char *tag, const char *format, ...);
int __android_log_write(int priority, const char *tag, const char *text);
#endif
