#include <dzc/EngineTypes.h>

#include <cassert>
#include <string>
#include <type_traits>

namespace {

void testDefaultValues() {
    const dzc::DatasetId datasetId;
    const dzc::ChunkId chunkId;
    const dzc::FrameId frameId;
    const dzc::TaskId taskId;
    const dzc::RenderSize size;
    const dzc::ColorRgba color;

    assert(datasetId.value == 0);
    assert(chunkId.value == 0);
    assert(frameId.value == 0);
    assert(taskId.value == 0);
    assert(size.width == 0);
    assert(size.height == 0);
    assert(size.devicePixelRatio == 1.0F);
    assert(color.red == 0.0F);
    assert(color.green == 0.0F);
    assert(color.blue == 0.0F);
    assert(color.alpha == 1.0F);
}

void testComparisons() {
    assert(dzc::DatasetId{1} == dzc::DatasetId{1});
    assert(dzc::DatasetId{1} != dzc::DatasetId{2});
    assert(dzc::ChunkId{3} == dzc::ChunkId{3});
    assert(dzc::FrameId{4} != dzc::FrameId{5});
    assert(dzc::TaskId{6} == dzc::TaskId{6});

    const dzc::RenderSize firstSize{1920, 1080, 1.0F};
    const dzc::RenderSize sameSize{1920, 1080, 1.0F};
    const dzc::RenderSize differentSize{1280, 720, 1.0F};
    assert(firstSize == sameSize);
    assert(firstSize != differentSize);

    const dzc::ColorRgba firstColor{1.0F, 0.5F, 0.25F, 1.0F};
    const dzc::ColorRgba sameColor{1.0F, 0.5F, 0.25F, 1.0F};
    const dzc::ColorRgba differentColor{0.0F, 0.5F, 0.25F, 1.0F};
    assert(firstColor == sameColor);
    assert(firstColor != differentColor);
}

void testStrongTyping() {
    static_assert(!std::is_convertible_v<dzc::DatasetId, dzc::ChunkId>);
    static_assert(!std::is_convertible_v<dzc::ChunkId, dzc::FrameId>);
    static_assert(!std::is_convertible_v<dzc::FrameId, dzc::TaskId>);
    static_assert(!std::is_convertible_v<dzc::TaskId, dzc::DatasetId>);
    static_assert(!std::is_convertible_v<std::uint64_t, dzc::DatasetId>);
    static_assert(!std::is_convertible_v<dzc::DatasetId, std::uint64_t>);
}

void testUtf8Path() {
    const std::string path = u8"数据/点云.dzcpc";
    assert(!path.empty());
}

} // namespace

int main() {
    testDefaultValues();
    testComparisons();
    testStrongTyping();
    testUtf8Path();
    return 0;
}