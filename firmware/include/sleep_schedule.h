#pragma once

#include <stdint.h>

namespace sleep_schedule {

constexpr uint32_t kClockSleepSeconds = 60;
constexpr uint32_t kMatchWakeLeadSeconds = 10 * 60;

constexpr uint32_t clockSleepSeconds() {
  return kClockSleepSeconds;
}

constexpr uint32_t secondsUntilMatchWake(uint32_t nowEpoch, uint32_t matchStartEpoch) {
  return (matchStartEpoch == 0 || matchStartEpoch <= nowEpoch + kMatchWakeLeadSeconds)
      ? 0
      : matchStartEpoch - nowEpoch - kMatchWakeLeadSeconds;
}

}  // namespace sleep_schedule
