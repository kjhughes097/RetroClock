#pragma once

#if defined(LOG_SERIAL_LEVEL) && (LOG_SERIAL_LEVEL & 1)
#define LOG_SERROR(format, ...)                       \
    do { if (Serial) {                                \
        Serial.print("  ERROR: ");                    \
        Serial.printf(format, ##__VA_ARGS__);         \
        Serial.println("");                           \
    } } while (0)
#else
#define LOG_SERROR(format, ...) ((void)0)
#endif

#if defined(LOG_SERIAL_LEVEL) && (LOG_SERIAL_LEVEL & 2)
#define LOG_SINFO(format, ...)                        \
    do { if (Serial) {                                \
        Serial.print("   INFO: ");                    \
        Serial.printf(format, ##__VA_ARGS__);         \
        Serial.println("");                           \
    } } while (0)
#else
#define LOG_SINFO(format, ...) ((void)0)
#endif

#if defined(LOG_SERIAL_LEVEL) && (LOG_SERIAL_LEVEL & 4)
#define LOG_SWARNING(format, ...)                     \
    do { if (Serial) {                                \
        Serial.print("WARNING: ");                    \
        Serial.printf(format, ##__VA_ARGS__);         \
        Serial.println("");                           \
    } } while (0)
#else
#define LOG_SWARNING(format, ...) ((void)0)
#endif

#if defined(LOG_SERIAL_LEVEL) && (LOG_SERIAL_LEVEL & 8)
#define LOG_SDEBUG(format, ...)                       \
    do { if (Serial) {                                \
        Serial.print("  DEBUG: ");                    \
        Serial.printf(format, ##__VA_ARGS__);         \
        Serial.println("");                           \
    } } while (0)
#else
#define LOG_SDEBUG(format, ...) ((void)0)
#endif
