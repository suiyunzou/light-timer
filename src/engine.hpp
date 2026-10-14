#pragma once
#include <cstdint>
#include <algorithm>
struct Countdown {
  uint64_t duration = 1500000, deadline = 0, frozen = 1500000;
  bool running = false, completed = false;
  uint64_t remaining(uint64_t now) const {
    return running ? (deadline > now ? deadline - now : 0) : frozen;
  }
  void start(uint64_t ms, uint64_t now) {
    duration = frozen = ms;
    deadline = now + ms;
    running = true;
    completed = false;
  }
  void toggle(uint64_t now) {
    if (completed)
      return;
    if (running) {
      frozen = remaining(now);
      running = false;
    } else {
      deadline = now + frozen;
      running = true;
    }
  }
  bool advance(uint64_t now) {
    if (running && remaining(now) == 0) {
      running = false;
      completed = true;
      frozen = 0;
      return true;
    }
    return false;
  }
  unsigned delay(uint64_t now, bool visible, bool controls, bool breathe) const {
    if (!running)
      return 0;
    auto r = remaining(now);
    if (!r)
      return 1;
    if (!visible || controls)
      return (unsigned)std::min<uint64_t>(r, 0x7fffffff);
    if (breathe && r <= 5000)
      return 50;
    return (unsigned)std::max<uint64_t>(16, r % 1000 + 1);
  }
};
