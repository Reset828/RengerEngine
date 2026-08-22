#include "data/io/pcl/PlyReader.h"

#include "data/chunk/IntensityQuantizer.h"

#include <dzc/Error.h>

#include <pcl/PCLPointCloud2.h>
#include <pcl/PCLPointField.h>
#include <pcl/io/ply_io.h>

#include <Eigen/Geometry>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace dzc {
namespace {

constexpr std::uint32_t kInvalidValueCode = 1U;
constexpr std::uint32_t kInternalErrorCode = 1U;
constexpr std::uint32_t kCorruptDataCode = 2U;
constexpr std::uint32_t kInvalidTaskCode = 1U;
constexpr std::uint32_t kCancelledCode = 7U;

Error makeError(
    ErrorDomain domain,
    std::uint32_t code,
    std::string userMessage,
    std::string diagnosticMessage,
    std::string context) {
    return Error{domain, code, std::move(userMessage), std::move(diagnosticMessage), std::move(context)};
}

Error corruptPlyMetadataError(std::string diagnosticMessage) {
    return makeError(
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "The PLY source metadata is invalid or cannot be read.",
        std::move(diagnosticMessage),
        "PlyReader::open");
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

bool isNumericPlyType(const std::string& type) noexcept {
    return type == "char" || type == "int8" ||
           type == "uchar" || type == "uint8" ||
           type == "short" || type == "int16" ||
           type == "ushort" || type == "uint16" ||
           type == "int" || type == "int32" ||
           type == "uint" || type == "uint32" ||
           type == "long" || type == "int64" ||
           type == "ulong" || type == "uint64" ||
           type == "float" || type == "float32" ||
           type == "double" || type == "float64";
}

bool isUnsignedBytePlyType(const std::string& type) noexcept {
    return type == "uchar" || type == "uint8";
}

struct StandardProperty final {
    std::size_t declarations{0U};
    bool isValid{true};
};

struct PlyHeaderValidation final {
    StandardProperty x;
    StandardProperty y;
    StandardProperty z;
    StandardProperty red;
    StandardProperty green;
    StandardProperty blue;
    StandardProperty alpha;
    StandardProperty intensity;
    bool hasSupportedFormat{false};
};

StandardProperty* standardPropertyForName(
    PlyHeaderValidation& validation,
    const std::string& name) noexcept {
    if (name == "x") {
        return &validation.x;
    }
    if (name == "y") {
        return &validation.y;
    }
    if (name == "z") {
        return &validation.z;
    }
    if (name == "red") {
        return &validation.red;
    }
    if (name == "green") {
        return &validation.green;
    }
    if (name == "blue") {
        return &validation.blue;
    }
    if (name == "alpha") {
        return &validation.alpha;
    }
    if (name == "intensity") {
        return &validation.intensity;
    }
    return nullptr;
}

bool recordVertexProperty(
    PlyHeaderValidation& validation,
    const std::vector<std::string>& tokens,
    std::string& diagnosticMessage) {
    if (tokens.size() < 3U || tokens[0] != "property") {
        return true;
    }

    const bool isList = tokens[1] == "list";
    const std::string& name = isList
        ? (tokens.size() == 5U ? tokens[4] : tokens.back())
        : tokens[2];
    StandardProperty* property = standardPropertyForName(validation, name);
    if (property == nullptr) {
        return true;
    }

    ++property->declarations;
    if (isList || tokens.size() != 3U) {
        property->isValid = false;
        diagnosticMessage = "PLY standard property '" + name + "' must be declared as a scalar.";
        return true;
    }

    const bool isColor = name == "red" || name == "green" || name == "blue" || name == "alpha";
    const bool isValidType = isColor ? isUnsignedBytePlyType(tokens[1]) : isNumericPlyType(tokens[1]);
    if (!isValidType) {
        property->isValid = false;
        diagnosticMessage = "PLY standard property '" + name + "' has an unsupported scalar type '" + tokens[1] + "'.";
    }
    return true;
}

bool inspectPlyHeader(
    const std::string& path,
    PlyHeaderValidation& validation,
    std::string& diagnosticMessage) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        diagnosticMessage = "The PLY header could not be opened for validation.";
        return false;
    }

    std::string line;
    bool inVertexElement = false;
    bool foundEndHeader = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream stream(line);
        std::vector<std::string> tokens;
        std::string token;
        while (stream >> token) {
            tokens.push_back(std::move(token));
        }
        if (tokens.empty() || tokens[0] == "comment" || tokens[0] == "obj_info") {
            continue;
        }
        if (tokens[0] == "end_header") {
            foundEndHeader = true;
            break;
        }
        if (tokens[0] == "format") {
            validation.hasSupportedFormat = tokens.size() >= 2U &&
                                            (tokens[1] == "ascii" || tokens[1] == "binary_little_endian");
            continue;
        }
        if (tokens[0] == "element") {
            inVertexElement = tokens.size() >= 2U && tokens[1] == "vertex";
            continue;
        }
        if (inVertexElement && tokens[0] == "property") {
            recordVertexProperty(validation, tokens, diagnosticMessage);
        }
    }

    if (!foundEndHeader) {
        diagnosticMessage = "The PLY header does not contain end_header.";
        return false;
    }
    if (!validation.hasSupportedFormat) {
        diagnosticMessage = "PLY supports only ascii and binary_little_endian encodings.";
        return false;
    }
    return true;
}

bool hasExactlyOneValidProperty(const StandardProperty& property) noexcept {
    return property.declarations == 1U && property.isValid;
}

bool validateRawStandardProperties(
    const PlyHeaderValidation& validation,
    bool& hasColor,
    bool& hasAlpha,
    bool& hasIntensity,
    std::string& diagnosticMessage) {
    if (!hasExactlyOneValidProperty(validation.x) ||
        !hasExactlyOneValidProperty(validation.y) ||
        !hasExactlyOneValidProperty(validation.z)) {
        diagnosticMessage = "PLY vertex properties must contain exactly one numeric scalar x, y, and z.";
        return false;
    }

    const bool anyColor = validation.red.declarations != 0U ||
                          validation.green.declarations != 0U ||
                          validation.blue.declarations != 0U ||
                          validation.alpha.declarations != 0U;
    hasColor = validation.red.declarations != 0U ||
               validation.green.declarations != 0U ||
               validation.blue.declarations != 0U;
    hasAlpha = validation.alpha.declarations != 0U;
    if (anyColor &&
        (!hasExactlyOneValidProperty(validation.red) ||
         !hasExactlyOneValidProperty(validation.green) ||
         !hasExactlyOneValidProperty(validation.blue) ||
         (hasAlpha && !hasExactlyOneValidProperty(validation.alpha)))) {
        diagnosticMessage = "PLY color properties must contain unique uint8 red, green, and blue, with optional unique uint8 alpha.";
        return false;
    }
    if (hasAlpha && !hasColor) {
        diagnosticMessage = "PLY alpha requires complete red, green, and blue properties.";
        return false;
    }

    hasIntensity = validation.intensity.declarations != 0U;
    if (hasIntensity && !hasExactlyOneValidProperty(validation.intensity)) {
        diagnosticMessage = "PLY intensity must be a unique numeric scalar property.";
        return false;
    }
    return true;
}

const pcl::PCLPointField* findUniqueField(
    const pcl::PCLPointCloud2& cloud,
    const std::string& name) noexcept {
    const pcl::PCLPointField* match = nullptr;
    for (const pcl::PCLPointField& field : cloud.fields) {
        if (field.name != name) {
            continue;
        }
        if (match != nullptr) {
            return nullptr;
        }
        match = &field;
    }
    return match;
}

bool isValidPclColorField(const pcl::PCLPointField& field, bool hasAlpha) noexcept {
    return field.count == 1U &&
           field.datatype == (hasAlpha ? pcl::PCLPointField::UINT32 : pcl::PCLPointField::FLOAT32);
}

bool validatePclMetadata(
    const pcl::PCLPointCloud2& cloud,
    bool hasColor,
    bool hasAlpha,
    bool hasIntensity,
    AttributeSchema& schema,
    std::string& diagnosticMessage) {
    for (const char* name : {"x", "y", "z"}) {
        const pcl::PCLPointField* field = findUniqueField(cloud, name);
        if (field == nullptr || field->count != 1U || !isNumericScalar(field->datatype)) {
            diagnosticMessage = "PCL did not expose a valid numeric scalar PLY coordinate field '" + std::string(name) + "'.";
            return false;
        }
    }
    schema.mask = static_cast<std::uint32_t>(PointAttribute::Position);

    if (hasColor) {
        const pcl::PCLPointField* colorField = findUniqueField(cloud, hasAlpha ? "rgba" : "rgb");
        if (colorField == nullptr || !isValidPclColorField(*colorField, hasAlpha)) {
            diagnosticMessage = "PCL did not expose the validated PLY color components as the expected packed color field.";
            return false;
        }
        schema.mask |= static_cast<std::uint32_t>(PointAttribute::Color);
    }

    if (hasIntensity) {
        const pcl::PCLPointField* intensityField = findUniqueField(cloud, "intensity");
        if (intensityField == nullptr || intensityField->count != 1U || !isNumericScalar(intensityField->datatype)) {
            diagnosticMessage = "PCL did not expose a valid numeric scalar PLY intensity field.";
            return false;
        }
        schema.mask |= static_cast<std::uint32_t>(PointAttribute::Intensity);
    }
    return true;
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
    if (height != 0U && width > std::numeric_limits<std::uint64_t>::max() / height) {
        return false;
    }
    pointCount = width * height;
    return true;
}

Error corruptPlyDataError(std::string diagnosticMessage) {
    return makeError(
        ErrorDomain::DataFormat,
        kCorruptDataCode,
        "The PLY source data is invalid or cannot be read.",
        std::move(diagnosticMessage),
        "PlyReader::readNext");
}

Error cancelledError() {
    return makeError(
        ErrorDomain::Task,
        kCancelledCode,
        "Point cloud read cancelled.",
        "PlyReader::readNext() observed a requested cancellation.",
        "PlyReader::readNext");
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

bool validateFieldFitsPoint(
    const pcl::PCLPointField& field,
    std::size_t pointStep,
    std::string& diagnosticMessage) {
    const std::size_t size = datatypeSize(field.datatype);
    std::uint64_t offset = 0U;
    if (size == 0U || !convertToUint64(field.offset, offset) ||
        offset > pointStep || size > pointStep - static_cast<std::size_t>(offset)) {
        diagnosticMessage = "PLY field offset or datatype exceeds the point record size.";
        return false;
    }
    return true;
}

bool validateReadableCloud(
    const pcl::PCLPointCloud2& cloud,
    std::uint64_t expectedPointCount,
    bool hasColor,
    bool colorHasAlpha,
    bool hasIntensity,
    AttributeSchema& schema,
    const pcl::PCLPointField*& x,
    const pcl::PCLPointField*& y,
    const pcl::PCLPointField*& z,
    const pcl::PCLPointField*& color,
    const pcl::PCLPointField*& intensity,
    std::string& diagnosticMessage) {
    std::uint64_t actualPointCount = 0U;
    if (!declaredPointCount(cloud, actualPointCount) || actualPointCount != expectedPointCount) {
        diagnosticMessage = "PLY body dimensions do not match the point count declared by its header.";
        return false;
    }
    if (!validatePclMetadata(cloud, hasColor, colorHasAlpha, hasIntensity, schema, diagnosticMessage)) {
        return false;
    }

    x = findUniqueField(cloud, "x");
    y = findUniqueField(cloud, "y");
    z = findUniqueField(cloud, "z");
    color = hasColor ? findUniqueField(cloud, colorHasAlpha ? "rgba" : "rgb") : nullptr;
    intensity = hasIntensity ? findUniqueField(cloud, "intensity") : nullptr;
    if (x == nullptr || y == nullptr || z == nullptr ||
        (hasColor && color == nullptr) || (hasIntensity && intensity == nullptr)) {
        diagnosticMessage = "PLY body field definitions are incomplete or invalid.";
        return false;
    }

    std::uint64_t pointStepValue = 0U;
    if (!convertToUint64(cloud.point_step, pointStepValue) || pointStepValue == 0U ||
        pointStepValue > std::numeric_limits<std::size_t>::max()) {
        diagnosticMessage = "PLY point record size is invalid.";
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
        diagnosticMessage = "PLY point data length or row layout does not match its dimensions and point record size.";
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

std::uint32_t normalizePackedColor(std::uint32_t packed, bool hasAlpha) noexcept {
    const std::uint32_t red = (packed >> 16U) & 0xFFU;
    const std::uint32_t green = (packed >> 8U) & 0xFFU;
    const std::uint32_t blue = packed & 0xFFU;
    const std::uint32_t alpha = hasAlpha ? ((packed >> 24U) & 0xFFU) : 0xFFU;
    return (red << 24U) | (green << 16U) | (blue << 8U) | alpha;
}

} // namespace

class PlyReader::Impl final {
public:
    pcl::PLYReader reader;
    std::string sourcePath;
    AttributeSchema schema;
    std::uint64_t declaredPointCount{0U};
    std::vector<glm::dvec3> positions;
    std::vector<std::uint32_t> colors;
    std::vector<std::uint16_t> intensities;
    std::optional<Error> terminalError;
    std::size_t nextPoint{0U};
    bool hasColor{false};
    bool colorHasAlpha{false};
    bool hasIntensity{false};
    bool isOpen{false};
    bool isConverted{false};
};

PlyReader::PlyReader()
    : m_impl(std::make_unique<Impl>()) {}

PlyReader::~PlyReader() = default;

Result<PointCloudSourceInfo> PlyReader::open(const std::string& path) {
    if (m_impl->isOpen) {
        return Result<PointCloudSourceInfo>::failure(makeError(
            ErrorDomain::Task,
            kInvalidTaskCode,
            "Point cloud reader is already open.",
            "open() was called while PlyReader already has an opened source.",
            "PlyReader::open"));
    }

    try {
        pcl::PCLPointCloud2 cloud;
        Eigen::Vector4f origin;
        Eigen::Quaternionf orientation;
        int plyVersion = 0;
        int dataType = 0;
        unsigned int dataOffset = 0U;
        const int headerResult = m_impl->reader.readHeader(
            path, cloud, origin, orientation, plyVersion, dataType, dataOffset);
        if (headerResult < 0) {
            return Result<PointCloudSourceInfo>::failure(
                corruptPlyMetadataError("pcl::PLYReader::readHeader() returned " + std::to_string(headerResult) + "."));
        }

        PlyHeaderValidation headerValidation;
        std::string validationDiagnostic;
        if (!inspectPlyHeader(path, headerValidation, validationDiagnostic)) {
            return Result<PointCloudSourceInfo>::failure(corruptPlyMetadataError(std::move(validationDiagnostic)));
        }

        bool hasColor = false;
        bool hasAlpha = false;
        bool hasIntensity = false;
        if (!validateRawStandardProperties(
                headerValidation,
                hasColor,
                hasAlpha,
                hasIntensity,
                validationDiagnostic)) {
            return Result<PointCloudSourceInfo>::failure(corruptPlyMetadataError(std::move(validationDiagnostic)));
        }

        PointCloudSourceInfo sourceInfo{};
        if (!validatePclMetadata(
                cloud,
                hasColor,
                hasAlpha,
                hasIntensity,
                sourceInfo.schema,
                validationDiagnostic)) {
            return Result<PointCloudSourceInfo>::failure(corruptPlyMetadataError(std::move(validationDiagnostic)));
        }
        if (!declaredPointCount(cloud, sourceInfo.declaredPointCount)) {
            return Result<PointCloudSourceInfo>::failure(corruptPlyMetadataError(
                "PLY header width and height cannot be represented as a uint64 point count."));
        }

        m_impl->sourcePath = path;
        m_impl->schema = sourceInfo.schema;
        m_impl->declaredPointCount = sourceInfo.declaredPointCount;
        m_impl->hasColor = hasColor;
        m_impl->colorHasAlpha = hasAlpha;
        m_impl->hasIntensity = hasIntensity;
        m_impl->isOpen = true;
        return Result<PointCloudSourceInfo>::success(std::move(sourceInfo));
    } catch (const std::exception& exception) {
        return Result<PointCloudSourceInfo>::failure(
            corruptPlyMetadataError(std::string("pcl::PLYReader::readHeader() threw: ") + exception.what()));
    } catch (...) {
        return Result<PointCloudSourceInfo>::failure(
            corruptPlyMetadataError("pcl::PLYReader::readHeader() threw an unknown exception."));
    }
}

Result<std::optional<PointBatch>> PlyReader::readNext(
    std::size_t maximumPoints,
    tasks::CancellationToken token) {
    if (!m_impl->isOpen) {
        return Result<std::optional<PointBatch>>::failure(makeError(
            ErrorDomain::Task,
            kInvalidTaskCode,
            "Point cloud reader is not open.",
            "readNext() requires a successfully opened PLY source.",
            "PlyReader::readNext"));
    }
    if (maximumPoints == 0U) {
        return Result<std::optional<PointBatch>>::failure(makeError(
            ErrorDomain::Configuration,
            kInvalidValueCode,
            "Maximum point count must be greater than zero.",
            "readNext() received maximumPoints equal to zero.",
            "PlyReader::readNext"));
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
            if (readResult < 0) {
                m_impl->terminalError = corruptPlyDataError(
                    "pcl::PLYReader::read() returned " + std::to_string(readResult) + ".");
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
            std::string validationDiagnostic;
            if (!validateReadableCloud(
                    cloud,
                    m_impl->declaredPointCount,
                    m_impl->hasColor,
                    m_impl->colorHasAlpha,
                    m_impl->hasIntensity,
                    schema,
                    x,
                    y,
                    z,
                    color,
                    intensity,
                    validationDiagnostic)) {
                m_impl->terminalError = corruptPlyDataError(std::move(validationDiagnostic));
                return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
            }
            if (schema.mask != m_impl->schema.mask) {
                m_impl->terminalError = corruptPlyDataError(
                    "PLY body fields do not match the attribute schema declared by its header.");
                return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
            }

            const std::size_t pointCount = static_cast<std::size_t>(m_impl->declaredPointCount);
            const std::size_t pointStep = static_cast<std::size_t>(cloud.point_step);
            std::vector<glm::dvec3> positions;
            std::vector<std::uint32_t> colors;
            std::vector<double> intensityValues;
            positions.reserve(pointCount);
            if (color != nullptr) {
                colors.reserve(pointCount);
            }
            if (intensity != nullptr) {
                intensityValues.reserve(pointCount);
            }

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
                    m_impl->terminalError = corruptPlyDataError("PLY coordinate field datatype cannot be decoded.");
                    return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
                }
                if (!std::isfinite(xValue) || !std::isfinite(yValue) || !std::isfinite(zValue)) {
                    continue;
                }

                positions.emplace_back(xValue, yValue, zValue);
                if (color != nullptr) {
                    colors.push_back(normalizePackedColor(
                        readValue<std::uint32_t>(point + static_cast<std::size_t>(color->offset)),
                        m_impl->colorHasAlpha));
                }
                if (intensity != nullptr) {
                    double intensityValue = 0.0;
                    if (!readNumericScalar(
                            point + static_cast<std::size_t>(intensity->offset),
                            intensity->datatype,
                            intensityValue)) {
                        m_impl->terminalError = corruptPlyDataError(
                            "PLY intensity field datatype cannot be decoded.");
                        return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
                    }
                    intensityValues.push_back(intensityValue);
                }
            }

            if (m_impl->declaredPointCount != 0U && positions.empty()) {
                m_impl->terminalError = corruptPlyDataError(
                    "All declared PLY points have non-finite coordinates.");
                return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
            }

            std::vector<std::uint16_t> intensities;
            if (intensity != nullptr) {
                const Result<IntensityQuantizationResult> quantized =
                    IntensityQuantizer::quantize(intensityValues);
                if (!quantized.hasValue()) {
                    m_impl->terminalError = corruptPlyDataError(
                        "PLY intensity quantization failed: " + quantized.error().diagnosticMessage);
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
            m_impl->isConverted = true;
        } catch (const std::exception& exception) {
            m_impl->terminalError = corruptPlyDataError(
                std::string("PLY data loading or conversion threw: ") + exception.what());
            return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
        } catch (...) {
            m_impl->terminalError = corruptPlyDataError("PLY data loading or conversion threw an unknown exception.");
            return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
        }
    }

    if (m_impl->nextPoint >= m_impl->positions.size()) {
        return Result<std::optional<PointBatch>>::success(std::nullopt);
    }

    const std::size_t remaining = m_impl->positions.size() - m_impl->nextPoint;
    const std::size_t count = remaining < maximumPoints ? remaining : maximumPoints;
    const std::size_t end = m_impl->nextPoint + count;
    PointBatch batch{};
    batch.schema = m_impl->schema;
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
        m_impl->terminalError = corruptPlyDataError(
            "PlyReader produced an invalid batch: " + validation.error().diagnosticMessage);
        return Result<std::optional<PointBatch>>::failure(*m_impl->terminalError);
    }
    m_impl->nextPoint = end;
    return Result<std::optional<PointBatch>>::success(std::optional<PointBatch>{std::move(batch)});
}

void PlyReader::close() noexcept {
    m_impl->sourcePath.clear();
    m_impl->schema = {};
    m_impl->declaredPointCount = 0U;
    m_impl->positions.clear();
    m_impl->colors.clear();
    m_impl->intensities.clear();
    m_impl->terminalError.reset();
    m_impl->nextPoint = 0U;
    m_impl->hasColor = false;
    m_impl->colorHasAlpha = false;
    m_impl->hasIntensity = false;
    m_impl->isOpen = false;
    m_impl->isConverted = false;
}

} // namespace dzc
