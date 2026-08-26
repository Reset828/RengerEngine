#include "GlShaderProgram.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace dzc::opengl;

namespace {

class FakeShaderOperations final : public IGlShaderOperations {
public:
    bool failVertexCompile{false};
    bool failFragmentCompile{false};
    bool failLink{false};
    bool failDelete{false};
    std::uint32_t nextId{1};
    std::vector<std::uint32_t> createdShaders;
    std::vector<std::uint32_t> deletedShaders;
    std::vector<std::uint32_t> createdPrograms;
    std::vector<std::uint32_t> deletedPrograms;
    std::vector<std::pair<GlShaderStage, std::string>> sources;
    std::string vertexLog{"fake vertex compile log"};
    std::string fragmentLog{"fake fragment compile log"};
    std::string linkLog{"fake link log"};

    bool createShader(GlShaderStage stage, std::uint32_t& id) const override {
        auto* self = const_cast<FakeShaderOperations*>(this);
        id = self->nextId++;
        self->createdShaders.push_back(id);
        self->sources.emplace_back(stage, std::string{});
        return true;
    }
    bool setShaderSource(std::uint32_t id, std::string_view source) const override {
        auto* self = const_cast<FakeShaderOperations*>(this);
        for (std::size_t i = 0; i < self->createdShaders.size(); ++i) {
            if (self->createdShaders[i] == id) {
                self->sources[i].second = std::string(source);
                return true;
            }
        }
        return false;
    }
    bool compileShader(std::uint32_t id, std::string& log) const override {
        auto* self = const_cast<FakeShaderOperations*>(this);
        bool vertex = false;
        for (std::size_t i = 0; i < self->createdShaders.size(); ++i) {
            if (self->createdShaders[i] == id) {
                vertex = self->sources[i].first == GlShaderStage::Vertex;
                break;
            }
        }
        log = vertex ? self->vertexLog : self->fragmentLog;
        return vertex ? !self->failVertexCompile : !self->failFragmentCompile;
    }
    bool createProgram(std::uint32_t& id) const override {
        auto* self = const_cast<FakeShaderOperations*>(this);
        id = self->nextId++;
        self->createdPrograms.push_back(id);
        return true;
    }
    bool attachShader(std::uint32_t, std::uint32_t) const override { return true; }
    bool linkProgram(std::uint32_t, std::string& log) const override {
        auto* self = const_cast<FakeShaderOperations*>(this);
        log = self->linkLog;
        return !self->failLink;
    }
    bool deleteShader(std::uint32_t id) const override {
        auto* self = const_cast<FakeShaderOperations*>(this);
        if (self->failDelete) return false;
        self->deletedShaders.push_back(id);
        return true;
    }
    bool deleteProgram(std::uint32_t id) const override {
        auto* self = const_cast<FakeShaderOperations*>(this);
        if (self->failDelete) return false;
        self->deletedPrograms.push_back(id);
        return true;
    }
};

void expectError(const dzc::Result<void>& result, GlShaderErrorCode code, std::string_view text = {}) {
    assert(!result.hasValue());
    assert(result.error().domain == dzc::ErrorDomain::OpenGL);
    assert(result.error().code == static_cast<std::uint32_t>(code));
    assert(!result.error().userMessage.empty());
    assert(!result.error().diagnosticMessage.empty());
    assert(!result.error().context.empty());
    if (!text.empty()) {
        assert(result.error().context.find(text) != std::string::npos ||
               result.error().diagnosticMessage.find(text) != std::string::npos);
    }
}

const std::string vertexSource = "#version 450 core\nvoid main(){}";
const std::string fragmentSource = "#version 450 core\nout vec4 c; void main(){ c=vec4(1);}";

void testSuccessAndReset() {
    auto fake = std::make_shared<FakeShaderOperations>();
    const auto token = GlContextThreadToken::current();
    GlShaderProgram program;
    assert(program.create(token, vertexSource, fragmentSource, "v-test", "f-test", fake).hasValue());
    assert(program.isValid());
    assert(program.reset(token).hasValue());
    assert(fake->deletedPrograms.size() == 1);
    assert(fake->deletedShaders.size() == 2);
    assert(program.reset(token).hasValue());
}

void testFailuresAndCleanup() {
    const auto token = GlContextThreadToken::current();
    auto fake = std::make_shared<FakeShaderOperations>();
    GlShaderProgram program;
    expectError(program.create(token, {}, fragmentSource, "vertex-name", "fragment-name", fake), GlShaderErrorCode::EmptySource, "vertex-name");
    expectError(program.create(token, vertexSource, {}, "vertex-name", "fragment-name", fake), GlShaderErrorCode::EmptySource, "fragment-name");

    fake->failVertexCompile = true;
    expectError(program.create(token, vertexSource, fragmentSource, "vertex-name", "fragment-name", fake), GlShaderErrorCode::VertexCompilationFailed, "fake vertex compile log");
    assert(fake->deletedShaders.size() == 1);
    fake->failVertexCompile = false;

    fake->failFragmentCompile = true;
    expectError(program.create(token, vertexSource, fragmentSource, "vertex-name", "fragment-name", fake), GlShaderErrorCode::FragmentCompilationFailed, "Fragment");
    assert(fake->deletedShaders.size() == 3);
    fake->failFragmentCompile = false;

    fake->failLink = true;
    expectError(program.create(token, vertexSource, fragmentSource, "vertex-name", "fragment-name", fake), GlShaderErrorCode::LinkFailed, "fake link log");
    assert(fake->deletedPrograms.size() == 1);
    assert(fake->deletedShaders.size() == 5);
}

void testFilesAndMove() {
    const auto token = GlContextThreadToken::current();
    const auto dir = std::filesystem::temp_directory_path() / "dzc-gl004-tests";
    std::filesystem::create_directories(dir);
    const auto v = dir / "test.vert";
    const auto f = dir / "test.frag";
    const auto empty = dir / "empty.frag";
    { std::ofstream(v, std::ios::binary) << vertexSource; }
    { std::ofstream(f, std::ios::binary) << fragmentSource; }
    { std::ofstream(empty, std::ios::binary); }

    auto fake = std::make_shared<FakeShaderOperations>();
    GlShaderProgram first;
    assert(first.createFromFiles(token, v, f, fake).hasValue());
    GlShaderProgram moved(std::move(first));
    assert(!first.isValid() && moved.isValid());
    assert(moved.reset(token).hasValue());

    GlShaderProgram missing;
    expectError(missing.createFromFiles(token, dir / "missing.vert", f, fake), GlShaderErrorCode::SourceReadFailed);
    expectError(missing.createFromFiles(token, v, empty, fake), GlShaderErrorCode::EmptySource);
    std::filesystem::remove_all(dir);
}

void testWrongThreadAndDestructor() {
    const auto token = GlContextThreadToken::current();
    auto fake = std::make_shared<FakeShaderOperations>();
    GlShaderProgram program;
    assert(program.create(token, vertexSource, fragmentSource, "v", "f", fake).hasValue());
    dzc::Result<void> result = dzc::Result<void>::success();
    std::thread worker([&] { result = program.reset(GlContextThreadToken::current()); });
    worker.join();
    expectError(result, GlShaderErrorCode::InvalidThreadToken);
    assert(program.releasePending());
    assert(fake->deletedPrograms.empty());
    assert(program.reset(token).hasValue());

    auto crossFake = std::make_shared<FakeShaderOperations>();
    auto* cross = new GlShaderProgram();
    assert(cross->create(token, vertexSource, fragmentSource, "v", "f", crossFake).hasValue());
    std::thread destroyer([cross] { delete cross; });
    destroyer.join();
    assert(crossFake->deletedPrograms.empty());
}

void testShaderFixtures() {
    const std::filesystem::path root = DZC_SOURCE_DIR;
    auto fake = std::make_shared<FakeShaderOperations>();
    GlShaderProgram program;
    assert(program.createFromFiles(GlContextThreadToken::current(),
        root / "shaders/opengl/point_cloud.vert",
        root / "shaders/opengl/point_cloud.frag", fake).hasValue());
    assert(fake->sources.size() == 2);
    assert(fake->sources[0].second.find("#version 450 core") != std::string::npos);
    assert(fake->sources[0].second.find("location = 0") != std::string::npos);
    assert(fake->sources[0].second.find("binding = 0") != std::string::npos);
    assert(fake->sources[0].second.find("binding = 1") != std::string::npos);
    assert(fake->sources[1].second.find("location = 2") != std::string::npos);
    assert(program.reset(GlContextThreadToken::current()).hasValue());
}

}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--real-context") {
        std::cout << "SKIPPED: real OpenGL Context infrastructure is provided by GL-007.\n";
        return 77;
    }
    testSuccessAndReset();
    testFailuresAndCleanup();
    testFilesAndMove();
    testWrongThreadAndDestructor();
    testShaderFixtures();
    return 0;
}
