/***************************************************************************
 *   Copyright (C) 2008 by H-Store Project                                 *
 *   Brown University                                                      *
 *   Massachusetts Institute of Technology                                 *
 *   Yale University                                                       *
 *                                                                         *
 *   This software may be modified and distributed under the terms         *
 *   of the MIT license.  See the LICENSE file for details.                *
 *                                                                         *
 ***************************************************************************/

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

namespace erpc {

// Log levels: higher means more verbose
#define LOG_LEVEL_OFF 0
#define LOG_LEVEL_ERROR 1    // Only fatal conditions
#define LOG_LEVEL_WARN 2     // Conditions from which it is possible to recover
#define LOG_LEVEL_INFO 3     // Reasonable to print (e.g., management packets)
#define LOG_LEVEL_REORDER 4  // Too frequent to print (e.g., reordered packets)
#define LOG_LEVEL_TRACE 5    // Extremely frequent (e.g., all datapath packets)
#define LOG_LEVEL_CC 6       // Even congestion control decisions!

#define LOG_DEFAULT_STREAM stdout

// If LOG_LEVEL is not defined, default to LOG_LEVEL_INFO in debug mode, and
// LOG_LEVEL_WARN in non-debug mode.
#ifndef LOG_LEVEL
#ifndef NDEBUG
#define LOG_LEVEL LOG_LEVEL_INFO
#else
#define LOG_LEVEL LOG_LEVEL_WARN
#endif
#endif

static void output_log_header(int level);

#if LOG_LEVEL >= LOG_LEVEL_ERROR
#define LOG_ERROR(...)                                    \
  output_log_header(LOG_DEFAULT_STREAM, LOG_LEVEL_ERROR); \
  fprintf(LOG_DEFAULT_STREAM, __VA_ARGS__);               \
  fflush(LOG_DEFAULT_STREAM)
#else
#define LOG_ERROR(...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#define LOG_WARN(...)                                    \
  output_log_header(LOG_DEFAULT_STREAM, LOG_LEVEL_WARN); \
  fprintf(LOG_DEFAULT_STREAM, __VA_ARGS__);              \
  fflush(LOG_DEFAULT_STREAM)
#else
#define LOG_WARN(...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#define LOG_INFO(...)                                    \
  output_log_header(LOG_DEFAULT_STREAM, LOG_LEVEL_INFO); \
  fprintf(LOG_DEFAULT_STREAM, __VA_ARGS__);              \
  fflush(LOG_DEFAULT_STREAM)
#else
#define LOG_INFO(...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_REORDER
#define LOG_REORDER(fd, ...)                \
  output_log_header(fd, LOG_LEVEL_REORDER); \
  fprintf(fd, __VA_ARGS__);                 \
  fflush(fd)
#else
#define LOG_REORDER(...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_TRACE
#define LOG_TRACE(fd, ...)                \
  output_log_header(fd, LOG_LEVEL_TRACE); \
  fprintf(fd, __VA_ARGS__);               \
  fflush(fd)
#else
#define LOG_TRACE(...) ((void)0)
#endif

#if LOG_LEVEL >= LOG_LEVEL_CC
#define LOG_CC(fd, ...)                \
  output_log_header(fd, LOG_LEVEL_CC); \
  fprintf(fd, __VA_ARGS__);            \
  fflush(fd)
#else
#define LOG_CC(...) ((void)0)
#endif

static std::string get_formatted_time() {
  const boost::posix_time::ptime now =
      boost::posix_time::microsec_clock::local_time();
  const boost::posix_time::time_duration td = now.time_of_day();

  const long minutes = td.minutes();
  const long seconds = td.seconds();
  const long milliseconds =
      td.total_milliseconds() -
      ((td.hours() * 3600 + minutes * 60 + seconds) * 1000);

  char buf[100];
  sprintf(buf, "%02ld:%02ld.%03ld", minutes, seconds, milliseconds);

  return std::string(buf);
}

// Output log message header
static void output_log_header(FILE *stream, int level) {
  std::string formatted_time = get_formatted_time();

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
    case LOG_LEVEL_REORDER:
      type = "REORDER";
      break;
    case LOG_LEVEL_TRACE:
      type = "TRACE";
      break;
    case LOG_LEVEL_CC:
      type = "CC";
      break;
    default:
      type = "UNKWN";
  }

  fprintf(stream, "%s %s: ", formatted_time.c_str(), type);
}

/// Return true iff REORDER/TRACE/CC mode logging is disabled. These modes can
/// print an unreasonable number of log messages.
static bool is_log_level_reasonable() { return LOG_LEVEL <= LOG_LEVEL_INFO; }

}  // End erpc

#endif  // ERPC_LOGGER_H
