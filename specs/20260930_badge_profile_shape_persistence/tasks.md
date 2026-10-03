# 任务拆解

> 状态：已完成

## 任务总览

- [x] task-01: [Record] 建立单一 schema 3 形状记录
- [x] task-02: [Store] 将形状接入工牌事务存储
- [x] task-03: [HTTP] 建立 envelope v3 与 profile 形状 API
- [x] task-04: [Web] 恢复形状并正确裁剪反显图片
- [x] task-05: [Integration] 更新嵌入资源并完成自动化回归
- [x] task-06: [Acceptance] 执行真机浏览器与重启验收

---

## task-01: [Record] 建立单一 schema 3 形状记录

**追踪需求：** R-01、R-04、NFR-01、NFR-02、NFR-03

**依赖任务：** 无

**修改范围：**

- `main/services/badge/badge_record.h`
- `main/services/badge/badge_record.c`
- `test/test_badge/test_main.c` 中记录层用例
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展并收敛。复用现有 record、CRC 和 view；把 header 扩为 44 字节并删除 schema 1/2 成功读取分支，不新建第二套记录编码器。

**代码注释要求：** 注释 shape 属于 header 元数据并由 header CRC 保护，以及旧 schema 被拒绝但不触发擦除的边界；普通偏移计算不写复述式注释。

**完成标准：**

- 定义稳定的 `square=0`、`rounded=1`、`circle=2` 枚举和合法性判断。
- 当前记录 schema 固定为 3，header 固定为 44 字节，payload 保持 `name + bio + image`。
- view 返回校验后的 shape 和正确图片指针。
- schema 1/2、越界 shape、shape/图片 CRC 损坏均判无效。
- 记录最大长度仍小于 `0x20000`；header CRC 固定覆盖 `0x00..0x23`，shape/reserved/payload CRC/header CRC/commit 偏移明确。

**自动化验证：**

- 测试目标：锁定 44 字节 schema 3 header、三种形状、两类 CRC 和图片偏移，并证明旧 schema 安全拒绝。
- 测试用例：在 `test_badge` 增加明确 header/payload 向量、三种 shape decode/validate、schema 1/2 reject、非法 shape 和损坏 CRC。
- 执行命令：`pio test -e native -f test_badge`。
- 兜底检查：本任务不构建目标固件；聚焦 native 测试不能证明真实 Flash 行为。

**人工验证关注点：** 无独立 UI；task-06 验证旧分区内容存在时设备回退默认工牌且不崩溃、不自动擦除。

**待确认问题：** 无。

---

## task-02: [Store] 将形状接入工牌事务存储

**追踪需求：** R-01、R-04、R-06、NFR-01、NFR-03

**依赖任务：** task-01

**修改范围：**

- `main/services/badge/badge_store.h`
- `main/services/badge/badge_store.c`
- `main/modes/badge/badge_default.c`
- `test/test_badge/test_main.c` 中 store 用例
- 受结构字段影响的 native fixture
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展。复用现有双槽、非活动槽写入、payload 读回 CRC 和最后 commit；shape 只进入 header meta 与 snapshot/upload meta。

**代码注释要求：** 注释 shape 与图片必须同事务提交，以及旧槽无效时只回退默认资料、不自动擦除。

**完成标准：**

- snapshot 和 upload meta 全链路携带 shape，默认工牌明确为 square。
- begin 写入包含 shape 的 schema 3 header，再按原布局写姓名和 bio；payload CRC 仍只覆盖姓名、bio 和图片。
- 图片写入起点和 finish 读回长度保持 `header + name + bio` 的统一公式。
- 三种形状保存、覆盖、双槽轮换和重新初始化后均恢复一致。
- 任意擦除、前缀写入、图片写入、读回或 commit 失败继续发布旧完整快照。

**自动化验证：**

- 测试目标：验证 shape 与图片同事务、重启恢复和失败回退。
- 测试用例：扩展 `test_badge` 的默认资料、save/reload、slot rotation、边界和故障注入；同步所有结构 fixture 的显式 shape。
- 执行命令：`pio test -e native -f test_badge`。
- 兜底检查：native 内存后端不能证明物理 Flash 时序，留给 task-06。

**人工验证关注点：** task-06 核对普通升级后旧资料回退、新资料保存、重启和断电边界；本任务不烧录。

**待确认问题：** 无。

---

## task-03: [HTTP] 建立 envelope v3 与 profile 形状 API

**追踪需求：** R-02、R-03、R-04、R-06、NFR-01、NFR-02、NFR-03

**依赖任务：** task-02

**修改范围：**

- `main/services/badge/badge_http_protocol.h`
- `main/services/badge/badge_http_protocol.c`
- `main/services/badge/badge_http_server.c`
- `test/test_badge_http/test_main.c`
- `test/test_personalization_http/test_main.c` 中受 profile snapshot 字段影响的 fixture
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展并收敛。保留现有 token、Busy、流式 parser、统一 server response 和更新事件；把多版本 header 分派收敛为固定 16 字节 v3。

**代码注释要求：** 注释旧 version 在 writer 启动前拒绝、reserved 必须为零及 shape 不允许静默回退的协议原因。

**完成标准：**

- v3 头严格按 magic/version/name/bio/shape/reserved/image length 解析。
- shape 只接受 0/1/2，非法值返回 `invalid_shape`，version 1/2 返回协议错误。
- 任意分块、精确长度、token、Busy、超时、断连和额外数据语义保持正确。
- `GET /api/profile` 对默认资料和三种形状返回稳定字符串，响应缓冲覆盖最坏转义长度。
- 图片仍流式写入，不新增完整 body 或图片缓冲。

**自动化验证：**

- 测试目标：直接验证 v3 布局、分块状态机、旧 version 拒绝、shape 错误与 profile JSON。
- 测试用例：更新 `test_badge_http` 的 envelope helper 和每字节/每边界 feed；增加三种 shape、非法/保留字段、version 1/2 reject、GET JSON；复跑 personalization HTTP 防止路由回归。
- 执行命令：`pio test -e native -f test_badge_http`，随后运行 `-f test_personalization_http`。
- 兜底检查：host adapter 不证明真实 socket 时序或 HTTP 任务栈水位。

**人工验证关注点：** task-06 使用内嵌新网页保存，确认无 `invalid_request`、重启或协议长度错误。

**待确认问题：** 无。

---

## task-04: [Web] 恢复形状并正确裁剪反显图片

**追踪需求：** R-02、R-03、R-05、R-06、NFR-02、NFR-03

**依赖任务：** task-03

**修改范围：**

- `main/web/badge/app.js`
- `main/web/badge/index.html`（仅资源版本）
- `test/web/badge_web.test.js`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 复用并最小扩展。复用现有 shape radio、`renderCrop()`、RGB565 编解码、profile state 和消息区域；只新增 v3 编码、shape 响应规范化与不可逆扩大判断纯函数。

**代码注释要求：** 注释设备反显源为什么必须再次应用相同 clip，以及从较小遮罩扩大时为何必须重新选择原图；普通 radio 同步无需注释。

**完成标准：**

- `buildEnvelope()` 生成精确 16 字节 v3 头并提交当前 shape。
- profile JSON 的 shape 严格规范化，非法/缺失值显示不兼容错误，不静默使用方形。
- load profile 时先恢复 shape/radio，再加载照片并统一调用 redraw。
- 重新进入页面后圆形/圆角外围被对应 clip 隐藏。
- profile 使用单一 `sourceShape` 表达图片来源：设备反显图记录原 shape，本地新图使用 `null`。
- 设备来源图从 circle 扩大到 rounded/square、rounded 扩大到 square 时禁用保存并提示重新选择原图；更严格裁剪和本地新图不受限。
- 工牌和个性化两个表单的 dirty、mutation、图片源与槽位状态继续隔离。

**自动化验证：**

- 测试目标：验证 v3 字节布局、shape 恢复和扩大判断，不只检查页面文本。
- 测试用例：扩展 `badge_web.test.js`，覆盖三种 shape 编码、非法值、radio 初始化所需纯逻辑、设备来源扩大门禁矩阵、本地新图不受限，以及原 19 个 Web 行为回归。
- 执行命令：`node --test test/web/badge_web.test.js`，并对两个 JS 文件运行 `node --check`。
- 兜底检查：Node 无 DOM/Canvas 真机实现，不能证明 Safari/Chrome 的视觉反显。

**人工验证关注点：** task-06 在两类手机浏览器分别检查三个 radio、反显边缘、切换提示、重新选图和保存反馈。

**待确认问题：** 无。

---

## task-05: [Integration] 更新嵌入资源并完成自动化回归

**追踪需求：** R-01～R-06、NFR-01～NFR-03

**依赖任务：** task-01、task-02、task-03、task-04

**修改范围：**

- `main/CMakeLists.txt`（仅既有资源依赖需要时）
- `README.md`（仅当前使用说明确有变化时）
- 本规格 `acceptance.md`、`tasks.md`、`changelog.md`
- 受结构初始化影响的剩余 native 测试

**公共能力处理：** 复用。使用现有 `.incbin` 资源依赖、静态资源版本、Native/Web 测试和分区布局检查，不新增脚本或构建框架。

**代码注释要求：** 按实现约束补充必要注释；集成接线本身不增加空泛注释。

**完成标准：**

- 内嵌固件包含新版 app.js，页面不会继续请求旧缓存版本。
- 全量 Native、Web 和分区布局检查通过，原三槽个性化、Wi-Fi、设置、导航测试不回归。
- ESP32-C3 目标构建通过并记录 Flash、RAM、固件哈希。
- `partitions.csv` 无变化，普通上传清单仍不覆盖持久分区。
- 建立独立验收矩阵，所有未执行真机项初始化为 `NOT RUN`。

**自动化验证：**

- `pio test -e native`
- `node --test test/web/badge_web.test.js`
- `python3 tools/test_verify_partition_layout.py`
- `pio run`
- `python3 tools/verify_partition_layout.py partitions.csv .pio/build/ai_passport_badge`
- `git diff --check`
- 目标构建只证明编译、链接和资源嵌入，不证明浏览器视觉与真实 Flash 恢复。

**人工验证关注点：** task-06 执行实际热点、浏览器、LCD 和重启检查；本任务不自动烧录。

**待确认问题：** 无。

---

## task-06: [Acceptance] 执行真机浏览器与重启验收

> 状态：已完成。用户于 2026-09-30 确认功能验证通过，并明确同意关闭全部任务；未提供的设备和浏览器版本不补写。

**追踪需求：** R-01～R-06、NFR-02、NFR-03

**依赖任务：** task-05

**修改范围：**

- 本规格 `acceptance.md`
- 本规格 `tasks.md`、`changelog.md`
- 发现缺陷时回到对应实现任务，不在验收任务混入未设计功能

**公共能力处理：** 不涉及。

**代码注释要求：** 不涉及。

**完成标准：**

- iPhone Safari 与 Android Chrome 分别保存方形、圆角、圆形，退出并重新进入后 radio 与反显正确。
- LCD 与网页预览一致，圆形/圆角无黑色外围暴露。
- 保存后重启仍恢复 shape、姓名、bio 和图片。
- 旧 schema 资料安全回退默认工牌且不自动擦除；重新配置后可正常保存 schema 3。
- 扩大形状时保存被禁用并提示重新选择原图；重新选图后可以正常切换和保存。
- 三槽个性化、热点退出和连续保存无回归、重启或 watchdog。
- 每个实际执行项目记录 `PASS` 或 `FAIL`，未执行项保持 `NOT RUN`。

**自动化验证：**

- 烧录前复核 task-05 的固件哈希和普通上传地址。
- 自动化结果只作为测试对象身份，不代替本任务真机结论。

**人工验证关注点：** 禁止全片擦除；不在日志或验收文档记录姓名、bio、照片、token 或设备身份原值。

**待确认问题：** 无。用户已确认验证通过并同意关闭全部任务。
