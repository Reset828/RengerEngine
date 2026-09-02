#include "GlTimerQueryPool.h"

#include <glad/glad.h>

#include <cmath>
#include <limits>
#include <utility>

namespace dzc::opengl {
namespace {

std::uint32_t code(GlTimerQueryErrorCode value) noexcept {
  return static_cast<std::uint32_t>(value);
}

Error makeError(GlTimerQueryErrorCode value, const char *message,
                const char *diagnostic, const char *context) {
  return Error{ErrorDomain::OpenGL, code(value), message, diagnostic, context};
}

bool prepareGlOperation() noexcept {
  if (glGetError == nullptr)
    return false;
  while (glGetError() != GL_NO_ERROR) {
  }
  return true;
}

bool glOperationSucceeded() noexcept {
  return glGetError != nullptr && glGetError() == GL_NO_ERROR;
}


class GladTimerQueryOperations final : public IGlTimerQueryOperations {
public:
  bool createTimerQuery(std::uint32_t &id) const noexcept override {
    id = 0U;
    if (glGenQueries == nullptr || !prepareGlOperation())
      return false;
    GLuint query = 0U;
    glGenQueries(1, &query);
    id = static_cast<std::uint32_t>(query);
    return id != 0U && glOperationSucceeded();
  }

  bool deleteTimerQuery(std::uint32_t id) const noexcept override {
    if (id == 0U)
      return true;
    if (glDeleteQueries == nullptr || !prepareGlOperation())
      return false;
    const GLuint query = static_cast<GLuint>(id);
    glDeleteQueries(1, &query);
    return glOperationSucceeded();
  }

  bool beginElapsedTimeQuery(std::uint32_t id) const noexcept override {
    if (id == 0U || glBeginQuery == nullptr || !prepareGlOperation())
      return false;
    glBeginQuery(GL_TIME_ELAPSED, static_cast<GLuint>(id));
    return glOperationSucceeded();
  }

  bool endElapsedTimeQuery() const noexcept override {
    if (glEndQuery == nullptr || !prepareGlOperation())
      return false;
    glEndQuery(GL_TIME_ELAPSED);
    return glOperationSucceeded();
  }

  bool isTimerQueryResultAvailable(std::uint32_t id,
                                   bool &available) const noexcept override {
    if (id == 0U || glGetQueryObjectiv == nullptr || !prepareGlOperation())
      return false;
    GLint result = GL_FALSE;
    glGetQueryObjectiv(static_cast<GLuint>(id), GL_QUERY_RESULT_AVAILABLE,
                       &result);
    if (!glOperationSucceeded())
      return false;
    available = result == GL_TRUE;
    return true;
  }

  bool readTimerQueryResultNanoseconds(
      std::uint32_t id, std::uint64_t &nanoseconds) const noexcept override {
    if (id == 0U || glGetQueryObjectui64v == nullptr || !prepareGlOperation())
      return false;
    GLuint64 result = 0U;
    glGetQueryObjectui64v(static_cast<GLuint>(id), GL_QUERY_RESULT, &result);
    if (!glOperationSucceeded())
      return false;
    nanoseconds = static_cast<std::uint64_t>(result);
    return true;
  }
};
} // namespace

std::shared_ptr<const IGlTimerQueryOperations>
makeDefaultGlTimerQueryOperations() {
  static const std::shared_ptr<const IGlTimerQueryOperations> operations =
      std::make_shared<const GladTimerQueryOperations>();
  return operations;
}

GlTimerQueryPool::GlTimerQueryPool(
    std::shared_ptr<const IGlTimerQueryOperations> operations) noexcept
    : m_operations(std::move(operations)) {}

GlTimerQueryPool::~GlTimerQueryPool() noexcept { releaseNoexcept(); }

GlTimerQueryPool::GlTimerQueryPool(GlTimerQueryPool &&other) noexcept
    : m_operations(std::move(other.m_operations)), m_slots(std::move(other.m_slots)),
      m_nextSlot(other.m_nextSlot), m_activeSlot(other.m_activeSlot),
      m_ownerThread(other.m_ownerThread), m_releasePending(other.m_releasePending) {
  other.resetMovedFrom();
}

GlTimerQueryPool &GlTimerQueryPool::operator=(GlTimerQueryPool &&other) noexcept {
  if (this != &other) {
    releaseNoexcept();
    m_operations = std::move(other.m_operations);
    m_slots = std::move(other.m_slots);
    m_nextSlot = other.m_nextSlot;
    m_activeSlot = other.m_activeSlot;
    m_ownerThread = other.m_ownerThread;
    m_releasePending = other.m_releasePending;
    other.resetMovedFrom();
  }
  return *this;
}

Result<void> GlTimerQueryPool::validateToken(const GlContextThreadToken &token,
                                             const char *context) const {
  if (!token.isCurrentThread() || m_ownerThread != std::this_thread::get_id()) {
    return Result<void>::failure(makeError(
        GlTimerQueryErrorCode::InvalidThreadToken,
        "The OpenGL timer query pool must be used on its owner Context thread",
        "The supplied Context thread token does not match the query pool owner",
        context));
  }
  return Result<void>::success();
}

Result<void> GlTimerQueryPool::initialize(const GlContextThreadToken &token) {
  if (!token.isCurrentThread()) {
    return Result<void>::failure(makeError(
        GlTimerQueryErrorCode::InvalidThreadToken,
        "The OpenGL timer query pool must be initialized on the current Context thread",
        "The supplied Context thread token does not represent the current thread",
        "GlTimerQueryPool::initialize"));
  }
  if (isInitialized() || m_releasePending) {
    return Result<void>::failure(makeError(
        GlTimerQueryErrorCode::InvalidState,
        "The OpenGL timer query pool is already initialized",
        "initialize requires an empty query pool without pending releases",
        "GlTimerQueryPool::initialize"));
  }
  if (!m_operations)
    m_operations = makeDefaultGlTimerQueryOperations();

  m_ownerThread = std::this_thread::get_id();
  m_slots.reserve(queryDelayFrames);
  for (std::size_t index = 0U; index < queryDelayFrames; ++index) {
    std::uint32_t id = 0U;
    if (m_operations->createTimerQuery(id) && id != 0U) {
      m_slots.push_back({id, false});
      continue;
    }

    if (id != 0U)
      m_slots.push_back({id, false});
    std::vector<QuerySlot> undeletedSlots;
    undeletedSlots.reserve(m_slots.size());
    for (const auto &slot : m_slots) {
      if (!m_operations->deleteTimerQuery(slot.id))
        undeletedSlots.push_back(slot);
    }
    m_slots = std::move(undeletedSlots);
    m_nextSlot = 0U;
    m_activeSlot.reset();
    m_releasePending = !m_slots.empty();
    if (!m_releasePending)
      m_ownerThread = std::thread::id{};
    return Result<void>::failure(makeError(
        GlTimerQueryErrorCode::CreationFailed,
        "OpenGL timer query creation failed",
        "Could not create every query in the fixed GPU timer pool",
        "GlTimerQueryPool::initialize"));
  }
  m_nextSlot = 0U;
  m_activeSlot.reset();
  m_releasePending = false;
  return Result<void>::success();
}

Result<std::optional<double>> GlTimerQueryPool::resolveElapsedMilliseconds(
    const GlContextThreadToken &token) {
  auto valid = validateToken(token, "GlTimerQueryPool::resolveElapsedMilliseconds");
  if (!valid.hasValue())
    return Result<std::optional<double>>::failure(valid.error());
  if (m_activeSlot.has_value()) {
    return Result<std::optional<double>>::failure(makeError(
        GlTimerQueryErrorCode::InvalidState,
        "The active OpenGL timer query has not ended",
        "GPU timer results can only be resolved between frame queries",
        "GlTimerQueryPool::resolveElapsedMilliseconds"));
  }
  if (m_slots.empty()) {
    return Result<std::optional<double>>::failure(makeError(
        GlTimerQueryErrorCode::InvalidState,
        "The OpenGL timer query pool is not initialized",
        "No timer query slots are available",
        "GlTimerQueryPool::resolveElapsedMilliseconds"));
  }

  auto &slot = m_slots[m_nextSlot];
  if (!slot.pending)
    return Result<std::optional<double>>::success(std::nullopt);

  bool available = false;
  if (!m_operations->isTimerQueryResultAvailable(slot.id, available)) {
    return Result<std::optional<double>>::failure(makeError(
        GlTimerQueryErrorCode::AvailabilityCheckFailed,
        "OpenGL timer query availability check failed",
        "Could not determine whether the delayed GPU query result is ready",
        "GlTimerQueryPool::resolveElapsedMilliseconds"));
  }
  if (!available)
    return Result<std::optional<double>>::success(std::nullopt);

  std::uint64_t nanoseconds = 0U;
  if (!m_operations->readTimerQueryResultNanoseconds(slot.id, nanoseconds)) {
    return Result<std::optional<double>>::failure(makeError(
        GlTimerQueryErrorCode::ResultReadFailed,
        "OpenGL timer query result read failed",
        "Could not read the completed delayed GPU query result",
        "GlTimerQueryPool::resolveElapsedMilliseconds"));
  }
  const double milliseconds = static_cast<double>(nanoseconds) / 1000000.0;
  if (!std::isfinite(milliseconds) || milliseconds < 0.0) {
    return Result<std::optional<double>>::failure(makeError(
        GlTimerQueryErrorCode::ResultReadFailed,
        "OpenGL timer query result is invalid",
        "The converted GPU elapsed time is not finite and nonnegative",
        "GlTimerQueryPool::resolveElapsedMilliseconds"));
  }
  slot.pending = false;
  return Result<std::optional<double>>::success(milliseconds);
}

Result<void> GlTimerQueryPool::beginFrame(const GlContextThreadToken &token) {
  auto valid = validateToken(token, "GlTimerQueryPool::beginFrame");
  if (!valid.hasValue())
    return valid;
  if (m_activeSlot.has_value()) {
    return Result<void>::failure(makeError(
        GlTimerQueryErrorCode::InvalidState,
        "An OpenGL timer query is already active",
        "beginFrame cannot begin a second elapsed-time query",
        "GlTimerQueryPool::beginFrame"));
  }
  if (m_slots.empty()) {
    return Result<void>::failure(makeError(
        GlTimerQueryErrorCode::InvalidState,
        "The OpenGL timer query pool is not initialized",
        "No timer query slots are available",
        "GlTimerQueryPool::beginFrame"));
  }

  auto &slot = m_slots[m_nextSlot];
  if (slot.pending)
    return Result<void>::success();
  if (!m_operations->beginElapsedTimeQuery(slot.id)) {
    return Result<void>::failure(makeError(
        GlTimerQueryErrorCode::BeginFailed,
        "OpenGL GPU timer query could not begin",
        "glBeginQuery(GL_TIME_ELAPSED) failed through the operation table",
        "GlTimerQueryPool::beginFrame"));
  }
  m_activeSlot = m_nextSlot;
  return Result<void>::success();
}

Result<void> GlTimerQueryPool::endFrame(const GlContextThreadToken &token) {
  auto valid = validateToken(token, "GlTimerQueryPool::endFrame");
  if (!valid.hasValue())
    return valid;
  if (!m_activeSlot.has_value())
    return Result<void>::success();
  const bool ended = m_operations->endElapsedTimeQuery();
  m_slots[*m_activeSlot].pending = true;
  m_nextSlot = (*m_activeSlot + 1U) % m_slots.size();
  m_activeSlot.reset();
  if (!ended) {
    return Result<void>::failure(makeError(
        GlTimerQueryErrorCode::EndFailed,
        "OpenGL GPU timer query could not end",
        "glEndQuery(GL_TIME_ELAPSED) failed through the operation table; "
        "the query slot remains pending until it is resolved or released",
        "GlTimerQueryPool::endFrame"));
  }
  return Result<void>::success();
}

Result<void> GlTimerQueryPool::reset(const GlContextThreadToken &token) {
  if (m_slots.empty() && !m_releasePending)
    return Result<void>::success();
  auto valid = validateToken(token, "GlTimerQueryPool::reset");
  if (!valid.hasValue())
    return valid;
  if (m_activeSlot.has_value()) {
    return Result<void>::failure(makeError(
        GlTimerQueryErrorCode::InvalidState,
        "The active OpenGL timer query has not ended",
        "Timer queries must be ended before their pool is released",
        "GlTimerQueryPool::reset"));
  }

  std::vector<QuerySlot> undeletedSlots;
  undeletedSlots.reserve(m_slots.size());
  for (const auto &slot : m_slots) {
    if (!m_operations || !m_operations->deleteTimerQuery(slot.id))
      undeletedSlots.push_back(slot);
  }
  m_slots = std::move(undeletedSlots);
  m_nextSlot = 0U;
  m_releasePending = !m_slots.empty();
  if (m_releasePending) {
    return Result<void>::failure(makeError(
        GlTimerQueryErrorCode::ReleaseFailed,
        "OpenGL timer query release failed",
        "One or more GPU timer queries could not be deleted",
        "GlTimerQueryPool::reset"));
  }
  m_activeSlot.reset();
  m_ownerThread = std::thread::id{};
  return Result<void>::success();
}

bool GlTimerQueryPool::isInitialized() const noexcept { return !m_slots.empty(); }

bool GlTimerQueryPool::hasActiveQuery() const noexcept { return m_activeSlot.has_value(); }

void GlTimerQueryPool::releaseNoexcept() noexcept {
  if (m_slots.empty())
    return;
  if (m_ownerThread != std::this_thread::get_id() || !m_operations) {
    m_releasePending = true;
    return;
  }

  std::vector<QuerySlot> undeletedSlots;
  undeletedSlots.reserve(m_slots.size());
  for (const auto &slot : m_slots) {
    if (!m_operations->deleteTimerQuery(slot.id))
      undeletedSlots.push_back(slot);
  }
  m_slots = std::move(undeletedSlots);
  m_nextSlot = 0U;
  m_activeSlot.reset();
  m_releasePending = !m_slots.empty();
  if (!m_releasePending)
    m_ownerThread = std::thread::id{};
}

void GlTimerQueryPool::resetMovedFrom() noexcept {
  m_operations.reset();
  m_slots.clear();
  m_nextSlot = 0U;
  m_activeSlot.reset();
  m_ownerThread = std::thread::id{};
  m_releasePending = false;
}

} // namespace dzc::opengl
