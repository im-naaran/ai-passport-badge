# 实现任务清单

> 状态：已确认

## 执行约束

- 必须按依赖关系执行；多个任务只有在修改范围不重叠时才可并行。本规格默认串行执行，避免当前全部新增/暂存工作区发生交叉覆盖。
- 每个任务只修改列出的范围及对应规格流水；若发现设计边界不成立，停止编码并返回需求或设计阶段更新。
- 自动化结果、目标构建、真机验收分别记录。未经用户实际确认，不得把 LCD、实体键、Safari/Chrome、Flash 恢复或设备功耗标记为通过。
- PlatformIO native 与 target 共用 `.pio/build`，必须串行运行。
- 普通实现和自动化不执行全片擦除、不烧录设备、不修改 `partitions.csv`、NVS、`badge_data` 或 `cardid@0x356000/0x4000`。

## 任务总览

- [x] task-01: [Badge Data] 演进工牌 bio 数据模型与 schema 兼容
- [x] task-02: [HTTP] 扩展 v2 上传协议与资料 API
- [x] task-03: [Web] 增加自定义语句和三种照片形状
- [x] task-04: [UI] 移除公共顶部栏并重排工牌页面
- [x] task-05: [Navigation] 跳过无内容的个性化模块
- [x] task-06: [Settings] 将电量作为设置页只读状态展示
- [x] task-07: [Integration] 完成应用接线与跨模块回归
- [x] task-08: [Docs/Validation] 更新文档并完成全量自动化验证
- [x] task-09: [Device] 执行新功能真机验收

---

## task-01: [Badge Data] 演进工牌 bio 数据模型与 schema 兼容

> 状态：已完成。聚焦 native `test_badge` 8/8 通过；尚未执行目标构建和真机验证。

**追踪需求：** R-04、R-06、R-08、NFR-01、NFR-02、NFR-03

**依赖任务：** 无

**修改范围：**

- `main/services/badge/badge_record.h`
- `main/services/badge/badge_record.c`
- `main/services/badge/badge_store.h`
- `main/services/badge/badge_store.c`
- `main/modes/badge/badge_default.h`
- `main/modes/badge/badge_default.c`
- `test/test_badge/test_main.c`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展。复用现有 UTF-8 解码、CRC、双槽事务和映射图片能力；新增 bio 校验和 schema 1/2 分支，不建立第二套资料存储。

**代码注释要求：** 注释 schema 1/2 在 offset 22、payload 顺序和 image 指针计算上的兼容差异；注释不得重复字段名称本身。

**实现内容：**

1. 增加 `BADGE_BIO_MAX_BYTES = 96`、bio 校验函数和 snapshot/upload meta 字段。
2. 将记录 meta 扩展为可区分 schema 1/2；schema 1 继续按 name+image 校验，schema 2 按 name+bio+image 校验。
3. 新写入只生成 schema 2，保持 40 字节 header、原槽大小和提交标记位置。
4. store 为活动/待提交 bio 使用固定 97 字节缓冲；begin/finish/refresh 覆盖完整事务和重启恢复。
5. 默认工牌 snapshot 使用 `BADGE_DEFAULT_TAGLINE` 作为 bio。

**完成标准：**

- schema 1 固定样本仍读取原姓名/照片且 bio 为空。
- schema 2 支持空、ASCII 和中文 bio；最长 96 字节合法，97 字节或非法 UTF-8/控制字符被拒绝。
- 任一 erase/write/read/commit 故障不发布半成品，旧活动资料保持可用。
- 最大记录仍小于 `BADGE_SLOT_SIZE`，未修改分区表。

**自动化验证：**

- 运行聚焦 native：`pio test -e native -f test_badge`。
- 测试新增：schema 1 fixture、schema 2 encode/decode/CRC、bio 边界、双槽新旧选择、重启恢复和故障注入。
- 本任务不以目标 build 作为唯一验证；build 留到 task-07/08。

**人工验证关注点：** 本任务不烧录。task-09 验证升级前资料读取、新 bio 保存和断电/重启恢复。

**待确认问题：** 无。

---

## task-02: [HTTP] 扩展 v2 上传协议与资料 API

> 状态：已完成。聚焦 native `test_badge_http` 12/12、回归 `test_badge` 8/8 通过；尚未执行目标构建和真机验证。

**追踪需求：** R-05、R-06、R-08、NFR-01、NFR-02、NFR-03

**依赖任务：** task-01

**修改范围：**

- `main/services/badge/badge_http_protocol.h`
- `main/services/badge/badge_http_protocol.c`
- `main/services/badge/badge_http_server.c`
- 必要时 `main/services/badge/badge_http_server.h`
- `test/test_badge_http/test_main.c`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展。继续使用现有定长 parser 状态、writer 接口、session token 和 HTTP 路由；不引入 multipart、base64 或完整 request body 缓冲。

**代码注释要求：** 注释 v1/v2 header 分支、任意 chunk 跨界处理及为何在 name/bio 完整校验后才启动 store writer。

**实现内容：**

1. 支持 14 字节 v2 envelope：magic/version/name length/bio length/image length。
2. 保持 12 字节 v1 envelope 写入兼容，映射为空 bio。
3. parser 增加固定 bio 缓冲和接收状态，保持图片流式写入。
4. 增加 `invalid_bio` 稳定错误码和 422 映射。
5. `GET /api/profile` 返回 JSON 转义后的 `bio`，其他照片/status 接口保持兼容。

**完成标准：**

- v1/v2 在 header、姓名、bio、图片任意分块位置都可正确完成或返回稳定错误。
- v2 长度、bio、token、Busy、超时、断连、额外数据和存储错误均不会发布部分资料。
- API 响应不回显个人文本到错误或日志，profile GET 可安全返回姓名和 bio。

**自动化验证：**

- 运行聚焦 native：`pio test -e native -f test_badge_http`。
- 覆盖 v1/v2 成功、1-byte chunks、所有边界 split、UTF-8 bio、JSON 转义及失败路径。
- 复跑 task-01：`pio test -e native -f test_badge`。

**人工验证关注点：** task-09 使用升级前可能缓存的旧网页和新网页分别保存；确认失败后旧工牌未变。

**待确认问题：** 无。

---

## task-03: [Web] 增加自定义语句和三种照片形状

> 状态：已完成。Node 内置 Web 测试 5/5 通过，外部 URL/远程 fetch 扫描无命中；尚未执行手机浏览器和 LCD 验证。

**追踪需求：** R-05、R-07、R-08、NFR-02、NFR-03

**依赖任务：** task-02

**修改范围：**

- `main/web/badge/index.html`
- `main/web/badge/style.css`
- `main/web/badge/app.js`
- `main/web/badge/badge_image.js`
- 新增无依赖 Web 测试文件（放在 `test/` 下、不进入固件构建）
- 必要时 `main/services/badge/badge_assets.c` 仅核对嵌入资源，不改变资源架构
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展。复用现有 Canvas cover geometry、旋转、拖动、缩放、RGB565 编解码和表单状态；形状几何进入同一最终像素路径。

**代码注释要求：** 注释 shape 必须进入 Canvas 最终像素而不能只靠 CSS；注释已有 RGB565 照片转离屏 Canvas 的不可逆边界。

**实现内容：**

1. 增加 bio 输入、UTF-8 字节计数、校验和错误反馈。
2. 增加方形/圆角/圆形互斥控件，默认方形；圆角固定 24 px，遮罩外填白。
3. 将 shape 集成到所有 redraw 路径，确保选图、拖动、缩放、旋转后预览与提交一致。
4. 把 `/api/photo` 读取结果保存为可重绘的 200×200 离屏 Canvas，使用户不重新选图也能切换 shape。
5. `buildEnvelope()` 生成 v2 name+bio+image body，保存按钮同时受四类状态约束。

**完成标准：**

- bio 为空可保存，超限/控制字符不可保存，计数按 UTF-8 字节而非 JS 字符数。
- 三种形状即时预览；最终 RGB565 始终 80,000 字节，角点/中心像素符合遮罩规则。
- 当前照片加载后可以直接切换形状并保存；页面不声称能恢复已被旧圆形图片丢弃的角落。
- 页面仍完全离线，无外部 URL、依赖、遥测或原图上传。

**自动化验证：**

- 新增 Node 内置 `node:test`/`assert` 测试，不增加 `package.json` 依赖；执行命令为 `node --test test/web/badge_web.test.js`。
- 覆盖 bio 字节计数/校验、v2 envelope 字段和总长、shape 规范化/几何、现有照片重绘入口及 RGB565 长度。
- 执行具体命令在实现时按测试文件落点写入任务记录；若环境无 Node，明确记录 `NOT RUN`，不得用 build 替代。
- 执行 `rg -n "https?://|fetch\(.*https" main/web/badge`，确认无外部资源和远程请求。

**人工验证关注点：** task-09 分别用 iPhone Safari 和 Android Chrome 测试新/旧照片、中文 bio、三种 shape、旋转/缩放/拖动、连续保存和断连。

**待确认问题：** 无。

---

## task-04: [UI] 移除公共顶部栏并重排工牌页面

> 状态：已完成。公共 shell 和工牌视图已完成全屏/bio 改造，`test_modes` 7/7 通过；`app_main.c` 旧调用按 task-07 接线范围保留，尚未目标构建或真机验收。

**追踪需求：** R-01、R-04、R-08、NFR-01、NFR-03

**依赖任务：** task-01

**修改范围：**

- `components/ui_common/include/ui_shell.h`
- `components/ui_common/src/ui_shell.c`
- `main/modes/badge/badge_view.h`
- `main/modes/badge/badge_view.c`
- 与 UI shell 尺寸直接相关的现有测试（若有）
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展并精简。保留唯一的 screen/content 创建入口和 label/page 工具，删除公共 header 状态，不建立各业务页自己的根 screen。

**代码注释要求：** 对图片 descriptor/缓存生命周期保留现有关键注释；补充 bio 空值只清空常驻对象、不会反复创建对象的业务意图。

**实现内容：**

1. `ui_shell_t` 仅保留全屏 `content`，删除 header/name/battery/divider 和 `ui_shell_update()`。
2. `ui_page_create()` 改为 240×320，确保子页完整覆盖旧顶部区域。
3. 工牌保留 200×200 照片，使用独立姓名和 bio label，默认与用户资料走同一 bio 渲染路径。
4. 空 bio 清空对应 label；姓名限制 1 行，bio 限制 2 行并使用次级文本色。

**完成标准：**

- 代码中不再存在公共顶部标题/电量对象或 36 px 偏移。
- 工牌三个元素均在 240×320 内，无对象反复创建。
- snapshot 更新只刷新变化的图片/文本，LVGL lock 约束不变。

**自动化验证：**

- 运行 `rg -n "ui_shell_update|->name|->battery|0, 36|240, 284" components/ui_common main`，人工审查命中只允许无关字段或测试说明。
- 运行受影响 native 模式测试：`pio test -e native -f test_modes`。
- 目标编译和真实布局在 task-07/09 验证；native 无法证明 LVGL 像素布局。

**人工验证关注点：** task-09 检查冷启动、切页、息屏唤醒无旧 header 残影；照片、姓名和两行中英文 bio 不重叠、不越界。

**待确认问题：** 无。

---

## task-05: [Navigation] 跳过无内容的个性化模块

> 状态：已完成。模式可用性、无候选 no-op、未来启用和设置返回回退已覆盖，`test_modes` 7/7 通过；尚未完成 task-07 应用接线和真机按键验收。

**追踪需求：** R-02、R-08、NFR-03

**依赖任务：** 无

**修改范围：**

- `main/navigation/mode.h`
- `main/navigation/navigation.h`
- `main/navigation/navigation.c`
- `main/modes/custom/custom_mode.h`
- `main/modes/custom/custom_mode.c`
- `main/modes/custom/custom_view.h`
- `main/modes/custom/custom_view.c`
- `test/test_modes/test_main.c`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展。把“是否可进入”作为通用 mode 能力，由 navigation 统一消费；不在按键控制器或 app main 中硬编码个性化 ID。

**代码注释要求：** 注释候选搜索为何最多检查 `count - 1` 项，以及没有候选时为什么不能触发 leave/enter。

**实现内容：**

1. `mode_t` 增加可选 `is_available` 回调，空回调默认可用。
2. navigation init/上下切换/直接激活/设置返回统一校验可用性。
3. `custom_mode_t` 增加 `has_content` 和未来可复用的状态 setter，本期生产初始化为 false。
4. 更新所有 mode descriptor 和测试 fake descriptor 为指定字段初始化，避免结构新增字段引发位置错配。

**完成标准：**

- 只有工牌可用时，上/下均保持工牌，不调用 leave/enter。
- 测试把个性化设为有内容后，可按正反方向切换。
- 不可用模式不能直接激活，也不能成为设置返回目标；至少一个模式可用时导航稳定。
- 确认键进入设置、设置返回和短按页面处理语义不变。

**自动化验证：**

- 运行聚焦 native：`pio test -e native -f test_modes`。
- 新增单一可用、动态启用、不可用 activate、return fallback、全不可用 init 失败和生命周期计数用例。

**人工验证关注点：** task-09 验证工牌页长按上/下无空白/闪烁，长按确认仍进设置，返回仍是工牌。

**待确认问题：** 无。

---

## task-06: [Settings] 将电量作为设置页只读状态展示

> 状态：已完成。`test_settings` 9/9、`test_integration` 4/4 通过；尚未执行目标构建和真机显示验收。

**追踪需求：** R-01、R-03、R-08、NFR-01、NFR-03

**依赖任务：** task-04

**修改范围：**

- `main/settings/settings_page.h`
- `main/settings/settings_page.c`
- `main/settings/settings_view.h`
- `main/settings/settings_view.c`
- `main/app_controller.h`
- `main/app_controller.c`
- `test/test_settings/test_main.c`
- `test/test_integration/test_main.c`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 复用并扩展。复用现有 controller 电量快照和主题颜色角色，只把只读 SOC 投影到设置页；不新增采样任务、消息队列或 NVS 字段。

**代码注释要求：** 注释业务页只更新快照而不因电量变化重绘的原因；设置页只读 label 不得混入可选行。

**实现内容：**

1. 设置 view 创建自身“设置”标题和 battery label，列表仍为四个可选行。
2. `settings_page_t` 保存 `battery_soc = -1`，提供受边界检查的更新接口。
3. 列表页显示电量/`--%`及原危险、警告、普通、弱化颜色；编辑/WiFi 子页可隐藏电量。
4. controller 收到新 SOC 后更新快照，仅在设置列表可见时请求渲染。

**完成标准：**

- 电量不会出现在工牌或个性化页面。
- 设置列表显示最新有效 SOC，初始不可用显示 `--%`；光标仍只遍历四行。
- 相同 SOC 不重绘；业务页的新 SOC 不重绘但进入设置后可见。
- CW2017 任务、采样周期和失败重试代码无变更。

**自动化验证：**

- 运行聚焦测试：
  - `pio test -e native -f test_settings`
  - `pio test -e native -f test_integration`
- 覆盖 -1/0/15/16/30/31/100 边界、值未变化、四行 cursor、业务页/设置页渲染请求差异。

**人工验证关注点：** task-09 检查电量位置、颜色和 `--%`；进入编辑/WiFi 再返回列表显示最新值且不改变光标语义。

**待确认问题：** 无。

---

## task-07: [Integration] 完成应用接线与跨模块回归

> 状态：已完成。全量 native 9 组 63/63、Node Web 5/5、目标构建通过；最终 controller 防御性修正后 `test_integration` 4/4 复跑通过。Flash 1,845,994/3,145,728，静态 RAM 59,276/327,680；未烧录或真机验收。

**追踪需求：** R-01～R-08、NFR-01、NFR-02、NFR-03

**依赖任务：** task-01、task-02、task-03、task-04、task-05、task-06

**修改范围：**

- `main/app_main.c`
- 必要时 `main/CMakeLists.txt`（仅现有源码/资源接线）
- `test/test_integration/test_main.c`
- 编译错误直接涉及的 descriptor/初始化位置；若超出已确认设计则停止并回退设计阶段
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 复用。使用已经扩展的 store、navigation、settings、view 和 Web 资源，不新增协调服务或第二条事件通道。

**代码注释要求：** 只在 custom 默认不可用接线、全屏首帧或资料更新事件存在非显然业务顺序时补充注释。

**实现内容：**

1. 创建并持有 `custom_mode_t`，默认无内容；更新 descriptor/view 初始化签名。
2. 移除 `ui_shell_update()` 调用，首帧和后续帧只渲染活动页面。
3. 接通 settings battery 快照、schema 2 profile update 和 badge bio 刷新。
4. 核对内嵌 Web 资源依赖和 CMake 白名单，无新增外部资源。
5. 修正跨模块接口编译点，不改变 WiFi、显示生命周期或按键优先级。

**完成标准：**

- 应用可初始化且默认活动模式为可用工牌。
- 资料保存事件可以刷新包含 bio 的 snapshot；不在工牌页时下次进入显示最新资料。
- 公共 header 已完全解除接线，设置页仍能接收电量。
- 工牌/设置/热点/息屏事件优先级与原有行为一致。

**自动化验证：**

- 先串行运行全量 native：`pio test -e native`。
- native 完成后运行目标构建：`pio run`。
- 记录用例总数、失败项、Flash/RAM 使用；若构建因环境而非代码失败，保存完整错误证据，不声明通过。
- 检查 map/源码依赖中没有新增 BLE、ANCS、AMS、消息或音乐模块。

**人工验证关注点：** 本任务不自动烧录；task-09 覆盖真实 LCD、按键、WiFi 和重启。

**待确认问题：** 无。

---

## task-08: [Docs/Validation] 更新文档并完成全量自动化验证

> 状态：已完成。README 已同步当前行为，新增 `acceptance.md` 并将全部真机项目保持 `NOT RUN`；最终复跑 Web 5/5、Native 63/63、目标构建均通过，功能范围 diff check 通过。

**追踪需求：** R-01～R-08、NFR-01～NFR-03

**依赖任务：** task-07

**修改范围：**

- `README.md`
- 必要的新功能真机验收文档，优先放在本规格目录中
- 本规格 `requirements.md`、`design.md`、`tasks.md`、`changelog.md` 的实现追踪状态
- 所有本轮改动文件的格式修正；不整理无关用户改动

**公共能力处理：** 不涉及新增公共能力。同步当前代码、操作说明、兼容策略、安全边界和未验证事项。

**代码注释要求：** 无新增业务代码；只修正本轮明确发现的不准确注释。

**实现内容：**

1. README 更新为无顶部栏、无内容个性化不可切换、设置电量、bio 和三种照片形状的当前行为。
2. 说明旧 schema 1 自动兼容、形状烘焙且不可恢复已裁掉角落、开放热点安全边界。
3. 建立/更新本规格真机验收矩阵，初始结果为 `NOT RUN`，不继承旧功能的通过状态。
4. 执行完整自动化并检查最终 diff，只修复本轮范围问题。

**完成标准：**

- 文档与实现一致，不把计划或自动化描述成真机完成。
- 所有任务需求追踪完整，changelog 按顺序追加。
- 最终 diff 无 whitespace error、无意外依赖/分区/锁文件修改、无隐私绝对路径进入项目文档（构建命令中的既有本机 PIO 路径仅存在规格执行说明，不进入 README）。

**自动化验证：**

1. Web Node 测试命令（由 task-03 确定）。
2. `pio test -e native`。
3. `pio run`。
4. `git diff --check`。
5. `rg` 检查外部 Web URL、旧公共 header 接线、受保护地址和范围外模块。

以上命令串行执行；构建只是基础兜底，Web/native 逻辑测试和 task-09 人工验收不可省略。

**人工验证关注点：** 审核 README 操作步骤、开放热点风险和 schema/shape 限制是否易懂；设备行为留给 task-09。

**待确认问题：** 无。

---

## task-09: [Device] 执行新功能真机验收

> 状态：已完成。用户已确认 iPhone 获取 `192.168.4.2`、离线页面访问、资料保存、设备图片显示以及圆形/圆角无白边等本轮核心链路，并明确要求关闭任务；Android、断电、完整边界与功耗等未逐项执行内容继续在 `acceptance.md` 保持 `NOT RUN`，作为已知非阻塞验收缺口，不记为 PASS。

**追踪需求：** R-01～R-08、NFR-02、NFR-03

**依赖任务：** task-08

**修改范围：**

- 本规格下的真机验收文档
- 本规格 `tasks.md`、`changelog.md`
- 若验收暴露缺陷，返回对应 task 修改相关生产文件，不在验收记录中掩盖

**公共能力处理：** 不涉及新增公共能力。使用现有安全普通上传、串口观察和验收矩阵。

**代码注释要求：** 本任务本身不改代码；缺陷修复按对应任务注释要求执行。

**执行前置：**

- 需要用户明确授权烧录，并先只读识别实际串口设备。
- 使用 `pio` 普通 upload；解析并核对写入 offset。
- 禁止全片擦除、禁止直接把 app 镜像写到 `0x0`、禁止覆盖 NVS、`badge_data@0x310000/0x40000` 或 `cardid@0x356000/0x4000`。

**完成标准：**

- LCD：无公共 header/残影，工牌照片、姓名、空/一行/两行中英文 bio 完整。
- 按键：个性化为空时上/下不切换、不闪屏；确认进入设置；设置返回工牌；息屏首击只唤醒。
- 设置：电量/`--%`、颜色、四行光标、编辑页和 WiFi 专页行为正确。
- Web：iPhone Safari 与 Android Chrome 均完成方形/圆角/圆形、新/已有照片、拖动/缩放/旋转、bio 边界和连续保存。
- 持久化：schema 1 升级资料保留；schema 2 重启恢复；模拟断连/失败后旧完整资料仍可用。
- WiFi：热点仍按设置开启、任意设备键退出、重复启停无明显异常。
- 验收文档记录设备、固件哈希、步骤、实际结果、证据和未通过项。

**自动化验证：** 烧录前复跑 task-08 全量自动化并记录结果；串口日志只作为运行证据之一，不能替代 LCD/手机观察。

**人工验证关注点：** 本任务全部为真实设备/浏览器验收。只有实际执行并记录的项目才能标记 PASS；未覆盖项保持 `NOT RUN`。

**待确认问题：** 无；执行到本任务时单独取得烧录授权。
