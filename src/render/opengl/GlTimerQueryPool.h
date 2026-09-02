#pragma once

#include "GlResource.h"

#include <dzc/Result.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace dzc::opengl {

enum class GlTimerQueryErrorCode : std::uint32_t {
  InvalidState = 1U,
  InvalidThreadToken = 2U,
  CreationFailed = 3U,
  BeginFailed = 4U,
  EndFailed = 5U,
  AvailabilityCheckFailed = 6U,
  ResultReadFailed = 7U,
  ReleaseFailed = 8U
};

class IGlTimerQueryOperations {
public:
  virtual ~IGlTimerQueryOperations() = default;

  virtual bool createTimerQuery(std::uint32_t &id) const noexcept = 0;
  virtual bool deleteTimerQuery(std::uint32_t id) const noexcept = 0;
  virtual bool beginElapsedTimeQuery(std::uint32_t id) const noexcept = 0;
  virtual bool endElapsedTimeQuery() const noexcept = 0;
  virtual bool isTimerQueryResultAvailable(std::uint32_t id,
                                           bool &available) const noexcept = 0;
  virtual bool readTimerQueryResultNanoseconds(
      std::uint32_t id, std::uint64_t &nanoseconds) const noexcept = 0;
};

std::shared_ptr<const IGlTimerQueryOperations>
makeDefaultGlTimerQueryOperations();

class GlTimerQueryPool final {
public:
  static constexpr std::size_t queryDelayFrames = 3U;

  GlTimerQueryPool() noexcept = default;
  explicit GlTimerQueryPool(
      std::shared_ptr<const IGlTimerQueryOperations> operations) noexcept;
  ~GlTimerQueryPool() noexcept;

  GlTimerQueryPool(const GlTimerQueryPool &) = delete;
  GlTimerQueryPool &operator=(const GlTimerQueryPool &) = delete;
  GlTimerQueryPool(GlTimerQueryPool &&other) noexcept;
  GlTimerQueryPool &operator=(GlTimerQueryPool &&other) noexcept;

  dzc::Result<void> initialize(const GlContextThreadToken &token);
  dzc::Result<std::optional<double>> resolveElapsedMilliseconds(
      const GlContextThreadToken &token);
  dzc::Result<void> beginFrame(const GlContextThreadToken &token);
  dzc::Result<void> endFrame(const GlContextThreadToken &token);
  dzc::Result<void> reset(const GlContextThreadToken &token);

  bool isInitialized() const noexcept;
  bool hasActiveQuery() const noexcept;
  bool releasePending() const noexcept { return m_releasePending; }

private:
  struct QuerySlot final {
    std::uint32_t id{0U};
    bool pending{false};
  };

  dzc::Result<void> validateToken(const GlContextThreadToken &token,
                                  const char *context) const;
  void releaseNoexcept() noexcept;
  void resetMovedFrom() noexcept;

  std::shared_ptr<const IGlTimerQueryOperations> m_operations;
  std::vector<QuerySlot> m_slots;
  std::size_t m_nextSlot{0U};
  std::optional<std::size_t> m_activeSlot;
  std::thread::id m_ownerThread;
  bool m_releasePending{false};
};

} // namespace dzc::opengl
