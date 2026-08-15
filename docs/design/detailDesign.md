# Dzc-RenderEngine 详细设计说明书

> 文档路径：`docs/design/detailDesign.md`  
> 项目性质：个人开源项目  
> 文档版本：0.1.0  
> 文档状态：初始详细设计基线  
> 编制日期：2026-08-13  
> 概要设计：[`architectureDesign.md`](architectureDesign.md)  
> 需求基线：[`../requirements/spec.md`](../requirements/spec.md)  
> 项目规范：[`../../agent.md`](../../agent.md)

## 1. 文档说明

### 1.1 编写目的

本文档将概要设计细化为可以指导 C++17 编码、Shader 编写、CMake 组织和测试实现的详细方案，包括公共接口、内部类职责、数据字段、线程归属、队列语义、文件格式、GPU 资源布局、同步规则、错误码和关键伪代码。

本文中的 C++ 片段是接口设计，不包含完整函数体。公共头文件只声明接口和必要公共数据结构；私有成员、辅助函数、底层句柄和算法实现必须放入 `.cpp`、Pimpl 或模块私有头文件中。

### 1.2 适用阶段

- Phase 1：OpenGL 4.5、`QOpenGLWidget`、空间网格 Chunk、CUDA-OpenGL Interop；
- Phase 2：Vulkan 1.2、普通 `QWindow`、八叉树 LOD、`.dzcpc`、多线程 Secondary Command Buffer、CUDA-Vulkan Interop；
- 两个阶段共享 Engine、数据模型、错误模型、命令/事件/快照协议和测试基础设施。

### 1.3 设计约束等级

- **必须**：实现和评审的强制约束；
- **应当**：默认实现，偏离时必须记录理由和验证方式；
- **可以**：不破坏接口契约的可选优化；
- **待确定**：当前输入不足，禁止擅自固定。

### 1.4 已确认详细设计决策

| 编号 | 决策 |
|---|---|
| DDD-001 | 文档细化到可直接指导编码的接口、字段、同步和测试级别 |
| DDD-002 | 函数使用小驼峰 `init/update/render/resize/shutdown` |
| DDD-003 | Engine 使用显式生命周期接口；Phase 1 由 GUI/OpenGL 线程调用帧接口，Phase 2 由渲染协调线程调用 |
| DDD-004 | 默认 OpenGL、CUDA auto、info 日志；显式后端失败不静默回退 |
| DDD-005 | Command/Event 使用有界互斥队列；Snapshot 使用 C++17 原子 shared_ptr 操作 |
| DDD-006 | 线程数按硬件并发计算并允许配置覆盖 |
| DDD-007 | Phase 1 Chunk 目标 262,144 点、最大 524,288 点 |
| DDD-008 | `.dzcpc` V1 使用小端、64 位偏移、4096 字节 Payload 对齐、CRC32、无压缩 |
| DDD-009 | intensity 内部统一量化为 `uint16`，保存原始最小/最大值 |
| DDD-010 | 八叉树叶节点目标 262,144、最大 524,288、深度 16、内部代表点上限 65,536 |
| DDD-011 | Vulkan 1.2、传统 Render Pass、2 飞行帧、Timeline 用于内部异步、外部二进制信号量用于 CUDA |
| DDD-012 | Vulkan 分配器使用内存页加最佳匹配空闲区间；Device Local 256 MiB，Host Visible 64 MiB |
| DDD-013 | GPU 预算优先取可用预算 70%，CPU 缓存预算取物理内存 25% 并限制于 1–8 GiB |
| DDD-014 | OpenGL/Vulkan 采用统一逻辑 Shader 布局；Push Constant 不超过 32 字节 |
| DDD-015 | QSettings/INI 保存桌面设置；UTF-8 文本日志；CSV 性能数据和 Markdown 摘要 |
| DDD-016 | Camera 只定义抽象接口和数据结构，具体行为等待参考源码 |

## 2. 文件、命名与可见性规则

### 2.1 文件类型

| 类型 | 用途 |
|---|---|
| `include/dzc/*.h` | 对应用层公开的最小稳定接口 |
| `src/<module>/*.h` | 模块内部接口，仅对应 Target 内可见 |
| `src/<module>/*.cpp` | 私有成员、辅助函数和实现细节 |
| `src/<module>/detail/*.h` | 仅在确有多个 `.cpp` 共享私有类型时使用 |
| `shaders/opengl/*.vert/.frag` | OpenGL GLSL 源文件 |
| `shaders/vulkan/*.vert/.frag` | Vulkan GLSL，构建为 SPIR-V |

### 2.2 命名

- 类、结构体、枚举：PascalCase；
- 函数、局部变量、参数：camelCase；
- 成员变量：统一使用 `m_` 加 camelCase，例如 `m_commandQueue`；
- 枚举值：PascalCase；
- 常量：`k` 加 PascalCase，例如 `kDefaultCommandCapacity`；
- CMake Target：小写蛇形并带 `dzc_` 前缀；
- 文件名与主类型一致，类文件使用 PascalCase，如 `Engine.h`。

### 2.3 注释

公共或模块接口的函数声明上方添加简短注释，说明功能、参数和返回值。简单实现体不重复注释；复杂同步、所有权转换、坐标精度和文件校验处必须说明“为什么”。

## 3. 公共基础类型

### 3.1 强类型标识

建议在 `include/dzc/EngineTypes.h` 中声明不透明数值标识：

```cpp
struct DatasetId final {
    std::uint64_t value{0};
};

struct ChunkId final {
    std::uint64_t value{0};
};

struct FrameId final {
    std::uint64_t value{0};
};

struct TaskId final {
    std::uint64_t value{0};
};
```

规则：

- `0` 表示无效标识；
- 不同标识类型不得隐式转换；
- GPU 句柄不得写入这些字段；
- `ChunkId` 在同一 Dataset 生命周期内稳定；
- `.dzcpc` 中保存的 Node/Chunk 标识在同一缓存版本内稳定。

### 3.2 Result 与错误

```cpp
enum class ErrorDomain : std::uint8_t {
    General,
    Configuration,
    FileIo,
    DataFormat,
    Task,
    OpenGL,
    Vulkan,
    Cuda,
    Interop,
    Cache,
    Resource,
    Internal
};

struct Error final {
    ErrorDomain domain{ErrorDomain::General};
    std::uint32_t code{0};
    std::string userMessage;
    std::string diagnosticMessage;
    std::string context;
};

template <typename T>
class Result final {
public:
    // 创建成功结果并保存返回值。
    static Result<T> success(T value);

    // 创建失败结果并保存错误信息。
    static Result<T> failure(Error error);

    // 返回当前结果是否成功。
    bool hasValue() const noexcept;

    // 返回成功值；调用前必须检查 hasValue()。
    const T& value() const;

    // 返回失败错误；仅失败结果可调用。
    const Error& error() const;
};
```

同时提供 `Result<void>` 特化。实现使用 `std::variant<T, Error>` 或等效私有存储；不允许异常跨越公共接口。

### 3.3 后端与能力枚举

```cpp
enum class RenderBackendType : std::uint8_t {
    OpenGL,
    Vulkan
};

enum class OptionalFeatureMode : std::uint8_t {
    Off,
    On,
    Auto
};

enum class ShadingMode : std::uint8_t {
    OriginalColor,
    FixedColor,
    Height,
    Intensity
};
```

显式指定的后端不可用时返回失败；不得自动切换到另一后端。`Auto` 只用于 CUDA 等可选能力。

### 3.4 尺寸、颜色与路径

```cpp
struct RenderSize final {
    std::uint32_t width{0};
    std::uint32_t height{0};
    float devicePixelRatio{1.0F};
};

struct ColorRgba final {
    float red{0.0F};
    float green{0.0F};
    float blue{0.0F};
    float alpha{1.0F};
};
```

公共路径使用 UTF-8 `std::string`。Windows 平台适配层负责与 UTF-16 原生 API 转换；Engine 不依赖 `QString`。

## 4. Engine 公共接口

### 4.1 EngineConfig

```cpp
struct ThreadConfig final {
    std::uint32_t workerThreads{0};
    std::uint32_t commandRecordingThreads{0};
    std::uint32_t maxConcurrentIoTasks{2};
};

struct MemoryBudgetConfig final {
    std::uint64_t cpuCacheBytes{0};
    std::uint64_t gpuCacheBytes{0};
};

struct CacheConfig final {
    bool enabled{true};
    std::string directory;
};

struct EngineConfig final {
    RenderBackendType backend{RenderBackendType::OpenGL};
    OptionalFeatureMode cudaMode{OptionalFeatureMode::Auto};
    ThreadConfig threads;
    MemoryBudgetConfig memory;
    CacheConfig cache;
    std::uint32_t commandQueueCapacity{1024};
    std::uint32_t eventQueueCapacity{1024};
};
```

值为 `0` 的线程数或预算表示使用自动计算。Phase 1 可以忽略磁盘缓存执行路径，但保留配置兼容性。

### 4.2 Engine 接口

```cpp
class Engine final {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    // 初始化 Engine、任务系统和所选渲染后端。
    Result<void> init(const EngineConfig& config);

    // 线程安全地提交一个 Engine 命令。
    Result<void> enqueueCommand(EngineCommand command);

    // 消费命令并更新场景状态；调用线程由当前后端决定。
    Result<void> update(const FrameInput& input);

    // 使用当前场景状态渲染一帧。
    Result<void> render();

    // 更新渲染目标尺寸和投影相关状态。
    Result<void> resize(const RenderSize& size);

    // 原子读取最新不可变状态快照。
    std::shared_ptr<const EngineSnapshot> getSnapshot() const;

    // 取出当前所有待处理事件。
    std::vector<EngineEvent> pollEvents();

    // 停止任务并按依赖逆序释放全部资源。
    void shutdown() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
```

### 4.3 调用线程规则

| 方法 | Phase 1 | Phase 2 | 线程安全 |
|---|---|---|---|
| `init` | GUI/OpenGL 初始化线程 | 装配线程，内部启动协调线程 | 仅调用一次 |
| `enqueueCommand` | GUI 或控制线程 | GUI 或控制线程 | 是 |
| `update` | GUI/OpenGL 线程 | Vulkan 协调线程 | 否，单消费者 |
| `render` | GUI/OpenGL 线程 | Vulkan 协调线程 | 否，单消费者 |
| `resize` | GUI/OpenGL 线程 | Vulkan 协调线程 | 否，命令转发后执行 |
| `getSnapshot` | GUI | GUI | 是 |
| `pollEvents` | GUI | GUI | 是，单消费者 |
| `shutdown` | 创建/装配线程 | 创建/装配线程 | 不与其他生命周期调用并发 |

`Engine::Impl` 必须检查状态机，重复 `init`、停止后 `render` 等非法调用返回 `Internal/InvalidState`。


## 5. Engine 内部状态与协调

### 5.1 Engine 状态机

```cpp
enum class EngineState : std::uint8_t {
    Created,
    Initializing,
    Ready,
    Running,
    Loading,
    Failed,
    ShuttingDown,
    Stopped
};
```

允许的迁移如下；未列出的迁移均返回 `Internal/InvalidState`：

| 当前状态 | 触发 | 下一状态 | 说明 |
|---|---|---|---|
| Created | `init` | Initializing | 只允许一次 |
| Initializing | 初始化成功 | Ready | 发布首个快照 |
| Initializing | 致命失败 | Failed | 保留诊断后允许关闭 |
| Ready/Running | LoadDataset | Loading | 旧数据集按替换规则处理 |
| Loading | 完成/取消/可恢复失败 | Ready 或 Running | Engine 继续可用 |
| Ready | 首次有效帧 | Running | 后续保持 Running |
| Ready/Running/Loading/Failed | `shutdown` | ShuttingDown | 拒绝新业务命令 |
| ShuttingDown | 资源释放完成 | Stopped | 终态 |

数据文件错误、缓存损坏、单块上传失败和 CUDA 可选能力缺失属于可恢复问题，不得使 Engine 自动进入 `Failed`。设备丢失且无法恢复、后端初始化失败、内部不变量破坏才进入 `Failed`。

### 5.2 `Engine::Impl`

`Engine::Impl` 只在 `Engine.cpp` 中完整定义，建议拥有以下对象：

```cpp
class Engine::Impl final {
private:
    EngineState m_state{EngineState::Created};
    EngineConfig m_config;
    std::unique_ptr<TaskSystem> m_taskSystem;
    std::unique_ptr<PointCloudReaderRegistry> m_readerRegistry;
    std::unique_ptr<IRenderBackend> m_renderBackend;
    std::unique_ptr<IComputeBackend> m_computeBackend;
    std::unique_ptr<Scene> m_scene;
    std::unique_ptr<CacheManager> m_cacheManager;
    std::unique_ptr<LodBuilder> m_lodBuilder;
    std::unique_ptr<ResidencyManager> m_residencyManager;
    std::unique_ptr<DiagnosticsService> m_diagnostics;
    BoundedQueue<EngineCommand> m_commandQueue;
    BoundedQueue<EngineEvent> m_eventQueue;
    std::shared_ptr<const EngineSnapshot> m_snapshot;
    CancellationSource m_datasetCancellation;
    std::thread::id m_frameThreadId;
};
```

具体类型可以因阶段裁剪，但所有权方向必须保持：`Engine::Impl` 拥有协调对象，后端拥有其私有 GPU 资源，Dataset 拥有逻辑数据，任务只持有稳定 ID、不可变描述或有期限的共享租约。

### 5.3 每帧更新次序

`update` 的固定顺序为：

1. 校验状态和调用线程；
2. 批量取出命令并执行合并；
3. 应用数据集任务完成消息；
4. 更新抽象相机控制器和 `CameraMatrices`；
5. 计算视锥体、Phase 1 可见 Chunk 或 Phase 2 LOD 需求；
6. 更新 Residency 优先级，提交受预算约束的读取、上传和淘汰；
7. 更新渲染后端的后端无关帧描述；
8. 汇总诊断指标；
9. 创建新 `EngineSnapshot`，并以原子 shared_ptr 发布。

`render` 只消费已经准备好的帧描述，不在绘制过程中解析文件或等待长时间 CPU 任务。

## 6. 命令、事件与状态快照

### 6.1 EngineCommand

命令使用封闭 `std::variant`，负载只包含值对象或拥有明确所有权的不可变对象：

```cpp
struct LoadDatasetCommand final { std::string path; };
struct CancelDatasetLoadCommand final { DatasetId datasetId; };
struct UnloadDatasetCommand final { DatasetId datasetId; };
struct SetPointSizeCommand final { float pixels{1.0F}; };
struct SetShadingModeCommand final { ShadingMode mode{ShadingMode::OriginalColor}; };
struct SetFixedColorCommand final { ColorRgba color; };
struct SetBackgroundColorCommand final { ColorRgba color; };
struct SetCudaModeCommand final { OptionalFeatureMode mode{OptionalFeatureMode::Auto}; };
struct ResetViewCommand final {};
struct SubmitInputCommand final { InputEvent event; };
struct ResizeCommand final { RenderSize size; };
struct ShutdownCommand final {};

using EngineCommand = std::variant<
    LoadDatasetCommand,
    CancelDatasetLoadCommand,
    UnloadDatasetCommand,
    SetPointSizeCommand,
    SetShadingModeCommand,
    SetFixedColorCommand,
    SetBackgroundColorCommand,
    SetCudaModeCommand,
    ResetViewCommand,
    SubmitInputCommand,
    ResizeCommand,
    ShutdownCommand>;
```

约束：

- `LoadDatasetCommand` 的路径必须是非空 UTF-8 字符串；
- 点大小在进入场景前检查有限数并限制到实现支持范围，建议初始范围 `[1, 64]` 像素；范围可通过配置调整，不作为相机行为；
- 连续参数命令按“最后一个值生效”合并；
- 加载、取消、卸载、输入和关闭命令不得跨语义重排；
- `ResetViewCommand` 只调用未来注入的相机控制器入口，当前不定义结果行为。

### 6.2 EngineEvent

```cpp
enum class EventSeverity : std::uint8_t {
    Info,
    Warning,
    RecoverableError,
    FatalError
};

struct EventContext final {
    DatasetId datasetId;
    ChunkId chunkId;
    TaskId taskId;
    FrameId frameId;
};

struct MessageEvent final {
    EventSeverity severity{EventSeverity::Info};
    std::string message;
    EventContext context;
};

struct ErrorEvent final {
    EventSeverity severity{EventSeverity::RecoverableError};
    Error error;
    EventContext context;
};

struct DatasetProgressEvent final {
    DatasetId datasetId;
    std::uint64_t completedUnits{0};
    std::uint64_t totalUnits{0};
};

struct DatasetLoadedEvent final { DatasetId datasetId; };
struct DatasetLoadCancelledEvent final { DatasetId datasetId; };
struct FeatureDegradedEvent final { std::string feature; std::string reason; };

using EngineEvent = std::variant<
    MessageEvent,
    ErrorEvent,
    DatasetProgressEvent,
    DatasetLoadedEvent,
    DatasetLoadCancelledEvent,
    FeatureDegradedEvent>;
```

进度事件允许合并；错误、完成、取消和能力降级事件不可静默丢弃。事件只用于离散通知，UI 持续显示的数据必须读取 Snapshot。

### 6.3 EngineSnapshot

```cpp
enum class DatasetState : std::uint8_t {
    None,
    Opening,
    Building,
    Ready,
    Cancelling,
    Error
};

struct DatasetSummary final {
    DatasetId id;
    DatasetState state{DatasetState::None};
    std::string displayName;
    std::uint64_t totalPointCount{0};
    std::uint64_t visiblePointCount{0};
    std::uint64_t chunkCount{0};
    std::uint64_t visibleChunkCount{0};
    double progress{0.0};
};

struct PerformanceSnapshot final {
    double framesPerSecond{0.0};
    double cpuFrameMilliseconds{0.0};
    double gpuFrameMilliseconds{0.0};
    std::uint64_t uploadedBytesThisFrame{0};
    std::uint32_t recordingWorkerCount{0};
};

struct MemorySnapshot final {
    std::uint64_t cpuResidentBytes{0};
    std::uint64_t cpuBudgetBytes{0};
    std::uint64_t gpuResidentBytes{0};
    std::uint64_t gpuBudgetBytes{0};
};

struct EngineSnapshot final {
    FrameId frameId;
    EngineState state{EngineState::Created};
    RenderBackendType backend{RenderBackendType::OpenGL};
    bool cudaAvailable{false};
    bool cudaEnabled{false};
    DatasetSummary dataset;
    PerformanceSnapshot performance;
    MemorySnapshot memory;
    float pointSize{1.0F};
    ShadingMode shadingMode{ShadingMode::OriginalColor};
    ColorRgba fixedColor;
    ColorRgba backgroundColor;
    std::optional<Error> mostRecentError;
};
```

发布规则：

- 生产者构造完整的 `std::shared_ptr<EngineSnapshot>`，转为 const 后调用 C++17 的 `std::atomic_store_explicit`；
- UI 使用 `std::atomic_load_explicit`；内存序使用 `release/acquire`；
- 发布后不得修改对象；
- Snapshot 不包含点数组、Chunk Payload、GPU 句柄或 Qt 类型；
- FPS 使用最近 120 帧或最近 1 秒中先达到者的滑动窗口；窗口未填满时按实际样本计算。

### 6.4 有界队列和溢出策略

`BoundedQueue<T>` 使用 `std::mutex`、`std::condition_variable` 和固定容量环形存储，默认容量为 1024。公共接口为 `tryPush(T)`、`tryPop()`、`tryPopBatch(maxCount)` 和 `close()`；队列不可复制、不可移动，元素要求可移动构造。容量必须大于零，零容量构造抛出 `std::invalid_argument`。Command 是多生产者单消费者，Event 是多生产者、UI 单消费者。

`tryPush`、`tryPop` 和 `tryPopBatch` 均为立即返回操作：队列满时入队失败，队列空时出队返回空值，批量出队按 FIFO 最多返回当前已有的 `maxCount` 个元素，`maxCount == 0` 返回空批次。队列关闭后拒绝新入队，但保留已接受元素供消费者排空；`close()` 幂等，析构函数自动调用。互斥量保护所有公开操作；条件变量仅发送状态通知，不向调用方提供阻塞等待接口。

| 队列情况 | 处理方式 |
|---|---|
| 可合并参数命令已存在 | 原位置更新为最新值，不增加长度 |
| 输入移动类事件拥塞 | 允许由 UI 适配层先合并为最新累计值 |
| 非关键 Info/Progress 事件满 | 合并同数据集进度，或丢弃最旧同类事件 |
| 关键命令队列满 | `enqueueCommand` 返回 `Task/QueueFull`，不阻塞 UI |
| Error/Complete/Cancel 事件队列满 | 写日志并保留一个“事件丢失”错误槽；下一次轮询优先返回 |
| ShutdownCommand | 通过独立原子停止标志保证可达，不依赖队列空位 |

不得让 UI 在入队时无限等待。队列关闭后，生产者收到 `Task/QueueClosed`。

## 7. TaskSystem 与异步任务

### 7.1 线程配置

令 `H = std::thread::hardware_concurrency()`；当 H 为 0 时按 4 处理：

```text
phase1WorkerThreads = clamp(H - 1, 2, 8)
phase2RecordingThreads = clamp(H / 2, 2, 8)
maxConcurrentIoTasks = 2
```

用户配置的非零值覆盖自动值，但必须至少为 1，并受实现安全上限保护。Phase 2 的通用 worker 与命令录制 worker 应分组，防止 I/O 或缓存构建占满录制线程。

### 7.2 接口与任务描述

```cpp
enum class TaskPriority : std::uint8_t {
    Critical,
    High,
    Normal,
    Low
};

class CancellationToken final {
public:
    CancellationToken() noexcept = default;

    bool isCancellationRequested() const noexcept;

private:
    // 仅供 CancellationSource 构造共享观察令牌。
};

class CancellationSource final {
public:
    CancellationSource();
    ~CancellationSource();

    CancellationSource(const CancellationSource&) = delete;
    CancellationSource& operator=(const CancellationSource&) = delete;
    CancellationSource(CancellationSource&&) noexcept;
    CancellationSource& operator=(CancellationSource&&) noexcept;

    CancellationToken token() const noexcept;
    bool requestCancellation() noexcept;
};

class TaskSystem final {
public:
    Result<TaskId> submit(
        TaskPriority priority,
        CancellationToken token,
        std::function<void(CancellationToken)> task);

    void stopAccepting() noexcept;
    void requestCancelAll() noexcept;
    void waitForCompletion() noexcept;
};
```

TS-001 中，`CancellationSource` 创建共享取消状态，`CancellationToken` 仅持有共享观察引用。默认构造 Token 不关联 Source，查询始终返回未取消；Source 析构时请求取消，因此仍存活的 Token 不会访问已销毁的 Source。`requestCancellation()` 以原子 compare-exchange 使用 acquire/release 语义实现线程安全且幂等的状态迁移，只有首次未取消到已取消的调用返回 `true`。Source 不可复制、可移动；移动赋值会先取消目标原有状态，再接管来源状态。TS-001 只提供轮询查询，不提供回调、阻塞等待或唤醒机制。

线程池使用每优先级 FIFO 队列。每个 Dataset 拥有 CancellationSource；Chunk 子任务继承令牌。任务必须在打开文件前、每个批次后、昂贵构建步骤前后和提交 GPU 上传前检查取消。

### 7.3 I/O 并发与背压

- 使用计数信号量等效实现限制同时活跃的文件读取任务，C++17 可用 mutex/condition_variable 自行实现；
- 一个 Reader 任务以批次输出，不一次性复制全文件；
- 下游待构建批次、CPU 驻留字节或上传队列达到高水位时暂停继续读取；
- 队列降到低水位后唤醒；高水位建议为容量 80%，低水位为 60%；
- 取消优先于继续生产，关闭时先停止接收新任务再汇合已有任务。

### 7.4 异常与任务结果

任务入口捕获全部异常并转换为 `Error`。第三方异常文本只写 `diagnosticMessage`；`userMessage` 使用可理解的稳定描述。任务完成结果通过内部完成队列返回 Engine 单消费者，不允许工作线程直接修改 Scene。


## 8. 点云核心数据模型

### 8.1 属性模式

```cpp
enum class PointAttribute : std::uint32_t {
    Position = 1U << 0U,
    Color = 1U << 1U,
    Intensity = 1U << 2U
};

struct AttributeSchema final {
    std::uint32_t mask{0};
    bool hasPosition() const noexcept;
    bool hasColor() const noexcept;
    bool hasIntensity() const noexcept;
};

struct IntensityMetadata final {
    bool available{false};
    double sourceMinimum{0.0};
    double sourceMaximum{0.0};
    double validMinimum{0.0};
    double validMaximum{0.0};
};
```

Position 是必需属性。颜色缺失时不创建 Color 流，Shader 使用固定颜色；intensity 缺失时不创建 Intensity 流。intensity 存在时内部存储为 `uint16_t`，线性量化到 `[0, 65535]`，同时保存源 min/max 与有效范围，以便显示和诊断。无效值不参与范围统计，可量化为 0 并由有效性统计记录。

### 8.2 包围盒与坐标精度

```cpp
struct Bounds3d final {
    glm::dvec3 minimum{0.0};
    glm::dvec3 maximum{0.0};
};

struct ChunkTransform final {
    glm::dvec3 origin{0.0};
};
```

每个源点使用 double 读取并参与全局包围盒计算。Dataset 原点建议取全局包围盒中心；每个 Chunk 原点取 Chunk 包围盒中心。GPU Position 保存：

```text
localPosition = float3(sourcePosition - chunkOrigin)
relativeChunkOrigin = float3(chunkOrigin - cameraOrigin)
worldRelative = relativeChunkOrigin + localPosition
```

CPU 保留 double 原点，Shader 只计算相对坐标，避免将大 GIS 坐标直接转换为 float。每帧 `cameraOrigin` 由相机状态提供，但其选取和相机行为仍由未来控制器设计决定。

### 8.3 SoA 点批次与 Chunk

```cpp
struct PointBatch final {
    AttributeSchema schema;
    std::vector<glm::dvec3> positions;
    std::vector<std::uint32_t> colorsRgba8;
    std::vector<std::uint16_t> intensities;
};

struct ChunkCpuData final {
    std::vector<glm::vec3> positions;
    std::vector<std::uint32_t> colorsRgba8;
    std::vector<std::uint16_t> intensities;
};

enum class ChunkState : std::uint8_t {
    MetadataOnly,
    LoadingCpu,
    CpuResident,
    UploadQueued,
    GpuResident,
    EvictPending,
    Error
};

struct ChunkMetadata final {
    ChunkId id;
    std::uint64_t pointCount{0};
    Bounds3d bounds;
    glm::dvec3 origin{0.0};
    AttributeSchema schema;
};
```

SoA 每条属性流连续存储；所有存在的流长度必须等于点数。颜色逻辑格式为 RGBA8；宿主内存按 4 字节元素存储，序列化时明确写出 R、G、B、A 字节，禁止依赖主机整数端序解释颜色。

### 8.4 Dataset

Dataset 只暴露后端无关查询，内部建议保存：源路径、源文件身份、状态、属性模式、IntensityMetadata、全局包围盒、Dataset 原点、Chunk/Node 索引、CPU 租约表和当前 CancellationSource。替换数据集时先创建新的 DatasetId；旧任务结果若 DatasetId 不匹配必须丢弃，不得写入新场景。

## 9. Phase 1 网格分块

### 9.1 参数

- 目标 Chunk 点数：262,144；
- 最大 Chunk 点数：524,288；
- 使用全局包围盒和采样密度估计初始立方网格边长；
- 网格坐标使用 64 位有符号整数并检查溢出；
- 单元超过最大点数时确定性细分，不生成超限 Chunk。

### 9.2 构建算法

1. Reader 流式读取一批点并验证有限坐标；
2. 根据估计网格边长计算 `floor((position - datasetMinimum) / cellSize)`；
3. 以三维 CellKey 分桶，桶内追加原始点序号或归一化属性；
4. 桶达到目标值后可提前封装，但必须确保后续同 Cell 数据的稳定子块编号；
5. 完成读取后，对超过最大点数的 Cell 按最长轴二分或扩大局部细分层级；
6. 每个输出 Chunk 计算 double 包围盒和原点，再生成 float3 局部坐标；
7. 按稳定 CellKey 和子块编号生成 ChunkId。

伪代码：

```text
buildGridChunks(stream):
    estimate cellSize from bounds, pointCount and targetPointCount
    for each batch in stream:
        check cancellation
        for each valid point in batch:
            key = checkedGridKey(point.position, cellSize)
            buckets[key].append(point)
            if bucket memory reaches flush threshold:
                spill deterministic run to temporary storage
    for key in sorted(allKeys):
        points = mergeRunsFor(key)
        partitions = splitDeterministically(points, maxPointCount)
        for partitionIndex, partition in partitions:
            emit makeChunk(key, partitionIndex, partition)
```

大文件不得要求所有源点同时驻留内存；当分桶数据超过 CPU 构建预算时写入项目缓存目录下的临时 run 文件。取消或失败时删除临时文件。

### 9.3 视锥体剔除

每帧使用 Chunk double 包围盒相对于 camera origin 转换为 float 或 double 平面测试。完全在任一平面负侧的 Chunk 不可见；相交或内部 Chunk 进入绘制列表。测试顺序优先使用上一帧最可能剔除的平面索引以减少平均比较次数。

## 10. PCD/PLY 读取与 PCL 隔离

### 10.1 Reader 接口

```cpp
struct PointCloudSourceInfo final {
    AttributeSchema schema;
    std::uint64_t declaredPointCount{0};
    Bounds3d bounds;
    IntensityMetadata intensity;
};

class IPointCloudReader {
public:
    virtual ~IPointCloudReader() = default;
    virtual Result<PointCloudSourceInfo> open(const std::string& path) = 0;
    virtual Result<std::optional<PointBatch>> readNext(
        std::size_t maximumPoints,
        CancellationToken token) = 0;
    virtual void close() noexcept = 0;
};
```

`dzc_data_pcl` 内部包含 PCL 头文件并实现 PCD/PLY Reader。适配器立刻把 PCL 字段转换到标准类型和 GLM；任何 PCL 类型不得离开该 Target 的私有接口。

### 10.2 字段映射和校验

- 必须识别 x/y/z，缺任一字段即拒绝；
- color 优先读取 rgba，其次 rgb；无 alpha 时写 255；
- intensity 接受可转换的整数或浮点标量，使用 double 收集范围后量化；
- NaN/Infinity 坐标点跳过并计数，若全部无效则失败；
- 声明点数、实际点数和数据长度不一致时返回 DataFormat 错误；
- PLY 属性名映射只限标准 x/y/z、red/green/blue/alpha、intensity，不猜测其他业务字段；
- Reader 不修改源文件。

## 11. `.dzcpc` V1 文件格式

### 11.1 通用规则

- Magic：8 字节 ASCII `DZCPC001`；
- 所有整数和 IEEE 754 浮点均为小端；
- 文件头与索引项按 64 字节边界排列；
- 每个 Payload 起点按 4096 字节对齐，填充字节必须为 0；
- V1 不压缩；
- Header、完整索引区和每个 Payload 分别使用 CRC32；
- CRC32 使用 IEEE 802.3 多项式 `0xEDB88320`、初值 `0xFFFFFFFF`、最终异或 `0xFFFFFFFF`；
- 校验字段本身在计算对应 CRC 时视为 0。

### 11.2 Header：固定 256 字节

| 偏移 | 大小 | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 8 | magic | `DZCPC001` |
| 8 | 2 | versionMajor | 1 |
| 10 | 2 | versionMinor | 0 |
| 12 | 4 | headerSize | 256 |
| 16 | 4 | flags | bit0 color，bit1 intensity，bit2 octree |
| 20 | 4 | byteOrder | `0x01020304` |
| 24 | 8 | sourceIdentityHash | 源文件身份摘要 |
| 32 | 8 | sourceFileSize | 构建时源大小 |
| 40 | 8 | sourceWriteTimeNs | 构建时修改时间 |
| 48 | 8 | totalPointCount | 64 位点数 |
| 56 | 8 | nodeCount | Node 索引数量 |
| 64 | 8 | chunkCount | Chunk 索引数量 |
| 72 | 8 | nodeIndexOffset | 64 字节对齐 |
| 80 | 8 | chunkIndexOffset | 64 字节对齐 |
| 88 | 8 | payloadRegionOffset | 4096 字节对齐 |
| 96 | 24 | datasetOrigin | 3 个 double |
| 120 | 48 | datasetBounds | min/max，各 3 个 double |
| 168 | 16 | intensityRange | source min/max，2 个 double |
| 184 | 16 | intensityValidRange | valid min/max，2 个 double |
| 200 | 8 | buildOptionsHash | 影响布局的参数摘要 |
| 208 | 8 | indexByteSize | 两类索引总长度 |
| 216 | 4 | indexCrc32 | 完整索引区 CRC |
| 220 | 4 | headerCrc32 | Header CRC |
| 224 | 32 | reserved | 必须写 0，读取时忽略 |

源身份至少由规范化绝对路径、文件大小、修改时间组成；Hash 仅用于快速比较，必须同时比较显式大小和时间。缓存是可删除、可重建的内部产物，不作为源数据真值。

### 11.3 NodeIndexEntry：固定 192 字节

| 字段 | 类型 | 说明 |
|---|---|---|
| nodeId | uint64 | 稳定节点 ID |
| parentNodeId | uint64 | 根节点为 0 |
| firstChildIndex | uint64 | 无子节点为最大 uint64 |
| childMask | uint8 | 8 个子节点存在位 |
| depth | uint8 | 0–16 |
| representativeChunkIndex | uint64 | 内部节点代表点 Chunk |
| leafChunkIndex | uint64 | 叶节点数据 Chunk；非叶为最大值 |
| pointCount | uint64 | 子树源点总数 |
| representativePointCount | uint32 | 最多 65,536 |
| origin | double[3] | 节点原点 |
| boundsMin | double[3] | 包围盒最小值 |
| boundsMax | double[3] | 包围盒最大值 |
| geometricError | double | 世界单位误差 |
| reserved | bytes | 补足 192，必须写 0 |

子节点在 Node 索引中连续存放，顺序为 child index 0 到 7 中实际存在者。

### 11.4 ChunkIndexEntry：固定 128 字节

| 字段 | 类型 | 说明 |
|---|---|---|
| chunkId | uint64 | 缓存内稳定 ID |
| ownerNodeId | uint64 | 所属节点 |
| pointCount | uint64 | 64 位点数 |
| attributeMask | uint32 | Position/Color/Intensity |
| payloadOffset | uint64 | 4096 字节对齐 |
| payloadSize | uint64 | 含 PayloadHeader 和属性流 |
| payloadCrc32 | uint32 | 整个 Payload CRC |
| positionOffset | uint64 | 相对 Payload 起点 |
| colorOffset | uint64 | 缺失时 0 |
| intensityOffset | uint64 | 缺失时 0 |
| origin | double[3] | Chunk 原点 |
| reserved | bytes | 补足 128，必须写 0 |

### 11.5 Payload 布局

Payload 开始为固定 64 字节 `ChunkPayloadHeader`，包含 payloadVersion、pointCount、attributeMask、各流 stride 和大小。随后：

1. Position：`pointCount * 12` 字节，float32 x/y/z；
2. Color：若存在，`pointCount * 4` 字节，R/G/B/A；
3. Intensity：若存在，`pointCount * 2` 字节，uint16；
4. 各流起点至少 16 字节对齐；Payload 后续文件区域按 4096 对齐。

索引和 Payload 必须由显式字段序列化函数读写，不得直接写入 C++ 结构体或依赖 `sizeof`、编译器填充和主机端序。

读取前必须使用 checked add/multiply 验证 `offset + size`、`pointCount * stride` 不溢出且不越过文件末尾。CRC、属性长度、索引引用和 Node 父子关系任一失败都使缓存整体失效并触发重建，不尝试猜测修复。

### 11.6 原子发布

构建路径为 `<final>.tmp.<processId>.<nonce>`。完整写入、flush、关闭并重新打开校验后，在同一文件系统内替换最终文件。Windows 使用平台适配层执行 replace-existing；失败时保留原有效缓存并删除临时文件。崩溃遗留临时文件在下次启动按命名模式和年龄清理。


## 12. Phase 2 八叉树、LOD 与流式驻留

### 12.1 八叉树构建参数

- 叶节点目标点数：262,144；
- 叶节点最大点数：524,288；
- 最大深度：16；
- 内部节点代表点最多：65,536；
- 输入顺序不影响树拓扑和代表点结果；
- 根节点使用包含 Dataset 包围盒的立方体，退化轴扩展到可表示的最小正边长。

### 12.2 确定性构建

```text
buildNode(points, bounds, depth):
    if points.size <= targetLeafPoints or depth == maxDepth:
        if points.size > maxLeafPoints:
            emit deterministic partitions under the same spatial leaf
        else:
            emit leaf chunk
        return

    children = stablePartitionByOctant(points, bounds.center)
    representative = deterministicVoxelSample(points, maxRepresentativePoints)
    emit internal node and representative chunk
    for childIndex in 0..7:
        if children[childIndex] is not empty:
            buildNode(children[childIndex], childBounds(bounds, childIndex), depth + 1)
```

Octant 位定义固定为：bit0 表示 x 大于等于中心，bit1 表示 y，bit2 表示 z。落在分割平面的点进入正半轴。稳定分区保留同一 Octant 内的源稳定序号顺序。

确定性体素代表点步骤：

1. 根据节点包围盒和代表点上限计算三轴体素分辨率；
2. 点映射到整数体素坐标；
3. 每个体素选取 Morton key 最小、再以源稳定序号最小的点；
4. 若候选仍超过 65,536，按 Morton key 等距抽取；
5. 代表点按 Morton key 排序写入；
6. geometricError 取体素对角线长度，叶节点为 0。

### 12.3 屏幕空间误差与滞回

对于节点包围球中心到相机的正距离 `distance`：

```text
projectedErrorPixels = geometricError * viewportHeight /
                       (2 * tan(verticalFieldOfView / 2) * max(distance, nearDistance))
```

- 当前使用父节点代表点且误差大于 2.0 px：请求细化；
- 当前使用子节点且误差低于 1.5 px：允许回退；
- 1.5–2.0 px 保持上一帧选择，避免闪烁；
- 与视锥体不相交的节点不请求；
- 当相机数据尚未由具体 Controller 提供时，只接受外部有效 `CameraMatrices`，不推断视场角或移动方式。

### 12.4 渐进显示

细化请求的子节点尚未 GPU Resident 时继续绘制最近可用祖先代表点；子节点全部可用后再替换祖先。回退时先确认祖先代表点可用。任何时刻不得因为 LOD 缺页让已经有祖先数据的区域完全消失。

请求优先级按以下元组降序：可见性、屏幕误差、距视图中心接近度、上一帧使用、节点深度；相同时按 NodeId 升序，保证可复现。

### 12.5 ResidencyManager

```cpp
enum class ResidencyLevel : std::uint8_t {
    Metadata,
    Cpu,
    Gpu
};

struct ResidencyRequest final {
    ChunkId chunkId;
    ResidencyLevel target{ResidencyLevel::Gpu};
    double priority{0.0};
    FrameId requestedFrame;
};
```

管理器维护 Metadata、CPU Resident、UploadQueued、GPU Resident、EvictPending 状态以及字节记账。绘制帧、上传任务和 CUDA 任务通过租约增加使用计数；只有计数为 0 且完成同步值已到达的资源可以释放。

### 12.6 CPU/GPU 预算

GPU 自动预算：

1. 支持 `VK_EXT_memory_budget` 时，取适合资源的 heap 可用 budget 的 70%；
2. 否则取 Device Local Heap 大小的 60%；
3. 用户非零配置覆盖自动预算，但不得超过运行时安全可用量；
4. OpenGL 无可靠预算扩展时使用显存查询能力；不可查询时采用保守配置并记录来源。

CPU 自动预算取物理内存 25%，常规情况下限制到 1–8 GiB；若系统当前可用内存不足 1 GiB，则预算降到可安全取得的值，不为满足下限强制分配。

预算包括 Chunk 数据、上传暂存、索引开销和后端明确记账的缓存；Swapchain、驱动私有分配等无法精确控制的内存单独报告。

### 12.7 淘汰

候选必须不可见、无租约、无未完成上传/计算/绘制引用。评分优先淘汰：长时间未使用、LOD 优先级低、可由缓存快速重载、占用大的资源。高水位为预算 95%，目标回收到 85%；分配失败可立即触发紧急淘汰。若仍不足，则维持祖先 LOD、暂停新细化并发送 Resource/BudgetExceeded，而不是越过预算强行分配。

## 13. 后端无关渲染契约

### 13.1 帧与绘制描述

```cpp
struct DrawChunk final {
    ChunkId chunkId;
    std::uint64_t pointCount{0};
    glm::vec3 relativeOrigin{0.0F};
    AttributeSchema schema;
};

struct RenderFrame final {
    FrameId frameId;
    CameraMatrices camera;
    RenderSize size;
    ColorRgba backgroundColor;
    float pointSize{1.0F};
    ShadingMode shadingMode{ShadingMode::OriginalColor};
    ColorRgba fixedColor;
    std::vector<DrawChunk> draws;
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    virtual Result<void> init(const RenderBackendConfig& config) = 0;
    virtual Result<void> upload(const UploadBatch& batch) = 0;
    virtual Result<void> update(const RenderFrame& frame) = 0;
    virtual Result<void> render() = 0;
    virtual Result<void> resize(const RenderSize& size) = 0;
    virtual void release(ChunkId chunkId) noexcept = 0;
    virtual void shutdown() noexcept = 0;
};
```

该接口是模块内部接口，不放入面向 UI 的公共头文件。`UploadBatch` 传递标准属性视图或共享不可变字节块，不携带 OpenGL/Vulkan/CUDA 句柄。

### 13.2 统一 Shader 逻辑布局

| 语义 | OpenGL | Vulkan | 格式 |
|---|---|---|---|
| Position | location 0 | location 0 | float3 |
| Color | location 1 | location 1 | rgba8 normalized |
| Intensity | location 2 | location 2 | uint16 normalized |
| FrameData | UBO binding 0 | set 0 binding 0 | 矩阵、相机相对参数 |
| ChunkData | SSBO binding 1 | set 0 binding 1 | Chunk relative origin 等 |

Vulkan Push Constant 不超过 32 bytes，只放 drawIndex、shadingMode、pointSize 等高频标量。OpenGL 可用等价 uniform，但逻辑字段必须保持一致。缺失属性流时 Pipeline/VAO 使用固定属性或通过 schema 分支，禁止绑定越界缓冲。

`FrameData` 的矩阵按 GLM 列主序约定；OpenGL 与 Vulkan 的深度范围差异由各后端投影适配函数处理，不能让 UI 构造 API 特定矩阵。

## 14. Phase 1 OpenGL 详细设计

### 14.1 私有组件

| 组件 | 职责 |
|---|---|
| `OpenGLBackend` | 生命周期、帧调度、资源表 |
| `GlBuffer` | Buffer RAII，记录目标与大小 |
| `GlVertexArray` | VAO RAII |
| `GlShaderProgram` | 运行时编译、链接、日志 |
| `GlChunkResource` | 一个 Chunk 的属性 Buffer、VAO 和点数 |
| `GlTimerQueryPool` | 延迟读取 GPU 帧时间 |
| `CudaGlInteropResource` | 私有 CUDA 注册状态 |

所有 GL 对象必须在拥有当前 Context 的线程创建和销毁。RAII 析构前若 Context 不可用，后端应在 Context 即将销毁信号中显式 `shutdown`，不得把正常资源销毁推迟到进程退出。

### 14.2 初始化

1. `QOpenGLWidget::initializeGL` 中调用 Engine `init`；
2. 检查 OpenGL 4.5 Core Profile；
3. 使用 GLAD 加载函数；
4. 查询点大小范围、SSBO/UBO 对齐、最大 Buffer 等能力；
5. 编译 `shaders/opengl` GLSL，绑定固定 location/binding；
6. 创建 Frame UBO、Chunk SSBO、计时查询池和默认状态；
7. 若 CUDA auto/on，执行同 GPU 能力检查；on 失败则初始化失败，auto 失败则降级事件。

### 14.3 上传和绘制

上传在 GL Context 线程执行。工作线程只准备 `ChunkCpuData`。创建 Position VBO，并按 schema 创建 Color/Intensity VBO；VAO 固定属性 location。Phase 1 每个可见 Chunk 执行一次 `glDrawArrays(GL_POINTS, 0, pointCount)`，首版不强制 MultiDrawIndirect。

每帧：

1. 清空颜色和深度；
2. 更新 FrameData UBO；
3. 批量写入可见 ChunkData SSBO；
4. 绑定 Shader 和公共状态；
5. 对可见 Chunk 绑定 VAO、设置 draw index 并绘制；
6. 结束 GPU timer query；
7. 延迟若干帧读取查询，避免同步等待。

禁止逐点 draw call、逐点状态变更和从 GPU 回读点数据。

### 14.4 Shader

Vertex Shader 将 localPosition 加 relativeChunkOrigin，再乘 view/projection；设置 `gl_PointSize`。Fragment Shader 根据 OriginalColor、FixedColor、Height、Intensity 输出颜色。Height 使用当前 Dataset 高程范围归一化；范围退化时使用 0.5。Intensity 读取 normalized uint16；无流时回退 FixedColor 并发布一次警告。

OpenGL Shader 源码运行时编译；错误事件包含阶段、文件和编译日志，但不包含超长重复源码。Shader 重载不属于首版要求。

## 15. CUDA-OpenGL 互操作

### 15.1 资源状态

```text
Unregistered -> Registered -> MappedForCuda -> Registered -> InUseByGl
```

只有 GL 创建并完成初始化的 Buffer 可以注册。CUDA 处理前确保 GL 对该资源的前序访问完成；注册资源映射后由 CUDA Kernel 原地处理，解除映射完成后 GL 才可再次绘制。正常路径不得将计算结果复制回 CPU 再上传。

### 15.2 同步与降级

Phase 1 使用 CUDA-GL 映射/解除映射语义以及必要的 GL fence 控制所有权。不得每帧调用 `glFinish`；只等待具体资源所需的 fence。设备不匹配、注册失败或 Kernel 失败：

- CUDA mode=auto：注销可用资源，切换 DisabledComputeBackend，发送降级事件；
- CUDA mode=on：返回 Interop 错误，不宣称零拷贝成功；
- CUDA mode=off：不创建 CUDA Context 或链接运行时路径。

预处理首版仅包括需求已定义的简单属性变换或过滤准备，不加入配准、修补、深度学习或通用点云算法。


## 16. Phase 2 Vulkan 详细设计

### 16.1 窗口与线程边界

Qt 使用普通 `QWindow` 作为原生窗口宿主。UI 层创建窗口并把平台无关的 Surface 创建描述交给装配层；`dzc_platform` 私有代码读取原生句柄。Engine 公共接口不得包含 `QWindow` 或 `VkSurfaceKHR`。

Vulkan 协调线程唯一负责 acquire、主 Command Buffer、队列提交、present、Swapchain 重建和后端状态迁移。录制线程只能使用分配给自己的 CommandPool 录制 Secondary Command Buffers，不调用 acquire/present，不修改共享容器。

### 16.2 私有组件

| 组件 | 职责 |
|---|---|
| `VulkanBackend` | 后端总协调与错误恢复 |
| `VulkanInstanceContext` | Instance、验证层、调试消息 |
| `VulkanDeviceContext` | Physical/Logical Device、Queue Family、能力 |
| `VulkanSwapchain` | Surface format、image/view、重建 |
| `VulkanRenderPass` | 传统 Render Pass 与 Framebuffer |
| `VulkanPipelineLibrary` | Shader Module、Pipeline Layout、Graphics Pipeline |
| `VulkanDescriptorManager` | Frame/Chunk Descriptor 资源 |
| `VulkanMemoryAllocator` | 内存页、专用分配、映射和统计 |
| `VulkanUploadManager` | Staging Ring、传输提交和完成值 |
| `VulkanFrameContext` | 每飞行帧命令、同步和临时分配 |
| `VulkanCommandRecorder` | 多线程 Secondary CB 录制 |
| `VulkanPipelineCacheStore` | Pipeline Cache 读取与原子保存 |
| `CudaVulkanInterop` | 外部内存和外部信号量私有实现 |

### 16.3 最低能力和设备选择

最低 Vulkan API 版本为 1.2。设备必须支持图形队列、目标 Surface present、Swapchain 扩展和项目所需 Buffer/Descriptor 限制。优先使用独立 transfer queue，但不存在时复用 graphics queue。设备评分考虑 discrete GPU、Device Local 内存、可选 memory budget、timeline semaphore 和与 CUDA 匹配能力；显式启用 CUDA=on 时必须选择可互操作的同一物理 GPU，否则失败。

逻辑设备启用实际使用的最小 Feature/Extension 集合。验证层仅在开发/测试配置启用；缺失验证层不得使 Release 构建失败。

### 16.4 帧上下文与同步

固定 2 个飞行帧。每个 `VulkanFrameContext` 拥有：

- 一个主 CommandPool 和 Primary Command Buffer；
- 每个录制 worker 一个独立 CommandPool；
- imageAvailable 二进制信号量；
- renderFinished 二进制信号量；
- frameComplete Fence；
- 帧内 Descriptor/Upload 临时分配区；
- 已提交资源租约列表。

帧开始只等待即将复用 FrameContext 的 Fence，不等待整个设备。随后 reset 对应 pools 和临时区。acquire 使用 imageAvailable；graphics submit 等待 imageAvailable，并发出 renderFinished；present 等待 renderFinished。

内部上传和延迟释放优先使用 Timeline Semaphore。每次提交获得单调递增值；资源记录最后使用值，只有查询到 completedValue 大于等于该值才销毁。正常帧、上传和 Swapchain 重建不得使用 `vkDeviceWaitIdle`；仅最终 shutdown 或无法细分等待的致命恢复兜底允许一次，并须记录原因。

### 16.5 传统 Render Pass

首版使用传统 Render Pass：一个颜色 Attachment 和可选深度 Attachment。颜色初始布局 undefined、最终布局 present；clear 在 Render Pass 开始执行。Swapchain 格式变化时重建 Render Pass 兼容对象、Pipeline 和 Framebuffer。仅尺寸变化且格式不变时可以复用兼容 Render Pass/Pipeline。

### 16.6 多线程 Secondary Command Buffer

协调线程把可见绘制列表切分为最多 `min(recordingThreadCount, drawCount)` 个连续区间。每个 worker：

1. 从当前 FrameContext 的专属 CommandPool 分配/重置 Secondary CB；
2. 使用 `VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT`；
3. 填写正确 inheritance renderPass/subpass/framebuffer；
4. 绑定共享 Pipeline 和 Descriptor；
5. 对区间内 DrawChunk 绑定 Buffer、Push Constant 并 `vkCmdDraw`；
6. 结束并返回 CB 与统计。

协调线程按区间序号稳定排序后在 Primary Render Pass 中调用 `vkCmdExecuteCommands`。录制任务必须在本帧截止点前完成；若单个 worker 失败，取消本帧提交并报告，不执行部分无效 CB。资源列表在任务启动前冻结，录制期间不发生会移动 Buffer 的分配器操作。

### 16.7 Descriptor 与 Pipeline

- set 0 binding 0：每帧 FrameData UBO；
- set 0 binding 1：ChunkData SSBO；
- 顶点属性 location 与第 13.2 节一致；
- Push Constant range 不超过 32 bytes；
- Descriptor Set 按飞行帧独立，避免更新正在使用的集合；
- Pipeline key 至少包含 Swapchain format、depth format、shading feature variant 和点渲染状态。

Vulkan Shader 在构建期由 GLSL 编译为 SPIR-V。CMake 必须跟踪 include 依赖并在编译失败时终止构建。运行时校验 SPIR-V 文件存在、长度为 4 的倍数和 magic 正确。

### 16.8 Pipeline Cache

启动时读取独立 Pipeline Cache 文件，验证项目 cache version、vendorId、deviceId、driverVersion 和 pipelineCacheUUID；不匹配则忽略。退出时取得缓存数据，写临时文件后原子替换。Pipeline Cache 与 `.dzcpc` 数据缓存分目录和版本，损坏只影响下次 Pipeline 创建耗时。

### 16.9 Swapchain 重建

触发条件：resize、acquire/present 返回 out-of-date，或 surface-lost 恢复。步骤：

1. 标记暂停新帧，但继续 UI 响应；
2. 等待仅引用旧 Swapchain 的两个 FrameContext Fence；
3. 查询非零 framebuffer 尺寸；最小化时不忙循环，等待后续 resize/event；
4. 创建新 Swapchain，并把旧 Swapchain 传给创建信息；
5. 按格式兼容性重建 image view、depth、Framebuffer、Render Pass/Pipeline；
6. 成功后销毁旧资源并恢复帧循环；失败保留可诊断状态。

## 17. Vulkan 显存与上传管理

### 17.1 分配器接口边界

`VulkanMemoryAllocator` 是 Vulkan Target 私有类型，返回内部 `AllocationHandle`，其中保存页 ID、offset、size、memory type、映射地址和专用标记。Buffer/Image RAII 对象拥有 AllocationHandle，并负责先销毁绑定资源、再把区间延迟归还分配器。

### 17.2 内存页

- Device Local 默认页：256 MiB；
- Host Visible 默认页：64 MiB；
- 请求大于对应半页，或 Vulkan 要求/建议 Dedicated Allocation 时独立分配；
- 页按 memoryTypeIndex 分池，不跨类型复用；
- Host Visible 优先 host coherent；非 coherent 时按 nonCoherentAtomSize 对齐 flush/invalidate。

每页维护按 offset 排序的空闲区间和按 size 可检索的索引。分配使用 best-fit：选择满足大小/对齐的最小区间，切分前后余量；释放时与相邻区间合并。所有 size、alignment、offset 运算使用 checked 64 位计算。

首版不压缩搬迁，不移动已绑定 Buffer；外部共享资源不得从普通页子分配。

### 17.3 Staging Ring Buffer

Host Visible Staging Ring 使用单调绝对 head/tail 计数映射到环形 offset。每个上传切片按设备对齐要求分配并关联 timeline 完成值。空间不足时：

1. 回收已完成切片；
2. 尝试换行并利用首部连续空间；
3. 仍不足则创建受预算约束的临时专用 staging；
4. 若预算不允许则推迟低优先级上传，不阻塞整个设备。

CPU 写入后必要时 flush；transfer CB 执行 buffer copy，并用 pipeline barrier 把 transfer write 转为 vertex/SSBO read。若 transfer 与 graphics queue family 不同，执行明确 ownership transfer。

### 17.4 延迟释放和记账

释放请求进入按 timelineValue 排序的队列。每帧开始和上传提交后查询完成值并回收。统计至少包括 reserved、allocated、free、dedicated、external 和 fragmentation ratio。外部分配计入 GPU 预算，但单独标识不可搬迁。

## 18. CUDA-Vulkan 互操作

### 18.1 设备匹配

优先使用 Vulkan physical device ID properties 的 UUID/LUID 与 CUDA device UUID/LUID 比较。Windows 上要求 LUID 和 node mask 兼容。禁止仅根据设备名称猜测匹配。同一资源必须由匹配的 Vulkan 与 CUDA Device 使用。

### 18.2 外部内存

互操作 Buffer 使用专用 Vulkan allocation，创建时带可导出外部内存 handle 类型；绑定后导出 OS handle，再导入 CUDA external memory。导入成功后立即按平台所有权规则关闭本地临时 OS handle；Vulkan allocation 和 CUDA external memory 生命周期由 `CudaVulkanInterop` 统一管理。

资源状态：

```text
VulkanWritable -> ReadyForCuda -> CudaWritable -> ReadyForVulkan -> VulkanReadable
```

状态迁移必须同时有队列提交、外部信号量和适当 memory barrier，不能只改 CPU 标志。

### 18.3 外部同步

CUDA 外部同步使用可导出的二进制信号量，避免假定所有 CUDA/平台组合支持导出 Timeline Semaphore。每个互操作槽维护一对方向明确的信号量：VulkanToCuda 和 CudaToVulkan；只在前一轮完整完成后复用。

1. Vulkan 提交写入/准备资源并 signal VulkanToCuda；
2. CUDA stream wait external semaphore；
3. CUDA Kernel 原地处理；
4. CUDA stream signal CudaToVulkan；
5. Vulkan graphics/compute submit wait CudaToVulkan，并用 barrier 进入 vertex/SSBO read。

不得 CPU 轮询后再 memcpy。任何外部 API 错误必须包含 Vulkan/CUDA 原始返回码和资源状态。

### 18.4 生命周期

关闭或淘汰互操作资源时：阻止新提交，等待与该资源关联的精确 fence/semaphore 完成，先销毁 CUDA 映射和 external memory/semaphore，再销毁 Vulkan Buffer、Semaphore 和 DeviceMemory。不得先释放导出内存。

## 19. Camera 抽象设计

### 19.1 数据结构

```cpp
struct CameraState final {
    glm::dvec3 position{0.0};
    glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
    double verticalFieldOfViewRadians{0.0};
    double nearPlane{0.0};
    double farPlane{0.0};
};

struct CameraMatrices final {
    glm::mat4 view{1.0F};
    glm::mat4 projection{1.0F};
    glm::dvec3 cameraOrigin{0.0};
};

struct FrustumPlane final { glm::dvec4 equation{0.0}; };
struct ViewFrustum final { std::array<FrustumPlane, 6> planes; };

enum class InputEventType : std::uint8_t {
    PointerMove,
    PointerButton,
    Wheel,
    Key,
    Focus,
    ResetRequest
};

struct InputEvent final {
    InputEventType type{InputEventType::PointerMove};
    std::uint32_t code{0};
    double valueX{0.0};
    double valueY{0.0};
    bool pressed{false};
    std::uint32_t modifiers{0};
};

class ICameraController {
public:
    virtual ~ICameraController() = default;
    virtual Result<void> submitInput(const InputEvent& event) = 0;
    virtual Result<void> update(double deltaSeconds, const Bounds3d& sceneBounds) = 0;
    virtual const CameraState& state() const noexcept = 0;
    virtual CameraMatrices matrices(const RenderSize& size) const = 0;
    virtual ViewFrustum frustum(const RenderSize& size) const = 0;
    virtual Result<void> reset(const Bounds3d& sceneBounds) = 0;
};
```

### 19.2 明确待定边界

本设计不选择任何具体相机控制模型；不定义鼠标键位、键盘映射、移动/旋转/缩放速度、初始视图、重置语义、near/far 自动规则及 UI 参数。上述内容必须等用户提供参考源码后另行设计，并更新需求、概要设计和本节。当前测试仅验证输入透传、接口替换、矩阵有限性和视锥体数学工具，不验证具体交互结果。


## 20. Qt 应用层与 Engine 通信

### 20.1 UI 类职责

| 类 | Qt 基类 | 职责 |
|---|---|---|
| `MainWindow` | `QMainWindow` | 菜单、Dock、状态栏、参数控件和视图容器 |
| `OpenGLRenderWidget` | `QOpenGLWidget` | Phase 1 Context 生命周期和帧回调 |
| `VulkanRenderWindow` | `QWindow` | Phase 2 原生窗口、暴露/尺寸事件和协调线程唤醒 |
| `RenderWindowContainer` | `QWidget` | 使用 `createWindowContainer` 嵌入 Vulkan QWindow |
| `EngineUiAdapter` | `QObject` | Qt 值与标准 Engine 命令/快照/事件转换 |
| `SettingsController` | `QObject` | QSettings/INI 读写 UI 设置 |
| `LogPanelModel` | Qt Model | 展示已格式化日志，不接触 Engine 内部对象 |

Qt 类只存在于 `dzc_app`。`EngineUiAdapter` 可以使用信号槽，但持有的是 `std::unique_ptr<Engine>` 或非拥有引用；Engine 本身不得继承 `QObject`。

### 20.2 Phase 1 帧驱动

- `initializeGL`：创建并初始化 Engine；
- `resizeGL`：构造 `ResizeCommand` 或在同一 GL 线程调用 `resize`；
- `paintGL`：计算 delta，调用 `update` 和 `render`，随后读取 Snapshot；
- 活动显示时由 Qt update 调度下一帧，窗口不可见或最小化时降低/停止连续刷新；
- 文件选择和参数信号只入队，不在槽函数执行 I/O。

### 20.3 Phase 2 帧驱动

`VulkanRenderWindow` 把 expose/resize/close 转换为线程安全控制消息。协调线程在窗口可渲染时执行 `update/render`；UI 线程只 enqueue 命令、轮询事件和读取 Snapshot。原生窗口销毁前必须先停止协调线程并完成 Vulkan Surface/Swapchain 释放。

### 20.4 UI 功能映射

- 文件菜单：选择 PCD/PLY，发送 LoadDataset；
- 加载面板：进度、取消、数据统计；
- 渲染参数：点大小、着色模式、固定色、背景色、CUDA 模式；
- 状态栏：后端、FPS、帧时间、可见点/块、CPU/GPU 驻留；
- 日志面板：Info/Warning/Error；
- View Reset：只发送 `ResetViewCommand`，具体效果待 Camera 设计；
- 输入事件：Qt 事件转换为 `InputEvent`，不把 `QKeyEvent` 等传入 Engine。

## 21. 配置、日志与性能报告

### 21.1 配置优先级

优先级从高到低：命令行、QSettings/INI、内置默认值。命令行至少支持概要设计列出的 backend、cuda、log-level、cache-directory、gpu-memory-budget、cpu-cache-budget；可补充线程和队列容量参数，但参数名变更必须同步帮助文本和测试。

默认值：

- backend：OpenGL；
- cuda：auto；
- log level：info；
- command/event capacity：1024；
- I/O 最大并发：2；
- 线程、CPU/GPU 预算：0，表示自动计算。

显式 backend 不可用时启动失败，不静默切换。命令行非法值返回 Configuration 错误并显示用法。QSettings 只由 UI 读取，再转换为 `EngineConfig`；Engine 不包含 QSettings。

### 21.2 日志格式与轮转

日志为 UTF-8 文本，每行：

```text
2026-08-13T23:59:59.123+08:00 [INFO] [Render.Vulkan] code=0 dataset=7 chunk=18 frame=42 message="..."
```

默认单文件最大 20 MiB，保留最近 10 个文件。达到上限后关闭、按序号轮转并重新创建；轮转失败时继续写当前可用 Sink 或 stderr，不能因日志失败终止渲染。记录内容不得逐点输出。Logger 使用默认容量 1024 的有界 FIFO 队列和单个后台日志线程，生产者线程安全提交 `LogRecord`，由日志线程串行调用主 Sink。队列满时 Trace/Debug/Info 可丢弃，Warn 等待队列空间；Error 在队列满时同步写备用 Sink，备用 Sink 缺失或失败通过 `write()` 返回 `false` 报告。Logger 的 `shutdown()` 先拒绝新记录，再排空已接受队列并汇合日志线程；不扩展 `ILogSink`，也不由 Logger 主动关闭具体 Sink。DG-004 不新增 Fatal 级别，文档中的 Error/Fatal 统一按 Error 处理。

### 21.3 指标采集

`DiagnosticsService` 维护 CPU scoped timer、GPU timestamp/query 延迟结果和固定字段指标注册器。帧统计由线程安全的 `FrameStatistics` 聚合器提供：调用方传入 `std::chrono::nanoseconds` frame delta，聚合器通过注入的 `IClock` 获取时间戳，同时维护最近最多 120 帧和最近 1 秒两个窗口。FPS 使用窗口样本数除以窗口首尾时间跨度，平均帧时使用窗口内有效样本的算术平均值并以毫秒输出；无样本返回零值。非正 delta 和时钟回退样本被拒绝，`snapshot()` 会按当前时钟淘汰过期时间窗口样本。`addFrame()`、`snapshot()` 和 `reset()` 均受互斥量保护。

通用指标由 `MetricsRegistry` 维护，并通过 `MetricsSnapshot` 返回一致值副本。快照固定包含以下分组：

- `PerformanceMetrics`：FPS、CPU frame ms、GPU frame ms；
- `GeometryMetrics`：visible/submitted points、visible/drawn chunks；
- `TransferMetrics`：reader bytes、cache bytes、upload bytes；
- `MemoryMetrics`：CPU/GPU resident 与 budget；
- `LodMetrics`：LOD 请求、命中、祖先回退；
- `RuntimeMetrics`：task queue depth、I/O 活跃数；
- `RecordingMetrics`：聚合 recording draw count、duration 和 worker count；
- `ComputeMetrics`：CUDA processed points 与 synchronization duration。

`MetricsRegistry` 不持有 `FrameStatistics`；调用方从 `FrameStatistics::snapshot()` 获取 FPS 和帧耗时后调用对应 setter。Registry 使用固定字段更新方法，不使用字符串键值注册表，也不保存逐 worker 动态列表。

`beginFrame(frameId)` 原样保存帧号，并清零 FPS、帧耗时、点/块/字节计数、LOD 计数、录制工作量和 CUDA 帧级指标；CPU/GPU resident、budget、task queue depth 和 I/O active count 等状态指标跨帧保留。`reset()` 清零全部字段并将帧号恢复为 0。帧号不校验回退、重复或严格递增。

所有公开操作由同一互斥量保护，`snapshot()` 不自动重置并返回调用时刻的一致副本。无符号整数累加采用饱和到最大值的规则；FPS、CPU/GPU frame ms、录制聚合耗时和 CUDA synchronization duration 只接受有限且非负值，非法输入返回 `false` 并保持旧值。录制聚合耗时合法输入执行累加，上溢时钳制到 double 最大值。
### 21.4 CSV 与 Markdown

性能明细 CSV 由调用方组装固定的 `PerformanceCsvRow` 后交给 `PerformanceCsvWriter` 写出。Writer 不直接依赖 `MetricsRegistry` 或 `FrameStatistics`，不持有动态字符串指标注册表，也不扩展 DG-006 公共字段。

`PerformanceCsvWriter` 使用 Pimpl、RAII 和互斥量，公共接口为：

```cpp
class PerformanceCsvWriter final {
public:
    explicit PerformanceCsvWriter(const std::filesystem::path& path);
    ~PerformanceCsvWriter();

    PerformanceCsvWriter(const PerformanceCsvWriter&) = delete;
    PerformanceCsvWriter& operator=(const PerformanceCsvWriter&) = delete;
    PerformanceCsvWriter(PerformanceCsvWriter&&) = delete;
    PerformanceCsvWriter& operator=(PerformanceCsvWriter&&) = delete;

    bool write(const PerformanceCsvRow& row);
    bool close() noexcept;
    bool isOpen() const noexcept;
};
```

固定表头和列顺序如下，不得增加其他指标列：

```text
utcTime,frameId,backend,width,height,cpuFrameMs,gpuFrameMs,fps,
visiblePoints,submittedPoints,visibleChunks,cpuResidentBytes,gpuResidentBytes,
uploadBytes,lodMisses,recordingWorkers
```

文件以二进制写入模式创建或截断；不自动创建父目录；成功打开后立即写表头。文件编码为 UTF-8、无 BOM；表头和数据记录使用单个 LF 字节结尾。`utcTime` 使用调用方提供的 `system_clock::time_point`，按 UTC `YYYY-MM-DDTHH:MM:SS.mmmZ` 格式化。整数使用无前导十进制；浮点使用 classic/C locale、小数点为 `.`、固定 6 位小数；`std::nullopt` 输出空字段并保持列数不变。

字符串字段遵循 CSV 转义：字段包含逗号、双引号、CR 或 LF 时用双引号包裹，字段内双引号加倍。optional 浮点为 NaN 或正负无穷时整行拒绝且不写入；打开、写入、flush 失败均以 `false` 报告，不抛异常、不写 `stderr`。`close()` 在互斥量保护下 flush 后关闭，重复调用幂等；析构自动调用 `close()`。不创建后台刷新线程，`write()`、`close()` 和 `isOpen()` 可并发调用，关闭后写入全部失败。

调用方从指标快照组装行时将 LOD 计数映射为 `lodMisses = max(lod.requests - lod.hits, 0)`；Writer 只输出上述固定列。Markdown 摘要包含项目版本、构建类型、操作系统、CPU、GPU、驱动、内存/显存、CUDA、数据集身份、点数、分辨率、后端、参数、样本帧数、平均 FPS、CPU/GPU 平均帧时和错误计数。基准硬件、低帧率百分位及可重复相机路径尚未确定，摘要必须标为 TBD，不得给出虚构验收数据。

Markdown 性能摘要由调用方组装固定的 `PerformanceSummary` 后交给 `PerformanceSummaryWriter` 写出。
固定数据结构为：

```cpp
struct PerformanceSummary final {
    std::optional<std::string> projectVersion;
    std::optional<std::string> buildType;
    std::optional<std::string> operatingSystem;
    std::optional<std::string> cpu;
    std::optional<std::string> gpu;
    std::optional<std::string> driver;
    std::optional<std::string> memory;
    std::optional<std::string> gpuMemory;
    std::optional<std::string> cuda;
    std::optional<std::string> datasetIdentity;
    std::optional<std::string> backend;
    std::optional<std::string> parameters;
    std::optional<std::string> benchmarkHardware;
    std::optional<std::string> cameraPath;
    std::optional<std::uint64_t> pointCount;
    std::optional<std::uint64_t> sampleFrameCount;
    std::optional<std::uint64_t> errorCount;
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;
    std::optional<double> averageFps;
    std::optional<double> averageCpuFrameMilliseconds;
    std::optional<double> averageGpuFrameMilliseconds;
    std::optional<double> lowFrameRatePercentile;
};
```

`PerformanceSummary` 包含 Environment、Dataset、Configuration、Statistics 和 Errors 五组固定字段；Writer 不依赖 `MetricsRegistry` 或 `FrameStatistics`，不使用动态字符串指标注册表。

`PerformanceSummaryWriter` 使用 Pimpl、RAII 和互斥量，固定接口为：

```cpp
class PerformanceSummaryWriter final {
public:
    explicit PerformanceSummaryWriter(const std::filesystem::path& path);
    ~PerformanceSummaryWriter();

    PerformanceSummaryWriter(const PerformanceSummaryWriter&) = delete;
    PerformanceSummaryWriter& operator=(const PerformanceSummaryWriter&) = delete;
    PerformanceSummaryWriter(PerformanceSummaryWriter&&) = delete;
    PerformanceSummaryWriter& operator=(PerformanceSummaryWriter&&) = delete;

    bool write(const PerformanceSummary& summary);
    bool close() noexcept;
    bool isOpen() const noexcept;
};
```

摘要文件固定输出 `# Performance Summary` 标题及 `Environment`、`Dataset`、`Configuration`、`Statistics`、`Errors` 五个 Markdown 表格章节，字段顺序不得改变。文件以二进制模式创建或截断，不自动创建父目录；编码为 UTF-8、无 BOM；所有行使用单个 LF。整数使用十进制无前导格式，double 使用 classic/C locale、小数点为 `.`、固定 6 位小数。普通 `std::nullopt` 输出空值；`benchmarkHardware`、`cameraPath` 和 `lowFrameRatePercentile` 缺失时输出 `TBD`。

字符串表格单元格中，反斜杠转换为 `\\`，`|` 转换为 `\|`，CR/LF 转为空格。optional double 为 NaN 或正负无穷时拒绝整份摘要且不写入。Writer 只允许首次成功写出一份完整摘要；`write()`、`close()` 和 `isOpen()` 均受同一互斥量保护，`close()` flush 后关闭并幂等，关闭后拒绝写入，析构函数自动关闭。

## 22. 错误码与恢复策略

### 22.1 稳定错误码

每个 Domain 内 code 稳定，日志可附底层 API 返回码但不能替代项目错误码。

| Domain | Code | 名称 | 默认恢复 |
|---|---:|---|---|
| General | 1 | InvalidArgument | 拒绝操作 |
| Configuration | 1 | InvalidValue | 提示并终止启动/保留旧值 |
| Configuration | 2 | BackendNotBuilt | 启动失败 |
| FileIo | 1 | OpenFailed | 数据集加载失败，Engine 可用 |
| FileIo | 2 | ReadFailed | 取消该任务，可重试 |
| FileIo | 3 | WriteFailed | 不发布缓存/报告失败 |
| DataFormat | 1 | MissingPosition | 拒绝数据集 |
| DataFormat | 2 | CorruptData | 拒绝数据集或缓存重建 |
| DataFormat | 3 | NumericOverflow | 拒绝对应输入 |
| Task | 1 | Cancelled | 正常取消事件 |
| Task | 2 | QueueFull | 调用方稍后重试/合并 |
| Task | 3 | QueueClosed | 忽略新任务并关闭 |
| OpenGL | 1 | UnsupportedVersion | Phase 1 初始化失败 |
| OpenGL | 2 | ShaderCompileFailed | 初始化/绘制配置失败 |
| OpenGL | 3 | ResourceCreationFailed | 淘汰后重试或报错 |
| Vulkan | 1 | UnsupportedVersion | Phase 2 初始化失败 |
| Vulkan | 2 | DeviceSelectionFailed | 初始化失败 |
| Vulkan | 3 | SwapchainFailed | 尝试重建 |
| Vulkan | 4 | DeviceLost | 一次受控恢复或安全停止 |
| Vulkan | 5 | PipelineCreationFailed | 对应后端失败 |
| Cuda | 1 | DeviceUnavailable | auto 降级，on 失败 |
| Cuda | 2 | KernelFailed | 停止该计算，按模式降级/失败 |
| Interop | 1 | DeviceMismatch | 禁止互操作 |
| Interop | 2 | ImportExportFailed | 释放半成资源，降级/失败 |
| Interop | 3 | SynchronizationFailed | 停止使用资源 |
| Cache | 1 | IdentityMismatch | 删除/忽略并重建 |
| Cache | 2 | VersionMismatch | 忽略并重建 |
| Cache | 3 | ChecksumMismatch | 忽略并重建 |
| Resource | 1 | BudgetExceeded | 降低 LOD、淘汰、推迟上传 |
| Resource | 2 | AllocationFailed | 淘汰后一次重试，仍失败则报告 |
| Internal | 1 | InvalidState | 拒绝非法生命周期调用 |
| Internal | 2 | InvariantViolation | 致命错误并安全停止 |

### 22.2 错误传播

同步调用用 `Result<T>`；异步错误写日志并发 `ErrorEvent`；Snapshot 保存最近一个对用户有意义的错误。第三方异常和 API 错误在所属适配层转换。析构、`shutdown` 和回滚函数不得抛异常。

### 22.3 恢复边界

- 源文件错误：只关闭当前 Dataset；
- `.dzcpc` 错误：忽略、删除或覆盖缓存，从源重建；
- 单 Chunk 失败：保持祖先/其他 Chunk，记录缺失；
- Swapchain out-of-date：重建，不视为致命；
- CUDA auto 失败：禁用 CUDA，继续图形路径；
- GPU 内存不足：淘汰、降低 LOD、暂停上传；
- Device lost：停止新提交，释放可安全释放的资源；首版不保证透明恢复，若无法重建则进入 Failed。

## 23. 生命周期、回滚与关闭

### 23.1 初始化事务

初始化按 Diagnostics → TaskSystem → Reader/Cache → Platform/Render → Compute/Interop → Scene 顺序。每一步由局部 RAII 对象持有；全部成功后转移给 `Engine::Impl`。任一步失败自动逆序析构，不能留下后台线程、临时缓存或注册互操作资源。

### 23.2 正常关闭顺序

1. 原子设置 stopping，关闭 Command 输入；
2. 取消 Dataset 和后台任务，停止接收新任务；
3. 停止协调线程产生新帧和上传；
4. 汇合 CPU worker/recording worker；
5. 等待具体在途 Frame/Upload 同步；
6. 解除 CUDA-GL/Vulkan 互操作；
7. 销毁 Pipeline、Descriptor、Chunk GPU 资源；
8. 销毁 Swapchain/Surface 或 GL 后端资源；
9. 释放 CPU Dataset/Cache/Reader；
10. flush 性能报告和日志；
11. 发布 Stopped 快照并关闭 Event 队列。

`shutdown` 必须幂等；析构函数调用 `shutdown` 作为兜底。不得在线程仍可能访问对象时释放资源，不得先销毁图形 Buffer 再注销 CUDA。

### 23.3 Dataset 替换

新 Load 命令到达时取消旧 Dataset 任务；旧 Dataset 的 GPU 资源在租约和同步完成后延迟释放。新 Dataset 只有在元数据可用并建立有效 Scene 后才成为 current。若新加载失败，UI 可选择继续显示旧数据集；具体 UI 选择需通过命令明确，Engine 不擅自删除仍有效的旧 Dataset。

## 24. CMake 与源码组织

### 24.1 Target 依赖

保持概要设计中的 Target 划分。强制依赖方向：`dzc_app → dzc_engine_api/core → render/compute/data abstractions`；具体 OpenGL/Vulkan/CUDA 实现只在 Composition Root 链接。`dzc_data_pcl` 是唯一链接 PCL 的 Target，`dzc_app` 是唯一链接 Qt Widgets 的 Target。

公共 include 目录只导出 `include/dzc`；模块私有头不得通过 PUBLIC include directories 泄漏。启用一个后端不能要求另一个后端 SDK 存在。

### 24.2 构建选项和 Shader

```cmake
option(DZC_ENABLE_OPENGL "Build OpenGL backend" ON)
option(DZC_ENABLE_VULKAN "Build Vulkan backend" OFF)
option(DZC_ENABLE_CUDA "Build CUDA compute and interop" OFF)
option(DZC_BUILD_TESTS "Build tests" ON)
```

至少启用一个图形后端，否则配置失败。启用项依赖缺失时 CMake 明确失败。Vulkan Shader 作为 custom command 输出 SPIR-V 并成为 Vulkan Target 依赖；OpenGL Shader 复制到运行目录或嵌入只读资源，仍在运行时编译。

### 24.3 编译规则

统一 C++17；警告级别按编译器设置并在项目代码启用较严格警告。Debug Vulkan 构建可启用验证层；Release 不依赖其存在。生成安装包时携带 Qt runtime、已选择后端所需运行库、Shader/SPIR-V 和许可证，不携带未启用后端依赖。


## 25. 测试详细设计

### 25.1 自有轻量测试框架

测试框架提供测试注册、断言、临时目录、能力标签和 JUnit/文本结果输出，不引入 GoogleTest。测试进程的退出码必须反映失败数。图形、CUDA 和性能用例带能力标签；环境不满足时报告 `Skipped` 及原因，不得伪造通过。

Engine 测试允许注入 `FakeRenderBackend`、`NullRenderBackend`、`FakeComputeBackend`、内存 Reader、确定性单线程执行器和内存日志 Sink。随机算法测试必须记录种子；八叉树代表点测试使用固定输入顺序扰动验证确定性。

### 25.2 单元测试

| 测试组 | 关键用例 |
|---|---|
| Result/Error | success/failure、错误域和无异常边界 |
| StateMachine | 全部合法迁移、非法调用、幂等 shutdown |
| BoundedQueue | 多生产者、容量、合并、关闭、关键事件保留 |
| Coordinates | 大坐标局部化、有限性、精度误差界限 |
| GridChunkBuilder | 目标/最大点数、稳定 ID、临时 run、取消 |
| Octree/Lod | Octant 边界、最大深度、代表点确定性、1.5/2.0 px 滞回 |
| Frustum | 内部、外部、相交、退化包围盒 |
| CacheFormat | 小端字段、对齐、CRC、溢出、截断、版本/身份失效 |
| Allocator | best-fit、对齐、合并、专用分配、延迟释放 |
| Budget | 自动预算、低内存降级、淘汰评分与祖先保留 |
| Diagnostics | FPS 窗口、CSV 列、轮转和队列丢弃策略 |

### 25.3 集成与图形测试

- PCD/PLY → Reader → Chunk，覆盖 XYZ、RGB/RGBA、intensity、缺字段、NaN 和损坏文件；
- Engine 命令 → 异步任务 → Event/Snapshot，覆盖加载、取消、替换、卸载；
- `.dzcpc` 写入 → 原子发布 → 重开 → 随机 Chunk 校验；
- OpenGL 4.5 上下文、Shader 编译、属性布局、resize 和资源释放；
- Vulkan Instance/Device/Swapchain、传统 Render Pass、两飞行帧、Secondary CB 和重建；
- Vulkan Validation Layer 测试中出现由项目代码导致的 error 级消息即失败；
- CUDA-GL/Vulkan 测试对比 CPU 参考结果，并验证无 CPU 回读再上传计数；
- 故障注入覆盖分配失败、缓存 CRC 错误、任务取消、互操作导入失败和 Swapchain out-of-date。

### 25.4 性能测试

Phase 1 至少使用 10,000,000 点；Phase 2 至少使用 100,000,000 点。每次运行先预热，再采集固定时长或固定帧数，原始帧数据写 CSV，摘要写 Markdown。Phase 2 在 1920×1080 持续相机运动场景的平均帧率必须不低于 30 FPS，目标 60 FPS。

基准 CPU/GPU、驱动、数据集、低帧率百分位阈值和相机运动路径仍待确定。因此在参考源码和基准环境确认前，只能验证报告链路和人工输入场景，不能宣称完成最终 `AC-P2-011`。

### 25.5 Phase 1 验收映射

| 验收项 | 详细设计证据 |
|---|---|
| AC-P1-001 构建 | 24.1–24.3 CMake 与依赖隔离 |
| AC-P1-002 OpenGL 4.5 | 14.2 能力检查和 GPU/版本日志 |
| AC-P1-003 PCD/PLY | 10 Reader 和 25.3 集成测试 |
| AC-P1-004 错误处理 | 22 错误模型和故障注入 |
| AC-P1-005 千万点 | 25.4 性能数据集 |
| AC-P1-006 基础渲染 | 13、14 和 resize 图形测试 |
| AC-P1-007 四种着色 | 13.2、14.4 |
| AC-P1-008 分块剔除 | 9.3 和可见统计 |
| AC-P1-009 UI 响应/取消 | 7、20 |
| AC-P1-010 状态统计 | 6.3、21.3 |
| AC-P1-011 CUDA 降级 | 15.2 |
| AC-P1-012 CUDA-GL | 15、25.3 |
| AC-P1-013 架构边界 | 2、4、20、24 |
| AC-P1-014 相机边界 | 19.2 |

### 25.6 Phase 2 验收映射

| 验收项 | 详细设计证据 |
|---|---|
| AC-P2-001 Vulkan 独立启动 | 16.3、24 |
| AC-P2-002 公共功能等价 | 4、6、13、20 |
| AC-P2-003 Swapchain | 16.9 |
| AC-P2-004 多线程录制 | 16.6、21.3 |
| AC-P2-005 Secondary CB | 16.6 |
| AC-P2-006 Pipeline Cache | 16.8 |
| AC-P2-007 异步调度 | 7、17.3 |
| AC-P2-008 显存预算 | 12.6–12.7、17 |
| AC-P2-009 LOD | 12.1–12.4 |
| AC-P2-010 亿级数据 | 11、12、25.4 |
| AC-P2-011 平均 FPS | 21.4、25.4；最终路径待定 |
| AC-P2-012 CUDA-Vulkan | 18 |
| AC-P2-013 同步正确 | 16.4、18.3、25.3 |
| AC-P2-014 后端隔离 | 13、20、24 |
| AC-P2-015 性能报告 | 21.4 |

## 26. 需求追踪矩阵

### 26.1 功能需求

| 需求 | 详细设计章节 |
|---|---|
| FR-COM-001 | 3.3、4.1、21.1、24.2 |
| FR-COM-002 | 14.2、16.3、18.1、21.1 |
| FR-DATA-001 | 10 |
| FR-DATA-002 | 20.4 |
| FR-DATA-003 | 6.2–6.3、20.4 |
| FR-DATA-004 | 6.1、7.2、20.4 |
| FR-DATA-005 | 6.3、8.4 |
| FR-DATA-006 | 8.2 |
| FR-REN-001 | 13、14、16 |
| FR-REN-002 | 6.1、13.2、14.4 |
| FR-REN-003 | 3.3、13.2、14.4 |
| FR-REN-004 | 6.1、13.1、20.4 |
| FR-REN-005 | 4.2–4.3、16.9、20.2–20.3 |
| FR-CAM-001 | 6.1、19.1、20.4 |
| FR-CAM-002 | 19.2、29.1 |
| FR-CAM-003 | 6.1、19.1–19.2 |
| FR-VIS-001 | 8.3、9、12.1 |
| FR-VIS-002 | 9.3、12.3 |
| FR-VIS-003 | 6.3、21.3 |
| FR-CUDA-001 | 3.3、4.1、15.2、21.1 |
| FR-CUDA-002 | 15.2、18.3 |
| FR-CUDA-003 | 15、18 |
| FR-CUDA-004 | 15.1、18.3 |
| FR-UI-001 | 20.1 |
| FR-UI-002 | 20.1–20.3 |
| FR-UI-003 | 20.4 |
| FR-UI-004 | 6.3、20.4 |
| FR-UI-005 | 20.4、21.2 |
| FR-STAT-001 | 6.3、21.3 |
| FR-STAT-002 | 21.3–21.4 |
| FR-GL-001 | 14.2 |
| FR-GL-002 | 13.2、14.3 |
| FR-GL-003 | 14.2、14.4、24.2 |
| FR-GL-004 | 15 |
| FR-VK-001 | 16.3 |
| FR-VK-002 | 16.9 |
| FR-VK-003 | 16.1、16.6 |
| FR-VK-004 | 16.6 |
| FR-VK-005 | 16.8 |
| FR-VK-006 | 17 |
| FR-VK-007 | 17.3 |
| FR-VK-008 | 16.4、16.9 |
| FR-LOD-001 | 11.3、12.1–12.2 |
| FR-LOD-002 | 12.3 |
| FR-LOD-003 | 12.4 |
| FR-LOD-004 | 12.6–12.7、17.4 |
| FR-LOD-005 | 12.5–12.7 |
| FR-VKCUDA-001 | 18.2 |
| FR-VKCUDA-002 | 18.3 |
| FR-VKCUDA-003 | 18.1 |

### 26.2 非功能需求

| 需求 | 详细设计章节 |
|---|---|
| NFR-PERF-001 | 9、11–12、25.4 |
| NFR-PERF-002 | 16.6、21.3–21.4、25.4 |
| NFR-PERF-003 | 7、20 |
| NFR-PERF-004 | 6.2–6.3、7、21.4 |
| NFR-REL-001 | 5.1、22.3 |
| NFR-REL-002 | 15.2、18.4、23 |
| NFR-REL-003 | 11.5、16.8、22.3 |
| NFR-MAIN-001 | 1、24.3 |
| NFR-MAIN-002 | 2、14.1、17.1、23 |
| NFR-MAIN-003 | 2、4.2、5.2 |
| NFR-MAIN-004 | 1.1、2.1、4.2、24.1 |
| NFR-MAIN-005 | 2.2 |
| NFR-MAIN-006 | 2.3 及复杂算法/同步章节 |
| NFR-PORT-001 | 21.2、24.3、29.2 |
| NFR-PORT-002 | 3.4、16.1、24.1 |
| NFR-PORT-003 | 15.2、18、21.1 |
| NFR-TEST-001 | 21.4、25.4、29.2 |
| NFR-TEST-002 | 21.3、25.2–25.3 |

## 27. 架构决策追踪

| ADR | 详细设计落实 |
|---|---|
| ADR-001 两阶段同文档 | 1.2、9–18、25.5–25.6 |
| ADR-002 单程序启动选后端 | 3.3、4.1、21.1、24 |
| ADR-003 模块化 Target | 2.1、24.1 |
| ADR-004 Command/Event/Snapshot | 4、6、20 |
| ADR-005 后端适配线程模型 | 4.3、7、14.1、16.1 |
| ADR-006 可重建 `.dzcpc` | 11、22.3 |
| ADR-007 网格到八叉树 | 9、12 |
| ADR-008 双精度原点与 SoA | 8.2–8.3、11.5 |
| ADR-009 GLAD 与自研 Vulkan 分配 | 14.2、17 |
| ADR-010 普通 QWindow Vulkan | 16.1、20.1、20.3 |
| ADR-011 GLSL 运行时与 SPIR-V 构建期 | 14.4、16.7、24.2 |
| ADR-012 Result 和异步错误 | 3.2、6.2、22 |
| ADR-013 自有日志与测试 | 21、25 |
| ADR-014 Camera/性能未决 | 19.2、21.4、25.4、29 |

## 28. 详细设计决策落实检查

| 决策 | 落实章节 |
|---|---|
| DDD-001 | 全文接口、字段、算法、同步和测试 |
| DDD-002 | 2.2、4.2 |
| DDD-003 | 4.3、5、14.1、16.1 |
| DDD-004 | 3.3、21.1 |
| DDD-005 | 6.3–6.4 |
| DDD-006 | 7.1 |
| DDD-007 | 9.1 |
| DDD-008 | 11 |
| DDD-009 | 8.1、10.2 |
| DDD-010 | 12.1–12.2 |
| DDD-011 | 16.3–16.5、18.3 |
| DDD-012 | 17 |
| DDD-013 | 12.6 |
| DDD-014 | 13.2、14.3、16.7 |
| DDD-015 | 21 |
| DDD-016 | 19、29.1 |

## 29. 待确定事项

### 29.1 相机交互

等待用户提供参考源码后确定 `ICameraController` 的实现类型、输入映射、速度、初始视图、重置语义、裁剪面策略和性能运动路径。在此之前不得实现或文档化为最终具体控制方案，也不得固定键位。

### 29.2 基准环境与性能阈值

仍需确认：CPU、GPU、显存、内存、存储、Windows 版本、驱动、CUDA 版本、正式数据集、低帧率百分位阈值和相机性能路径。当前已确认的 Phase 2 强制指标仅为 1920×1080 持续运动场景平均不低于 30 FPS，目标 60 FPS；最终验收必须在基准环境确定后执行。

### 29.3 非推测规则

实现遇到以下情况必须暂停对应决策并向用户确认：参考源码与 Camera 抽象冲突；源数据包含未定义业务属性；需要改变 `.dzcpc` V1 已确认布局；需要引入新的第三方分配器/日志/测试库；需要改变后端回退语义、预算比例、LOD 阈值或 Chunk 点数。可以继续开发不依赖该决策的模块。

## 30. 设计变更规则

改变公共接口、模块依赖、线程归属、文件格式、坐标精度、GPU 属性布局、CUDA 零拷贝路径、Vulkan 同步/分配、Camera、验收指标或需求追踪时，必须：

1. 记录变更原因和替代方案；
2. 更新需求文档（若外部行为改变）；
3. 更新概要设计的 ADR/模块边界；
4. 更新本详细设计、错误码和测试；
5. 说明缓存兼容与迁移策略；
6. 重新执行受影响的单元、集成、图形和性能测试。

## 31. 结论

本详细设计将 Dzc-RenderEngine 的两阶段架构落实为可编码的 C++17 接口、状态机、线程与队列语义、点云数据布局、网格/八叉树算法、`.dzcpc` V1、OpenGL/Vulkan 资源与同步、CUDA 零拷贝互操作、Qt 边界、诊断、错误、关闭和测试方案。Phase 1 先验证公共模型与 OpenGL 管线；Phase 2 在保持 UI/Engine 契约的基础上增加 Vulkan 多线程录制、LOD 流式调度和预算管理。Camera 具体行为及最终性能基准仍明确保持待确定，等待用户后续输入。
