/*
 * ============================================================================
 *  logger.h —— 串口日志（header-only，避免多重定义）
 * ============================================================================
 *  提供带级别、时间戳的日志宏：
 *    LOG_INFO(...) / LOG_WARN(...) / LOG_ERROR(...)
 *  所有日志统一走 Serial（USB 调试串口），便于联调观察。
 * ============================================================================
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <stdarg.h>

// 初始化串口日志
inline void logger_init(long baud) {
  Serial.begin(baud);
}

// 内部实现：打印 [级别] 时间戳 + 格式化消息
inline void logger_log(const char *level, const char *fmt, ...) {
  char buf[128];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  Serial.print(F("["));
  Serial.print(level);
  Serial.print(F("] "));
  Serial.print(millis());
  Serial.print(F("ms "));
  Serial.println(buf);
}

#define LOG_INFO(...)   logger_log("INFO",  __VA_ARGS__)
#define LOG_WARN(...)   logger_log("WARN",  __VA_ARGS__)
#define LOG_ERROR(...)  logger_log("ERROR", __VA_ARGS__)

#endif // LOGGER_H
