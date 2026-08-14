# Packaging and Release 任务清单

> 文件：`docs/tasks/packaging.md`  
> 所属阶段：最终验收  
> 模块状态：未开始  
> 前置模块：[project-foundation](./project-foundation.md)、[diagnostics](./diagnostics.md)、[qt-application](./qt-application.md)、[opengl-renderer](./opengl-renderer.md)、[vulkan-renderer](./vulkan-renderer.md)、[cuda-opengl-interop](./cuda-opengl-interop.md)、[cuda-vulkan-interop](./cuda-vulkan-interop.md)、[integration-testing](./integration-testing.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

建立 Windows 10/11 Release 构建、运行时依赖部署、配置与可写目录约定、安装包冒烟和发布清单，使已选择后端能够在干净环境启动并按能力降级。

## 2. 范围边界

**包含：** Windows Release 配置；Qt Runtime 部署；按开关收集后端依赖；Shader/SPIR-V 与许可证；默认配置与版本信息；可写目录；安装包冒烟；无 CUDA 降级验证；发布清单。  
**不包含：** Linux 正式发布验收；未启用后端的运行库；驱动或 CUDA Toolkit 自动安装；代码签名证书采购和商店发布。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [ ] **PK-001 定义 Windows Release 构建预设**
- [ ] **PK-002 实现 Qt 5.15.19 Runtime 部署**
- [ ] **PK-003 按构建选项部署图形后端依赖**
- [ ] **PK-004 部署 OpenGL Shader 与 Vulkan SPIR-V**
- [ ] **PK-005 汇总第三方许可证**
- [ ] **PK-006 提供默认配置与配置优先级说明**
- [ ] **PK-007 定义日志、缓存与报告可写目录**
- [ ] **PK-008 执行 OpenGL-only 安装目录冒烟**
- [ ] **PK-009 执行 Vulkan-only 安装目录冒烟**
- [ ] **PK-010 验证无 CUDA 环境降级**
- [ ] **PK-011 嵌入版本与构建信息**
- [ ] **PK-012 生成发布清单与最终安装包验收记录**

## 5. 子任务说明

### PK-001 定义 Windows Release 构建预设

- **状态**：未开始
- **目标**：创建 Windows 10/11 x64 Release CMake Preset 和可重复构建说明。
- **前置任务**：PF-008
- **预计文件**：`CMakePresets.json`、`cmake/ReleaseOptions.cmake`、`docs/release/windows-build.md`
- **实现要求**：固定 C++17 和 Release 配置；预设分别支持 OpenGL-only、Vulkan-only、OpenGL+Vulkan，以及可选 CUDA；不得硬编码个人绝对路径。
- **验收检查**：新环境按文档可配置和构建；后端开关与产物清晰可见。
- **测试要求**：运行各无 CUDA 预设配置/构建，CUDA 预设按能力标记。
- **追踪**：NFR-PORT-001、NFR-MAIN-005、24.3

### PK-002 实现 Qt 5.15.19 Runtime 部署

- **状态**：未开始
- **目标**：收集应用实际依赖的 Qt DLL 和平台插件。
- **前置任务**：PK-001, QT-012
- **预计文件**：`cmake/DzcDeployQt.cmake`、`packaging/windows/qt-runtime.md`
- **实现要求**：使用 Qt 5.15.19；只部署所需模块/插件；不得把 Qt 类型引入 Engine；Debug/Release Runtime 不混用。
- **验收检查**：打包目录在无 Qt 开发环境 PATH 条件下可启动主窗口；缺少插件错误可诊断。
- **测试要求**：清理 PATH 的隔离环境执行启动冒烟。
- **追踪**：FR-UI-001/002、NFR-PORT-001、24.3

### PK-003 按构建选项部署图形后端依赖

- **状态**：未开始
- **目标**：根据启用后端收集必要运行库并排除未启用后端依赖。
- **前置任务**：PK-001
- **预计文件**：`cmake/DzcDeployBackends.cmake`、`packaging/windows/backend-runtime.md`
- **实现要求**：OpenGL-only 不携带/要求 Vulkan SDK；Vulkan-only 不要求 OpenGL 后端文件；不得打包 GPU 驱动。
- **验收检查**：依赖扫描中不存在未启用后端 DLL；缺失必需运行库在打包阶段失败。
- **测试要求**：对 OpenGL-only、Vulkan-only、双后端目录运行依赖清单测试。
- **追踪**：FR-COM-001/002、NFR-MAIN-001、NFR-PORT-001、24.1/24.3

### PK-004 部署 OpenGL Shader 与 Vulkan SPIR-V

- **状态**：未开始
- **目标**：把运行期 OpenGL Shader 和构建期 Vulkan SPIR-V 放入确定目录并生成清单。
- **前置任务**：GL-004, VK-001, PK-003
- **预计文件**：`cmake/DzcDeployShaders.cmake`、`packaging/windows/shader-manifest.json`
- **实现要求**：只部署已启用后端资源；清单含相对路径、哈希和 Shader 版本；缺失资源使打包失败。
- **验收检查**：安装后两后端均能找到对应 Shader；篡改/缺失可报告清晰错误。
- **测试要求**：打包目录运行资源清单校验和两后端启动测试。
- **追踪**：FR-GL-003、FR-VK-005、NFR-REL-003、24.2

### PK-005 汇总第三方许可证

- **状态**：未开始
- **目标**：为 Qt、GLM、PCL、Vulkan/CUDA 相关可再分发组件生成许可证目录和清单。
- **前置任务**：PK-003
- **预计文件**：`LICENSES/README.md`、`packaging/licenses/ThirdPartyManifest.md`、`cmake/DzcDeployLicenses.cmake`
- **实现要求**：只列项目实际链接/分发组件；不得猜测许可证文本，文本来源需由已使用依赖包提供并在实施时核对。
- **验收检查**：发布目录包含项目许可证和所有实际分发第三方组件条目；未启用组件不误列为随包分发。
- **测试要求**：脚本对依赖清单与许可证条目做一一校验。
- **追踪**：NFR-MAIN-005、24.3

### PK-006 提供默认配置与配置优先级说明

- **状态**：未开始
- **目标**：打包安全默认 INI，并记录命令行、配置文件和默认值的覆盖顺序。
- **前置任务**：DG-006, PF-006
- **预计文件**：`config/dzc-render-engine.ini`、`docs/release/configuration.md`、`packaging/windows/config-manifest.json`
- **实现要求**：默认 OpenGL、CUDA auto，沿用详细设计已确认默认值；不得固定未确认 Camera 键位、速度或性能路径。
- **验收检查**：无用户配置可启动；合法覆盖生效；未知/非法项有诊断；Camera 待定项不出现在默认配置。
- **测试要求**：运行默认、覆盖、非法值和缺失配置测试。
- **追踪**：FR-COM-001、FR-CUDA-001、21.1、DDD-004/005/006

### PK-007 定义日志、缓存与报告可写目录

- **状态**：未开始
- **目标**：在 Windows 用户目录下建立日志、缓存、Pipeline Cache 和性能报告路径。
- **前置任务**：DG-007, DC-011, VK-011
- **预计文件**：`src/platform/WindowsAppPaths.h`、`src/platform/WindowsAppPaths.cpp`、`docs/release/runtime-paths.md`、`tests/unit/WindowsAppPathsTests.cpp`
- **实现要求**：安装目录视为只读；路径创建失败返回 Error；UTF-8 路径边界明确；公共 API 不暴露 Windows/Qt 类型。
- **验收检查**：普通用户无需管理员权限即可写日志/cache/report；只读安装目录不影响运行。
- **测试要求**：临时用户目录、Unicode 路径和权限失败测试。
- **追踪**：FR-STAT-002、NFR-PORT-001/002、21.2/21.4

### PK-008 执行 OpenGL-only 安装目录冒烟

- **状态**：未开始
- **目标**：在打包后的干净目录验证无 Vulkan/CUDA SDK 时 OpenGL 启动、加载和关闭。
- **前置任务**：PK-002 至 PK-007, IT-015
- **预计文件**：`tests/packaging/OpenGlPackageSmoke.ps1`
- **实现要求**：从发布目录运行；清理开发环境 PATH 影响；加载小型 PCD/PLY；保存日志和退出码。
- **验收检查**：主窗口启动，OpenGL 4.5 环境检查明确，数据可加载并正常退出；不查找 Vulkan/CUDA 开发文件。
- **测试要求**：在 Windows 10/11 目标环境或等价干净 VM 执行。
- **追踪**：AC-P1-001/002/003/013、NFR-PORT-001

### PK-009 执行 Vulkan-only 安装目录冒烟

- **状态**：未开始
- **目标**：验证 Vulkan-only 包在无 OpenGL 后端文件、无 Vulkan SDK 开发环境时启动和渲染。
- **前置任务**：PK-002 至 PK-007, IT-016
- **预计文件**：`tests/packaging/VulkanPackageSmoke.ps1`
- **实现要求**：只依赖系统 Vulkan Loader/驱动和随包资源；执行加载、resize、关闭并收集 Validation 不可用时的 Release 行为。
- **验收检查**：Vulkan 后端独立启动并渲染；不加载 OpenGL 后端 DLL；无 SDK 环境不影响运行。
- **测试要求**：在具有 Vulkan 1.2 驱动的干净 Windows 环境执行。
- **追踪**：AC-P2-001/002/003/014、NFR-PORT-001

### PK-010 验证无 CUDA 环境降级

- **状态**：未开始
- **目标**：在未安装 CUDA Runtime/无兼容设备条件下验证 CUDA auto/off 和显式 on。
- **前置任务**：PK-008, PK-009
- **预计文件**：`tests/packaging/NoCudaFallbackSmoke.ps1`
- **实现要求**：auto 记录原因并继续图形后端；off 不探测 CUDA；on 明确失败且提示环境检查结果。
- **验收检查**：OpenGL/Vulkan 包在 auto/off 下均可用；on 不静默降级。
- **测试要求**：清理 CUDA PATH/模拟无兼容设备执行模式矩阵。
- **追踪**：FR-CUDA-001、FR-COM-002、NFR-PORT-003、AC-P1-011

### PK-011 嵌入版本与构建信息

- **状态**：未开始
- **目标**：生成应用版本、Git 提交、构建时间、编译器和后端开关信息并显示于日志/关于窗口。
- **前置任务**：PK-001, QT-012
- **预计文件**：`cmake/DzcVersion.cmake`、`include/dzc/Version.h.in`、`src/app/AboutDialog.cpp`、`docs/release/versioning.md`
- **实现要求**：版本来源必须明确；不得依赖运行时 Git；可复现构建可通过环境变量固定时间字段。
- **验收检查**：日志和 UI 显示同一版本/提交/开关；安装包文件名含版本。
- **测试要求**：构建信息单元测试和打包产物命名检查。
- **追踪**：FR-UI-005、NFR-MAIN-005、21.2

### PK-012 生成发布清单与最终安装包验收记录

- **状态**：未开始
- **目标**：汇总二进制、依赖、资源、许可证、配置、测试证据和已知限制。
- **前置任务**：PK-008 至 PK-011, IT-015, IT-016
- **预计文件**：`docs/release/release-checklist.md`、`docs/release/known-limitations.md`、`packaging/windows/package-manifest.json`
- **实现要求**：明确 Windows 10/11 为当前验收平台；Linux 只记录架构边界；Camera 和性能未决项必须如实列为 Blocked/限制。
- **验收检查**：清单中每个文件有来源/哈希；所有已执行冒烟有证据；未完成阻塞项未被误标通过。
- **测试要求**：运行 Manifest 哈希校验、文档追踪检查和安装目录最终冒烟。
- **追踪**：NFR-PORT-001/002/003、AC-P1-001、AC-P2-001/015、24.3、29

## 6. 模块级验收

- [ ] Windows 10/11 x64 Release 包可从干净目录启动，不依赖开发环境路径
- [ ] Qt、启用后端资源、运行库、配置和许可证完整，未启用后端依赖不随包分发
- [ ] OpenGL-only 与 Vulkan-only 安装目录冒烟测试均有证据
- [ ] 无 CUDA 环境下 auto/off 可用，显式 on 明确失败且不静默降级
- [ ] 发布清单如实记录 Camera、基准硬件和最终性能路径等阻塞项；Linux 不作为当前验收范围

## 7. 交接记录

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
