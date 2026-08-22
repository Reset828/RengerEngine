#include "data/io/pcl/PcdReader.h"

#include "data/chunk/IntensityQuantizer.h"

#include <dzc/Error.h>

#include <pcl/PCLPointCloud2.h>
#include <pcl/PCLPointField.h>
#include <pcl/io/pcd_io.h>

#include <Eigen/Geometry>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dzc {
namespace {

constexpr std::uint32_t kInvalidValueCode = 1U;
constexpr std::uint32_t kCorruptDataCode = 2U;
constexpr std::uint32_t kInvalidTaskCode = 1U;
constexpr std::uint32_t kCancelledCode = 7U;
constexpr int kPcdAsciiDataType = 0;
constexpr int kPcdBinaryDataType = 1;

Error makeError(
    ErrorDomain domain,
    std::uint32_t code,
    std::string userMessage,
    std::string diagnosticMessage,
    std::string context) {
    return Error{domain, code, std::move(userMessage), std::move(diagnosticMessage), std::move(context)};
}

Error corruptPcdMetadataError(std::string diagnosticMessage) {
    return makeError(
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "The PCD source metadata is invalid or cannot be read.",
        std::move(diagnosticMessage),
        "PcdReader::open");
}

Error corruptPcdDataError(std::string diagnosticMessage) {
    return makeError(
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "The PCD point data is invalid or cannot be converted.",
        std::move(diagnosticMessage),
        "PcdReader::readNext");
}

Error cancelledError() {
    return makeError(
        ErrorDomain::Task,
        kCancelledCode,
        "Point cloud read cancelled.",
        "readNext() observed a requested cancellation.",
        "PcdReader::readNext");
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

std::size_t datatypeSize(std::uint8_t datatype) noexcept {
    switch (datatype) {
    case pcl::PCLPointField::INT8:
    case pcl::PCLPointField::UINT8:
        return 1U;
    case pcl::PCLPointField::INT16:
    case pcl::PCLPointField::UINT16:
        return 2U;
    case pcl::PCLPointField::INT32:
    case pcl::PCLPointField::UINT32:
    case pcl::PCLPointField::FLOAT32:
        return 4U;
    case pcl::PCLPointField::INT64:
    case pcl::PCLPointField::UINT64:
    case pcl::PCLPointField::FLOAT64:
        return 8U;
    default:
        return 0U;
    }
}

bool isValidCoordinateField(const pcl::PCLPointField& field) noexcept {
    return field.count == 1U && isNumericScalar(field.datatype);
}

bool isValidPackedColorField(const pcl::PCLPointField& field) noexcept {
    return field.count == 1U &&
           (field.datatype == pcl::PCLPointField::FLOAT32 ||
            field.datatype == pcl::PCLPointField::UINT32);
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

const pcl::PCLPointField* findUniqueField(
    const pcl::PCLPointCloud2& cloud,
    const char* name,
    std::string& diagnosticMessage) {
    const pcl::PCLPointField* match = nullptr;
    for (const pcl::PCLPointField& field : cloud.fields) {
        if (field.name != name) {
            continue;
        }
        if (match != nullptr) {
            diagnosticMessage = std::string("PCD defines duplicate ") + name + " fields.";
            return nullptr;
        }
        match = &field;
    }
    return match;
}

bool validateAndBuildSchema(
    const pcl::PCLPointCloud2& cloud,
    AttributeSchema& schema,
    std::string& diagnosticMessage) {
    const pcl::PCLPointField* x = findUniqueField(cloud, "x", diagnosticMessage);
    if (x == nullptr && !diagnosticMessage.empty()) {
        return false;
    }
    const pcl::PCLPointField* y = findUniqueField(cloud, "y", diagnosticMessage);
    if (y == nullptr && !diagnosticMessage.empty()) {
        return false;
    }
    const pcl::PCLPointField* z = findUniqueField(cloud, "z", diagnosticMessage);
    if (z == nullptr && !diagnosticMessage.empty()) {
        return false;
    }

    if (x == nullptr || y == nullptr || z == nullptr ||
        !isValidCoordinateField(*x) || !isValidCoordinateField(*y) || !isValidCoordinateField(*z)) {
        diagnosticMessage =
            "PCD header must define exactly one x, y, and z field, each with COUNT 1 and a numeric scalar datatype.";
        return false;
    }

    bool hasColor = false;
    bool hasIntensity = false;
    for (const pcl::PCLPointField& field : cloud.fields) {
        hasColor = hasColor || field.name == "rgb" || field.name == "rgba";
        hasIntensity = hasIntensity || field.name == "intensity";
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

bool validateFieldFitsPoint(
    const pcl::PCLPointField& field,
    std::size_t pointStep,
    std::string& diagnosticMessage) {
    const std::size_t size = datatypeSize(field.datatype);
    std::uint64_t offset = 0U;
    if (size == 0U || !convertToUint64(field.offset, offset) ||
        offset > pointStep || size > pointStep - static_cast<std::size_t>(offset)) {
        diagnosticMessage = "PCD field offset or datatype exceeds the point record size.";
        return false;
    }
    return true;
}

bool validateReadableCloud(
    const pcl::PCLPointCloud2& cloud,
    std::uint64_t expectedPointCount,
    AttributeSchema& schema,
    const pcl::PCLPointField*& x,
    const pcl::PCLPointField*& y,
    const pcl::PCLPointField*& z,
    const pcl::PCLPointField*& color,
    bool& colorHasAlpha,
    const pcl::PCLPointField*& intensity,
    std::string& diagnosticMessage) {
    std::uint64_t actualPointCount = 0U;
    if (!declaredPointCount(cloud, actualPointCount) || actualPointCount != expectedPointCount) {
        diagnosticMessage = "PCD body dimensions do not match the point count declared by its header.";
        return false;
    }
    if (!validateAndBuildSchema(cloud, schema, diagnosticMessage)) {
        return false;
    }

    x = findUniqueField(cloud, "x", diagnosticMessage);
    y = findUniqueField(cloud, "y", diagnosticMessage);
    z = findUniqueField(cloud, "z", diagnosticMessage);
    const pcl::PCLPointField* rgb = findUniqueField(cloud, "rgb", diagnosticMessage);
    const pcl::PCLPointField* rgba = findUniqueField(cloud, "rgba", diagnosticMessage);
    intensity = findUniqueField(cloud, "intensity", diagnosticMessage);
    if (x == nullptr || y == nullptr || z == nullptr || !diagnosticMessage.empty()) {
        diagnosticMessage = "PCD body field definitions are incomplete or invalid.";
        return false;
    }
    if ((rgb != nullptr && !isValidPackedColorField(*rgb)) ||
        (rgba != nullptr && !isValidPackedColorField(*rgba))) {
        diagnosticMessage =
            "PCD rgb and rgba fields must be unique COUNT 1 FLOAT32 or UINT32 packed values.";
        return false;
    }
    if (intensity != nullptr && !isValidCoordinateField(*intensity)) {
        diagnosticMessage = "PCD intensity field must be unique COUNT 1 numeric scalar value.";
        return false;
    }

    color = rgba != nullptr ? rgba : rgb;
    colorHasAlpha = rgba != nullptr;
    std::uint64_t pointStepValue = 0U;
    if (!convertToUint64(cloud.point_step, pointStepValue) || pointStepValue == 0U ||
        pointStepValue > std::numeric_limits<std::size_t>::max()) {
        diagnosticMessage = "PCD point record size is invalid.";
        return false;
    }
    const std::size_t pointStep = static_cast<std::size_t>(pointStepValue);
    if (!validateFieldFitsPoint(*x, pointStep, diagnosticMessage) ||
        !validateFieldFitsPoint(*y, pointStep, diagnosticMessage) ||
        !validateFieldFitsPoint(*z, pointStep, diagnosticMessage) ||
        (color != nullptr && !validateFieldFitsPoint(*color, pointStep, diagnosticMessage)) ||
        (intensity != nullptr && !validateFieldFitsPoint(*intensity, pointStep, diagnosticMessage))) {
        return false;
    }

    std::uint64_t width = 0U;
    std::uint64_t height = 0U;
    std::uint64_t rowStep = 0U;
    if (!convertToUint64(cloud.width, width) || !convertToUint64(cloud.height, height) ||
        !convertToUint64(cloud.row_step, rowStep) ||
        width > std::numeric_limits<std::size_t>::max() / pointStep ||
        rowStep != width * pointStep ||
        height > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(rowStep) ||
        cloud.data.size() != static_cast<std::size_t>(height) * static_cast<std::size_t>(rowStep)) {
        diagnosticMessage = "PCD point data length or row layout does not match its dimensions and point record size.";
        return false;
    }
    return true;
}

template <typename Value>
Value readValue(const std::uint8_t* source) noexcept {
    Value value{};
    std::memcpy(&value, source, sizeof(Value));
    return value;
}

bool readNumericScalar(const std::uint8_t* source, std::uint8_t datatype, double& value) noexcept {
    switch (datatype) {
    case pcl::PCLPointField::INT8: value = static_cast<double>(readValue<std::int8_t>(source)); return true;
    case pcl::PCLPointField::UINT8: value = static_cast<double>(readValue<std::uint8_t>(source)); return true;
    case pcl::PCLPointField::INT16: value = static_cast<double>(readValue<std::int16_t>(source)); return true;
    case pcl::PCLPointField::UINT16: value = static_cast<double>(readValue<std::uint16_t>(source)); return true;
    case pcl::PCLPointField::INT32: value = static_cast<double>(readValue<std::int32_t>(source)); return true;
    case pcl::PCLPointField::UINT32: value = static_cast<double>(readValue<std::uint32_t>(source)); return true;
    case pcl::PCLPointField::INT64: value = static_cast<double>(readValue<std::int64_t>(source)); return true;
    case pcl::PCLPointField::UINT64: value = static_cast<double>(readValue<std::uint64_t>(source)); return true;
    case pcl::PCLPointField::FLOAT32: value = static_cast<double>(readValue<float>(source)); return true;
    case pcl::PCLPointField::FLOAT64: value = readValue<double>(source); return true;
    default: return false;
    }
}

std::uint32_t readPackedColor(const std::uint8_t* source) noexcept {
    return readValue<std::uint32_t>(source);
}

std::uint32_t normalizePackedColor(std::uint32_t packed, bool hasAlpha) noexcept {
    const std::uint32_t red = (packed >> 16U) & 0xFFU;
    const std::uint32_t green = (packed >> 8U) & 0xFFU;
    const std::uint32_t blue = packed & 0xFFU;
    const std::uint32_t alpha = hasAlpha ? ((packed >> 24U) & 0xFFU) : 0xFFU;
    return (red << 24U) | (green << 16U) | (blue << 8U) | alpha;
}

} // namespace

class PcdReader::Impl final {
public:
    pcl::PCDReader reader;
    pcl::PCLPointCloud2 headerCloud;
    std::string sourcePath;
    AttributeSchema schema;
    std::uint64_t declaredPointCount{0U};
    std::uint64_t consumedSourcePoints{0U};
    std::vector<glm::dvec3> positions;
    std::vector<std::uint32_t> colors;
    std::vector<std::uint16_t> intensities;
    std::optional<Error> terminalError;
    std::size_t nextPoint{0U};
    int dataType{-1};
    bool isOpen{false};
    bool isConverted{false};
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
                corruptPcdMetadataError("pcl::PCDReader::readHeader() returned " + std::to_string(headerResult) + "."));
        }
        if (dataType != kPcdAsciiDataType && dataType != kPcdBinaryDataType) {
            return Result<PointCloudSourceInfo>::failure(corruptPcdMetadataError(
                "PCD DATA binary_compressed is not supported by PcdReader."));
        }

        PointCloudSourceInfo sourceInfo{};
        std::string validationDiagnostic;
        if (!validateAndBuildSchema(cloud, sourceInfo.schema, validationDiagnostic)) {
            return Result<PointCloudSourceInfo>::failure(corruptPcdMetadataError(std::move(validationDiagnostic)));
        }
        if (!declaredPointCount(cloud, sourceInfo.declaredPointCount)) {
            return Result<PointCloudSourceInfo>::failure(
                corruptPcdMetadataError("PCD header width and height cannot be represented as a uint64 point count."));
        }

        m_impl->headerCloud = std::move(cloud);
        m_impl->sourcePath = path;
        m_impl->schema = sourceInfo.schema;
        m_impl->declaredPointCount = sourceInfo.declaredPointCount;
        m_impl->dataType = dataType;
        m_impl->isOpen = true;
        return Result<PointCloudSourceInfo>::success(std::move(sourceInfo));
    } catch (const std::exception& exception) {
        return Result<PointCloudSourceInfo>::failure(
            corruptPcdMetadataError(std::string("pcl::PCDReader::readHeader() threw: ") + exception.what()));
    } catch (...) {
        return Result<PointCloudSourceInfo>::failure(
            corruptPcdMetadataError("pcl::PCDReader::readHeader() threw an unknown exception."));
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
        return Result<std::optional<PointBatch>>::failure(cancelledError());
    }
    if (!m_impl->isConverted && m_impl->declaredPointCount == 0U) {
        m_impl->isConverted = true;
        return Result<std::optional<PointBatch>>::success(std::nullopt);
    }
    if (m_impl->terminalError.has_value()) {
        return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
    }

    if (!m_impl->isConverted) {
        try {
            pcl::PCLPointCloud2 cloud;
            const int readResult = m_impl->reader.read(m_impl->sourcePath, cloud);
            if (readResult != 0) {
                m_impl->terminalError = corruptPcdDataError(
                    "pcl::PCDReader::read() returned " + std::to_string(readResult) + ".");
                return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
            }
            if (token.isCancellationRequested()) {
                return Result<std::optional<PointBatch>>::failure(cancelledError());
            }

            AttributeSchema schema;
            const pcl::PCLPointField* x = nullptr;
            const pcl::PCLPointField* y = nullptr;
            const pcl::PCLPointField* z = nullptr;
            const pcl::PCLPointField* color = nullptr;
            const pcl::PCLPointField* intensity = nullptr;
            bool colorHasAlpha = false;
            std::string validationDiagnostic;
            if (!validateReadableCloud(
                    cloud,
                    m_impl->declaredPointCount,
                    schema,
                    x,
                    y,
                    z,
                    color,
                    colorHasAlpha,
                    intensity,
                    validationDiagnostic)) {
                m_impl->terminalError = corruptPcdDataError(std::move(validationDiagnostic));
                return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
            }
            if (schema.mask != m_impl->schema.mask) {
                m_impl->terminalError = corruptPcdDataError(
                    "PCD body fields do not match the attribute schema declared by its header.");
                return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
            }

            std::vector<glm::dvec3> positions;
            std::vector<std::uint32_t> colors;
            std::vector<double> intensityValues;
            const std::size_t pointCount = static_cast<std::size_t>(m_impl->declaredPointCount);
            positions.reserve(pointCount);
            if (color != nullptr) {
                colors.reserve(pointCount);
            }
            if (intensity != nullptr) {
                intensityValues.reserve(pointCount);
            }

            const std::size_t pointStep = static_cast<std::size_t>(cloud.point_step);
            for (std::size_t index = 0U; index < pointCount; ++index) {
                if (token.isCancellationRequested()) {
                    return Result<std::optional<PointBatch>>::failure(cancelledError());
                }
                const std::uint8_t* point = cloud.data.data() + index * pointStep;
                double xValue = 0.0;
                double yValue = 0.0;
                double zValue = 0.0;
                if (!readNumericScalar(point + static_cast<std::size_t>(x->offset), x->datatype, xValue) ||
                    !readNumericScalar(point + static_cast<std::size_t>(y->offset), y->datatype, yValue) ||
                    !readNumericScalar(point + static_cast<std::size_t>(z->offset), z->datatype, zValue)) {
                    m_impl->terminalError = corruptPcdDataError("PCD coordinate field datatype cannot be decoded.");
                    return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
                }
                if (!std::isfinite(xValue) || !std::isfinite(yValue) || !std::isfinite(zValue)) {
                    continue;
                }

                positions.emplace_back(xValue, yValue, zValue);
                if (color != nullptr) {
                    colors.push_back(normalizePackedColor(
                        readPackedColor(point + static_cast<std::size_t>(color->offset)), colorHasAlpha));
                }
                if (intensity != nullptr) {
                    double intensityValue = 0.0;
                    if (!readNumericScalar(
                            point + static_cast<std::size_t>(intensity->offset),
                            intensity->datatype,
                            intensityValue)) {
                        m_impl->terminalError = corruptPcdDataError(
                            "PCD intensity field datatype cannot be decoded.");
                        return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
                    }
                    intensityValues.push_back(intensityValue);
                }
            }

            if (m_impl->declaredPointCount != 0U && positions.empty()) {
                m_impl->terminalError = corruptPcdDataError(
                    "All declared PCD points have non-finite coordinates.");
                return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
            }

            std::vector<std::uint16_t> intensities;
            if (intensity != nullptr) {
                const Result<IntensityQuantizationResult> quantized =
                    IntensityQuantizer::quantize(intensityValues);
                if (!quantized.hasValue()) {
                    m_impl->terminalError = corruptPcdDataError(
                        "PCD intensity quantization failed: " + quantized.error().diagnosticMessage);
                    return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
                }
                intensities = quantized.value().values;
            }
            if (token.isCancellationRequested()) {
                return Result<std::optional<PointBatch>>::failure(cancelledError());
            }

            m_impl->positions = std::move(positions);
            m_impl->colors = std::move(colors);
            m_impl->intensities = std::move(intensities);
            m_impl->consumedSourcePoints = m_impl->declaredPointCount;
            m_impl->isConverted = true;
        } catch (const std::exception& exception) {
            m_impl->terminalError = corruptPcdDataError(
                std::string("PCD data loading or conversion threw: ") + exception.what());
            return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
        } catch (...) {
            m_impl->terminalError = corruptPcdDataError("PCD data loading or conversion threw an unknown exception.");
            return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
        }
    }

    if (m_impl->nextPoint >= m_impl->positions.size()) {
        return Result<std::optional<PointBatch>>::success(std::nullopt);
    }

    const std::size_t remaining = m_impl->positions.size() - m_impl->nextPoint;
    const std::size_t count = remaining < maximumPoints ? remaining : maximumPoints;
    PointBatch batch{};
    batch.schema = m_impl->schema;
    const std::size_t end = m_impl->nextPoint + count;
    batch.positions.assign(
        m_impl->positions.begin() + static_cast<std::ptrdiff_t>(m_impl->nextPoint),
        m_impl->positions.begin() + static_cast<std::ptrdiff_t>(end));
    if (batch.schema.hasColor()) {
        batch.colorsRgba8.assign(
            m_impl->colors.begin() + static_cast<std::ptrdiff_t>(m_impl->nextPoint),
            m_impl->colors.begin() + static_cast<std::ptrdiff_t>(end));
    }
    if (batch.schema.hasIntensity()) {
        batch.intensities.assign(
            m_impl->intensities.begin() + static_cast<std::ptrdiff_t>(m_impl->nextPoint),
            m_impl->intensities.begin() + static_cast<std::ptrdiff_t>(end));
    }
    const Result<void> validation = batch.validate();
    if (!validation.hasValue()) {
        m_impl->terminalError = corruptPcdDataError(
            "PcdReader produced an invalid batch: " + validation.error().diagnosticMessage);
        return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
    }
    m_impl->nextPoint = end;
    return Result<std::optional<PointBatch>>::success(std::optional<PointBatch>{std::move(batch)});
}

Result<PointCloudReadProgress> PcdReader::readProgress() const {
    if (!m_impl->isOpen) {
        return Result<PointCloudReadProgress>::failure(makeError(
            ErrorDomain::Task,
            kInvalidTaskCode,
            "Point cloud reader is not open.",
            "readProgress() requires a successfully opened PCD source.",
            "PcdReader::readProgress"));
    }

    PointCloudReadProgress progress;
    progress.consumedSourcePoints = m_impl->consumedSourcePoints;
    progress.totalSourcePoints = m_impl->declaredPointCount;
    return Result<PointCloudReadProgress>::success(std::move(progress));
}

void PcdReader::close() noexcept {
    m_impl->headerCloud = pcl::PCLPointCloud2{};
    m_impl->sourcePath.clear();
    m_impl->schema = {};
    m_impl->declaredPointCount = 0U;
    m_impl->consumedSourcePoints = 0U;
    m_impl->positions.clear();
    m_impl->colors.clear();
    m_impl->intensities.clear();
    m_impl->terminalError.reset();
    m_impl->nextPoint = 0U;
    m_impl->dataType = -1;
    m_impl->isOpen = false;
    m_impl->isConverted = false;
}

} // namespace dzc
