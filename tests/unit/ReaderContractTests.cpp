#include "data/io/IPointCloudReader.h"

#include <dzc/Error.h>

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kInvalidTaskCode = 1U;
constexpr std::uint32_t kInvalidValueCode = 1U;
constexpr std::uint32_t kCancelledCode = 7U;
constexpr std::uint32_t kPositionMask =
    static_cast<std::uint32_t>(dzc::PointAttribute::Position);

dzc::Error makeError(
    dzc::ErrorDomain domain,
    std::uint32_t code,
    const char* userMessage,
    const char* diagnosticMessage) {
    return dzc::Error{domain, code, userMessage, diagnosticMessage, "FakePointCloudReader"};
}

dzc::PointBatch makePointBatch(std::vector<glm::dvec3> positions) {
    dzc::PointBatch batch{};
    batch.schema.mask = kPositionMask;
    batch.positions = std::move(positions);
    return batch;
}

class FakePointCloudReader final : public dzc::IPointCloudReader {
public:
    explicit FakePointCloudReader(
        dzc::PointCloudSourceInfo sourceInfo,
        std::vector<glm::dvec3> positions)
        : m_sourceInfo(std::move(sourceInfo)),
          m_positions(std::move(positions)) {}

    dzc::Result<dzc::PointCloudSourceInfo> open(const std::string& path) override {
        if (m_isOpen) {
            return dzc::Result<dzc::PointCloudSourceInfo>::failure(makeError(
                dzc::ErrorDomain::Task,
                kInvalidTaskCode,
                "Point cloud reader is already open.",
                "open() was called while a source was already open."));
        }

        m_openedPath = path;
        m_nextPosition = 0U;
        m_isOpen = true;
        return dzc::Result<dzc::PointCloudSourceInfo>::success(m_sourceInfo);
    }

    dzc::Result<std::optional<dzc::PointBatch>> readNext(
        std::size_t maximumPoints,
        dzc::tasks::CancellationToken token) override {
        if (token.isCancellationRequested()) {
            return dzc::Result<std::optional<dzc::PointBatch>>::failure(makeError(
                dzc::ErrorDomain::Task,
                kCancelledCode,
                "Point cloud read cancelled.",
                "readNext() observed a requested cancellation."));
        }
        if (!m_isOpen) {
            return dzc::Result<std::optional<dzc::PointBatch>>::failure(makeError(
                dzc::ErrorDomain::Task,
                kInvalidTaskCode,
                "Point cloud reader is not open.",
                "readNext() requires a successfully opened source."));
        }
        if (maximumPoints == 0U) {
            return dzc::Result<std::optional<dzc::PointBatch>>::failure(makeError(
                dzc::ErrorDomain::Configuration,
                kInvalidValueCode,
                "Maximum point count must be greater than zero.",
                "readNext() received maximumPoints equal to zero."));
        }
        if (m_nextPosition == m_positions.size()) {
            return dzc::Result<std::optional<dzc::PointBatch>>::success(std::nullopt);
        }

        const std::size_t remaining = m_positions.size() - m_nextPosition;
        const std::size_t pointCount = remaining < maximumPoints ? remaining : maximumPoints;
        std::vector<glm::dvec3> positions(
            m_positions.begin() + static_cast<std::ptrdiff_t>(m_nextPosition),
            m_positions.begin() + static_cast<std::ptrdiff_t>(m_nextPosition + pointCount));
        m_nextPosition += pointCount;
        return dzc::Result<std::optional<dzc::PointBatch>>::success(
            makePointBatch(std::move(positions)));
    }

    dzc::Result<dzc::PointCloudReadProgress> readProgress() const override {
        if (!m_isOpen) {
            return dzc::Result<dzc::PointCloudReadProgress>::failure(makeError(
                dzc::ErrorDomain::Task,
                kInvalidTaskCode,
                "Point cloud reader is not open.",
                "readProgress() requires a successfully opened source."));
        }
        dzc::PointCloudReadProgress progress;
        progress.consumedSourcePoints = static_cast<std::uint64_t>(m_nextPosition);
        progress.totalSourcePoints = m_sourceInfo.declaredPointCount;
        return dzc::Result<dzc::PointCloudReadProgress>::success(std::move(progress));
    }

    void close() noexcept override {
        m_isOpen = false;
        m_nextPosition = 0U;
        m_openedPath.clear();
    }

private:
    dzc::PointCloudSourceInfo m_sourceInfo;
    std::vector<glm::dvec3> m_positions;
    std::string m_openedPath;
    std::size_t m_nextPosition{0U};
    bool m_isOpen{false};
};

dzc::PointCloudSourceInfo sampleSourceInfo(std::uint64_t declaredPointCount) {
    dzc::PointCloudSourceInfo sourceInfo{};
    sourceInfo.schema.mask = kPositionMask;
    sourceInfo.declaredPointCount = declaredPointCount;
    return sourceInfo;
}

void assertError(
    const dzc::Result<std::optional<dzc::PointBatch>>& result,
    dzc::ErrorDomain domain,
    std::uint32_t code) {
    assert(!result.hasValue());
    assert(result.error().domain == domain);
    assert(result.error().code == code);
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
}

void assertOpenError(
    const dzc::Result<dzc::PointCloudSourceInfo>& result,
    dzc::ErrorDomain domain,
    std::uint32_t code) {
    assert(!result.hasValue());
    assert(result.error().domain == domain);
    assert(result.error().code == code);
}

void assertValidBatch(const std::optional<dzc::PointBatch>& batch, std::size_t expectedCount) {
    assert(batch.has_value());
    assert(batch->positions.size() == expectedCount);
    assert(batch->validate().hasValue());
}

void testReadsBatchesAndRepeatedEof() {
    FakePointCloudReader reader(
        sampleSourceInfo(5U),
        {
            glm::dvec3{1.0, 0.0, 0.0},
            glm::dvec3{2.0, 0.0, 0.0},
            glm::dvec3{3.0, 0.0, 0.0},
            glm::dvec3{4.0, 0.0, 0.0},
            glm::dvec3{5.0, 0.0, 0.0}});

    const dzc::Result<dzc::PointCloudSourceInfo> opened = reader.open("sample.pcd");
    assert(opened.hasValue());
    assert(opened.value().schema.mask == kPositionMask);
    assert(opened.value().declaredPointCount == 5U);

    const auto first = reader.readNext(2U, {});
    assert(first.hasValue());
    assertValidBatch(first.value(), 2U);
    const auto second = reader.readNext(2U, {});
    assert(second.hasValue());
    assertValidBatch(second.value(), 2U);
    const auto third = reader.readNext(2U, {});
    assert(third.hasValue());
    assertValidBatch(third.value(), 1U);

    const auto end = reader.readNext(2U, {});
    assert(end.hasValue());
    assert(!end.value().has_value());
    const auto repeatedEnd = reader.readNext(2U, {});
    assert(repeatedEnd.hasValue());
    assert(!repeatedEnd.value().has_value());
}

void testEmptySourceEndsImmediately() {
    FakePointCloudReader reader(sampleSourceInfo(0U), {});
    assert(reader.open("empty.ply").hasValue());

    const auto result = reader.readNext(4U, {});
    assert(result.hasValue());
    assert(!result.value().has_value());
}

void testCancelledReadFailsWithoutAdvancing() {
    FakePointCloudReader reader(
        sampleSourceInfo(2U),
        {glm::dvec3{1.0, 2.0, 3.0}, glm::dvec3{4.0, 5.0, 6.0}});
    assert(reader.open("cancelled.pcd").hasValue());

    dzc::tasks::CancellationSource cancellationSource;
    assert(cancellationSource.requestCancellation());
    const auto cancelled = reader.readNext(1U, cancellationSource.token());
    assertError(cancelled, dzc::ErrorDomain::Task, kCancelledCode);

    const auto resumed = reader.readNext(1U, {});
    assert(resumed.hasValue());
    assertValidBatch(resumed.value(), 1U);
}

void testInvalidLifecycleAndMaximumPoints() {
    FakePointCloudReader reader(
        sampleSourceInfo(2U),
        {glm::dvec3{1.0, 0.0, 0.0}, glm::dvec3{2.0, 0.0, 0.0}});

    assertError(reader.readNext(1U, {}), dzc::ErrorDomain::Task, kInvalidTaskCode);
    assert(reader.open("first.pcd").hasValue());
    assertOpenError(reader.open("second.pcd"), dzc::ErrorDomain::Task, kInvalidTaskCode);

    const auto zeroMaximum = reader.readNext(0U, {});
    assertError(zeroMaximum, dzc::ErrorDomain::Configuration, kInvalidValueCode);

    const auto firstBatch = reader.readNext(2U, {});
    assert(firstBatch.hasValue());
    assertValidBatch(firstBatch.value(), 2U);
}

void testCloseIsIdempotentAndAllowsReopen() {
    FakePointCloudReader reader(
        sampleSourceInfo(1U),
        {glm::dvec3{1.0, 0.0, 0.0}});
    assert(reader.open("first.pcd").hasValue());
    reader.close();
    reader.close();
    assertError(reader.readNext(1U, {}), dzc::ErrorDomain::Task, kInvalidTaskCode);

    assert(reader.open("second.ply").hasValue());
    const auto batch = reader.readNext(1U, {});
    assert(batch.hasValue());
    assertValidBatch(batch.value(), 1U);
}

} // namespace

int main() {
    testReadsBatchesAndRepeatedEof();
    testEmptySourceEndsImmediately();
    testCancelledReadFailsWithoutAdvancing();
    testInvalidLifecycleAndMaximumPoints();
    testCloseIsIdempotentAndAllowsReopen();
    return 0;
}
