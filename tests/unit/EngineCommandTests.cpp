#include "dzc/EngineCommand.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace {

using dzc::EngineCommand;

template <typename Command>
void verifyValueTypeSemantics() {
    static_assert(std::is_default_constructible_v<Command>);
    static_assert(std::is_move_constructible_v<Command>);
    static_assert(std::is_move_assignable_v<Command>);

    Command source{};
    Command moved{std::move(source)};
    Command assigned{};
    assigned = std::move(moved);
}

void testCommandAlternatives() {
    static_assert(std::variant_size_v<EngineCommand> == 11U);
    static_assert(std::is_default_constructible_v<EngineCommand>);
    static_assert(std::is_move_constructible_v<EngineCommand>);
    static_assert(std::is_move_assignable_v<EngineCommand>);
    static_assert(!std::is_constructible_v<EngineCommand, int>);
    static_assert(!std::is_constructible_v<EngineCommand, std::string>);

    verifyValueTypeSemantics<dzc::LoadDatasetCommand>();
    verifyValueTypeSemantics<dzc::CancelDatasetLoadCommand>();
    verifyValueTypeSemantics<dzc::UnloadDatasetCommand>();
    verifyValueTypeSemantics<dzc::SetPointSizeCommand>();
    verifyValueTypeSemantics<dzc::SetShadingModeCommand>();
    verifyValueTypeSemantics<dzc::SetFixedColorCommand>();
    verifyValueTypeSemantics<dzc::SetBackgroundColorCommand>();
    verifyValueTypeSemantics<dzc::SetCudaModeCommand>();
    verifyValueTypeSemantics<dzc::ResetViewCommand>();
    verifyValueTypeSemantics<dzc::ResizeCommand>();
    verifyValueTypeSemantics<dzc::ShutdownCommand>();

    const dzc::LoadDatasetCommand load{"clouds/data.dzcpc"};
    const dzc::CancelDatasetLoadCommand cancel{{42U}};
    const dzc::UnloadDatasetCommand unload{{43U}};
    const dzc::SetPointSizeCommand pointSize{4.0F};
    const dzc::SetShadingModeCommand shading{dzc::ShadingMode::Height};
    const dzc::SetFixedColorCommand fixedColor{{1.0F, 0.5F, 0.25F, 1.0F}};
    const dzc::SetBackgroundColorCommand backgroundColor{{0.1F, 0.2F, 0.3F, 1.0F}};
    const dzc::SetCudaModeCommand cuda{dzc::OptionalFeatureMode::On};
    const dzc::ResetViewCommand reset{};
    const dzc::ResizeCommand resize{{1920U, 1080U, 2.0F}};
    const dzc::ShutdownCommand shutdown{};

    assert(std::get<dzc::LoadDatasetCommand>(EngineCommand{load}).path == load.path);
    assert(std::get<dzc::CancelDatasetLoadCommand>(EngineCommand{cancel}).datasetId == cancel.datasetId);
    assert(std::get<dzc::UnloadDatasetCommand>(EngineCommand{unload}).datasetId == unload.datasetId);
    assert(std::get<dzc::SetPointSizeCommand>(EngineCommand{pointSize}).pixels == pointSize.pixels);
    assert(std::get<dzc::SetShadingModeCommand>(EngineCommand{shading}).mode == shading.mode);
    assert(std::get<dzc::SetFixedColorCommand>(EngineCommand{fixedColor}).color == fixedColor.color);
    assert(std::get<dzc::SetBackgroundColorCommand>(EngineCommand{backgroundColor}).color == backgroundColor.color);
    assert(std::get<dzc::SetCudaModeCommand>(EngineCommand{cuda}).mode == cuda.mode);
    assert(std::holds_alternative<dzc::ResetViewCommand>(EngineCommand{reset}));
    assert(std::get<dzc::ResizeCommand>(EngineCommand{resize}).size == resize.size);
    assert(std::holds_alternative<dzc::ShutdownCommand>(EngineCommand{shutdown}));

    EngineCommand movable{load};
    EngineCommand moved{std::move(movable)};
    EngineCommand assigned{dzc::ShutdownCommand{}};
    assigned = std::move(moved);
    assert(std::get<dzc::LoadDatasetCommand>(assigned).path == load.path);
}

void testDefaultValues() {
    assert(dzc::LoadDatasetCommand{}.path.empty());
    assert(dzc::CancelDatasetLoadCommand{}.datasetId == dzc::DatasetId{});
    assert(dzc::UnloadDatasetCommand{}.datasetId == dzc::DatasetId{});
    assert(dzc::SetPointSizeCommand{}.pixels == 1.0F);
    assert(dzc::SetShadingModeCommand{}.mode == dzc::ShadingMode::OriginalColor);
    assert(dzc::SetFixedColorCommand{}.color == dzc::ColorRgba{});
    assert(dzc::SetBackgroundColorCommand{}.color == dzc::ColorRgba{});
    assert(dzc::SetCudaModeCommand{}.mode == dzc::OptionalFeatureMode::Auto);
    assert(dzc::ResizeCommand{}.size == dzc::RenderSize{});
}

void testValidCommands() {
    assert(dzc::isValidEngineCommand(EngineCommand{dzc::LoadDatasetCommand{"clouds/data.dzcpc"}}));
    assert(dzc::isValidEngineCommand(EngineCommand{dzc::LoadDatasetCommand{u8"点云/数据.dzcpc"}}));
    assert(dzc::isValidEngineCommand(EngineCommand{dzc::SetPointSizeCommand{1.0F}}));
    assert(dzc::isValidEngineCommand(EngineCommand{dzc::SetPointSizeCommand{64.0F}}));

    assert(dzc::isValidEngineCommand(EngineCommand{dzc::CancelDatasetLoadCommand{{1U}}}));
    assert(dzc::isValidEngineCommand(EngineCommand{dzc::UnloadDatasetCommand{{2U}}}));
    assert(dzc::isValidEngineCommand(EngineCommand{dzc::SetShadingModeCommand{}}));
    assert(dzc::isValidEngineCommand(EngineCommand{dzc::SetFixedColorCommand{}}));
    assert(dzc::isValidEngineCommand(EngineCommand{dzc::SetBackgroundColorCommand{}}));
    assert(dzc::isValidEngineCommand(EngineCommand{dzc::SetCudaModeCommand{}}));
    assert(dzc::isValidEngineCommand(EngineCommand{dzc::ResetViewCommand{}}));
    assert(dzc::isValidEngineCommand(EngineCommand{dzc::ResizeCommand{}}));
    assert(dzc::isValidEngineCommand(EngineCommand{dzc::ShutdownCommand{}}));
}

void testInvalidDatasetPaths() {
    assert(!dzc::isValidEngineCommand(EngineCommand{dzc::LoadDatasetCommand{}}));
    assert(!dzc::isValidEngineCommand(EngineCommand{dzc::LoadDatasetCommand{""}}));
    assert(!dzc::isValidEngineCommand(EngineCommand{dzc::LoadDatasetCommand{std::string{"\xE2\x82"}}}));
    assert(!dzc::isValidEngineCommand(EngineCommand{dzc::LoadDatasetCommand{std::string{"\xE2\x28\xA1"}}}));
    assert(!dzc::isValidEngineCommand(EngineCommand{dzc::LoadDatasetCommand{std::string{"\xC0\x80"}}}));
    assert(!dzc::isValidEngineCommand(EngineCommand{dzc::LoadDatasetCommand{std::string{"\xE0\x80\x80"}}}));
    assert(!dzc::isValidEngineCommand(EngineCommand{dzc::LoadDatasetCommand{std::string{"\xED\xA0\x80"}}}));
    assert(!dzc::isValidEngineCommand(EngineCommand{dzc::LoadDatasetCommand{std::string{"\xF4\x90\x80\x80"}}}));
}

void testInvalidPointSizes() {
    assert(!dzc::isValidEngineCommand(EngineCommand{dzc::SetPointSizeCommand{0.999F}}));
    assert(!dzc::isValidEngineCommand(EngineCommand{dzc::SetPointSizeCommand{64.001F}}));
    assert(!dzc::isValidEngineCommand(
        EngineCommand{dzc::SetPointSizeCommand{std::numeric_limits<float>::quiet_NaN()}}));
    assert(!dzc::isValidEngineCommand(
        EngineCommand{dzc::SetPointSizeCommand{std::numeric_limits<float>::infinity()}}));
    assert(!dzc::isValidEngineCommand(
        EngineCommand{dzc::SetPointSizeCommand{-std::numeric_limits<float>::infinity()}}));
}

} // namespace

int main() {
    testCommandAlternatives();
    testDefaultValues();
    testValidCommands();
    testInvalidDatasetPaths();
    testInvalidPointSizes();
    return 0;
}
