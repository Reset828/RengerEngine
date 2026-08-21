# CameraPath 回放测试

CA-007 使用仅在 `tests/performance` 中编译的内存路径。`CameraPath` 包含初始有效 `RenderSize`、场景 `Bounds3d` 与绝对时间戳步骤；每步可选一个 `InputEvent`。回放器始终默认构造 `OrbitCameraController`，不注入 `CameraState`。它先缓存尺寸和场景 Bounds，然后逐步提交事件、以相邻时间差 `update` 并记录 `CameraState`/`CameraMatrices`。

测试以两次回放的逐帧 double `1e-12`、float `1e-5` 绝对+相对容差验证确定性，并覆盖轨迹球、平移、滚轮、失焦、`ResetRequest`、无输入步骤和结构/输入错误。没有文件格式、Renderer、GPU 或 FPS 采集；这不是正式性能基准，也不完成 `AC-P2-011`。