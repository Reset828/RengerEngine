# 轨道相机交互设计

> 状态：CA-001 至 CA-007 已实现；Engine/Qt 集成与正式性能验收未完成
> 追踪：FR-CAM-002、FR-CAM-003、CA-005、CA-006、CA-007
> 参考来源：用户提供的未跟踪 `camera.cpp`；仅提取其透视轨道球交互、旋转限制、平移和缩放规则，不复用其 Vulkan、渲染器或模型矩阵框架。

## 1. 目标与边界

`dzc::OrbitCameraController` 是后端无关、GLM-only 的透视轨道控制器，实现既有 `ICameraController`、`CameraState`、`CameraMatrices`、`ViewFrustum`、`InputEvent`、`Bounds3d` 和 `RenderSize` 公共接口。

- 坐标约定为右手坐标系、世界 `+Y` 向上；相机本地 `-Z` 始终指向 orbit target。
- 仅支持透视轨道球；不支持正交模式、键盘控制、惯性、阻尼或按 `deltaSeconds` 驱动的连续运动。
- CA-006 不实现 Engine 注入、`SubmitInputCommand`、Qt 事件映射、GPU 或渲染器；这些属于后续 Engine/Qt 集成任务。
- 实现固定采用 OpenGL Phase 1 的 `ClipDepthRange::NegativeOneToOne` 和 `glm::perspectiveRH_NO`。后续后端若需要其他深度约定，必须通过独立设计与实现任务处理，不能静默改变当前契约。

## 2. 控制状态与尺寸缓存

控制器私有保存 orbit target、distance、orientation、当前拖拽类型及上一个归一化指针位置、最近有效 `RenderSize`、最近有效场景 Bounds、pending reset，以及最近完整矩阵/视锥输出。默认可用相机为 target `(0,0,0)`、distance `3`、单位 orientation、45° 垂直 FOV、near/far `0.001/1000`；`CameraState.position = target + orientation * (0,0,distance)`。

有效 `RenderSize` 要求 `width > 0`、`height > 0`，且 `devicePixelRatio` 有限并大于零；DPR 只参与有效性检查，几何 aspect 始终为 width/height。无效尺寸不得覆盖缓存；由于既有接口不能返回 `Result`，`matrices/frustum` 在这种情况下返回最近一次完整有效输出，若尚无有效输出则返回各自默认值。无有效尺寸时 `reset()` 失败且不产生部分状态；尺寸变化不自动重新框选或清除用户视角。

## 3. 输入与交互规则

所有指针坐标以视口左上角为原点，`PointerMove.valueX/valueY` 是 `[0, 1]` 的绝对归一化坐标。有效输入更新状态；失败时不得改变控制器状态。

| InputEvent | 已确认语义 |
|---|---|
| `PointerButton`, `code == 0`, `pressed == true` | 开始左键轨迹球拖拽，记录当前位置。 |
| `PointerButton`, `code == 2`, `pressed == true` | 开始右键屏幕平面平移拖拽，记录当前位置。 |
| 任意 `PointerButton`, `pressed == false` | 结束当前拖拽；不要求 `code` 与当前按钮匹配。 |
| `PointerMove` + 左键拖拽 | 应用下述轨迹球旋转。 |
| `PointerMove` + 右键拖拽 | 沿当前相机屏幕 `right/up` 平面平移 orbit target。 |
| `Wheel` | `valueY > 0` 时距离乘 `0.9`；`valueY < 0` 时距离乘 `1.1`；零值不改变距离。滚轮按符号处理，不按幅度累计；交互结果钳制到 `[0.1, 1000]`。 |
| `Focus` | `pressed == false` 表示失焦并取消所有拖拽；`pressed == true` 表示获得焦点，不改变相机。 |
| `ResetRequest` | 仅设置 pending reset。下一次 `update(deltaSeconds, sceneBounds)` 使用该次有效 Bounds 执行 reset；`submitInput()` 不同步执行 Bounds 相关计算。 |
| `Key` | 本控制器不定义键盘交互。 |

`PointerMove` 的任一坐标非有限或超出 `[0, 1]`、PointerButton 按下的起始坐标非有限或超出范围、`Wheel.valueY` 非有限，以及未知的按下 `PointerButton.code` 都返回 `ErrorDomain::General`、错误码 `1`（`InvalidArgument` 语义）和非空诊断，且状态不变。

## 4. 轨迹球、平移与缩放

轨迹球以当前有效视口 `width`、`height` 和 `minExtent = min(width, height)` 映射输入点：

```text
x = (2 * nx - 1) * width  / minExtent
y = (1 - 2 * ny) * height / minExtent
```

令 `d = sqrt(x*x + y*y)`。当 `d <= 1/sqrt(2)` 时取 `z = sqrt(1 - d*d)`，否则取 `z = 0.5 / d`，随后归一化 `(x, y, z)`。前后两个球面点决定拖拽旋转；旋转灵敏度固定为 `4.71238898038`，即 270°/单位归一化屏幕长度。

候选旋转必须保持旋转后的本地 `+Z` 世界方向满足 `worldZ.y >= -1e-6`。候选不满足时，固定进行 16 次二分搜索，在 `[0, 1]` 内只应用最大合法角度比例；该限制用于防止视角翻转。

右键平移按当前距离、45° 垂直 FOV 和 aspect 计算屏幕可见尺寸：`visibleHeight = 2 * distance * tan(45° / 2)`，`visibleWidth = visibleHeight * aspect`。根据前后归一化指针坐标的差值，按 `target += right * (deltaX * visibleWidth) - up * (deltaY * visibleHeight)` 沿当前相机 `right/up` 平面直接移动 orbit target。

## 5. Reset、更新与动态裁剪面

`reset(sceneBounds)` 要求 Bounds 有效且已有有效尺寸缓存；任一前置条件不满足时返回 `DataFormat/2` 与非空诊断并保持原状态。成功时将 Bounds 中心作为 orbit target，清除 orientation、拖拽和 pending reset，并以 Bounds 包围球半径的 `1.05` 倍、当前 aspect 与 45° 垂直 FOV 计算水平/垂直均可完整容纳的距离。该自动框选距离允许大于 `1000`；`[0.1,1000]` 仅限制滚轮交互。

`update(deltaSeconds, sceneBounds)` 不产生动画。`deltaSeconds` 必须有限且不小于零；Bounds 必须有效。成功时缓存最新有效 Bounds、处理 pending reset，并更新动态 near/far；任一校验或 pending reset 失败时不提交部分状态。

设 `c` 为 Bounds 中心、`r = length((maximum - minimum) / 2)` 为包围球半径、`s = length(camera.position - c)`，动态裁剪面为：

```text
near = max(0.001, 0.9 * max(s - r, 0))
far  = max(near * 2, 1.1 * (s + r))
```

该规则在相机位于包围球内和退化 Bounds 时仍保守覆盖场景。非法 Bounds、无效尺寸前置条件与非有限中间数均返回 `ErrorDomain::DataFormat`、错误码 `2`（`CorruptData`）和非空诊断。

## 6. CA-006 矩阵、视锥与验证

`matrices(size)` 对有效尺寸生成 `cameraOrigin = CameraState.position`（保持 double 精度）、仅包含 orientation 逆旋转的相机相对 `view`，以及 `glm::perspectiveRH_NO(45°, aspect, near, far)` 投影。绝对相机位置不会窄化写入 float view 矩阵。

`frustum(size)` 从相机相对空间中的 `projection * view` 用 `NegativeOneToOne` 提取规范化六平面，然后以 double `cameraOrigin` 平移为全局世界空间平面。因此返回的 `ViewFrustum` 可直接传给全局 `Bounds3d` 的 `intersects()`，而不要求调用方自行转换 Bounds。无法得到完整有效输出时遵从尺寸缓存回退契约。

专项 `dzc_orbit_camera_controller` Golden/行为测试覆盖默认姿态、OpenGL 投影及世界空间视锥、轨迹球映射与防翻转、平移、释放与失焦、滚轮边界、原子失败、reset/pending reset、动态裁剪面、尺寸缓存、resize 保持视角和大坐标场景。Engine Controller 注入和 Qt 映射仍未实现。


## 7. CA-007 确定性路径回放

`tests/performance/CameraPath.*` 仅定义内存 C++ 路径：有效 `RenderSize`、有效 `Bounds3d` 和按非递减绝对时间排列的可选 `InputEvent`。回放从默认 `OrbitCameraController` 开始，先调用 `matrices(size)` 与 `update(0, bounds)`，每一步先提交事件、以当前时间减前一步时间调用 `update`，再采样 state/matrices。`ResetRequest` 因此由同一步 update 正常执行。两次回放逐帧以 double `1e-12`、float `1e-5` 的绝对+相对容差比较；该测试不采集 FPS，也不代表 `AC-P2-011`。
