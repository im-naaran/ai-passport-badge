# 实现任务

> 状态：已确认；task-01～task-11 已完成；8 KiB HTTP 栈候选核心保存链路已通过，task-12 的其余真机矩阵保持打开

## 执行原则

- 严格按依赖关系执行；同一文件范围有交叉的任务必须串行。
- 每个任务只做列出的最小变更，完成聚焦验证后再勾选并追加 changelog。
- 自动化通过不代表真机通过。LCD、实体按键、手机浏览器、断电和普通烧录保留必须在 task-12 单独确认。
- 禁止全片擦除；任何上传前先核对普通分段写入不覆盖 NVS、`badge_data`、`custom_data` 或 `cardid`。

---

- [x] task-01: [Partition] 新增个性化数据分区与布局校验门禁

> 状态：已完成。新增 `custom_data@0x35A000/0x100000` 和固定布局/普通上传保护校验；Python 6/6 用例、目标构建产物及真实上传清单校验通过，尚未执行真机烧录。

**追踪需求：** R-07、R-08、NFR-01、NFR-03

**依赖任务：** 无

**修改范围：**

- `partitions.csv`
- 新增 `tools/verify_partition_layout.py`
- 新增 `tools/test_verify_partition_layout.py`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 新建。当前 `tools/` 没有实际存在的分区校验脚本；使用 Python 标准库创建针对本固件固定布局的最小校验，不引入通用分区框架。

**代码注释要求：** 注释 `custom_data` 必须位于 `cardid` 之后的保护原因，以及普通 upload 不应包含数据分区镜像的检查边界。

**实现内容：**

1. 新增 `custom_data,data,0x41,0x35A000,0x100000`，保持其他分区地址和大小不变。
2. 校验 8 MB 上限、4 KiB 对齐、非重叠和 factory/`badge_data`/`cardid`/`custom_data` 精确边界。
3. 支持对构建产物的普通上传地址列表做保护检查，不允许覆盖四个持久数据区域。
4. 为缺失、偏移错误、大小错误、重叠、越界和受保护写入建立负例。

**完成标准：**

- 新分区精确位于 `0x35A000/0x100000`，结束地址为 `0x45A000`。
- `badge_data@0x310000/0x40000` 和 `cardid@0x356000/0x4000` 未移动。
- 校验器对正确布局通过，对所有负例稳定失败并给出不含设备数据的错误。

**自动化验证：**

- `python3 tools/test_verify_partition_layout.py`
- `python3 tools/verify_partition_layout.py partitions.csv`
- `git diff --check -- partitions.csv tools/verify_partition_layout.py tools/test_verify_partition_layout.py`

**人工验证关注点：** task-12 烧录前核对实际 upload 命令只写 bootloader、partition table 和 factory app；此任务不烧录。

**待确认问题：** 无。

---

- [x] task-02: [Storage] 实现个性化记录编解码与校验

> 状态：已完成。新增固定 40 字节 little-endian 记录、图片/墓碑严格校验、CRC、回绕 sequence 和六 bank 边界计算；聚焦测试纳入 `test_custom`，与 task-03 合计 11/11 通过。

**追踪需求：** R-01、R-02、R-07、NFR-01、NFR-03

**依赖任务：** 无

**修改范围：**

- 新增 `main/services/custom/custom_record.h`
- 新增 `main/services/custom/custom_record.c`
- 新增 `test/test_custom/test_main.c`
- `tools/native_sources.py`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 新建并局部复用。复用现有 badge CRC32 算法的行为或提取不改变语义的小型公共 CRC 能力；不改造工牌记录 schema，也不让 custom 依赖工牌尺寸常量。

**代码注释要求：** 注释显式 little-endian、header CRC 排除字段、commit 最后写和 sequence 回绕比较；普通字段赋值不加说明性噪声。

**实现内容：**

1. 定义三槽、240×320、stride 480、153,600 字节、40 字节头、`0x28000` bank 和 1 MiB 分区常量。
2. 实现图片记录与空槽墓碑的头准备、finalize、commit 和 decode。
3. 严格校验 magic/schema/header size/逻辑槽/flags/格式/尺寸/stride/长度/header CRC/payload CRC/commit。
4. 实现回绕安全 sequence 比较及物理 bank 边界计算。

**完成标准：**

- 不依赖 C struct 内存布局即可生成和读取固定 40 字节头。
- occupied 与 tombstone 的字段组合严格互斥，保留位非零会被拒绝。
- 记录不能跨逻辑槽或超过 `0x28000` bank。

**自动化验证：**

- `/Users/naaran/.platformio/penv/bin/pio test -e native -f test_custom`
- 覆盖正确图片、正确墓碑、每个头字段损坏、payload 损坏、未提交、槽号错配、sequence 回绕和边界偏移。

**人工验证关注点：** 无；纯记录逻辑由 native 用例验证，Flash 行为在 task-04/12 验证。

**待确认问题：** 无。

---

- [x] task-03: [Storage] 实现三槽独立双副本事务 store

> 状态：已完成。实现三槽独立 A/B 选择、分块保存、读回校验、最后 commit、事务墓碑、幂等清空、占用/相邻查询和只读降级；`test_custom` 11/11 通过。

**追踪需求：** R-01、R-03、R-06、R-07、NFR-01、NFR-03

**依赖任务：** task-02

**修改范围：**

- 新增 `main/services/custom/custom_store.h`
- 新增 `main/services/custom/custom_store.c`
- 扩展 `test/test_custom/test_main.c`
- `tools/native_sources.py`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 新建。沿用 `badge_store` 的后端函数表、分块 CRC、读回校验和最后 commit 模式，但保持独立类型，避免更改现有工牌事务路径。

**代码注释要求：** 注释非活动 bank 选择、墓碑为何保留旧图片、已空清空为何幂等 no-op，以及全局 pending 事务如何保护槽位目标。

**实现内容：**

1. 初始化时独立扫描三个逻辑槽的 A/B bank，选择最新有效图片或墓碑。
2. 提供 snapshot、占用掩码、是否有内容、首个有效槽和相邻有效槽查询。
3. 实现 `begin_update/write/finish/abort`，只更新目标逻辑槽的非活动 bank。
4. 实现事务化 `clear(slot)`；occupied 写新墓碑，empty 直接成功且不擦写。
5. 提供后端不可用时的只读空状态初始化。

**完成标准：**

- 三槽可分别处于 empty/occupied，sequence 和 active bank 相互独立。
- 任意写入、读回或 commit 前失败只保留旧快照；其他槽不受影响。
- 完成保存或清空后 snapshot 指向最新已提交记录，且没有完整图片 RAM 副本。

**自动化验证：**

- `/Users/naaran/.platformio/penv/bin/pio test -e native -f test_custom`
- 覆盖首次保存、连续覆盖、A/B 轮换、三槽隔离、分块写、Busy、abort、读回失败、墓碑、幂等清空、清空失败回退和清空后再上传。

**人工验证关注点：** task-12 执行真实 Flash 覆盖/清空/重启和断电恢复；native 内存后端不能证明物理 Flash 时序。

**待确认问题：** 无。

---

- [x] task-04: [Storage] 接入 ESP-IDF `custom_data` 分区后端

> 状态：已完成。新增对 `custom_data` 名称、subtype、地址和大小的精确绑定，以及 mmap 和带范围/对齐检查的 erase/write/read 适配；按任务依赖尚未注册进目标 CMake，目标编译与真机 mmap 留待 task-10/12。

**追踪需求：** R-07、R-08、NFR-01、NFR-03

**依赖任务：** task-01、task-03

**修改范围：**

- 新增 `main/services/custom/custom_partition.h`
- 新增 `main/services/custom/custom_partition.c`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 新建。参考 `badge_partition` 的 erase/write/read/mmap 适配方式，增加对名称、subtype、地址、大小和映射范围的严格校验；不改变 badge 后端。

**代码注释要求：** 注释整分区 mmap 的指针生命周期、地址/大小双重校验和失败时不得自动擦除的原因。

**实现内容：**

1. 精确查找并验证 `custom_data` 分区。
2. mmap 1 MiB 数据区，组装 custom store 后端函数表。
3. 所有 erase/write/read 操作先验证 offset + length 不溢出且位于分区内。
4. 任何初始化或映射错误只返回 IO error，不格式化、不擦除、不影响其他分区。

**完成标准：**

- 后端只可能访问 `custom_data` 范围。
- 映射指针在应用生命周期内稳定，能作为 LVGL 数据源。
- 错误分区事实会安全降级，不会尝试修复或擦除。

**自动化验证：**

- task-10 目标构建必须编译该适配层。
- task-01 布局脚本验证静态范围；本任务执行 `git diff --check`。

**人工验证关注点：** task-12 通过启动日志确认 mmap 初始化，观察三槽全量记录时的启动耗时和稳定性。

**待确认问题：** 无。

---

- [x] task-05: [Mode] 实现个性化槽位状态与短按切图

> 状态：已完成。个性化模式以 store 占用状态为唯一真值，支持保留/回退当前槽并在稀疏有效槽之间双向循环；模式与集成聚焦测试 13/13 通过。

**追踪需求：** R-02、R-03、R-08、NFR-03

**依赖任务：** task-03

**修改范围：**

- `main/modes/custom/custom_mode.h`
- `main/modes/custom/custom_mode.c`
- `test/test_modes/test_main.c`
- `test/test_integration/test_main.c`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展。复用 `mode_t::is_available`、enter 和 handle_key，不在 navigation/app_controller 中硬编码个性化槽位规则。

**代码注释要求：** 注释 store 是可用性的唯一真值、最近槽失效时的回退规则，以及短按与全局长按职责边界。

**实现内容：**

1. `custom_mode_descriptor()` 接收 custom store，移除独立 `has_content` setter。
2. `is_available` 实时读取占用掩码。
3. enter 保留最近有效槽，否则选择编号最小的有效槽。
4. 短按上/下循环有效槽、跳过空槽；短按确认和单图切换为 no-op。
5. 验证清空最后一槽后设置返回通过现有 navigation 回退工牌。

**完成标准：**

- 0 张不可进入，1 张短按不变，2/3 张双向顺序稳定。
- 空槽永不显示，最近槽在本次开机内尽量保留。
- 长按切业务页、进入设置、设置返回和唤醒语义不变。

**自动化验证：**

- `/Users/naaran/.platformio/penv/bin/pio test -e native -f test_modes`
- `/Users/naaran/.platformio/penv/bin/pio test -e native -f test_integration`
- 新增 0/1/2/3 张、稀疏槽位、正反循环、动态保存、动态清空和最后一槽回退用例。

**人工验证关注点：** task-12 用实体按键验证短按切图、长按切页和息屏首次按键只唤醒。

**待确认问题：** 无。

---

- [x] task-06: [Display] 实现 240×320 个性化全屏视图

> 状态：已完成。新增单一常驻 240×320 RGB565 全屏 image，直接引用 store 映射数据，并仅在指针或 sequence 变化时清理 LVGL cache；ESP32-C3 目标构建通过，LCD 像素效果留待 task-12。

**追踪需求：** R-02、R-03、R-08、NFR-01、NFR-02

**依赖任务：** task-05

**修改范围：**

- `main/modes/custom/custom_view.h`
- `main/modes/custom/custom_view.c`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 复用。参考 `badge_view` 的常驻 LVGL 对象、mmap 图片描述符和 cache drop 模式，只实现一个全屏 image，不新建通用图片组件。

**代码注释要求：** 注释 Flash 映射数据为何无需复制、图片指针/sequence 变化为何必须先 drop LVGL cache。

**实现内容：**

1. 初始化单个 240×320 image，坐标 `(0,0)`，不创建文字、边框或槽位提示。
2. render 从 custom mode 获取当前 snapshot，设置 RGB565、stride 480 的描述符。
3. 仅当 image 指针或 sequence 变化时丢弃缓存并换源；其余 render 不重复创建对象。
4. 保持所有 LVGL 操作由现有应用锁保护。

**完成标准：**

- view 不分配完整图片副本，不缩放、不裁剪。
- 切槽能换源，重复渲染同一槽不造成对象或缓存增长。
- 页面结构中没有遮挡图片的 UI 对象。

**自动化验证：**

- task-10 目标构建验证 LVGL API、类型和链接。
- 运行 `rg -n "lv_obj_create|lv_image_create|lv_image_cache_drop|240|320" main/modes/custom` 并审查对象只在 init 创建、cache 只在换源清理。

**人工验证关注点：** task-12 检查全屏方向、颜色、无拉伸、无残影和连续切图观感；构建不能证明像素结果。

**待确认问题：** 无。

---

- [x] task-07: [HTTP] 增加个性化读取、流式保存与幂等清空 API

> 状态：已完成。新增三槽 metadata/photo、153,600 字节流式 POST 和事务化幂等 DELETE，沿用 token、Busy 与反馈机制并发布个性化更新事件；个性化 HTTP 5/5、既有 HTTP/Wi-Fi 22/22、全量 Native 81/81 及目标构建通过。应用注入与事件消费仍按计划留在 task-10。

**追踪需求：** R-04、R-06、R-07、R-08、NFR-03

**依赖任务：** task-03

**修改范围：**

- `main/services/badge/badge_http_server.h`
- `main/services/badge/badge_http_server.c`
- `main/services/badge/badge_wifi_service.h`
- `main/services/badge/badge_wifi_service.c`
- `main/CMakeLists.txt`（仅注册 task-07 直接依赖的 `custom_record/store`；分区和应用接线仍留待 task-10）
- `test/test_badge_http/test_main.c` 或新增 `test/test_personalization_http/test_main.c`
- `test/test_badge_wifi/test_main.c`
- `tools/native_sources.py`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展并复用。复用现有 server 的 token、response、request reader、1 KiB buffer 和 Wi-Fi Busy/反馈窗口；新增严格槽位路径解析和 personalization 更新事件，不改工牌 envelope。

**代码注释要求：** 注释 wildcard 路由不重叠约束、URL 槽号转换、DELETE 幂等理由和 HTTP worker 只发布事件不操作 UI 的边界。

**实现内容：**

1. HTTP server 注入 custom store，增加 metadata、photo、slot POST、slot DELETE 路由。
2. 启用 wildcard matcher并提高 handler 容量；严格解析末尾 1～3。
3. POST 校验 token/content type/153,600 长度，通过 request reader 分块写 store。
4. DELETE 校验 token 和空 body，调用幂等 clear，并与所有保存共享全局 Busy。
5. 增加 `PERSONALIZATION_UPDATED` 事件和稳定错误码，不输出敏感数据。
6. 保持 `/api/profile`、`/api/photo`、旧工牌 envelope 和静态资源路由原样可用。

**完成标准：**

- 三槽元数据正确；occupied 可读取，empty 返回 `slot_empty`。
- POST/DELETE 成功提交后才发布更新事件；所有失败路径正确 abort/finish Busy。
- 保存或清空响应丢失后，重新 GET 可判断状态，重复 DELETE 安全成功。

**自动化验证：**

- `/Users/naaran/.platformio/penv/bin/pio test -e native -f test_badge_http`
- 若拆组则同时运行 `-f test_personalization_http`。
- `/Users/naaran/.platformio/penv/bin/pio test -e native -f test_badge_wifi`
- 覆盖 metadata/photo、路径负例、token、content type/length、分块边界、Busy、截断、超时、断连、store 失败、幂等 DELETE、事件和现有工牌回归。

**人工验证关注点：** task-12 记录真实手机 153,600 字节上传耗时、断连提示和热点按键安全退出。

**待确认问题：** 无。

---

- [x] task-08: [Web Core] 泛化矩形图片编辑与 RGB565 转换

> 状态：已完成。现有裁剪核心已泛化为独立输出宽高，新增矩形 cover、旋转后边界、CSS/Canvas 指针坐标换算及 240×320 RGB565 往返验证，同时保留 200×200 工牌形状与透明合成行为；task-08 聚焦 Web 测试 13/13 通过。

**追踪需求：** R-02、R-05、NFR-02、NFR-03

**依赖任务：** 无

**修改范围：**

- `main/web/badge/badge_image.js`
- `test/web/badge_web.test.js`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展。把现有方形 cover 算法泛化为输出宽高参数，工牌和个性化共用一套实现；不复制第二套裁剪器。

**代码注释要求：** 注释旋转后宽高、矩形 cover 计算、CSS/Canvas 坐标比例和 RGB565 长度边界。

**实现内容：**

1. `coverGeometry` / `renderCrop` 支持独立 output width/height。
2. 保留 200×200 工牌三种形状行为。
3. 增加 240×320 方形输出、横竖原图、缩放、旋转和平移限制。
4. 增加 DOM pointer delta 到 Canvas 坐标的纯函数，适配窄屏 CSS 缩放。
5. 验证 240×320 RGBA 转 RGB565 恰为 153,600 字节且可反解。

**完成标准：**

- 个性化 cover 填满 3:4 Canvas，不拉伸；边界拖动不露空白。
- 工牌现有 200×200 square/rounded/circle 输出逐像素关键点测试保持通过。
- 算法不依赖 DOM，可由 Node 直接测试。

**自动化验证：**

- `node --test test/web/badge_web.test.js`
- 新增 240×320、90°/270°、极宽/极高源图、pan clamp、CSS 比例和 RGB565 round-trip 用例。

**人工验证关注点：** task-12 在 Safari/Chrome 检查双指针之外的单指拖动、range 缩放和旋转预览；Node 不证明浏览器解码差异。

**待确认问题：** 无。

---

- [x] task-09: [Web UI] 新增三槽个性化配置、反显、保存与清空

> 状态：已完成。页面默认保留工牌配置并新增独立个性化表单、三槽状态、240×320 编辑、按需反显、目标槽保存、带槽号二次确认的幂等清空、未保存确认和 mutation 锁定；完整 Web 测试 19/19、JS 语法、离线资源扫描及 diff check 通过。手机浏览器人工交互留待 task-12。

**追踪需求：** R-04、R-05、R-06、R-08、NFR-02、NFR-03

**依赖任务：** task-07、task-08

**修改范围：**

- `main/web/badge/index.html`
- `main/web/badge/style.css`
- `main/web/badge/app.js`
- `test/web/badge_web.test.js`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展。复用现有离线页面、图片解码和反馈样式，建立工牌/个性化两个明确的局部 state；不引入框架或全局状态库。

**代码注释要求：** 注释两个表单状态隔离、异步请求捕获发起槽位、切换时释放旧 source 和 mutation 期间锁定控件的原因。

**实现内容：**

1. 增加工牌/个性化一级切换，默认仍为工牌；保留原工牌字段和顺序。
2. 增加三槽 occupied/empty 选择、240×320 Canvas、选择图片、拖动、缩放、旋转和保存。
3. occupied 槽按需 GET 并反显，empty 槽清空画布；一次只加载当前槽。
4. 切换存在未保存编辑时确认放弃；mutation 期间禁止切换目标。
5. 清空仅对 occupied 可用，确认文案包含槽号；取消不请求，成功清空 UI，失败保留原图。
6. 更新静态资源版本，确保嵌入依赖和手机缓存获得新页面。

**完成标准：**

- 工牌和个性化图片、transform、消息和提交状态不会串用。
- 320 px 宽手机页面无阻断操作的横向溢出，实际 Canvas 仍为 240×320。
- 保存/清空请求的方法、URL、token、body 和错误提示符合设计。
- 原工牌读取、编辑和保存流程保持可用。

**自动化验证：**

- `node --test test/web/badge_web.test.js`
- 覆盖默认工牌 tab、槽位加载/切换、empty 清理、保存、二次确认取消/成功/失败、mutation 锁定、状态隔离及现有工牌回归。
- 扫描 HTML/JS/CSS 无 CDN、远程字体或外部 fetch。

**人工验证关注点：** task-12 在 iPhone Safari 和 Android Chrome 逐项验证 tab、三槽、反显、编辑、保存、清空、窄屏和错误提示。

**待确认问题：** 无。

---

- [x] task-10: [Integration] 完成应用接线、资源嵌入和目标构建

> 状态：已完成。custom 分区/store 已接入启动流程，并注入模式与 HTTP；初始化失败仅禁用个性化。个性化更新事件通过主循环请求渲染，新增网页资源已确认嵌入固件。Native 11 组 82/82、Web 19/19、布局 6/6及目标构建通过；当前修复候选 Flash 1,875,148 字节（59.6%），RAM 59,420 字节（18.1%），map 未发现禁用业务符号。

> 2026-09-29 真机缺陷修复：`1.log` 两次复现个性化 POST 后 HTTP 任务栈保护故障，ELF 定位到 `custom_store_finish()` 的 1,152 字节栈帧与调用方 1 KiB 接收缓冲叠加。两段流式缓冲统一降为 512 字节后，目标栈帧分别为 640/576 字节；Native 81/81 和目标构建通过。修复后真机复验属于 task-12，尚未执行。

> 2026-09-29 二次真机缺陷修复：更新后的 `1.log` 证明 512 字节缓冲候选仍会在相同图片上传时触发 `A stack overflow in task httpd`，而同长度的简单图片可完成，说明协议和固定长度无误但 ESP-IDF 默认 4 KiB HTTP 栈缺少最差路径余量。HTTP 任务栈显式提高到 8 KiB，并保留 512 字节流式缓冲；Native 82/82、布局检查和目标构建通过。最新候选仍须在 task-12 使用原失败图片复验。

**追踪需求：** R-01～R-08、NFR-01、NFR-02、NFR-03

**依赖任务：** task-04、task-05、task-06、task-07、task-09

**修改范围：**

- `main/app_main.c`
- `main/app_controller.h`
- `main/app_controller.c`
- `main/CMakeLists.txt`
- `tools/embed_assets.py`（仅在当前资源声明不能自动覆盖时）
- 受接线影响的 native 测试
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 复用并扩展。复用现有 storage 初始化、主循环 Wi-Fi 事件、change-driven render 和 `.incbin` 资源依赖；只增加 custom 实例与事件分支。

**代码注释要求：** 注释 custom 初始化失败为何只降级个性化、HTTP 更新为何经主循环接线，以及嵌入资源必须显式建立增量构建依赖。

**实现内容：**

1. 注册 custom partition 源文件并初始化分区/store；custom record/store 已因 task-07 的 HTTP 直接依赖提前注册。
2. 将 custom store 注入 mode 和 HTTP server，初始化全屏 view。
3. 处理 personalization updated 事件并请求变化驱动渲染。
4. custom 初始化失败时保留工牌、设置和热点，个性化 API 返回后端不可用。
5. 确保 HTML/CSS/JS 变更触发嵌入对象重建。
6. 运行布局校验、目标构建并检查 Flash/RAM/map 业务隔离。

**完成标准：**

- 所有新模块进入正确目标，native source 选择无重复或遗漏。
- 固件在 custom 可用/不可用两种初始化路径均能完成应用启动。
- 目标构建不超过 3 MiB app，静态 RAM 无完整图片级增长。
- Map 中无 BLE、ANCS、AMS、消息或音乐业务符号。

**自动化验证：**

- `/Users/naaran/.platformio/penv/bin/pio test -e native`
- `node --test test/web/badge_web.test.js`
- `/Users/naaran/.platformio/penv/bin/pio run`
- `python3 tools/verify_partition_layout.py partitions.csv .pio/build/ai_passport_badge`
- `git diff --check`
- 检查目标 size/map，并记录 firmware.bin SHA-256；构建只证明编译、链接和静态资源边界。

**人工验证关注点：** task-12 才能确认 LCD、实体按键、mmap、手机浏览器和 Flash 数据保留；本任务不得写成真机 PASS。

**待确认问题：** 无。

---

- [x] task-11: [Documentation] 同步使用说明与建立验收矩阵

> 状态：已完成。README 已同步三槽全屏图片、短按切槽、网页保存/清空、事务保护和四个持久区域的普通上传边界；新增 `acceptance.md`，记录自动化证据并为分区、LCD/按键、双手机浏览器、事务恢复和稳定性提供可填写矩阵。

**追踪需求：** R-04、R-08、NFR-02、NFR-03

**依赖任务：** task-10

**修改范围：**

- `README.md`
- 新增 `specs/20260929_personalization_image_gallery/acceptance.md`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 不涉及代码公共能力。文档复用现有“自动化与真机分开”的验收格式，但只记录本功能实际证据。

**代码注释要求：** 不涉及。

**实现内容：**

1. README 增加三槽个性化、短按切图、网页配置和清空说明。
2. 明确普通上传、禁止全片擦除及四个持久区域保护。
3. 验收矩阵分为自动化基线、分区/烧录、LCD/按键、Wi-Fi/Web、事务恢复、稳定性。
4. 未执行的实机项目全部保持 `NOT RUN`，不根据构建结果推断。

**完成标准：**

- 文档与最终 API/UI/按键行为一致，不描述未交付功能。
- 自动化结果、固件哈希和资源数据可追溯。
- 所有真机项目具备入口、步骤、预期和证据栏。

**自动化验证：**

- 重跑 task-10 全量命令。
- `git diff --check -- README.md specs/20260929_personalization_image_gallery`
- 针对功能范围审查 staged/unstaged diff，确认无无关生产改动。

**人工验证关注点：** 将 task-12 所需的设备、手机、浏览器、烧录前后哈希和故障注入步骤完整列入矩阵。

**待确认问题：** 无。

---

- [ ] task-12: [Acceptance] 执行真机、手机浏览器与数据保护验收

> 状态：部分完成。用户已确认 SHA-256 `771efc…6e439` 的 8 KiB HTTP 栈候选功能测试通过，原失败图片保存不再重启；代码整理后的最终构建为 `caa365…c9a1b`，尚未做设备冒烟测试。断电、Android、完整三槽组合和长期稳定性等未执行项目继续保持 `NOT RUN`。

**追踪需求：** R-01～R-08、NFR-01、NFR-02、NFR-03

**依赖任务：** task-11

**修改范围：**

- `specs/20260929_personalization_image_gallery/acceptance.md`
- 本规格 `tasks.md`、`changelog.md`
- 不修改生产代码；发现缺陷时回到对应需求/设计/实现任务

**公共能力处理：** 不涉及。

**代码注释要求：** 不涉及。

**实现内容：**

1. 烧录前记录 NVS 设置、工牌资料、`cardid` 和已有 custom 数据证据，核对安全分段地址后普通上传。
2. 验证 0/1/2/3 张组合、稀疏槽、短按切图、长按切页、设置返回和息屏唤醒。
3. 用 iPhone Safari 和 Android Chrome 验证三槽反显、编辑、保存、覆盖、清空和错误状态。
4. 验证最后一槽清空后的工牌回退及重启恢复。
5. 在可控条件下覆盖保存/清空断连或断电，确认旧值回退和其他槽隔离。
6. 记录启动扫描耗时、切图延迟、连续五轮配置后的堆/稳定性趋势。

**完成标准：**

- 每个已执行项目记录 `PASS` 或 `FAIL`、日期、实际观察和非敏感证据位置。
- 未执行项目保持 `NOT RUN`；出现 FAIL 时不得宣布功能完成。
- 普通烧录前后 NVS、工牌、`cardid` 和未操作的 custom 槽保持一致。

**自动化验证：**

- 烧录前以 task-11 最终代码再次运行全量 native、Web、布局检查和目标构建。
- 自动化结果仅登记为基线，不替代本任务实机项目。

**人工验证关注点：** 本任务本身即人工验收；禁止全片擦除，照片、姓名、bio、token 和设备身份不得写入共享日志或验收文档。

**待确认问题：** 真机烧录、断电故障注入和设备数据哈希读取需在执行到本任务时由用户明确授权并配合。
