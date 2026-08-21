#include "data/io/pcl/PcdReader.h"

#include <dzc/Error.h>

#include <pcl/PCLPointCloud2.h>
#include <pcl/PCLPointField.h>
#include <pcl/io/pcd_io.h>

#include <Eigen/Geometry>

#include <cstdint>
#include <exception>
#include <limits>
#include <string>
#include <utility>

namespace dzc {
namespace {

constexpr std::uint32_t kInvalidValueCode = 1U;
constexpr std::uint32_t kCorruptDataCode = 2U;
constexpr std::uint32_t kInvalidTaskCode = 1U;
constexpr std::uint32_t kCancelledCode = 7U;
constexpr std::uint32_t kInternalErrorCode = 1U;

Error makeError(
    ErrorDomain domain,
    std::uint32_t code,
    std::string userMessage,
    std::string diagnosticMessage,
    std::string context) {
    return Error{domain, code, std::move(userMessage), std::move(diagnosticMessage), std::move(context)};
}

Error corruptPcdError(std::string diagnosticMessage) {
    return makeError(
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "The PCD source metadata is invalid or cannot be read.",
        std::move(diagnosticMessage),
        "PcdReader::open");
}

bool isNumericScalar(std::uint8_t datatype) noexcept {
    switch (datatype) {
    case pcl::PCLPointField::INT8:
    case pcl::PCLPointField::UINT8:
    case pcl::PCLPointField::INT16:
    case pcl::PCLPointField::UINT16:
    case pcl::PCLPointField::INT32:
    case pcl::PCLPointField::UINT32:
    case pcl::PCLPointField::INT64:
    case pcl::PCLPointField::UINT64:
    case pcl::PCLPointField::FLOAT32:
    case pcl::PCLPointField::FLOAT64:
        return true;
    default:
        return false;
    }
}

bool isValidCoordinateField(const pcl::PCLPointField& field) noexcept {
    return field.count == 1U && isNumericScalar(field.datatype);
}

template <typename Unsigned>
bool convertToUint64(Unsigned value, std::uint64_t& converted) noexcept {
    static_assert(std::numeric_limits<Unsigned>::is_integer, "Point dimensions must be integral.");
    if constexpr (std::numeric_limits<Unsigned>::digits > std::numeric_limits<std::uint64_t>::digits) {
        if (value > static_cast<Unsigned>(std::numeric_limits<std::uint64_t>::max())) {
            return false;
        }
    }
    converted = static_cast<std::uint64_t>(value);
    return true;
}

bool declaredPointCount(
    const pcl::PCLPointCloud2& cloud,
    std::uint64_t& pointCount) noexcept {
    std::uint64_t width = 0U;
    std::uint64_t height = 0U;
    if (!convertToUint64(cloud.width, width) || !convertToUint64(cloud.height, height)) {
        return false;
    }
    if (width != 0U && height > std::numeric_limits<std::uint64_t>::max() / width) {
        return false;
    }
    pointCount = width * height;
    return true;
}

bool validateAndBuildSchema(
    const pcl::PCLPointCloud2& cloud,
    AttributeSchema& schema,
    std::string& diagnosticMessage) {
    std::size_t xCount = 0U;
    std::size_t yCount = 0U;
    std::size_t zCount = 0U;
    bool validX = false;
    bool validY = false;
    bool validZ = false;
    bool hasColor = false;
    bool hasIntensity = false;

    for (const pcl::PCLPointField& field : cloud.fields) {
        if (field.name == "x") {
            ++xCount;
            validX = isValidCoordinateField(field);
        } else if (field.name == "y") {
            ++yCount;
            validY = isValidCoordinateField(field);
        } else if (field.name == "z") {
            ++zCount;
            validZ = isValidCoordinateField(field);
        } else if (field.name == "rgb" || field.name == "rgba") {
            hasColor = true;
        } else if (field.name == "intensity") {
            hasIntensity = true;
        }
    }

    if (xCount != 1U || yCount != 1U || zCount != 1U || !validX || !validY || !validZ) {
        diagnosticMessage =
            "PCD header must define exactly one x, y, and z field, each with COUNT 1 and a numeric scalar datatype.";
        return false;
    }

    schema.mask = static_cast<std::uint32_t>(PointAttribute::Position);
    if (hasColor) {
        schema.mask |= static_cast<std::uint32_t>(PointAttribute::Color);
    }
    if (hasIntensity) {
        schema.mask |= static_cast<std::uint32_t>(PointAttribute::Intensity);
    }
    return true;
}

} // namespace

class PcdReader::Impl final {
public:
    pcl::PCDReader reader;
    pcl::PCLPointCloud2 cloud;
    bool isOpen{false};
};

PcdReader::PcdReader()
    : m_impl(std::make_unique<Impl>()) {}

PcdReader::~PcdReader() = default;

Result<PointCloudSourceInfo> PcdReader::open(const std::string& path) {
    if (m_impl->isOpen) {
        return Result<PointCloudSourceInfo>::failure(makeError(
            ErrorDomain::Task,
            kInvalidTaskCode,
            "Point cloud reader is already open.",
            "open() was called while PcdReader already has an opened source.",
            "PcdReader::open"));
    }

    try {
        pcl::PCLPointCloud2 cloud;
        Eigen::Vector4f origin;
        Eigen::Quaternionf orientation;
        int pcdVersion = 0;
        int dataType = 0;
        unsigned int dataOffset = 0U;
        const int headerResult = m_impl->reader.readHeader(
            path, cloud, origin, orientation, pcdVersion, dataType, dataOffset);
        if (headerResult != 0) {
            return Result<PointCloudSourceInfo>::failure(
                corruptPcdError("pcl::PCDReader::readHeader() returned " + std::to_string(headerResult) + "."));
        }

        PointCloudSourceInfo sourceInfo{};
        std::string validationDiagnostic;
        if (!validateAndBuildSchema(cloud, sourceInfo.schema, validationDiagnostic)) {
            return Result<PointCloudSourceInfo>::failure(corruptPcdError(std::move(validationDiagnostic)));
        }
        if (!declaredPointCount(cloud, sourceInfo.declaredPointCount)) {
            return Result<PointCloudSourceInfo>::failure(
                corruptPcdError("PCD header width and height cannot be represented as a uint64 point count."));
        }

        m_impl->cloud = std::move(cloud);
        m_impl->isOpen = true;
        return Result<PointCloudSourceInfo>::success(std::move(sourceInfo));
    } catch (const std::exception& exception) {
        return Result<PointCloudSourceInfo>::failure(
            corruptPcdError(std::string("pcl::PCDReader::readHeader() threw: ") + exception.what()));
    } catch (...) {
        return Result<PointCloudSourceInfo>::failure(
            corruptPcdError("pcl::PCDReader::readHeader() threw an unknown exception."));
    }
}

Result<std::optional<PointBatch>> PcdReader::readNext(
    std::size_t maximumPoints,
    tasks::CancellationToken token) {
    if (!m_impl->isOpen) {
        return Result<std::optional<PointBatch>>::failure(makeError(
            ErrorDomain::Task,
            kInvalidTaskCode,
            "Point cloud reader is not open.",
            "readNext() requires a successfully opened PCD source.",
            "PcdReader::readNext"));
    }
    if (maximumPoints == 0U) {
        return Result<std::optional<PointBatch>>::failure(makeError(
            ErrorDomain::Configuration,
            kInvalidValueCode,
            "Maximum point count must be greater than zero.",
            "readNext() received maximumPoints equal to zero.",
            "PcdReader::readNext"));
    }
    if (token.isCancellationRequested()) {
        return Result<std::optional<PointBatch>>::failure(makeError(
            ErrorDomain::Task,
            kCancelledCode,
            "Point cloud read cancelled.",
            "readNext() observed a requested cancellation.",
            "PcdReader::readNext"));
    }
    return Result<std::optional<PointBatch>>::failure(makeError(
        ErrorDomain::Internal,
        kInternalErrorCode,
        "PCD batch reading is not available yet.",
        "PcdReader metadata opening is implemented; point batch conversion is deferred to IO-005.",
        "PcdReader::readNext"));
}

void PcdReader::close() noexcept {
    m_impl->cloud = pcl::PCLPointCloud2{};
    m_impl->isOpen = false;
}

} // namespace dzc