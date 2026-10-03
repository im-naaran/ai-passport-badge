# 技术设计文档

> 状态：已确认

## 1. 方案概览

采用“独立存储、原生像素、现有链路扩展”的方案：

1. 在 `cardid` 后新增 1 MiB `custom_data` 分区，三个逻辑槽位各使用两个物理记录，通过 sequence、CRC 和最后提交标记实现独立事务更新。
2. 个性化图片固定为 240×320 RGB565 LE。浏览器生成最终像素，ESP32 按块写入 Flash，LVGL 直接引用 mmap 数据，不保存 JPEG/PNG，也不建立完整堆副本。
3. 清空槽位写入新的已提交“空槽墓碑”，不直接抹掉两个副本；这样掉电前后总能选出确定状态，并使 DELETE 请求可安全重试。
4. `custom_mode` 直接读取个性化 store 的槽位快照：至少一张图片时可进入；短按上/下循环有效槽位；长按仍由 navigation 统一处理。
5. 保留现有工牌 API、协议和默认网页入口，在同一个 HTTP Server 中新增 `/api/personalization` 元数据及按槽位读取、保存、清空接口。
6. Web 端将现有图片编辑算法泛化为矩形输出，为个性化提供独立的 240×320 Canvas、槽位选择和保存/清空状态；工牌 200×200 行为继续由回归测试保护。

该方案完整支持三个槽位，不引入 2 槽或 1 槽降级路径，也不修改 `badge_data` 的记录格式。

## 2. 模块与修改边界

| 模块 | 主要修改 | 保持不变 |
| --- | --- | --- |
| `partitions.csv` | 在 `cardid` 后新增 `custom_data@0x35A000/0x100000` | NVS、factory、`badge_data`、`cardid` 地址与大小 |
| `main/services/custom/` | 新增记录格式、三槽 store、ESP 分区后端 | 不复用或改写工牌 schema |
| `main/modes/custom/` | 接入 store、可用性、当前槽位、短按切图和全屏 view | 稳定 mode ID 2、全局长按导航 |
| `main/services/badge/badge_http_server.*` | 注入 custom store，增加个性化路由与内容更新事件 | 现有工牌三个 API 及 envelope v1/v2 |
| `main/services/badge/badge_wifi_service.*` | 增加个性化内容更新事件 | SoftAP、DHCP、token、Busy、退出生命周期 |
| `main/web/badge/` | 增加配置类别、三槽 UI、矩形编辑、保存和清空 | 工牌字段、形状选择、现有保存行为 |
| `main/app_main.c` / `app_controller.*` | 初始化 custom store，接线更新事件与渲染 | 电量、息屏、亮度和工牌刷新路径 |
| `test/` / `tools/` | 新增存储、模式、HTTP、Web 和分区校验 | 保留全部现有回归 |

不把 `badge_store` 抽象成多态通用大框架。两个数据模型虽共享事务思想，但记录尺寸、逻辑槽数量、字段和 HTTP 语义不同；复制少量清晰的边界代码比改造已验收的工牌链路风险更低。

## 3. Flash 分区与记录设计

### 3.1 分区布局

```text
0x000000 ─────────────────────────────────────────────
           bootloader / partition table
0x009000   nvs             0x006000
0x00F000   phy_init        0x001000
0x010000   factory         0x300000
0x310000   badge_data      0x040000
0x350000   未使用间隔       0x006000
0x356000   cardid          0x004000
0x35A000   custom_data     0x100000
0x45A000   其余未分配空间   0x3A6000
0x800000 ─────────────────────────────────────────────
```

- `custom_data` 使用 ESP-IDF 自定义 data subtype `0x41`；现有 `badge_data` 继续使用 `0x40`。
- 起点和大小均按 4 KiB 擦除扇区对齐。
- 普通 PlatformIO upload 只更新 bootloader、分区表和 factory app，不生成或写入 `custom_data` 镜像。

### 3.2 逻辑槽与物理记录

```text
custom_data 0x100000
├── logical slot 1
│   ├── bank A 0x28000
│   └── bank B 0x28000
├── logical slot 2
│   ├── bank A 0x28000
│   └── bank B 0x28000
├── logical slot 3
│   ├── bank A 0x28000
│   └── bank B 0x28000
└── reserved 0x10000
```

- 单个 bank 为 163,840 字节，可容纳 40 字节头和 153,600 字节图片，剩余空间保持擦除态。
- bank 偏移统一由 `logical_slot * 2 + physical_bank` 计算，并在调用 Flash API 前验证范围和加法溢出。
- 分区尾部 64 KiB 本期不使用，不作为隐藏的第四槽，也不参与记录扫描。

### 3.3 固定 40 字节记录头

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u32` | magic | 个性化记录固定 magic |
| 4 | `u16` | schema | 初始为 1 |
| 6 | `u16` | header_size | 固定 40 |
| 8 | `u32` | sequence | 每个逻辑槽独立递增，使用回绕安全比较 |
| 12 | `u16` | logical_slot | 内部 0～2 |
| 14 | `u16` | flags | bit 0 为 `occupied`，其余必须为 0 |
| 16 | `u16` | image_format | 有图时为 RGB565 LE；墓碑为 0 |
| 18 | `u16` | width | 有图时 240；墓碑为 0 |
| 20 | `u16` | height | 有图时 320；墓碑为 0 |
| 22 | `u16` | stride | 有图时 480；墓碑为 0 |
| 24 | `u32` | image_length | 有图时 153,600；墓碑为 0 |
| 28 | `u32` | payload_crc | 图片 CRC；空 payload 使用空输入 CRC |
| 32 | `u32` | header_crc | 覆盖发布前的固定头字段，不包含自身与 commit |
| 36 | `u32` | commit | 最后写入的固定提交标记 |

- 整数按 little-endian 显式编解码，不依赖 C struct padding 或 CPU 对齐。
- `occupied=1` 时尺寸、stride、格式和长度必须全部严格匹配常量；`occupied=0` 时这些字段必须全为 0。
- 记录所属逻辑槽必须与物理区域一致，防止有效记录被错误解释到另一个槽。
- 头 CRC、payload CRC 和 commit 全部有效后记录才参与选择。

### 3.4 Store 状态与 API

新增 `custom_record.*`、`custom_store.*` 和 `custom_partition.*`，核心类型保持纯 C，便于 native 测试：

```c
enum {
    CUSTOM_SLOT_COUNT = 3,
    CUSTOM_IMAGE_WIDTH = 240,
    CUSTOM_IMAGE_HEIGHT = 320,
    CUSTOM_IMAGE_STRIDE = 480,
    CUSTOM_IMAGE_BYTES = 153600,
    CUSTOM_BANK_SIZE = 0x28000,
    CUSTOM_PARTITION_SIZE = 0x100000,
};

typedef struct {
    bool occupied;
    const uint8_t *image;
    uint32_t sequence;
} custom_slot_snapshot_t;
```

Store 对外提供：

- 初始化与降级：`custom_store_init()`、只读空状态初始化。
- 查询：单槽 snapshot、三槽占用掩码、是否存在内容、沿方向查找下一个有效槽。
- 上传事务：`begin_update(slot)`、`write(chunk)`、`finish()`、`abort()`。
- 清空事务：`clear(slot)`；已为空时直接返回成功，保证 DELETE 幂等且不增加 Flash 擦写。

Store 只允许一个 pending 事务，因此工牌保存、个性化保存和清空还需受现有 Wi-Fi `upload_in_progress` 全局 Busy 保护。目标槽号、pending bank、长度和 CRC 保存在小型状态结构中，不分配完整图片缓冲。

### 3.5 启动选择与事务顺序

每个逻辑槽独立验证 A/B 两个 bank：

1. 两个都无效：槽位为空，sequence 基线为 0。
2. 只有一个有效：选择该记录。
3. 两个都有效：用现有工牌相同的回绕安全 sequence 规则选择较新记录。
4. 最新记录为图片：槽位 occupied，snapshot 指向 mmap payload。
5. 最新记录为墓碑：槽位为空；更旧图片仍存在于另一 bank，但不得对外显示。

图片保存顺序：

```text
校验槽位/固定长度
→ 擦除非活动 bank
→ 写未提交头
→ 按块写 153600 B 图片并累计 CRC
→ 写 payload/header CRC
→ 分块读回并复算 CRC
→ 最后写 commit
→ 重新选择该逻辑槽 snapshot
```

清空顺序与保存一致，但写入 `occupied=0`、payload 长度为 0 的墓碑。commit 前任意失败都调用 abort 并保留旧活动记录；commit 成功后即使 HTTP 响应丢失，重试 DELETE 也按“已为空”成功返回。

## 4. 分区后端与内存策略

- `custom_partition_open()` 只接受名称、subtype、起点和大小都符合预期的 `custom_data` 分区，避免仅按名字绑定错误布局。
- 初版映射整个 1 MiB 分区，snapshot 直接保存 bank payload 指针；三张图不会复制到内部 RAM。
- 映射失败时 custom store 进入只读空状态，工牌、设置和热点仍可运行；个性化 API 返回 `storage_unavailable`，模式不可进入。
- 目标构建后记录静态 RAM，真机启动时确认 mmap 成功并测量三槽双 bank 全量 CRC 扫描耗时。若 MMU 映射或启动时间不满足验收，再回到设计阶段改为按活动 bank 映射，不能静默减少槽位数。
- HTTP 上传继续使用 1 KiB 级小缓冲；CRC 读回同样分块，不在栈或堆上声明 153,600 字节数组。

## 5. 设备模式与显示设计

### 5.1 `custom_mode_t`

`custom_mode_descriptor()` 改为接收 `custom_store_t *`，mode state 包含：

- store 指针；
- 当前内部槽号；
- 当前槽号是否已经选择；
- view 指针。

行为如下：

- `is_available`：实时查询占用掩码是否非 0，不缓存独立 `has_content` 真值，避免 Web 更新后两份状态漂移。
- `enter`：当前槽仍 occupied 时保留；否则选择编号最小的有效槽。
- `handle_key`：短按上/下按反向/正向循环有效槽，跳过空槽；短按确认返回 `MODE_STAY`。槽位实际变化时请求后续 render，但不自行操作 LVGL。
- `render`：由 view 查询当前 snapshot 并更新图片源。

现有 `custom_mode_set_content_available()` 删除，由 store 成为唯一内容来源；对应测试 fake 改为 store fixture。

### 5.2 全屏 view

- `custom_view` 创建一个 240×320 `lv_image`，位置 `(0, 0)`，不创建标题、编号、提示或额外 label。
- 图片描述符固定 `LV_COLOR_FORMAT_RGB565`、240×320、stride 480，data 指向 custom partition 映射区。
- view 记录已渲染的 image 指针和 sequence；二者任一变化时先 `lv_image_cache_drop()`，再更新描述符和 source。
- 所有对象创建和 render 继续位于应用 LVGL lock 内；store/HTTP 回调不直接触碰 LVGL。

### 5.3 清空最后一槽后的导航

热点只能从设置页启动，因此清空操作发生时业务页面不在前台：

1. HTTP 清空提交后发布个性化更新事件。
2. custom store 的占用掩码立即变化，`is_available` 随之反映。
3. 若此前从个性化进入设置且最后一槽被清空，设置返回时现有 `navigation::return_index()` 会发现 return ID 已不可用，并回退第一个有效业务模式，即工牌。
4. 如果仍有其他图片，`enter` 自动选取仍有效的最近槽或首个槽。

不在 HTTP worker 中强制切换页面，也不在 navigation 中硬编码 mode ID 2。

## 6. HTTP API 设计

### 6.1 路由

现有工牌路由不变，新增：

| 方法 | 路径 | 成功响应 | 用途 |
| --- | --- | --- | --- |
| GET | `/api/personalization` | JSON | 返回尺寸和三个槽位占用状态 |
| GET | `/api/personalization/photo/{1..3}` | RGB565 binary | 读取已有槽位图片 |
| POST | `/api/personalization/slot/{1..3}` | `{"ok":true}` | 保存当前 Canvas 像素到目标槽 |
| DELETE | `/api/personalization/slot/{1..3}` | `{"ok":true,"occupied":false}` | 幂等清空目标槽 |

运行时启用 ESP-IDF wildcard URI matcher，并注册互不重叠的 `/photo/*` 与 `/slot/*`。核心层只接受路径末尾单个 ASCII 数字 1～3，不接受符号、前导零、额外路径或越界值；进入 store 前集中转换为 0～2。

### 6.2 元数据响应

```json
{
  "slotCount": 3,
  "width": 240,
  "height": 320,
  "imageBytes": 153600,
  "slots": [
    {"slot": 1, "occupied": true},
    {"slot": 2, "occupied": false},
    {"slot": 3, "occupied": true}
  ]
}
```

- 不返回 sequence、Flash 地址、文件名或图片摘要。
- 读取空槽图片返回 404 `slot_empty`；存储后端不可用返回 503 `storage_unavailable`。

### 6.3 保存协议

- slot 由 URL 指定，body 只包含固定 153,600 字节 RGB565 LE 像素，不再增加一套可变 envelope。
- 请求必须为 `application/octet-stream`、content length 精确匹配，并携带现有 `X-Passport-Session` token。
- 通过 token 和 Busy 后先 `custom_store_begin_update()`，再复用请求 read 回调按块写入；额外数据、截断、超时、断连或 store 错误均 abort。
- 成功提交后调用 `badge_wifi_service_personalization_updated()`，再发送响应并沿用现有保存成功反馈窗口。

### 6.4 清空协议

- DELETE 携带 token，body 必须为空；非法槽位、非法 token 或非空 body 均拒绝。
- 清空也调用 `badge_wifi_service_begin_upload()` / `finish_upload()`，与工牌保存和个性化保存使用同一全局互斥及安全退出延迟。
- 已空槽返回成功且不写 Flash，支持客户端在“提交成功但响应丢失”后安全重试。
- 已占用槽调用事务墓碑写入；成功后发布同一个个性化更新事件。

### 6.5 错误与日志

扩展稳定错误码：`invalid_slot`、`slot_empty`、`storage_unavailable`，继续使用 `invalid_token`、`invalid_length`、`busy`、`timeout` 和 `storage_error`。

日志只记录 HTTP 方法、稳定路由类别、槽位编号、字节数和结果码；不记录 token、图片字节、原文件名、body 或可恢复图片的摘要。

## 7. Wi-Fi 事件与应用接线

- `badge_wifi_event_type_t` 增加 `BADGE_WIFI_EVENT_PERSONALIZATION_UPDATED`；不把 custom store 依赖放进 Wi-Fi 状态机。
- HTTP 完成保存或有效清空后发布该事件。事件不携带图片指针；应用循环收到后从 store 获取最新状态。
- `app_controller_personalization_updated()` 只请求 change-driven render，并允许模式可用性在下一次导航查询时生效。
- 现有 `BADGE_WIFI_EVENT_PROFILE_UPDATED` 和工牌 sequence 去重逻辑不改变。
- `init_storage()` 分别初始化 badge/custom 两个分区：badge 失败沿用默认工牌，custom 失败降级为空；两者错误互不扩散。

## 8. Web 页面与状态设计

### 8.1 信息架构

页面标题下增加两个一级配置切换项：

```text
[ 工牌配置 ] [ 个性化配置 ]
```

- 初始仍选择“工牌配置”，保留现有用户路径和表单顺序。
- 两个表单拥有独立 Canvas、图片 source、transform、dirty/uploading 状态、消息区域和保存按钮。
- 切换配置类别或个性化槽位时若当前有未保存编辑，先提示确认放弃；取消则保持当前界面。

### 8.2 个性化表单

```text
个性化图片
[ 槽位 1 · 已保存 ] [ 槽位 2 · 空 ] [ 槽位 3 · 已保存 ]

┌──────── 240 ────────┐
│                     │
│    3:4 图片预览      │ 320
│                     │
└─────────────────────┘

[选择图片]  缩放  [旋转 90°]
[保存到槽位 N]
[清空槽位 N]
```

- 实际 Canvas 固定 `width=240 height=320`；CSS 在极窄屏下等比缩小显示，指针坐标按 CSS/Canvas 比例换算，不能继续假设 1 CSS px = 1 Canvas px。
- 选择 occupied 槽时按需 GET 图片并通过 RGB565 → RGBA 反显；一次只保留当前槽 source。
- 选择 empty 槽时清空 Canvas、禁用保存直至选图，并隐藏或禁用清空按钮。
- 从设备读回的 RGB565 图可继续拖动/缩放/旋转，但与工牌遮罩相同，已经被 cover 裁掉的原始画面无法恢复；页面不声称保留原图。

### 8.3 图片算法泛化

保留 `badge_image.js` 文件和现有导出兼容，内部新增矩形几何：

```js
coverGeometry(sourceWidth, sourceHeight, transform, outputWidth, outputHeight)
renderCrop(context, image, transform, outputWidth, outputHeight, shape)
```

- cover scale 改为 `max(outputWidth / rotatedWidth, outputHeight / rotatedHeight) * zoom`。
- X/Y 最大平移分别由渲染宽高与输出宽高计算。
- 工牌继续传入 200×200 并应用 square/rounded/circle；个性化传入 240×320 且固定 square，不显示形状选项。
- `rgbaToRgb565()` 和 `rgb565ToImageData()` 继续按 pixel count/width/height 工作，新增 153,600 字节断言。
- 为现有工牌调用保留兼容包装或同步更新全部调用与测试，不留下两套几何实现。

### 8.4 保存与清空状态

- 保存：从个性化 Canvas 读取 RGBA，转换为 RGB565，POST 到当前 slot URL；成功后更新 occupied、清除 dirty，继续显示当前画布。
- 清空：仅 occupied 时可点，使用包含槽号的确认对话框；取消时不发请求；确认后 DELETE。
- 清空成功：释放当前 source、清空 Canvas、将槽标记 empty、禁用保存/清空；其他槽本地状态不变。
- 清空失败：保留当前图片和 occupied 状态，提示“原图片仍保留”。
- `uploading`/`clearing` 归一为同一个 mutation 状态，任何配置保存或清空进行中时禁用所有会改变请求目标的控件，防止响应落到错误槽。

## 9. 公共能力复用评估

| 检索范围 | 现有能力 | 差距 | 决策 |
| --- | --- | --- | --- |
| `badge_record/store/partition` | 双副本、CRC、commit、mmap | 单逻辑资料且字段不同 | 复用模式和 CRC 基础；新建 custom 模块，不泛化已验收接口 |
| `mode_t` / `navigation` | 可用性、生命周期、短/长按分派、返回回退 | custom 没有 store/按键 | 扩展 custom mode；navigation 无需业务硬编码 |
| `badge_wifi_service` | 会话 Busy、安全停止、事件队列 | 只有 profile 更新事件 | 新增一种内容更新事件，复用同一事务互斥 |
| `badge_http_server` | token、request reader、response、离线资源 | 无 wildcard 槽位 API、无 DELETE | 在现有 server 内扩展，保持工牌路由不变 |
| `badge_image.js` | 解码、旋转、cover、RGB565 | cover 假定方形、指针假定同像素 | 泛化矩形几何和坐标换算，保留工牌回归 |
| `tools/` | 当前只有资源嵌入与 native source 脚本 | 无现存布局验证脚本 | 新建最小 Python 分区校验和自身测试 |

不新增通用 Repository、事件总线、动态路由框架或 Canvas 编辑器类。三槽常量固定，简单数组和纯函数比动态配置更容易验证。

## 10. 实现约束与注释策略

- 生产代码注释只解释业务边界和非显然安全逻辑：墓碑为何不能擦双副本、commit 必须最后写、sequence 回绕、mmap 指针生命周期、LVGL cache drop、CSS/Canvas 坐标换算及旧工牌兼容。
- 不为显然的 getter、固定常量或逐字段赋值添加空泛注释。
- 所有槽位转换集中在路径解析/公共边界函数，内部只使用 0～2；UI 只显示 1～3。
- Flash 偏移先做乘法/加法边界检查，再调用 erase/write/read；不得依赖断言保护生产输入。
- HTTP worker 不直接调用 navigation 或 LVGL；应用主循环是唯一 UI 状态接线位置。
- 工牌协议、custom 协议和记录常量分别命名，避免复用 `BADGE_IMAGE_*` 造成尺寸混淆。
- 不升级依赖、不修改 ESP-IDF/PlatformIO 版本、不引入第三方前端库。

## 11. 验证设计

### 11.1 Native 聚焦测试

新增 `test/test_custom/`：

- 六 bank 边界和槽位映射。
- 单槽首次保存、覆盖、A/B 轮换、三个槽互不影响。
- header/payload/commit 损坏、部分写、读回 CRC 失败均回退旧记录。
- sequence 回绕选择。
- 墓碑清空、清空掉电回退、已空幂等清空、清空后再次上传。
- 占用掩码、首个/相邻有效槽查找。

扩展 `test_modes` / `test_integration`：

- 0/1/2/3 张时可用性与短按循环。
- 空槽跳过、单图 no-op、最近槽失效后回退。
- 清空最后一槽后设置返回工牌。
- 长按导航、设置、息屏唤醒行为保持原语义。

### 11.2 HTTP 测试

扩展 `test_badge_http` 或按职责拆出 `test_personalization_http`：

- 元数据 JSON 和三槽 photo GET。
- 路径严格解析：0、4、前导零、尾随字符和错误类别。
- POST 固定 content type/length/token、1 字节分块、跨边界分块、截断、超时、断连、额外数据、Busy 和 store 失败。
- DELETE token、空 body、occupied 墓碑、empty 幂等、Busy、失败保留及更新事件。
- 现有工牌所有路由与协议测试继续通过。

### 11.3 Web 测试

- 240×320 横竖图 cover scale、旋转后尺寸、X/Y pan clamp。
- CSS 缩放下 pointer delta 到 Canvas 坐标换算。
- 240×320 RGB565 输出恰为 153,600 字节且可反解。
- 三槽 occupied/empty UI、按需加载、不跨槽复用图片。
- 保存 URL/方法/body/token；清空二次确认取消不请求、确认 DELETE、成功/失败状态。
- 工牌 200×200、三形状、bio、version 2 envelope 原测试不变。

### 11.4 分区与目标构建

新增 Python 标准库脚本和单测，解析 `partitions.csv` 并验证：

- Flash 8 MB 边界、4 KiB 对齐、所有分区不重叠。
- factory 固定 `0x10000/0x300000`。
- `badge_data@0x310000/0x40000`。
- `cardid@0x356000/0x4000`。
- `custom_data@0x35A000/0x100000`。
- 普通 upload 镜像列表不包含三个数据分区。

最后运行全量 native、Web、`git diff --check` 和 `pio run`，记录 Flash/RAM 变化并检查 map 中仍无 BLE、ANCS、AMS、消息或音乐业务符号。

### 11.5 真机验收

自动化之外单独记录：

- 普通烧录前后工牌、NVS、`cardid` 和已有 custom 图片保留。
- iPhone Safari 与 Android Chrome 的 240×320 选择、拖动、缩放、旋转、反显、覆盖和清空。
- 1/2/3 张组合下的设备长按切页与短按循环。
- 全屏像素方向、颜色、边缘、无拉伸/残影。
- 保存与清空阶段断连/断电后的旧值回退。
- 三槽双 bank 全部有效时的启动耗时、切图延迟、堆和 MMU 映射稳定性。

## 12. 主要风险与规避

| 风险 | 规避方式 |
| --- | --- |
| 新分区覆盖受保护数据 | 固定地址常量、CSV 解析测试、目标产物校验和普通上传范围检查 |
| 清空时直接擦除导致掉电后状态不确定 | 使用 sequence 更高的已提交墓碑；旧图片保留到后续覆盖 |
| 三槽状态和 mode `has_content` 漂移 | 删除独立布尔真值，`is_available` 实时查询 store 占用掩码 |
| mmap 1 MiB 超出真机资源 | 目标初始化检查与真机 MMU/启动验收；失败时功能降级，不影响工牌 |
| 切图仍显示 LVGL 缓存旧内容 | image 指针/sequence 双判定，换源前显式 drop cache |
| 矩形算法回归现有工牌裁剪 | 单一泛化实现，保留全部 200×200/形状测试并新增 240×320 用例 |
| 手机窄屏拖动比例错误 | 实际 Canvas 固定像素，pointer delta 按 DOM rect 比例换算 |
| 保存/清空响应落到已切换槽 | mutation 期间锁定类别和槽位控件，请求捕获发起时 slot |
| 清空成功但响应丢失造成用户不确定 | DELETE 幂等；重载元数据可确认最终状态 |
| HTTP handler 超过默认上限或 wildcard 冲突 | 明确增加 handler 容量，使用 `/photo/*` 与 `/slot/*` 不重叠前缀并覆盖路由测试 |

## 13. 需求追踪

| 需求 | 设计落点 |
| --- | --- |
| R-01、R-07 | 独立 `custom_data`、三逻辑槽 × 双 bank、记录头与事务 store |
| R-02 | 240×320 RGB565 常量、mmap snapshot、全屏 LVGL image |
| R-03 | store 占用掩码、custom mode enter/handle_key/is_available、navigation 返回回退 |
| R-04 | 双配置入口、三槽元数据与按需反显、状态隔离 |
| R-05 | 矩形 cover、Canvas 坐标换算、浏览器生成原生像素 |
| R-06 | slot URL、流式 POST、幂等 DELETE、token/Busy/错误状态 |
| R-08 | 主循环事件接线、工牌 API 不变、日志与烧录边界 |
| NFR-01 | 1 MiB 分区、零完整堆副本、构建资源记录 |
| NFR-02 | 离线原生 Web、窄屏 CSS、现有工牌回归 |
| NFR-03 | Native/HTTP/Web/布局/目标构建/真机分层证据 |

## 14. 待确认问题

无。
