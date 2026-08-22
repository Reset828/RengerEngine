#include "data/io/PointCloudReaderRegistry.h"

#include <dzc/Error.h>

#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

constexpr std::uint32_t kCorruptDataCode = 2U;
constexpr std::uint32_t kInternalErrorCode = 1U;

class FakePointCloudReader final : public dzc::IPointCloudReader {
public:
    dzc::Result<dzc::PointCloudSourceInfo> open(const std::string&) override {
        return dzc::Result<dzc::PointCloudSourceInfo>::success({});
    }

    dzc::Result<std::optional<dzc::PointBatch>> readNext(
        std::size_t,
        dzc::tasks::CancellationToken) override {
        return dzc::Result<std::optional<dzc::PointBatch>>::success(std::nullopt);
    }

    dzc::Result<dzc::PointCloudReadProgress> readProgress() const override {
        return dzc::Result<dzc::PointCloudReadProgress>::success({});
    }

    void close() noexcept override {}
};

void assertError(
    const dzc::Result<std::unique_ptr<dzc::IPointCloudReader>>& result,
    dzc::ErrorDomain domain,
    std::uint32_t code) {
    assert(!result.hasValue());
    assert(result.error().domain == domain);
    assert(result.error().code == code);
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
}

void testRoutesFinalExtensionsCaseInsensitively() {
    std::uint32_t pcdCalls = 0U;
    std::uint32_t plyCalls = 0U;
    dzc::PointCloudReaderRegistry registry(
        [&pcdCalls]() {
            ++pcdCalls;
            return std::make_unique<FakePointCloudReader>();
        },
        [&plyCalls]() {
            ++plyCalls;
            return std::make_unique<FakePointCloudReader>();
        });

    const auto pcd = registry.create("dir.v1/cloud.PCD");
    assert(pcd.hasValue());
    assert(pcd.value() != nullptr);
    assert(pcdCalls == 1U);
    assert(plyCalls == 0U);

    const auto ply = registry.create("cloud.backup.pLy");
    assert(ply.hasValue());
    assert(ply.value() != nullptr);
    assert(pcdCalls == 1U);
    assert(plyCalls == 1U);
}

void testRoutesNonexistentPathsWithoutFilesystemAccess() {
    std::uint32_t pcdCalls = 0U;
    dzc::PointCloudReaderRegistry registry(
        [&pcdCalls]() {
            ++pcdCalls;
            return std::make_unique<FakePointCloudReader>();
        },
        []() { return std::make_unique<FakePointCloudReader>(); });

    const auto result = registry.create("Z:/a-path-that-does-not-exist/input.pcd");
    assert(result.hasValue());
    assert(result.value() != nullptr);
    assert(pcdCalls == 1U);
}

void testUnsupportedPathsDoNotCallCreators() {
    std::uint32_t pcdCalls = 0U;
    std::uint32_t plyCalls = 0U;
    dzc::PointCloudReaderRegistry registry(
        [&pcdCalls]() {
            ++pcdCalls;
            return std::make_unique<FakePointCloudReader>();
        },
        [&plyCalls]() {
            ++plyCalls;
            return std::make_unique<FakePointCloudReader>();
        });

    for (const std::string& path : {"", "cloud", "cloud.xyz", ".pcd", ".ply"}) {
        assertError(registry.create(path), dzc::ErrorDomain::DataFormat, kCorruptDataCode);
    }
    assert(pcdCalls == 0U);
    assert(plyCalls == 0U);
}

void testEmptyCreatorsAndNullReadersFail() {
    dzc::PointCloudReaderRegistry missingPcd(
        {},
        []() { return std::make_unique<FakePointCloudReader>(); });
    assertError(missingPcd.create("cloud.pcd"), dzc::ErrorDomain::Internal, kInternalErrorCode);

    dzc::PointCloudReaderRegistry missingPly(
        []() { return std::make_unique<FakePointCloudReader>(); },
        {});
    assertError(missingPly.create("cloud.ply"), dzc::ErrorDomain::Internal, kInternalErrorCode);

    dzc::PointCloudReaderRegistry nullReader(
        []() -> std::unique_ptr<dzc::IPointCloudReader> { return {}; },
        []() { return std::make_unique<FakePointCloudReader>(); });
    assertError(nullReader.create("cloud.pcd"), dzc::ErrorDomain::Internal, kInternalErrorCode);
}

void testCreatorExceptionsFailAtRegistryBoundary() {
    dzc::PointCloudReaderRegistry standardException(
        []() -> std::unique_ptr<dzc::IPointCloudReader> {
            throw std::runtime_error("PCD creator failed");
        },
        []() { return std::make_unique<FakePointCloudReader>(); });
    const auto standardResult = standardException.create("cloud.pcd");
    assertError(standardResult, dzc::ErrorDomain::Internal, kInternalErrorCode);
    assert(standardResult.error().diagnosticMessage.find("PCD creator failed") != std::string::npos);

    dzc::PointCloudReaderRegistry unknownException(
        []() { return std::make_unique<FakePointCloudReader>(); },
        []() -> std::unique_ptr<dzc::IPointCloudReader> { throw 7; });
    assertError(unknownException.create("cloud.ply"), dzc::ErrorDomain::Internal, kInternalErrorCode);
}

void testMoveTransfersCreatorsAndMovedFromRegistryFails() {
    static_assert(!std::is_copy_constructible_v<dzc::PointCloudReaderRegistry>);
    static_assert(!std::is_copy_assignable_v<dzc::PointCloudReaderRegistry>);
    static_assert(std::is_move_constructible_v<dzc::PointCloudReaderRegistry>);
    static_assert(std::is_move_assignable_v<dzc::PointCloudReaderRegistry>);

    std::uint32_t pcdCalls = 0U;
    dzc::PointCloudReaderRegistry original(
        [&pcdCalls]() {
            ++pcdCalls;
            return std::make_unique<FakePointCloudReader>();
        },
        []() { return std::make_unique<FakePointCloudReader>(); });

    dzc::PointCloudReaderRegistry moved(std::move(original));
    assertError(original.create("cloud.pcd"), dzc::ErrorDomain::Internal, kInternalErrorCode);

    const auto movedResult = moved.create("cloud.pcd");
    assert(movedResult.hasValue());
    assert(movedResult.value() != nullptr);
    assert(pcdCalls == 1U);

    dzc::PointCloudReaderRegistry assigned(
        []() { return std::make_unique<FakePointCloudReader>(); },
        []() { return std::make_unique<FakePointCloudReader>(); });
    assigned = std::move(moved);
    assertError(moved.create("cloud.pcd"), dzc::ErrorDomain::Internal, kInternalErrorCode);

    const auto assignedResult = assigned.create("cloud.pcd");
    assert(assignedResult.hasValue());
    assert(assignedResult.value() != nullptr);
    assert(pcdCalls == 2U);
}

} // namespace

int main() {
    testRoutesFinalExtensionsCaseInsensitively();
    testRoutesNonexistentPathsWithoutFilesystemAccess();
    testUnsupportedPathsDoNotCallCreators();
    testEmptyCreatorsAndNullReadersFail();
    testCreatorExceptionsFailAtRegistryBoundary();
    testMoveTransfersCreatorsAndMovedFromRegistryFails();
    return 0;
}
