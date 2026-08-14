# Camera Abstraction 任务清单

> 文件：`docs/tasks/camera-abstraction.md`  
> 所属阶段：Phase 1（部分阻塞）  
> 模块状态：未开始  
> 前置模块：[project-foundation](./project-foundation.md)、[point-cloud-data](./point-cloud-data.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

建立 CameraState、CameraMatrices、ViewFrustum、InputEvent 和 ICameraController 抽象；在参考源码到来前不实现具体交互模型。

## 2. 范围边界

**包含：** 相机数据结构；抽象输入事件；控制器接口；矩阵/视锥体验证工具；Fake Controller；Qt 输入映射契约。  
**不包含：** 具体键位；鼠标行为；速度；初始视图；重置语义；最终性能运动路径。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；
- CA-005 及其后续具体控制器任务在用户提供参考源码前保持未勾选；因此本模块只能标记“抽象完成”，不能标记“整体完成”。

## 4. 子任务 Checklist

- [ ] **CA-001 定义 CameraState 和 CameraMatrices**
- [ ] **CA-002 定义 ViewFrustum 和平面工具**
- [ ] **CA-003 定义 InputEvent**
- [ ] **CA-004 定义 ICameraController 和 Fake**
- [ ] **CA-005 分析用户提供的相机参考源码** —— **阻塞：等待用户提供相机参考源码**
- [ ] **CA-006 实现确认后的具体 Camera Controller** —— **阻塞：等待 CA-005 完成**
- [ ] **CA-007 定义可重复性能相机路径** —— **阻塞：等待相机源码和性能基准确认**

## 5. 子任务说明

### CA-001 定义 CameraState 和 CameraMatrices

- **状态**：未开始
- **目标**：实现详细设计 19.1 的纯数据结构。
- **前置任务**：project-foundation/PF-004, point-cloud-data/PD-002
- **预计文件**：`include/dzc/CameraTypes.h`、`tests/unit/CameraTypesTests.cpp`
- **实现要求**：只使用 GLM；默认值不声称是可用相机配置；不含 Qt 类型。
- **验收检查**：类型可编译、复制并保持 double position/cameraOrigin。
- **测试要求**：静态类型和默认值测试。
- **追踪**：DDD-016、FR-CAM-001

### CA-002 定义 ViewFrustum 和平面工具

- **状态**：未开始
- **目标**：实现六平面结构、规范化和有限性校验。
- **前置任务**：CA-001
- **预计文件**：`include/dzc/ViewFrustum.h`、`src/camera/ViewFrustum.cpp`、`tests/unit/ViewFrustumTests.cpp`
- **实现要求**：不从具体控制方式推导，只处理输入矩阵或平面。
- **验收检查**：有效平面可规范化，退化平面返回 Error。
- **测试要求**：标准矩阵、退化和非有限输入测试。
- **追踪**：FR-VIS-002、19.1

### CA-003 定义 InputEvent

- **状态**：未开始
- **目标**：实现 PointerMove/Button/Wheel/Key/Focus/ResetRequest 值类型。
- **前置任务**：CA-001
- **预计文件**：`include/dzc/InputEvent.h`、`tests/unit/InputEventTests.cpp`
- **实现要求**：code/modifiers 只作为抽象数值，不在 Engine 固定 Qt key 或鼠标键。
- **验收检查**：全部事件类型可表达且无 Qt 依赖。
- **测试要求**：构造、移动和数值边界测试。
- **追踪**：FR-CAM-001

### CA-004 定义 ICameraController 和 Fake

- **状态**：未开始
- **目标**：声明 submitInput/update/state/matrices/frustum/reset，并提供测试 Fake。
- **前置任务**：CA-002, CA-003
- **预计文件**：`include/dzc/ICameraController.h`、`tests/fakes/FakeCameraController.h`、`tests/unit/CameraControllerContractTests.cpp`
- **实现要求**：接口不规定控制模型；Fake 只记录调用并返回预置结果。
- **验收检查**：Engine 可通过接口注入 Fake，输入和 reset 请求可观察。
- **测试要求**：契约和 Engine 注入测试。
- **追踪**：FR-CAM-001/003、DDD-016

### CA-005 分析用户提供的相机参考源码

- **状态**：阻塞/未开始
- **目标**：提取控制模型、事件映射、速度、初始视图、重置和裁剪面规则。
- **前置任务**：用户提供参考源码
- **预计文件**：`docs/design/cameraInteractionDesign.md`
- **实现要求**：必须先展示分析结果并向用户确认；不得从现有文档猜测。
- **验收检查**：用户明确确认相机详细决策，需求/概要/详细设计同步更新。
- **测试要求**：文档审查；本任务不写控制器代码。
- **追踪**：TBD-001、TBD-004

### CA-006 实现确认后的具体 Camera Controller

- **状态**：阻塞/未开始
- **目标**：按 CA-005 的确认文档实现具体控制器。
- **前置任务**：CA-005
- **预计文件**：`src/camera/<ConfirmedController>.h`、`src/camera/<ConfirmedController>.cpp`、`tests/unit/<ConfirmedController>Tests.cpp`
- **实现要求**：类名、文件和行为以用户确认结果为准；当前不得预创建具体方案。
- **验收检查**：确认的输入、速度、reset 和矩阵行为逐项通过。
- **测试要求**：基于参考源码的 Golden/行为测试。
- **追踪**：FR-CAM-002/003

### CA-007 定义可重复性能相机路径

- **状态**：阻塞/未开始
- **目标**：基于确认后的 Controller 创建性能路径。
- **前置任务**：CA-006, 用户确认基准环境
- **预计文件**：`tests/performance/CameraPath.*`、`docs/testing/cameraPath.md`
- **实现要求**：路径必须可重放并记录起点、事件和时间；当前不可虚构。
- **验收检查**：多次重放输出相同视图序列和时长。
- **测试要求**：确定性重放测试。
- **追踪**：NFR-TEST-001、TBD-004

## 6. 模块级验收

- [ ] Camera 公共类型和接口不依赖 Qt
- [ ] Fake Controller 可驱动 Engine 与剔除测试
- [ ] 未确认前不存在具体控制器、键位或速度实现
- [ ] 参考源码到来后完成用户确认和对应行为测试

## 7. 交接记录

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
