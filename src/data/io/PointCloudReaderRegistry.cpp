#include "data/io/PointCloudReaderRegistry.h"

#include <dzc/Error.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kCorruptDataCode = 2U;
constexpr std::uint32_t kInternalErrorCode = 1U;

std::string toAsciiLowercase(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

Error unsupportedFormatError(const std::string& path) {
    return Error{
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "Point cloud format is not supported.",
        "The source path does not have a supported .pcd or .ply final extension: " + path,
        "PointCloudReaderRegistry::create"};
}

Error internalError(const std::string& diagnosticMessage) {
    return Error{
        ErrorDomain::Internal,
        kInternalErrorCode,
        "Point cloud reader creation is unavailable.",
        diagnosticMessage,
        "PointCloudReaderRegistry::create"};
}

} // namespace

class PointCloudReaderRegistry::Impl final {
public:
    Impl(PointCloudReaderCreator pcd, PointCloudReaderCreator ply)
        : pcdCreator(std::move(pcd)),
          plyCreator(std::move(ply)) {}

    PointCloudReaderCreator pcdCreator;
    PointCloudReaderCreator plyCreator;
};

PointCloudReaderRegistry::PointCloudReaderRegistry(
    PointCloudReaderCreator pcdCreator,
    PointCloudReaderCreator plyCreator)
    : m_impl(std::make_unique<Impl>(std::move(pcdCreator), std::move(plyCreator))) {}

PointCloudReaderRegistry::~PointCloudReaderRegistry() = default;

PointCloudReaderRegistry::PointCloudReaderRegistry(PointCloudReaderRegistry&& other) noexcept = default;

PointCloudReaderRegistry& PointCloudReaderRegistry::operator=(
    PointCloudReaderRegistry&& other) noexcept = default;

Result<std::unique_ptr<IPointCloudReader>> PointCloudReaderRegistry::create(
    const std::string& path) const {
    if (!m_impl) {
        return Result<std::unique_ptr<IPointCloudReader>>::failure(
            internalError("The registry was used after it was moved from."));
    }

    std::string extension;
    try {
        extension = toAsciiLowercase(std::filesystem::path(path).extension().string());
    } catch (const std::exception& exception) {
        return Result<std::unique_ptr<IPointCloudReader>>::failure(
            unsupportedFormatError(path));
    } catch (...) {
        return Result<std::unique_ptr<IPointCloudReader>>::failure(
            unsupportedFormatError(path));
    }

    const PointCloudReaderCreator* creator = nullptr;
    if (extension == ".pcd") {
        creator = &m_impl->pcdCreator;
    } else if (extension == ".ply") {
        creator = &m_impl->plyCreator;
    } else {
        return Result<std::unique_ptr<IPointCloudReader>>::failure(unsupportedFormatError(path));
    }

    if (!*creator) {
        return Result<std::unique_ptr<IPointCloudReader>>::failure(
            internalError("The selected reader creator is not configured."));
    }

    try {
        std::unique_ptr<IPointCloudReader> reader = (*creator)();
        if (!reader) {
            return Result<std::unique_ptr<IPointCloudReader>>::failure(
                internalError("The selected reader creator returned a null reader."));
        }
        return Result<std::unique_ptr<IPointCloudReader>>::success(std::move(reader));
    } catch (const std::exception& exception) {
        return Result<std::unique_ptr<IPointCloudReader>>::failure(
            internalError(std::string("The selected reader creator threw: ") + exception.what()));
    } catch (...) {
        return Result<std::unique_ptr<IPointCloudReader>>::failure(
            internalError("The selected reader creator threw an unknown exception."));
    }
}

} // namespace dzc
