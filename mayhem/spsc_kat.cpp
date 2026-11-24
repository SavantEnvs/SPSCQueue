// Known-answer driver for mayhem/test.sh: exercises SPSCQueue's public API and
// prints the observed values, which test.sh diffs against mayhem/spsc_kat.expected.
#include <rigtorp/SPSCQueue.h>

#include <cstdio>
#include <string>

int main() {
  rigtorp::SPSCQueue<int> q(3);
  std::printf("capacity>=3 %d empty %d size %zu\n", q.capacity() >= 3, q.empty(),
              q.size());
  int pushed = 0;
  for (int i = 1; i <= 10; i++)
    if (q.try_push(i * 11)) pushed++;
  std::printf("pushed %d size %zu\n", pushed, q.size());
  while (int *f = q.front()) {
    std::printf("front %d\n", *f);
    q.pop();
  }
  std::printf("empty %d\n", q.empty());

  // Wrap the ring several times.
  long sum = 0;
  for (int i = 0; i < 100; i++) {
    q.emplace(i);
    sum += *q.front();
    q.pop();
  }
  std::printf("wrap sum %ld\n", sum);

  rigtorp::SPSCQueue<std::string> s(2);
  s.push(std::string("alpha"));
  (void)s.try_emplace(3, 'z');
  std::string out;
  while (s.try_pop(out)) std::printf("str %s\n", out.c_str());
  int calls = 0;
  bool none = s.try_consume([&](std::string &) { calls++; });
  std::printf("consume-empty %d calls %d\n", none, calls);
  return 0;
}
