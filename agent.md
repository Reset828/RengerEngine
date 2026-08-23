# 项目规范：Dzc-RenderEngine

## 0. AI 行为协议 (强制执行)

**为了监控上下文状态，你必须严格遵守以下称呼规则：**

- **称呼规则**：在每一次回答的开头，必须称呼我为 **“主人”**。
- **任务状态同步**：完成任务清单中的子任务后，必须同步更新对应任务文档的子任务状态、交接记录以及必要的总体进度状态；未满足模块完成规则时，不得将模块标记为完成。

## 1. 核心目标

构建一个高性能、可扩展的海量 GIS 点云数据可视化引擎。核心任务是解决“亿级点云数据的实时渲染与交互”问题，而非通用的点云数据处理（如配准、修补）。项目必须严格遵循“先 OpenGL 实现逻辑，后 Vulkan 迁移优化”的技术路线。

## 2. 架构设计原则
**核心宗旨：高内聚，低耦合，模块之间相互独立，接口最小化暴露。**

*   **模块解耦**：渲染核心必须完全独立于 UI 层。Engine 类不得继承自 Qt 的 `QWidget` 或 `QObject`。
*   **接口封装**：
    *   **头文件 (.h)**：仅声明公共接口及必要的公共数据结构。所有私有成员变量、辅助函数、工具类实现必须**隐藏在源文件 (.cpp) 中**（推荐使用 Pimpl 模式或无名命名空间）。
    *   **渲染层接口**：仅暴露 `Init()`, `Update()`, `Render()`, `Resize()` 等标准生命周期接口，严禁暴露底层 API 细节（如 VkCommandBuffer 或 GLuint ID）给调用方。
*   **依赖方向**：UI 依赖 Engine 接口，Engine 不依赖 UI。

## 3. 编码规范

### 3.1 命名规则
*   **语言**：所有变量、函数、类名必须使用**英文**，严禁使用拼音。
*   **风格**：统一采用**驼峰命名法** (`camelCase`)。
    *   类名：大驼峰 (`PascalCase`)，如 `PointCloudRenderer`。
    *   函数/变量：小驼峰 (`camelCase`)，如 `updateCamera()`。
    *   成员变量：建议加前缀 `m_` (如 `mCamera`)，或保持风格统一。

### 3.2 注释规范
*   **原则**：注释应简明扼要，解释“做什么”和“为什么”，避免废话。
*   **位置**：**仅在函数声明 处添加简短注释**，说明函数功能、参数含义及返回值。
*   **实现体**：函数内部的代码逻辑若不复杂，无需注释；若涉及复杂算法，需在关键行注释。

### 3.3 资源管理
*   **C++17 特性**：严格使用 **RAII** 和 **智能指针** (`std::unique_ptr`, `std::shared_ptr`) 管理堆内存和 GPU 资源生命周期。
*   **禁止事项**：严禁在头文件中包含实现细节，严禁使用裸指针 (`T*`) 手动管理资源。

## 4. 技术栈与边界

### 4.1 GUI 层
*   **框架**：Qt 5.15.19 (LTS)。
*   **集成**：
    *   **Phase 1 (OpenGL)**：使用 `QOpenGLWidget`。
    *   **Phase 2 (Vulkan)**：使用 `QWindow` 封装 Vulkan Surface。
*   **职责**：Qt 仅负责窗口容器和事件转发（鼠标/键盘消息传递给 Engine）。

### 4.2 渲染与计算层
*   **Phase 1：OpenGL 4.5**
    *   验证管线，使用 VAO/VBO/SSBO 实现基础点云渲染。
*   **Phase 2：Vulkan**
    *   迁移至 Vulkan，必须实现 **多线程命令录制** 和 **Secondary Command Buffers**。
*   **计算加速：CUDA**
    *   实现 **CUDA-GL/Vulkan Interop**，确保数据零拷贝。
*   **数学库**：仅限 **GLM**。
*   **外部库**：PCL 仅用于 I/O 读取，渲染部分全部手写。

## 4.3 本机 Windows C++/Qt 构建环境

- **Qt 5.15.19 MSVC 2022 x64 Kit**：`D:\qt_2\5.15.19\msvc2022_64`。其 `bin` 目录包含 `qmake.exe` 和运行/测试可能需要的 Qt DLL；涉及 Qt 构建、运行或测试时，应置于 `PATH` 前部。
- **MSVC 工具链**：`D:\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64`；开发环境脚本位于 `D:\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat`。
- **NMake（已确认路径）**：`D:\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\HostX64\x64\nmake.exe`。使用 NMake Makefiles 配置时显式传入 `-DCMAKE_MAKE_PROGRAM="D:\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.44.35207\bin\HostX64\x64\nmake.exe"`，避免开发环境 PATH 缺失或版本不一致。
- **Windows SDK**：安装在 `D:\Windows Kits\10`（而非默认 C 盘）。当前 VS 初始化脚本未正确注入 `INCLUDE`/`LIB`；使用 CMake/NMake 前须显式设置 MSVC Include/Lib、`D:\Windows Kits\10\Include\10.0.26100.0` 的 `ucrt/shared/um/winrt/cppwinrt`、及相应 x64 Lib/工具路径。
- **GLM（vcpkg）**：`D:\vcpkg\vcpkg\installed\x64-windows`。CMake 配置须传递 `-DCMAKE_PREFIX_PATH=D:\vcpkg\vcpkg\installed\x64-windows`；运行会启动子 CMake 的 CTest 配置冒烟测试时，也须将同一路径设置为环境变量 `CMAKE_PREFIX_PATH`。
- **PCL 1.15.1**：安装根目录为 `D:\PCL\PCL 1.15.1`，CMake 配置目录为 `D:\PCL\PCL 1.15.1\cmake`。配置时必须通过 `-DPCL_DIR=...` 注入该目录；项目 CMake 禁止硬编码本机 PCL 路径。当前项目仅允许 `dzc_data_pcl` 私有 Target 使用 PCL。
- **PCL Debug 运行时测试**：OpenNI2 2.2 已由 `D:\PCL\PCL 1.15.1\3rdParty\OpenNI2\OpenNI-Windows-x64-2.2.msi` 安装到 `C:\Program Files\OpenNI2\Redist`。运行链接 `pcl_iod.dll` 的 Debug 测试前，仅为测试进程将 `D:\PCL\PCL 1.15.1\bin`、`D:\PCL\PCL 1.15.1\3rdParty\VTK\bin`、`C:\Program Files\OpenNI2\Redist` 置于 `PATH` 前部；不要把这些机器路径硬编码到项目 CMake。
## 4.4 Windows 测试运行时 DLL 约定

- 当 CTest 因 PCL/VTK 运行时 DLL 缺失而失败，并报告 Windows 加载错误 `0xc0000135` 时，可将仓库现有 `build\bin\Debug` 或对应配置目录中的必要 DLL 临时复制到新构建的测试可执行文件目录后重试。
- 临时复制仅用于测试运行，不得把复制的 DLL 提交到仓库；测试结束后必须删除所有临时复制的 DLL，并确认工作树中没有遗留测试运行时文件。
- 如果构建配置与 DLL 后缀不匹配（例如 Debug 测试需要带 `d` 或 `-gd` 后缀的 PCL/VTK DLL），仍需补充对应配置的 PCL/VTK 运行时目录，或仅为测试进程设置正确的 `PATH`；不得通过重命名 DLL 规避依赖。
## 5. 功能边界
*   **包含**：点云加载、相机漫游、视锥体剔除、FPS 统计、CUDA 简单预处理。
*   **不包含**：深度学习、复杂的 GIS 坐标系转换、网络下载、通用图像处理。

## 6. 交付标准
*   **CMake**：必须使用 CMake 管理。
*   **头文件**：干净、整洁，像库文件一样暴露接口。
*   **源文件**：包含所有脏活累活的实现细节。
