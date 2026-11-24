// libFuzzer harness for rigtorp::SPSCQueue (include/rigtorp/SPSCQueue.h).
//
// The library is a single header with no parser: its whole input surface is the
// public API. The input chooses
//   byte 0      : element type (int / std::string) and allocator flavour
//                 (plain allocate() / allocate_at_least())
//   bytes 1..8  : the requested capacity (full 64-bit range; a top byte of 0x7f
//                 maps to SIZE_MAX - low byte so the size_t overflow edge in the
//                 constructor is reachable)
//   rest        : a single-threaded stream of producer/consumer operations,
//                 each checked against a std::deque model.
//
// The allocators refuse (throw std::bad_alloc) requests above 64Ki elements, so a
// huge-but-valid capacity does not trip ASan's allocation-size limit; the queue's
// contract says allocation exceptions propagate from the constructor.
// Blocking push()/emplace() are only issued when the model says there is room
// (they spin forever on a full queue with no consumer), and pop() only after
// front() returned non-null (its documented precondition).
// No state survives an input: the queue and model live on the stack.
#include <rigtorp/SPSCQueue.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace {

constexpr size_t kMaxElems = size_t{1} << 16;

template <typename T> struct LimitAlloc {
  using value_type = T;
  LimitAlloc() = default;
  template <typename U> LimitAlloc(const LimitAlloc<U> &) {}
  T *allocate(size_t n) {
    if (n > kMaxElems) throw std::bad_alloc();
    return std::allocator<T>().allocate(n);
  }
  void deallocate(T *p, size_t n) { std::allocator<T>().deallocate(p, n); }
  bool operator==(const LimitAlloc &) const { return true; }
  bool operator!=(const LimitAlloc &) const { return false; }
};

// Same, but also offers allocate_at_least() (rounding up to a multiple of 8),
// which the queue uses to grow capacity() beyond the request.
template <typename T> struct AtLeastAlloc {
  using value_type = T;
  struct result {
    T *ptr;
    size_t count;
  };
  AtLeastAlloc() = default;
  template <typename U> AtLeastAlloc(const AtLeastAlloc<U> &) {}
  T *allocate(size_t n) {
    if (n > kMaxElems) throw std::bad_alloc();
    return std::allocator<T>().allocate(n);
  }
  result allocate_at_least(size_t n) {
    if (n > kMaxElems) throw std::bad_alloc();
    size_t m = (n + 7) & ~size_t{7};
    return {std::allocator<T>().allocate(m), m};
  }
  void deallocate(T *p, size_t n) { std::allocator<T>().deallocate(p, n); }
  bool operator==(const AtLeastAlloc &) const { return true; }
  bool operator!=(const AtLeastAlloc &) const { return false; }
};

[[noreturn]] void mismatch() { std::abort(); }

template <typename T, typename Alloc, typename Mk>
void run(const uint8_t *d, size_t n, Mk mk) {
  if (n < 8) return;
  uint64_t cap;
  std::memcpy(&cap, d, 8);
  d += 8;
  n -= 8;
  if ((cap >> 56) == 0x7f) cap = SIZE_MAX - (cap & 0xff);
  try {
    rigtorp::SPSCQueue<T, Alloc> q(static_cast<size_t>(cap));
    std::deque<T> model;
    const size_t qcap = q.capacity();
    for (size_t i = 0; i < n; i++) {
      const uint8_t b = d[i];
      const T v = mk(b);
      const bool room = model.size() < qcap;
      switch (b % 10) {
      case 0: { // try_push(const T&)
        bool ok = q.try_push(v);
        if (ok != room) mismatch();
        if (ok) model.push_back(v);
        break;
      }
      case 1: { // try_push(T&&)
        T tmp = v;
        bool ok = q.try_push(std::move(tmp));
        if (ok != room) mismatch();
        if (ok) model.push_back(v);
        break;
      }
      case 2: { // try_emplace
        bool ok = q.try_emplace(v);
        if (ok != room) mismatch();
        if (ok) model.push_back(v);
        break;
      }
      case 3: // push (blocking): only with room
        if (room) {
          q.push(v);
          model.push_back(v);
        }
        break;
      case 4: // emplace (blocking): only with room
        if (room) {
          q.emplace(v);
          model.push_back(v);
        }
        break;
      case 5:
      case 6: { // front + pop
        T *f = q.front();
        if (!f) {
          if (!model.empty()) mismatch();
        } else {
          if (model.empty() || !(*f == model.front())) mismatch();
          q.pop();
          model.pop_front();
        }
        break;
      }
      case 7: { // try_pop (recent API; falls back to front+pop on old headers)
        if constexpr (requires(T &out) { q.try_pop(out); }) {
          T out{};
          bool ok = q.try_pop(out);
          if (ok == model.empty()) mismatch();
          if (ok) {
            if (!(out == model.front())) mismatch();
            model.pop_front();
          }
        } else {
          T *f = q.front();
          if (f) {
            q.pop();
            model.pop_front();
          }
        }
        break;
      }
      case 8: { // try_consume (recent API)
        if constexpr (requires { q.try_consume([](T &) {}); }) {
          bool seen = false;
          bool ok = q.try_consume([&](T &slot) {
            seen = true;
            if (model.empty() || !(slot == model.front())) mismatch();
          });
          if (ok != seen || ok == model.empty()) mismatch();
          if (ok) model.pop_front();
        }
        break;
      }
      default: // observers
        if (q.size() != model.size() || q.empty() != model.empty() ||
            q.capacity() != qcap)
          mismatch();
        break;
      }
    }
    // The destructor drains whatever is still queued.
  } catch (const std::bad_alloc &) {
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 1) return 0;
  const uint8_t sel = data[0];
  auto mkint = [](uint8_t x) { return int(x) * 0x010101; };
  auto mkstr = [](uint8_t x) { return std::string(size_t(x) % 40, char('a' + x % 26)); };
  switch (sel & 3) {
  case 0: run<int, LimitAlloc<int>>(data + 1, size - 1, mkint); break;
  case 1: run<std::string, LimitAlloc<std::string>>(data + 1, size - 1, mkstr); break;
  case 2: run<int, AtLeastAlloc<int>>(data + 1, size - 1, mkint); break;
  default: run<std::string, AtLeastAlloc<std::string>>(data + 1, size - 1, mkstr); break;
  }
  return 0;
}
