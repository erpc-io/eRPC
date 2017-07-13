/**
 * @file logger.h
 * @brief Logging macros that can be optimized out
 * @author Hideaki, modified by Anuj
 */

#ifndef ERPC_LOGGER_H
#define ERPC_LOGGER_H

#include <boost/date_time/posix_time/posix_time.hpp>
#include <ctime>
#include <string>

namespace ERpc {

// Log levels
#define LOG_LEVEL_OFF 1000
#define LOG_LEVEL_ERROR 500
#define LOG_LEVEL_WARN 400
#define LOG_LEVEL_INFO 300   // Reasonable to print (e.g., management packets)
#define LOG_LEVEL_DEBUG 200  // Too frequent to print (e.g., reordered packets)
#define LOG_LEVEL_TRACE 100  // Extremely frequent (e.g., all datapath packets)

#define LOG_OUTPUT_STREAM stdout

// Set compile options automatically if LOG_LEVEL is not defined
#ifndef LOG_LEVEL
#ifndef NDEBUG
#define LOG_LEVEL LOG_LEVEL_DEBUG
#else
#define LOG_LEVEL LOG_LEVEL_INFO
#endif
#endif

// For compilers which do not support __FUNCTION__
#if !defined(__FUNCTION__) && !defined(__GNUC__)
#define __FUNCTION__ ""
#endif

void outputLogHeader_(const char *file, int line, const char *func, int level);

#if LOG_LEVEL <= LOG_LEVEL_ERROR
#define LOG_ERROR(...)                                                 \
  outputLogHeader_(__FILE__, __LINE__, __FUNCTION__, LOG_LEVEL_ERROR); \
  fprintf(LOG_OUTPUT_STREAM, __VA_ARGS__);                             \
  fprintf(LOG_OUTPUT_STREAM, "\n");                                    \
  fflush(stdout)
#else
#define LOG_ERROR(...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_WARN
#define LOG_WARN(...)                                                 \
  outputLogHeader_(__FILE__, __LINE__, __FUNCTION__, LOG_LEVEL_WARN); \
  fprintf(LOG_OUTPUT_STREAM, __VA_ARGS__);                            \
  fprintf(LOG_OUTPUT_STREAM, "\n");                                   \
  fflush(stdout)
#else
#define LOG_WARN(...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define LOG_INFO(...)                                                 \
  outputLogHeader_(__FILE__, __LINE__, __FUNCTION__, LOG_LEVEL_INFO); \
  fprintf(LOG_OUTPUT_STREAM, __VA_ARGS__);                            \
  fprintf(LOG_OUTPUT_STREAM, "\n");                                   \
  fflush(stdout)
#else
#define LOG_INFO(...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_DEBUG
#define LOG_DEBUG(...)                                                 \
  outputLogHeader_(__FILE__, __LINE__, __FUNCTION__, LOG_LEVEL_DEBUG); \
  fprintf(LOG_OUTPUT_STREAM, __VA_ARGS__);                             \
  fprintf(LOG_OUTPUT_STREAM, "\n");                                    \
  fflush(stdout)
#else
#define LOG_DEBUG(...) ((void)0)
#endif

#if LOG_LEVEL <= LOG_LEVEL_TRACE
#define LOG_TRACE(...)                                                 \
  outputLogHeader_(__FILE__, __LINE__, __FUNCTION__, LOG_LEVEL_TRACE); \
  fprintf(LOG_OUTPUT_STREAM, __VA_ARGS__);                             \
  fprintf(LOG_OUTPUT_STREAM, "\n");                                    \
  fflush(stdout)
#else
#define LOG_TRACE(...) ((void)0)
#endif

// Output log message header in this format: [type] [file:line:function] time -
// ex: [ERROR] [somefile.cpp:123:doSome()] 2008/07/06 10:00:00 -
inline void outputLogHeader_(const char *file, int line, const char *func,
                             int level) {
  const boost::posix_time::ptime now =
      boost::posix_time::microsec_clock::local_time();
  const boost::posix_time::time_duration td = now.time_of_day();

  const long minutes = td.minutes();
  const long seconds = td.seconds();
  const long milliseconds =
      td.total_milliseconds() -
      ((td.hours() * 3600 + minutes * 60 + seconds) * 1000);

  const char *type;
  switch (level) {
    case LOG_LEVEL_ERROR:
      type = "ERROR";
      break;
    case LOG_LEVEL_WARN:
      type = "WARN";
      break;
    case LOG_LEVEL_INFO:
      type = "INFO";
      break;
    case LOG_LEVEL_DEBUG:
      type = "DEBUG";
      break;
    case LOG_LEVEL_TRACE:
      type = "TRACE";
      break;
    default:
      type = "UNKWN";
  }

  fprintf(LOG_OUTPUT_STREAM, "%02ld:%02ld.%03ld [%s:%d:%s] %s: ", minutes,
          seconds, milliseconds, file, line, func, type);
}

}  // End ERpc

#endif  // ERPC_LOGGER_H
