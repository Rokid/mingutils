#ifndef ROKID_LOG_H
#define ROKID_LOG_H

#include <stdint.h>
#include <stdarg.h>

typedef enum {
  ROKID_LOGLEVEL_VERBOSE = 0,
  ROKID_LOGLEVEL_DEBUG,
  ROKID_LOGLEVEL_INFO,
  ROKID_LOGLEVEL_WARNING,
  ROKID_LOGLEVEL_ERROR,

  ROKID_LOGLEVEL_NUMBER,
} RokidLogLevel;

typedef enum {
  ROKID_LOGWRITER_FD = 0,
  ROKID_LOGWRITER_SOCKET
} RokidBuiltinLogWriter;

// name of endpoint is duplicated
#define RLOG_EDUP -1
// name of endpoint not found
#define RLOG_ENOTFOUND -2
// endpoint already enabled
#define RLOG_EALREADY -3
// writer init failed
#define RLOG_EFAULT -4
// invalid arguments
#define RLOG_EINVAL -5

#ifdef __cplusplus
class RLogWriter {
public:
  virtual ~RLogWriter() = default;

  virtual bool init(void* arg) = 0;

  virtual void destroy() = 0;

  virtual bool write(const char *data, uint32_t size) = 0;
};

class RLog {
public:
  static void print(RokidLogLevel lv, const char* tag, const char* fmt, ...);

  static int32_t add_endpoint(const char* name, RLogWriter* writer);

  static int32_t add_endpoint(const char *name, RokidBuiltinLogWriter type);

  static void remove_endpoint(const char* name);

  static int32_t enable_endpoint(const char* name, void* init_arg, bool enable);
};

extern "C" {
#endif

// built-in writer
// "std"

typedef int32_t (*RokidLogInit)(void *arg, void *init_arg);
typedef void (*RokidLogDestroy)(void *);
typedef int32_t (*RokidLogWrite)(const char *, uint32_t, void *);
typedef struct {
  RokidLogInit init;
  RokidLogDestroy destroy;
  RokidLogWrite write;
} RokidLogWriter;

int32_t rokid_log_add_endpoint(const char *name, RokidLogWriter *writer, void *arg);

int32_t rokid_log_add_builtin_endpoint(const char *name, RokidBuiltinLogWriter type);

void rokid_log_remove_endpoint(const char *name);

int32_t rokid_log_enable_endpoint(const char *name, void *init_arg, bool enable);

#ifdef __cplusplus
} // extern "C"
#endif

#ifndef ROKID_LOG_ENABLED
#define ROKID_LOG_ENABLED 2
#endif

#ifdef __cplusplus
#define rokid_log_print RLog::print
#else
extern "C" void rokid_log_print(RokidLogLevel lv, const char* tag, const char* fmt, ...);
#endif

#if ROKID_LOG_ENABLED <= 0
#define KLOGV(tag, fmt, ...) rokid_log_print(ROKID_LOGLEVEL_VERBOSE, tag, fmt, ##__VA_ARGS__)
#else
#define KLOGV(tag, fmt, ...)
#endif

#if ROKID_LOG_ENABLED <= 1
#define KLOGD(tag, fmt, ...) rokid_log_print(ROKID_LOGLEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#else
#define KLOGD(tag, fmt, ...)
#endif

#if ROKID_LOG_ENABLED <= 2
#define KLOGI(tag, fmt, ...) rokid_log_print(ROKID_LOGLEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#else
#define KLOGI(tag, fmt, ...)
#endif

#if ROKID_LOG_ENABLED <= 3
#define KLOGW(tag, fmt, ...) rokid_log_print(ROKID_LOGLEVEL_WARNING, tag, fmt, ##__VA_ARGS__)
#else
#define KLOGW(tag, fmt, ...)
#endif

#if ROKID_LOG_ENABLED <= 4
#define KLOGE(tag, fmt, ...) rokid_log_print(ROKID_LOGLEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#else
#define KLOGE(tag, fmt, ...)
#endif

#endif // ROKID_LOG_H
