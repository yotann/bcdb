#ifndef MEMODB_NNGMM_H
#define MEMODB_NNGMM_H

// This is a C++ wrapper for the NNG library, like nngpp, except that it
// doesn't use exceptions. This is important if the LLVM build we're using had
// exceptions disabled.

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Error.h>
#include <nng/nng.h>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/supplemental/util/platform.h>

namespace nng {

enum class error : int {
  success = 0,
  closed = NNG_ECLOSED,
  canceled = NNG_ECANCELED,
};

inline const char *to_string(error e) noexcept {
  return nng_strerror(static_cast<int>(e));
}

struct socket_view {
protected:
  nng_socket s{0};

public:
  socket_view() = default;

  socket_view(nng_socket s) noexcept : s(s) {}

  nng_socket get() const noexcept { return s; }

  void dial(const char *addr, int flags = 0) const {
    int r = nng_dial(s, addr, nullptr, flags);
    if (r != 0)
      llvm::report_fatal_error("nng_dial failed");
  }

  void listen(const char *addr, int flags = 0) const {
    int r = nng_listen(s, addr, nullptr, flags);
    if (r != 0)
      llvm::report_fatal_error("nng_listen failed");
  }
};

struct socket : socket_view {
  socket() = default;
  explicit socket(nng_socket s) noexcept : socket_view(s) {}
  socket(const socket &rhs) = delete;
  socket(socket &&rhs) noexcept : socket_view(rhs.s) { rhs.s.id = 0; }
  socket &operator=(const socket &rhs) = delete;
  socket &operator=(socket &&rhs) {
    if (this != &rhs) {
      if (s.id != 0)
        nng_close(s);
      s = rhs.s;
      rhs.s.id = 0;
    }
    return *this;
  }
  ~socket() {
    if (s.id != 0)
      nng_close(s);
  }
};

struct pipe_view {
protected:
  nng_pipe p{0};

public:
  pipe_view() = default;
  pipe_view(nng_pipe p) noexcept : p(p) {}

  nng_sockaddr get_opt_addr(const char *name) const {
    nng_sockaddr out;
    int r = nng_pipe_get_addr(p, name, &out);
    if (r != 0)
      llvm::report_fatal_error("nng_pipe_get_addr failed");
    return out;
  }
};

struct msg_body {
  nng_msg *m;

public:
  explicit msg_body(nng_msg *m) noexcept : m(m) {}
  template <typename T = void> T *data() noexcept {
    return static_cast<T *>(nng_msg_body(m));
  }
  size_t size() const noexcept { return nng_msg_len(m); }
  void append(llvm::ArrayRef<std::uint8_t> v) {
    int r = nng_msg_append(m, v.data(), v.size());
    if (r != 0)
      llvm::report_fatal_error("nng_msg_append failed");
  }
};

struct msg_view {
protected:
  nng_msg *m = nullptr;

public:
  msg_view() = default;
  msg_view(nng_msg *m) noexcept : m(m) {}
  nng_msg *get() noexcept { return m; }
  msg_body body() noexcept { return msg_body(m); }
  pipe_view get_pipe() noexcept { return nng_msg_get_pipe(m); }
};

struct msg : msg_view {
  msg() = default;
  explicit msg(nng_msg *m) noexcept : msg_view(m) {}
  explicit msg(size_t size) {
    int r = nng_msg_alloc(&m, size);
    if (r != 0)
      llvm::report_fatal_error("nng_msg_alloc failed");
  }
  msg(const msg &rhs) = delete;
  msg(msg &&rhs) = delete;
  msg &operator=(const msg &rhs) = delete;
  msg &operator=(msg &&rhs) {
    if (this != &rhs) {
      if (m != nullptr)
        nng_msg_free(m);
      m = rhs.m;
      rhs.m = nullptr;
    }
    return *this;
  }
  nng_msg *release() noexcept {
    auto out = m;
    m = nullptr;
    return out;
  }
};

inline msg make_msg(size_t size) { return msg(size); }

struct aio_view {
protected:
  nng_aio *a = nullptr;

public:
  aio_view() = default;
  nng_aio *get() noexcept { return a; }
  error result() const noexcept { return (error)nng_aio_result(a); }
  void wait() const noexcept { nng_aio_wait(a); }
  void set_msg(msg_view m) noexcept { nng_aio_set_msg(a, m.get()); }
  void set_msg(msg &&m) noexcept { set_msg(m.release()); }
  msg_view get_msg() const noexcept { return nng_aio_get_msg(a); }
  msg release_msg() noexcept {
    auto m = nng_aio_get_msg(a);
    nng_aio_set_msg(a, nullptr);
    return msg(m);
  }
};

inline void sleep(nng_duration ms, aio_view a) noexcept {
  nng_sleep_aio(ms, a.get());
}

struct aio : aio_view {
  aio() = default;
  aio(const aio &rhs) = delete;
  aio(aio &&rhs) = delete;
  aio &operator=(const aio &rhs) = delete;
  aio &operator=(aio &&rhs) = delete;
  explicit aio(void (*cb)(void *), void *arg) {
    int r = nng_aio_alloc(&a, cb, arg);
    if (r != 0)
      llvm::report_fatal_error("nng_aio_alloc failed");
  }
};

struct ctx_view {
protected:
  nng_ctx c{0};

public:
  ctx_view() = default;
  void send(aio_view a) noexcept { nng_ctx_send(c, a.get()); }
  void recv(aio_view a) noexcept { nng_ctx_recv(c, a.get()); }
  void set_opt_ms(const char *name, nng_duration value) {
    int r = nng_ctx_set_ms(c, name, value);
    if (r != 0)
      llvm::report_fatal_error("nng_ctx_set_ms failed");
  }
};

struct ctx : ctx_view {
  ctx() = default;
  ctx(const ctx &rhs) = delete;
  ctx(ctx &&rhs) = delete;
  ctx &operator=(const ctx &rhs) = delete;
  ctx &operator=(ctx &&rhs) {
    if (this != &rhs) {
      if (c.id != 0) {
        nng_ctx_close(c);
      }
      c = rhs.c;
      rhs.c.id = 0;
    }
    return *this;
  }
  explicit ctx(socket_view s) {
    int r = nng_ctx_open(&c, s.get());
    if (r != 0)
      llvm::report_fatal_error("nng_ctx_open failed");
  }
};

namespace rep {
namespace v0 {

inline socket open() {
  nng_socket s;
  int r = nng_rep0_open(&s);
  if (r != 0)
    llvm::report_fatal_error("nng_rep0_open failed");
  return socket(s);
}

} // end namespace v0
} // end namespace rep

namespace req {
namespace v0 {

inline socket open() {
  nng_socket s;
  int r = nng_req0_open(&s);
  if (r != 0)
    llvm::report_fatal_error("nng_req0_open failed");
  return socket(s);
}

inline void set_opt_resend_time(ctx_view s, nng_duration v) {
  s.set_opt_ms(NNG_OPT_REQ_RESENDTIME, v);
}

} // end namespace v0
} // end namespace req

inline void msleep(nng_duration dt) noexcept { nng_msleep(dt); }

inline uint32_t random() noexcept { return nng_random(); }

}; // end namespace nng

#endif // MEMODB_NNGMM_H
