#pragma once

#include "GlTimerQueryPool.h"

#include <cstdint>
#include <optional>

namespace dzc::opengl::test {

class FakeTimerQueryOperations final : public IGlTimerQueryOperations {
public:
  bool createTimerQuery(std::uint32_t &id) const noexcept override {
    if (failCreate ||
        (failCreateAfter.has_value() && createCalls >= *failCreateAfter))
      return false;
    id = nextId++;
    ++createCalls;
    return true;
  }

  bool deleteTimerQuery(std::uint32_t id) const noexcept override {
    if (failDelete || id == 0U)
      return false;
    ++deleteCalls;
    return true;
  }

  bool beginElapsedTimeQuery(std::uint32_t id) const noexcept override {
    if (failBegin || id == 0U)
      return false;
    ++beginCalls;
    return true;
  }

  bool endElapsedTimeQuery() const noexcept override {
    if (failEnd)
      return false;
    ++endCalls;
    return true;
  }

  bool isTimerQueryResultAvailable(std::uint32_t,
                                   bool &available) const noexcept override {
    if (failAvailability)
      return false;
    ++availabilityCalls;
    available = resultAvailable;
    return true;
  }

  bool readTimerQueryResultNanoseconds(
      std::uint32_t, std::uint64_t &nanoseconds) const noexcept override {
    if (failRead)
      return false;
    ++readCalls;
    nanoseconds = resultNanoseconds;
    return true;
  }

  mutable bool failCreate{false};
  mutable std::optional<std::uint32_t> failCreateAfter;
  mutable bool failDelete{false};
  mutable bool failBegin{false};
  mutable bool failEnd{false};
  mutable bool failAvailability{false};
  mutable bool failRead{false};
  mutable bool resultAvailable{true};
  mutable std::uint64_t resultNanoseconds{1000000U};
  mutable std::uint32_t nextId{1U};
  mutable std::uint32_t createCalls{0U};
  mutable std::uint32_t deleteCalls{0U};
  mutable std::uint32_t beginCalls{0U};
  mutable std::uint32_t endCalls{0U};
  mutable std::uint32_t availabilityCalls{0U};
  mutable std::uint32_t readCalls{0U};
};

} // namespace dzc::opengl::test
