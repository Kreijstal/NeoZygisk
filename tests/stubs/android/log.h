#pragma once
// Only Android's logger is stubbed for the Linux mount integration test.
enum { ANDROID_LOG_VERBOSE, ANDROID_LOG_DEBUG, ANDROID_LOG_INFO,
       ANDROID_LOG_WARN, ANDROID_LOG_ERROR, ANDROID_LOG_FATAL };
static inline int __android_log_print(int, const char *, const char *, ...) { return 0; }
