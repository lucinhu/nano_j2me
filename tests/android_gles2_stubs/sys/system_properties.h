#ifndef NJ_TEST_ANDROID_SYSTEM_PROPERTIES_H
#define NJ_TEST_ANDROID_SYSTEM_PROPERTIES_H
#define PROP_VALUE_MAX 92
int __system_property_get(const char *key, char *value);
#endif
