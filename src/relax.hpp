#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Original short ambient phrase. Generated only on playback; no files or audio thread.
inline std::vector<unsigned char> relaxationWave(int seconds) {
  constexpr int rate = 22050;
  seconds = std::clamp(seconds, 1, 5);
  const int count = rate * seconds;
  std::vector<unsigned char> data(44 + count * 2);
  auto put = [&](int offset, uint32_t value) { std::memcpy(data.data() + offset, &value, 4); };
  std::memcpy(data.data(), "RIFF", 4);
  put(4, (uint32_t)data.size() - 8);
  std::memcpy(data.data() + 8, "WAVEfmt ", 8);
  put(16, 16);
  const uint16_t format[] = {1, 1};
  std::memcpy(data.data() + 20, format, 4);
  put(24, rate);
  put(28, rate * 2);
  const uint16_t bits[] = {2, 16};
  std::memcpy(data.data() + 32, bits, 4);
  std::memcpy(data.data() + 36, "data", 4);
  put(40, count * 2);
  constexpr double tau = 6.283185307179586;
  const double chord[] = {130.8128, 195.9977, 261.6256, 329.6276, 391.9954};
  const double melody[] = {523.2511, 659.2551, 783.9909, 659.2551};
  for (int i = 0; i < count; ++i) {
    const double t = double(i) / rate;
    const double progress = double(i) / (count - 1);
    double sample = 0;
    for (int n = 0; n < 5; ++n)
      sample += std::sin(tau * chord[n] * t + .10 * std::sin(tau * .18 * t)) *
                (1 + .12 * std::sin(tau * .25 * t + n)) / 5.;
    for (int n = 0; n < 4; ++n) {
      double local = t - seconds * (.10 + .19 * n);
      if (local > 0)
        sample += .20 * std::sin(tau * melody[n] * local) *
                  std::min(1., local / .06) * std::exp(-local * 2.3);
    }
    // Smooth attack and release for every allowed rest duration, including one second.
    double attack = std::min(1., progress / .20);
    double release = std::min(1., (1 - progress) / .35);
    double envelope = (.5 - .5 * std::cos(attack * tau / 2)) *
                      (.5 - .5 * std::cos(release * tau / 2));
    int16_t value = (int16_t)(sample * envelope * 4800);
    std::memcpy(data.data() + 44 + i * 2, &value, 2);
  }
  return data;
}
