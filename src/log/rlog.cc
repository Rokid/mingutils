#include <sys/mman.h>
#include <sys/time.h>
#include <stdarg.h>
#include <string>
#include <map>
#include "rlog.h"
#include "sock-svc-writer.h"

#define WRITE_BUFFER_SIZE 4096
#define WRITER_FLAG_AUTOPTR 0x1
#define WRITER_FLAG_ENABLED 0x2
#ifdef __APPLE__
#define PRIusec "%d"
#else
#define PRIusec "%ld"
#endif

using namespace std;

class RLogWriterInfo {
public:
  RLogWriter *writer = nullptr;
  void *arg = nullptr;
  uint32_t flags = 0;
};
typedef map<string, RLogWriterInfo> WriterMap;
typedef map<string, RLogWriterInfo *> WriterPointerMap;

class RLogInst {
public:
  RLogInst(uint32_t bsz) {
    buffer = (char *)mmap(nullptr, bsz, PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bufsize = bsz;
  }

  ~RLogInst() {
    if (buffer)
      munmap(buffer, bufsize);
  }

  int32_t add_endpoint(const string &name, RLogWriter* writer,
                       uint32_t flags) {
    RLogWriterInfo info;
    info.writer = writer;
    info.flags = flags;
    auto r = writers.insert(make_pair(name, info));
    if (!r.second)
      return RLOG_EDUP;
    return 0;
  }

  void remove_endpoint(const string &name) {
    auto it = writers.find(name);
    if (it != writers.end()) {
      enable_endpoint(name, nullptr, false);

      if (it->second.flags & WRITER_FLAG_AUTOPTR)
        delete it->second.writer;
      writers.erase(it);
    }
  }

  int32_t enable_endpoint(const string &name, void *init_arg, bool enable) {
    auto it = writers.find(name);
    if (it == writers.end())
      return RLOG_ENOTFOUND;
    if (enable) {
      if (it->second.flags & WRITER_FLAG_ENABLED)
        return RLOG_EALREADY;
      if (!it->second.writer->init(init_arg))
        return RLOG_EFAULT;
      writer_mutex.lock();
      it->second.arg = init_arg;
      it->second.flags |= WRITER_FLAG_ENABLED;
      enabled_writers.insert(make_pair(name, &it->second));
      writer_mutex.unlock();
    } else {
      if ((it->second.flags & WRITER_FLAG_ENABLED) == 0)
        return 0;
      writer_mutex.lock();
      it->second.arg = nullptr;
      it->second.flags &= (~WRITER_FLAG_ENABLED);
      enabled_writers.erase(name);
      writer_mutex.unlock();
      it->second.writer->destroy();
    }
    return 0;
  }

  void print(RokidLogLevel lv, const char* tag, const char* fmt, va_list ap) {
    if (tag == nullptr || fmt == nullptr)
      return;
    uint32_t c = format_string(buffer, bufsize, lv, tag, fmt, ap);

    writer_mutex.lock();
    auto it = enabled_writers.begin();
    while (it != enabled_writers.end()) {
      it->second->writer->write(buffer, c);
      ++it;
    }
    writer_mutex.unlock();
  }

private:
  static uint32_t format_string(char *out, uint32_t maxout,
                                RokidLogLevel lv, const char *tag,
                                const char *fmt, va_list ap) {
    int off = snprintf(out, maxout, "%s/%c  [", tag,
                       loglevel2char(lv));
    off += format_timestamp(out + off, maxout - off);
    off += snprintf(out + off, maxout - off, "]  ");
    off += vsnprintf(out + off, maxout - off, fmt, ap);
    if (off >= maxout)
      off = maxout - 1;
    out[off] = '\n';
    ++off;
    return off;
  }

  static uint32_t format_timestamp(char *out, uint32_t maxout) {
    struct timeval tv;
    struct tm ltm;

    gettimeofday(&tv, nullptr);
    localtime_r(&tv.tv_sec, &ltm);
    return snprintf(out, maxout, "%04d-%02d-%02d %02d:%02d:%02d." PRIusec,
                    ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday,
                    ltm.tm_hour, ltm.tm_min, ltm.tm_sec, tv.tv_usec);
  }

  static char loglevel2char(RokidLogLevel lv) {
    static char level_chars[] = {
      'V', 'D', 'I', 'W', 'E'
    };
    if (lv < ROKID_LOGLEVEL_VERBOSE || lv >= ROKID_LOGLEVEL_NUMBER)
      return 'U';
    return level_chars[lv];
  }

private:
  WriterMap writers;
  WriterPointerMap enabled_writers;
  mutex writer_mutex;
  char *buffer = nullptr;
  uint32_t bufsize = 0;
};

static RLogInst rlog_inst_(WRITE_BUFFER_SIZE);
static int ooxx = ([]() {
  RLog::add_endpoint("std", ROKID_LOGWRITER_FD);
  rlog_inst_.enable_endpoint("std", (void *)STDOUT_FILENO, true);
}(), 0);

int32_t RLog::add_endpoint(const char* name, RLogWriter* writer) {
  if (name == nullptr)
    return RLOG_EINVAL;
  return rlog_inst_.add_endpoint(name, writer, 0);
}

int32_t RLog::add_endpoint(const char *name, RokidBuiltinLogWriter type) {
  if (name == nullptr)
    return RLOG_EINVAL;
  RLogWriter *writer;
  if (type == ROKID_LOGWRITER_FD) {
    writer = new FileDescWriter();
  } else if (type == ROKID_LOGWRITER_SOCKET) {
    writer = new SocketServiceWriter();
  } else
    return RLOG_EINVAL;
  int32_t r = rlog_inst_.add_endpoint(name, writer, WRITER_FLAG_AUTOPTR);
  if (r != 0)
    delete writer;
  return r;
}

void RLog::remove_endpoint(const char* name) {
  if (name == nullptr)
    return;
  rlog_inst_.remove_endpoint(name);
}

int32_t RLog::enable_endpoint(const char* name, void* init_arg,
                              bool enable) {
  if (name == nullptr)
    return RLOG_EINVAL;
  return rlog_inst_.enable_endpoint(name, init_arg, enable);
}

void RLog::print(RokidLogLevel lv, const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  rlog_inst_.print(lv, tag, fmt, ap);
  va_end(ap);
}

class WrapCWriter : public RLogWriter {
public:
  RokidLogWriter *writer;
  void *arg;

  bool init(void *init_arg) {
    if (writer->init)
      return writer->init(arg, init_arg) ? false : true;
    return true;
  }

  void destroy() {
    if (writer->destroy)
      writer->destroy(arg);
  }

  bool write(const char *data, uint32_t size) {
    if (writer->write) {
      return writer->write(data, size, arg) ? false : true;
    }
    return true;
  }
};

int32_t rokid_log_add_endpoint(const char *name, RokidLogWriter *writer, void *arg) {
  WrapCWriter *ww = new WrapCWriter();
  ww->writer = writer;
  ww->arg = arg;
  int32_t r = rlog_inst_.add_endpoint(name, ww, WRITER_FLAG_AUTOPTR);
  if (r != 0)
    delete ww;
  return r;
}

int32_t rokid_log_add_builtin_endpoint(const char *name, RokidBuiltinLogWriter type) {
  return RLog::add_endpoint(name, type);
}

void rokid_log_remove_endpoint(const char *name) {
  RLog::remove_endpoint(name);
}

int32_t rokid_log_enable_endpoint(const char *name, void *init_arg, bool enable) {
  return RLog::enable_endpoint(name, init_arg, enable);
}
