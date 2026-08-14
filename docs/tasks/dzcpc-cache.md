# DZCPC Cache 任务清单

> 文件：`docs/tasks/dzcpc-cache.md`  
> 所属阶段：Phase 2  
> 模块状态：未开始  
> 前置模块：[point-cloud-data](./point-cloud-data.md)、[point-cloud-io](./point-cloud-io.md)、[task-system](./task-system.md)、[diagnostics](./diagnostics.md)  
> 输入基线：[需求文档](../requirements/spec.md)、[概要设计](../design/architectureDesign.md)、[详细设计](../design/detailDesign.md)、[项目规范](../../agent.md)

## 1. 模块目标

实现可删除、可验证、可原子重建的 `.dzcpc` V1 点云缓存格式和流式访问。

## 2. 范围边界

**包含：** 源身份；小端序列化；Header/索引/Payload；CRC32；对齐；范围校验；临时文件原子替换；缓存失效与清理。  
**不包含：** 八叉树构建算法；压缩；公开交换格式；GPU 上传。

## 3. 完成规则

只有同时满足以下条件，模块才可以在 [progress.md](./progress.md) 中标记完成：

- 本文所有非阻塞必需任务均已勾选；
- 所有自动化测试通过，能力缺失用例只能明确标记为 Skipped，不能伪造通过；
- 对应公共接口和私有实现符合 `agent.md` 的命名、Pimpl、RAII 和依赖边界；
- 相关需求、设计和测试文档已经同步；

## 4. 子任务 Checklist

- [ ] **DC-001 实现 checked 二进制运算工具**
- [ ] **DC-002 实现小端 Reader/Writer**
- [ ] **DC-003 实现 CRC32**
- [ ] **DC-004 定义 DZCPC V1 逻辑记录**
- [ ] **DC-005 实现 Header 序列化与校验**
- [ ] **DC-006 实现 Node/Chunk 索引编解码**
- [ ] **DC-007 实现 Chunk Payload 编解码**
- [ ] **DC-008 实现源文件身份**
- [ ] **DC-009 实现流式 DZCPC Writer**
- [ ] **DC-010 实现原子发布和临时清理**
- [ ] **DC-011 实现 DZCPC Reader**
- [ ] **DC-012 实现 CacheManager**

## 5. 子任务说明

### DC-001 实现 checked 二进制运算工具

- **状态**：未开始
- **目标**：提供 64 位 checked add/multiply、alignment 和范围检查。
- **前置任务**：project-foundation/PF-005
- **预计文件**：`src/data/cache/CheckedBinaryMath.h`、`tests/unit/CheckedBinaryMathTests.cpp`
- **实现要求**：任何 offset/size/pointCount 运算溢出都返回 DataFormat/NumericOverflow。
- **验收检查**：边界和溢出输入无未定义行为。
- **测试要求**：uint64 边界、对齐和文件范围测试。
- **追踪**：11.5、NFR-REL-003

### DC-002 实现小端 Reader/Writer

- **状态**：未开始
- **目标**：逐字段读写整数、float/double 和字节数组。
- **前置任务**：DC-001
- **预计文件**：`src/data/cache/LittleEndianIo.h`、`src/data/cache/LittleEndianIo.cpp`、`tests/unit/LittleEndianIoTests.cpp`
- **实现要求**：不得直接写 C++ struct、sizeof 或依赖编译器填充。
- **验收检查**：Golden 字节序与往返值一致。
- **测试要求**：整数、IEEE754、截断输入测试。
- **追踪**：DDD-008、11.1

### DC-003 实现 CRC32

- **状态**：未开始
- **目标**：实现 IEEE 0xEDB88320 CRC32 和分段更新。
- **前置任务**：DC-002
- **预计文件**：`src/data/cache/Crc32.h`、`src/data/cache/Crc32.cpp`、`tests/unit/Crc32Tests.cpp`
- **实现要求**：初值/终值与设计一致；校验字段按 0 计算由调用层负责。
- **验收检查**：标准测试向量与分段结果一致。
- **测试要求**：空输入、123456789、随机分段测试。
- **追踪**：DDD-008、11.1

### DC-004 定义 DZCPC V1 逻辑记录

- **状态**：未开始
- **目标**：定义 Header、NodeIndexRecord、ChunkIndexRecord、PayloadHeader 值类型和常量。
- **前置任务**：DC-002
- **预计文件**：`src/data/cache/DzcpcFormat.h`、`tests/unit/DzcpcFormatTests.cpp`
- **实现要求**：Magic DZCPC001、固定尺寸、flags 和 reserved 规则与详细设计一致；类型不是磁盘内存映射 struct。
- **验收检查**：常量和字段校验函数覆盖全部约束。
- **测试要求**：版本、flags、reserved 和尺寸测试。
- **追踪**：11.2~11.5

### DC-005 实现 Header 序列化与校验

- **状态**：未开始
- **目标**：写读固定 256 字节 Header 并验证 CRC/端序/偏移。
- **前置任务**：DC-003, DC-004
- **预计文件**：`src/data/cache/DzcpcHeaderCodec.h`、`src/data/cache/DzcpcHeaderCodec.cpp`、`tests/unit/DzcpcHeaderCodecTests.cpp`
- **实现要求**：Header CRC 计算时自身字段置 0；reserved 写 0。
- **验收检查**：Golden Header 可往返，单字节损坏被拒绝。
- **测试要求**：Golden bytes、CRC、版本、端序和截断测试。
- **追踪**：DDD-008、11.2

### DC-006 实现 Node/Chunk 索引编解码

- **状态**：未开始
- **目标**：按 192/128 字节逐字段序列化索引。
- **前置任务**：DC-004, DC-005
- **预计文件**：`src/data/cache/DzcpcIndexCodec.h`、`src/data/cache/DzcpcIndexCodec.cpp`、`tests/unit/DzcpcIndexCodecTests.cpp`
- **实现要求**：验证父子引用、childMask、depth、Chunk 引用、64 字节对齐和完整索引 CRC。
- **验收检查**：合法树索引往返，越界/环/非法引用被拒绝。
- **测试要求**：Golden 索引、破坏引用、CRC 和溢出测试。
- **追踪**：11.3/11.4、FR-LOD-001

### DC-007 实现 Chunk Payload 编解码

- **状态**：未开始
- **目标**：写读 64 字节头和 SoA Position/Color/Intensity 流。
- **前置任务**：DC-003, DC-004, point-cloud-data/PD-006
- **预计文件**：`src/data/cache/DzcpcPayloadCodec.h`、`src/data/cache/DzcpcPayloadCodec.cpp`、`tests/unit/DzcpcPayloadCodecTests.cpp`
- **实现要求**：流起点至少 16 字节对齐，Payload 4096 对齐；V1 无压缩；颜色按 RGBA 字节。
- **验收检查**：所有 schema 往返一致，错误 stride/长度/CRC 被拒绝。
- **测试要求**：XYZ、RGB、I、全属性、截断和 CRC 测试。
- **追踪**：DDD-008/009、11.5

### DC-008 实现源文件身份

- **状态**：未开始
- **目标**：计算规范化路径、大小、修改时间和快速摘要。
- **前置任务**：DC-005
- **预计文件**：`src/data/cache/SourceIdentity.h`、`src/data/cache/SourceIdentity.cpp`、`tests/unit/SourceIdentityTests.cpp`
- **实现要求**：Hash 只做快速比较，必须同时比较显式大小和时间；Windows UTF-8 路径经平台层转换。
- **验收检查**：源变更会使身份失配，相同源保持稳定。
- **测试要求**：临时文件修改、路径规范化和 Unicode 路径测试。
- **追踪**：ADR-006、11.2

### DC-009 实现流式 DZCPC Writer

- **状态**：未开始
- **目标**：按 Header→索引→对齐 Payload 写临时文件。
- **前置任务**：DC-006, DC-007, DC-008, task-system/TS-001
- **预计文件**：`src/data/cache/DzcpcWriter.h`、`src/data/cache/DzcpcWriter.cpp`、`tests/integration/DzcpcWriterTests.cpp`
- **实现要求**：支持取消；未完成文件不得被视为有效缓存；写入后 flush/close/reopen 校验。
- **验收检查**：多 Chunk 文件布局、对齐和 CRC 全部正确。
- **测试要求**：多 schema、取消、磁盘写失败和大偏移模拟测试。
- **追踪**：FR-LOD-001、NFR-REL-002

### DC-010 实现原子发布和临时清理

- **状态**：未开始
- **目标**：同文件系统 replace-existing，失败保留旧有效缓存。
- **前置任务**：DC-009
- **预计文件**：`src/platform/AtomicFileReplace.h`、`src/platform/AtomicFileReplace.cpp`、`src/data/cache/CacheTempCleaner.cpp`、`tests/integration/AtomicCachePublishTests.cpp`
- **实现要求**：临时名包含 pid/nonce；遗留文件按模式和年龄清理；不得递归误删其他文件。
- **验收检查**：成功替换、替换失败、旧缓存保留和遗留清理正确。
- **测试要求**：临时目录故障注入测试。
- **追踪**：11.6、NFR-REL-003

### DC-011 实现 DZCPC Reader

- **状态**：未开始
- **目标**：打开后先校验 Header/索引，按 Chunk 读取 Payload。
- **前置任务**：DC-006, DC-007, DC-008
- **预计文件**：`src/data/cache/DzcpcReader.h`、`src/data/cache/DzcpcReader.cpp`、`tests/integration/DzcpcReaderTests.cpp`
- **实现要求**：任何结构/CRC/身份错误使缓存整体失效；不猜测修复。
- **验收检查**：随机 Chunk 读取数据与源构建结果一致。
- **测试要求**：往返、损坏 Header/索引/Payload、截断和身份失配测试。
- **追踪**：ADR-006、NFR-REL-003

### DC-012 实现 CacheManager

- **状态**：未开始
- **目标**：选择命中、重建、删除和失效事件策略。
- **前置任务**：DC-010, DC-011, engine-core/EC-009
- **预计文件**：`src/data/cache/CacheManager.h`、`src/data/cache/CacheManager.cpp`、`tests/integration/CacheManagerTests.cpp`
- **实现要求**：缓存错误不使 Engine Failed；重建任务受取消和背压控制。
- **验收检查**：命中直接读取，失效从源重建，失败仍可保持 Engine 可用。
- **测试要求**：命中/失效/损坏/禁用缓存集成测试。
- **追踪**：FR-LOD-001、NFR-REL-003

## 6. 模块级验收

- [ ] DZCPC001 Golden 文件、CRC、对齐和端序测试通过
- [ ] 损坏、截断、身份或版本失配均安全重建
- [ ] 取消/崩溃中间产物不会发布为有效缓存
- [ ] 缓存 Reader 可随机读取 Chunk 且不依赖 PCL/GPU

## 7. 交接记录

- 完成日期：
- 完成人：
- 关键变更：
- 未解决问题：
- 测试命令与结果：
- 关联提交：

## 8. 变更约束

若实现需要改变公共接口、模块依赖、已确认参数或需求行为，必须先更新需求与设计文档，不得在编码任务中自行改变。
