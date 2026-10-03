# 技术设计文档

> 状态：已确认；设计已落地，真机验收待用户执行

## 1. 方案概览

本次采用“扩展现有边界、保持数据路径单一”的方案：

1. 删除公共 UI 壳中的 36 px 顶部栏，将公共内容容器扩展为 240×320；设置页自行创建标题和只读电量标签。
2. 为 `mode_t` 增加可选的可用性回调，导航在上/下切换、直接激活和设置返回时统一跳过不可用模式；个性化模块以显式状态报告当前无内容。
3. 将自定义语句作为工牌资料的一等字段，升级 Flash 记录到 schema 2，并兼容读取 schema 1。
4. 将 HTTP 上传 envelope 升级到 version 2，同时继续接受 version 1 上传并将其语句解释为空，降低浏览器缓存旧脚本造成的升级风险。
5. 图片形状只在浏览器 Canvas 中生成最终像素，继续上传固定 200×200 RGB565 LE 图片；设备端不保存形状元数据、不增加运行时裁剪。

该方案不改变 Flash 分区、WiFi 生命周期、电量采样任务、设置 NVS schema 或设备端图片格式。

## 2. 涉及模块

| 模块 | 修改内容 | 不修改内容 |
| --- | --- | --- |
| `components/ui_common/ui_shell` | 移除标题、电量、分隔线；提供全屏内容容器 | 主题、字体、通用 label/page 工具 |
| `main/navigation` | 增加模式可用性判断和跳过算法 | 稳定 ID、长按确认进入设置的语义 |
| `main/modes/custom` | 增加显式 `has_content` 状态与可用性回调 | 个性化数据模型、存储和上传 |
| `main/modes/badge` | 分离姓名/自定义语句标签并适配全屏 | 200×200 固定照片尺寸 |
| `main/settings` | 设置页自有标题和只读电量投影 | 四个可选择设置项及编辑行为 |
| `main/services/badge` | schema 2、bio 字段、v2 envelope、JSON 字段 | 双槽事务、CRC、提交标记和分区 |
| `main/web/badge` | bio 输入、三种形状、v2 envelope | 离线资源、拖动/缩放/旋转和 RGB565 输出 |
| `main/app_*` | 新状态接线、移除 shell update、按可见性请求电量重绘 | 单线程事件编排、battery task、WiFi 事件路径 |
| `test/` | 新旧兼容、导航、电量、协议测试 | 现有测试框架和目标构建入口 |

## 3. UI 与布局设计

### 3.1 公共全屏容器

`ui_shell_t` 缩减为只持有 `content`：

```c
typedef struct {
    lv_obj_t *content;
} ui_shell_t;
```

- `ui_shell_init()` 继续设置活动 screen 主题，但不再创建 header、name、battery 和 divider。
- `content` 位于 `(0, 0)`，尺寸为 `240×320`，保持不可滚动和应用生命周期常驻。
- `ui_page_create()` 创建的业务页同步改为 `240×320`，以保证子页覆盖完整旧区域。
- 删除 `ui_shell_update()` 及 `app_main.c` 的调用，避免保留失效 API。

### 3.2 工牌布局

工牌使用两个独立文本对象，不再把姓名和默认语句拼到同一个 label：

| 元素 | 建议位置/尺寸 | 说明 |
| --- | --- | --- |
| 照片 | `(20, 8)` / `200×200` | 保持现有尺寸和缓存行为 |
| 姓名 | `(12, 216)` / `216×30` | 单行、居中、主文本 |
| 自定义语句 | `(12, 250)` / `216×54` | 最多两行、居中、次级文本 |

- `bio` 为空时清空 label 文本，不创建/删除对象。
- 默认资料把 `BADGE_DEFAULT_TAGLINE` 放入默认 snapshot 的 `bio`，渲染层不再依据 `is_default` 拼接特殊文案。
- `ui_label_format()` 负责按宽度格式化，姓名 1 行，bio 2 行；保存前的字节上限保证不会依赖静默截断维持协议合法性。
- 实现阶段可在不改变上述边界的前提下微调 1～4 px 间距，以真机字形基线为准；不得缩小照片规避文本布局。

### 3.3 设置布局与电量

`settings_view_t` 增加常驻的 `title` 和 `battery` label，均属于设置页而非公共 shell：

- 顶部区域约 44 px，左侧显示“设置”，右侧显示电量或 `--%`。
- 四行设置列表放在下方区域，光标和行数仍严格为 4。
- 进入 `SETTINGS_EDIT_*` 或 `SETTINGS_WIFI_CONFIG` 时保留“设置”标题，可隐藏电量以给子页内容更多空间；返回 `SETTINGS_LIST` 时恢复最新电量。
- 电量颜色通过复用现有 `UI_COLOR_ROLE_DANGER/WARNING/TEXT_PRIMARY/TEXT_MUTED` 规则计算。

`settings_page_t` 增加 `int battery_soc`，初始化为 `-1`，并提供：

```c
bool settings_page_set_battery(settings_page_t *page, int soc);
```

- 只接受 `-1` 或 0～100；值未变化返回 `false`。
- `app_controller_battery_result()` 同步 controller 和 settings page 的快照。
- 仅当活动模式是设置且 `page == SETTINGS_LIST` 时因电量变化请求重绘；在业务页只更新快照，下一次进入设置自然呈现最新值。
- 采样调度、队列和失败重试保持现状；若后续收到失败值，现有 controller 行为仍保留上一次有效 SOC，不在本次扩大失败状态机。

## 4. 导航与个性化可用性

### 4.1 模式接口

在 `mode_t` 增加可选回调：

```c
bool (*is_available)(void *context);
```

- 回调为空表示模式始终可用，保持工牌和设置等现有描述符的默认行为。
- 可用性是导航属性，不由按键层按 mode ID 特判。

`custom_mode_t` 变为最小显式状态：

```c
typedef struct {
    bool has_content;
    struct custom_view *view;
} custom_mode_t;
```

- 初始化默认 `has_content = false`。
- `custom_mode_descriptor(custom_mode_t *)` 提供 `is_available` 回调。
- 预留 `custom_mode_set_content_available()`，未来个性化数据接入后可更新状态并请求渲染；本期没有生产路径把它设为 `true`，测试使用该入口验证未来启用行为。
- `custom_view` 继续保持最小空页面，不新增内容模型或存储。

### 4.2 导航算法

新增内部 `mode_available()` 与“按方向寻找下一个可用模式”逻辑：

1. 从当前索引的相邻项开始，按上/下方向最多检查 `count - 1` 个候选。
2. 找到可用候选时才执行当前模式 `leave`、更新索引和目标 `enter`。
3. 没有其他可用候选时保持当前模式，不调用 leave/enter，不产生空白页。
4. `navigation_init()` 选择首个可用业务模式；完全没有可用模式时初始化失败。
5. `navigation_activate()` 拒绝不可用目标。
6. 从设置返回时先按 `return_id` 查找可用模式；原目标不可用则回退到第一个可用业务模式。

长按确认、短按转交、息屏手势消费和 WiFi 专页任意键退出仍由原有层级处理，不进入可用性算法。

## 5. 工牌数据模型与 Flash 兼容

### 5.1 公共字段与校验

新增常量：

```c
BADGE_BIO_MAX_BYTES = 96
```

`badge_profile_snapshot_t` 增加 `const char *bio`；`badge_upload_meta_t` 增加 `bio` 和 `bio_length`；store 增加固定 `active_bio[97]` 与 `pending_bio[97]`。

新增 `badge_bio_valid()`：

- 长度 0 合法；非空必须不超过 96 字节。
- 逐码点验证 UTF-8，拒绝 C0 控制字符和 DEL。
- `NULL` 仅在长度为 0 时合法。
- 与姓名共享内部 UTF-8 解码器，不重复实现另一套字符规则。

网页和固件保存时统一去除输入首尾空白后编码；Flash 读取仍验证实际字节，不依赖前端可信。

### 5.2 schema 2 布局

保持 40 字节 header 和现有偏移，使用原保留字段 offset 22 保存 `bio_length`：

| 偏移 | schema 1 | schema 2 |
| --- | --- | --- |
| 4 | `schema = 1` | `schema = 2` |
| 12 | `name_length` | `name_length` |
| 22 | 必须为 0 | `bio_length`，0～96 |
| 24 | `image_length` | `image_length` |
| payload | name + image | name + bio + image |

- `badge_record_decode()` 接受 schema 1 和 2，并把 schema/bio length 返回到 meta。
- schema 1 的 bio length 强制解释为 0；schema 1 的 payload/CRC 仍按“name + image”验证。
- `badge_record_prepare()` 只生成 schema 2 新记录。
- schema 2 payload CRC 顺序为 `name → bio → image`。
- `badge_record_validate()` 根据 schema/长度计算 image 指针，不复制 80 KB 图片。
- 记录总长上限继续由 `BADGE_SLOT_SIZE` 检查；最大新增 96 字节，远小于槽余量。

### 5.3 store 事务

`badge_store_begin_update()` 的顺序保持为：校验 meta → 选择/擦除非活动槽 → 写未提交 header → 写姓名与 bio → 初始化 payload CRC → 接收图片流。

- 新字段和图片仍属于一个事务；bio 写失败直接保持旧活动槽。
- `badge_store_finish()` 读回校验范围扩大为 name + bio + image，然后写提交标记并 `refresh()`。
- `refresh()` 对 schema 1 填充 `active_bio = ""`；对 schema 2 复制并 NUL 结尾。
- 默认 snapshot 的 bio 指向静态 `BADGE_DEFAULT_TAGLINE`，不占 store 动态内存。

## 6. HTTP 协议与 API

### 6.1 envelope version 2

version 2 使用 14 字节小端头：

```text
offset  size  field
0       4     magic = 0x46525042
4       2     version = 2
6       2     name_length
8       2     bio_length
10      4     image_length = 80000
payload       name + bio + image
```

解析器状态增加 bio 缓冲和接收计数，继续支持任意 socket chunk 边界：

- 只有 header、姓名和 bio 使用固定小缓冲；图片仍逐块交给 store writer。
- content length 必须严格等于 header + name + bio + 80,000。
- 姓名和 bio 均完整接收并校验后才启动 store writer。
- 错误新增稳定代码 `invalid_bio`，映射 422。

### 6.2 version 1 写入兼容

解析器继续接受现有 12 字节 version 1 envelope：

- 按旧偏移解析 `name_length` 和 `image_length`。
- `bio_length = 0`，最终仍由 store 写成 schema 2。
- 保留 token、Busy、长度、超时、断连和额外数据语义。

这样升级后即使浏览器短暂使用缓存的旧 `app.js`，仍可保存姓名和照片，只是会把 bio 保存为空。旧 Flash schema 1 的读取兼容与 HTTP v1 写入兼容分别测试。

### 6.3 GET API

`GET /api/profile` 返回：

```json
{
  "name": "张三",
  "bio": "保持好奇，持续创造",
  "width": 200,
  "height": 200,
  "isDefault": false
}
```

- 姓名与 bio 分别 JSON 转义，响应缓冲按两个字段最坏转义长度计算。
- API 不返回照片形状，因为形状已烘焙进像素且不可可靠反推。
- `GET /api/photo` 和 `/api/status` 的图片尺寸/长度不变。

## 7. Web 前端设计

### 7.1 表单状态

`state` 增加：

```js
{
  bio: "",
  shape: "square", // square | rounded | circle
  image: DrawableSource
}
```

- 页面新增单行 bio 输入框、`0/96 字节`计数和三个互斥形状按钮/radio。
- `validBio()` 允许空字符串，拒绝超长和控制字符。
- `saveDisabled()` 同时检查图片、姓名、bio 和上传状态。
- `buildEnvelope()` 生成 version 2，并使用规范化后的姓名/bio UTF-8 字节。

### 7.2 现有照片成为可重绘源

当前页面只把 `/api/photo` 写入预览 Canvas，没有保存可重绘 source。改为：

1. RGB565 转换为 `ImageData`。
2. 写入一个固定 200×200 的离屏 Canvas。
3. 将该 Canvas 作为 `state.image` 的 DrawableSource。
4. 初始化 transform，并通过统一 `redraw()` 生成预览。

因此用户不重新选图也能切换形状并保存。若现有照片已烘焙过圆形白角，切回方形无法恢复被丢弃的原始角落，这是无原图/无 shape 元数据设计下的明确边界；页面不伪装能够恢复。

### 7.3 形状渲染

扩展 `BadgeImage.renderCrop(context, image, transform, outputSize, shape)`：

1. 清空 200×200 Canvas，保留透明底用于网页预览。
2. 建立形状路径：
   - `square`：完整矩形；
   - `rounded`：固定 24 px radius 的 round rect，提供无 `roundRect()` 浏览器的路径回退；
   - `circle`：中心 `(100,100)`、半径 `100`。
3. `save()`、按路径 `clip()`，执行现有 cover/rotate/pan/zoom drawImage，再 `restore()`。
4. `getImageData()` 取得带透明遮罩外区的像素；转 RGB565 时将透明度合成到设备工牌页背景 `#0B0F0E`，避免无 Alpha 的 RGB565 在 LCD 上出现白色外圈。

形状几何提取为无 DOM 的纯函数，便于 Node 内置测试验证类型、半径、边界点和非法值回退；Canvas 集成测试验证关键路径调用和输出尺寸。CSS 的 `border-radius` 只作为控件/画布视觉辅助，不能承担最终图片裁剪。

### 7.4 页面交互

- 形状切换立即调用 `redraw()`；没有图片时仅更新选择状态。
- 选图、缩放、旋转、拖动仍走同一个 `redraw()`，不会绕过形状遮罩。
- `/api/profile` 初始化姓名和 bio；shape 固定初始化为 `square`。
- 错误文案新增 `invalid_bio: 自定义语句格式不正确`。
- 保持页面离线、无第三方脚本、无遥测。

## 8. 应用数据流

### 8.1 启动与渲染

```text
Flash schema 1/2
  -> badge_store snapshot(name, bio, image)
  -> badge_mode
  -> badge_view(photo + name + bio)

battery task -> queue -> app_controller
  -> settings_page.battery_soc
  -> 仅设置列表可见时请求重绘
```

初始化顺序调整为：创建 custom state → 生成 descriptors → navigation 选择首个可用模式（工牌）→ 创建全屏 shell 与各常驻 page → 首帧渲染。

### 8.2 网页保存

```text
照片源 + transform + shape
  -> 200x200 Canvas 最终像素
  -> RGB565 LE
姓名 + bio + 图片
  -> envelope v2
  -> HTTP 分块解析
  -> badge_store 非活动槽事务
  -> commit + refresh snapshot
  -> PROFILE_UPDATED 事件
  -> 工牌页即时刷新或下次进入刷新
```

## 9. 公共能力复用评估

### 检索范围

- `components/ui_common/`：页面、label、主题和字体。
- `main/navigation/`、`main/modes/custom/`：稳定 ID、生命周期和切换。
- `main/services/badge/`：UTF-8 校验、记录、store、HTTP parser/server。
- `main/web/badge/`：Canvas 变换、RGB565 编解码和表单逻辑。
- `main/settings/`、`main/app_controller.*`：设置状态、渲染投影和电量快照。
- `test/test_badge*`、`test/test_modes`、`test/test_settings`：现有 Unity 测试模式。

### 复用与扩展决策

| 能力 | 结论 | 决策原因 |
| --- | --- | --- |
| 全屏页面 | 扩展 `ui_shell` | 公共壳已经是唯一 screen/content 创建入口，不新建第二套根容器 |
| 模式可用性 | 扩展 `mode_t/navigation` | 属于通用导航规则，避免在 app controller 硬编码个性化 ID |
| UTF-8 校验 | 扩展 `badge_record` 内部解码器 | 姓名和 bio 规则高度一致，避免两套 UTF-8 实现漂移 |
| 工牌存储 | 扩展 schema/store | 保持既有双槽断电安全和映射图片零复制 |
| HTTP 协议 | 扩展现有流式 parser | 避免 multipart、JSON base64 或完整 body 缓冲 |
| Canvas 裁剪 | 扩展 `badge_image.js` | 所有照片变换已经集中在该模块，形状必须进入同一最终像素路径 |
| 电量 | 复用 controller 快照和颜色角色 | 不新增采样器、队列或设置持久化字段 |

不新增新的通用服务、后台任务、图片缓存、Flash 分区或依赖库。

## 10. 实现约束

- 关键兼容判断需注释：schema 1/2 payload 差异、HTTP v1/v2 header 差异、不可用模式搜索上限、形状为何烘焙进像素。
- 简单坐标和单次表单接线写在现有模块中，不为单一调用新增抽象层。
- `mode_t` 新字段使用指定初始化器更新生产代码；测试中的位置初始化器同步改为指定字段，降低未来字段追加风险。
- 不改变分区表、PlatformIO 版本、依赖锁、sdkconfig、WiFi adapter 或 settings NVS schema。
- 不写入或清理用户当前暂存文件；只修改任务明确列出的已有文件和本规格文档。
- Web 文本规范化和固件校验必须双层存在，不能只信任浏览器。

## 11. 验证设计

### 11.1 Native / Unity

- `test_badge`
  - bio 空/96 字节/97 字节、中文边界、控制字符和非法 UTF-8。
  - schema 1 记录读取为 bio 空；schema 2 name/bio/image 指针与 CRC 正确。
  - 新资料保存、重启恢复、写入/读回/commit 失败保持旧资料。
- `test_badge_http`
  - v2 header/name/bio/image 在每个分块位置拆分。
  - v1 上传兼容并生成空 bio。
  - 非法 bio、长度、token、Busy、断连和 storage error。
  - profile GET 返回转义后的 bio。
- `test_modes`
  - 只有工牌可用时上下键保持原页且不触发生命周期。
  - 个性化变为可用后双向切换；直接激活/设置返回拒绝或跳过不可用页。
- `test_settings` / `test_integration`
  - 电量更新、未变化、不可用值和设置列表投影。
  - 业务页电量变化不触发无意义渲染，进入设置显示最新快照。

### 11.2 Web 纯逻辑

使用 Node 内置 `node:test`/`assert`，不新增 npm 依赖：

- 姓名/bio UTF-8 字节计数和校验。
- v2 envelope 字段、总长度、空 bio 和中文 bio。
- 三种 shape 的规范化、路径几何和非法 shape 回退。
- 已有 200×200 照片可成为 redraw source，shape 切换不要求重新选图。
- RGB565 输出仍为 80,000 字节。

若当前环境没有 Node，任务应记录 Web 自动化未运行，不得以 `pio run` 冒充该项通过。

### 11.3 目标构建与人工验收

- 自动化兜底：`pio test -e native`，然后串行执行 `pio run`，避免共享 `.pio/build` 并发冲突。
- LCD：全屏无旧 header 残留，照片 200×200，姓名/bio 间距和中文两行。
- 按键：个性化为空时上下不切页；确认进入设置；设置返回工牌；息屏首击只唤醒。
- 设置：电量位置、颜色、`--%`、四行光标和三个子页。
- 手机：iPhone Safari、Android Chrome 的已有照片、新照片、拖动/缩放/旋转、三种形状、连续保存。
- 持久化：旧 schema 1 升级读取、新 schema 2 重启恢复、失败保持旧资料。

自动化结果与真机结果分开汇报；未实际烧录时不得声明设备验收完成。

## 12. 风险与规避

| 风险 | 规避方式 |
| --- | --- |
| 全屏后旧 header 区域残影 | 根内容覆盖完整 240×320，首帧强制刷新；真机检查切页/唤醒 |
| 新字段使旧记录失效 | decode 明确双 schema 分支，加入固定 schema 1 fixture 回归 |
| 浏览器缓存旧 JS | 设备继续接受 v1 envelope；新页面使用 v2 |
| 圆形只改 CSS 未改上传像素 | shape 进入 `renderCrop()` 和最终 Canvas；测试关键几何/输出 |
| 读取已有圆形无法恢复角落 | 不存 shape、不承诺反推；页面默认 square 并明确无原图边界 |
| 只有一个可用模式时反复 leave/enter | 搜索成功后才切换，无候选直接停留 |
| 设置电量造成业务页频繁重绘 | 后台只更新 snapshot，设置列表可见时才请求渲染 |
| bio 增加内存/布局压力 | 固定 96 字节、两个常驻小 label、无图片副本；目标构建和真机观察 |
| 修改已暂存工作区产生混淆 | 不重置/覆盖用户改动；最终按路径报告本轮修改 |

## 13. 需求追踪

| 需求 | 设计落点 |
| --- | --- |
| R-01 | 3.1、3.2、3.3 |
| R-02 | 4.1、4.2 |
| R-03 | 3.3、8.1 |
| R-04 | 3.2、5.1～5.3 |
| R-05 | 6、7.1、7.4 |
| R-06 | 5、6 |
| R-07 | 7.2、7.3 |
| R-08 | 8、10 |
| NFR-01 | 5.3、6.1、10 |
| NFR-02 | 5.2、6.2、7 |
| NFR-03 | 11 |

## 14. 待确认问题

无。
