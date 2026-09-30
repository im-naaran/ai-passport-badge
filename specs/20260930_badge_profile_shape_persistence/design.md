# 技术设计

> 状态：已确认

## 方案概述

采用“Flash schema 3 + HTTP envelope version 3 + Web 恢复形状”的单版本方案：

1. 为工牌资料定义稳定形状枚举 `square/rounded/circle`。
2. schema 3 将记录头扩展为 44 字节，在 header 中保存 shape；payload 继续保持姓名、bio 和 80,000 字节 RGB565 图片。
3. envelope version 3 使用 16 字节固定头携带形状；解析器移除 version 1/2 兼容分支。
4. `GET /api/profile` 返回形状字符串；网页先恢复 radio，再用相同遮罩重绘 `/api/photo` 的 RGB565 反显源。
5. schema 1/2 不再作为有效记录；设备安全回退到默认工牌，用户重新配置后写入 schema 3。

选择这一方案的原因：形状是资料元数据，应与尺寸、格式等字段一起由 header CRC 保护；单独放入 NVS 会产生图片与形状提交不同步的问题；项目仍处于开发阶段，无需维持 40 字节旧头，因此直接扩展 header 比在 payload 插入特殊字节更清晰，也能让 payload CRC 和流式图片偏移保持原有结构。

## 涉及模块

| 模块 | 修改方向 |
| --- | --- |
| `main/services/badge/badge_record.*` | 44 字节 schema 3 header、形状枚举与 CRC |
| `main/services/badge/badge_store.*` | snapshot/upload meta 传递形状，复用原 payload 事务 |
| `main/services/badge/badge_http_protocol.*` | envelope v3 固定头、任意分块解析、旧版本拒绝和非法形状错误 |
| `main/services/badge/badge_http_server.c` | profile JSON 返回形状，映射 `invalid_shape` |
| `main/modes/badge/badge_default.c` | 默认工牌显式使用 `square` |
| `main/web/badge/app.js` | v3 上传、profile 形状恢复和扩大形状警告 |
| `main/web/badge/index.html` | 更新静态资源版本，避免旧脚本继续提交 |
| `main/CMakeLists.txt` | 仅在既有静态资源版本/依赖需要时更新，不新增构建工具 |
| `test/test_badge` | schema 3、旧 schema 拒绝、store 事务和形状持久化 |
| `test/test_badge_http` | envelope v3、旧 version 拒绝、server JSON 和错误映射 |
| `test/web/badge_web.test.js` | v3 编码、形状恢复及不可逆切换规则 |

不修改 `partitions.csv`、个性化三槽模块、LVGL 工牌视图、Wi-Fi 状态机或设置模块。

## 类型与稳定值

在工牌记录公共头中定义稳定枚举：

```c
typedef enum {
    BADGE_PHOTO_SHAPE_SQUARE = 0,
    BADGE_PHOTO_SHAPE_ROUNDED = 1,
    BADGE_PHOTO_SHAPE_CIRCLE = 2,
} badge_photo_shape_t;
```

- `badge_record_meta_t`、`badge_profile_snapshot_t`、`badge_upload_meta_t` 增加 `shape`。
- 默认工牌固定为 `square`。
- 提供一个小型形状合法性判断供 record、store 和 HTTP 复用；字符串映射只在 HTTP server/Web 各自边界处理，不创建跨语言生成器。

## Flash schema 3

### 记录布局

schema 3 使用 44 字节 header：

```text
0x00 magic          u32
0x04 schema=3       u16
0x06 header_size=44 u16
0x08 sequence       u32
0x0C name_length    u16
0x0E image_format   u16
0x10 width          u16
0x12 height         u16
0x14 stride         u16
0x16 bio_length     u16
0x18 image_length   u32
0x1C shape          u16
0x1E reserved=0     u16
0x20 payload_crc    u32
0x24 header_crc     u32
0x28 commit         u32

name[name_length]
bio[bio_length]
image[80000]
```

最大占用为 `44 + 48 + 96 + 80000 = 80188` 字节，低于单槽 `0x20000`。

### 读取规则

- 只接受 schema 3 且 `header_size=44`，图片起点固定为 `header + name + bio`。
- schema 1/2 或其他版本直接判为无效记录，由 store 在没有有效 schema 3 槽时回退到内置默认工牌。
- 启动扫描不擦除无效槽；首次保存再按双槽事务覆盖目标槽。
- header CRC 固定覆盖 `0x00..0x23`，包含 shape、reserved 和 payload CRC，但不包含 header CRC 自身及 commit；payload CRC 继续只覆盖姓名、bio 和图片。
- schema 3 只接受 `square/rounded/circle`。
- `badge_record_view_t.image` 始终指向真实图片起点，上层不感知 schema 的偏移差异。

### 事务写入

`badge_store_begin_update()`：

1. 校验姓名、bio、图片元数据和形状范围。
2. 擦除非活动槽并写入 schema 3 header。
3. 写入姓名和 bio，并按现有顺序初始化 payload CRC。
4. 后续图片仍通过现有 `badge_store_write()` 分块写入；shape 已在 header 中，不增加 payload 特殊分支。

`badge_store_finish()` 继续按 `name + bio + image` 读回验证 payload CRC，再写 header CRC 和最终 commit。失败路径继续保留旧活动槽。

## HTTP envelope version 3

### 头部格式

version 3 使用 16 字节：

| 偏移 | 长度 | 字段 |
| --- | --- | --- |
| 0 | 4 | magic `BPRF`，little-endian |
| 4 | 2 | version = 3 |
| 6 | 2 | name length |
| 8 | 2 | bio length |
| 10 | 1 | shape：0/1/2 |
| 11 | 1 | reserved，必须为 0 |
| 12 | 4 | image length = 80000 |

最大 HTTP body 为 `16 + 48 + 96 + 80000 = 80160` 字节。

### 解析规则

- 解析器固定接收 16 字节头；magic 或 version 不是当前值时返回协议错误。
- shape 只接受 `square/rounded/circle`，reserved 必须为 0；否则返回 `BADGE_HTTP_ERROR_SHAPE`。
- 执行精确 content length、会话 token、Busy、任意 socket 分块、超时、断连和额外数据检查。
- parser 只把固定头、姓名和 bio 放入小缓冲；图片流式写入方式不变。

HTTP server 将 `BADGE_HTTP_ERROR_SHAPE` 映射为 `422 {"error":"invalid_shape"}`。version 1/2 在 writer 启动前以协议错误拒绝。

## 查询 API

`GET /api/profile` 扩展为：

```json
{
  "name": "张三",
  "bio": "保持好奇",
  "width": 200,
  "height": 200,
  "isDefault": false,
  "shape": "circle"
}
```

- 形状字符串只允许 `square`、`rounded`、`circle`。
- 默认工牌返回 `square`。
- 重新核算固定响应缓冲，按姓名和 bio 最坏 JSON 转义再加 shape 字段留出边界，不使用堆分配。
- `GET /api/photo` 保持 80,000 字节原始 RGB565 响应。

## Web 状态与交互

### 上传

- `buildEnvelope(name, bio, image, shape)` 生成 version 3 的 16 字节头。
- 形状先经过现有 `normalizedShape()`，但提交边界不把非法值静默变成方形；非法状态直接阻止保存。
- 保存 body 仍由固定头、姓名、bio 和 RGB565 组成，不额外上传原图。

### 反显初始化

`profile` 状态增加：

```js
{
  shape: "square",
  sourceShape: null
}
```

加载顺序保持串行：

1. 请求 `/api/profile`，规范化 `shape`。
2. 设置 `profile.shape`，选中对应 radio。
3. 请求 `/api/photo`，转换为离屏 Canvas，并把 `sourceShape` 设为设备返回的 shape。
4. 通过现有 `redrawProfile()` 按当前形状裁剪，而不是直接把离屏 Canvas 当成最终预览。

因此 schema 3 的圆形/圆角图片进入页面后会立刻再次应用相同遮罩，近黑外围不会暴露。

### 不可逆形状扩大

定义遮罩覆盖等级 `circle < rounded < square`，只用于 UI 判断：

- 当 `sourceShape` 非空且目标形状更大时，显示提示：角落已在上次保存时丢失，必须重新选择原图；保存按钮保持禁用。
- 用户选择新本地图片后把 `sourceShape` 设为 `null`，三种形状可自由切换。
- 相同形状或更严格裁剪可以直接保存；成功后当前设备资料记录所选 shape。
- 这一约束避免产生“metadata 标记为方形，但像素仍是圆形黑角”的不一致记录。

相关判断提取为无 DOM 的纯函数，供 Node 测试；DOM 事件仍保留在 `app.js`，不新建通用状态框架。

## 数据流

### 新网页保存

```text
radio shape
  -> Canvas clip
  -> transparent pixels composite to #0B0F0E
  -> envelope v3(shape + RGB565)
  -> HTTP streaming parser
  -> badge_store schema 3 transaction
  -> snapshot(shape + image)
  -> profile updated event
```

### 重新进入网页

```text
schema 3 snapshot
  -> GET /api/profile shape
  -> select matching radio
  -> GET /api/photo RGB565
  -> offscreen source
  -> redraw with matching clip
  -> correct transparent Web preview
```

设备 LVGL 继续直接显示已烘焙的 RGB565，不读取 shape 做二次裁剪。

## 版本行为矩阵

| 输入 | 新固件行为 | 新网页表现 |
| --- | --- | --- |
| Flash schema 1/2 | 忽略并回退默认工牌，不擦除 | 只看到默认工牌 |
| Flash schema 3 | 校验并恢复 | 自动选中并正确遮罩 |
| envelope v1/2 | 协议错误，writer 不启动 | 当前内嵌网页不会发送 |
| envelope v3 | 校验 shape，写 schema 3 | 下次精确恢复 |

## 公共能力复用评估

| 能力类型 | 检索范围 | 现有实现 | 差距 | 决策 | 影响 |
| --- | --- | --- | --- | --- | --- |
| 记录版本化 | `badge_record.*`、`badge_store.*` | 固定头、CRC、双槽 | header 无 shape | 收敛并扩展 | 44 字节 schema 3 header，不新建 store |
| 流式协议 | `badge_http_protocol.*` | 任意分块状态机 | 无 v3 shape | 收敛并扩展 | 固定 16 字节头，图片路径不变 |
| 查询 API | `badge_http_server.c` | 有界 JSON、统一错误映射 | 无 shape 字段 | 扩展 | 增加字符串映射和缓冲边界 |
| Web 裁剪 | `badge_image.js`、`app.js` | 三种 path、统一 redraw | 未恢复 persisted shape | 复用 | 不新增图像算法，只修正状态来源 |
| 形状扩大判断 | `main/web/badge` | 无对应能力 | 需表达不可逆边界 | 新建最小纯函数 | 只服务 profile 编辑和 Node 测试 |
| 分区/图片存储 | `partitions.csv`、badge slot | 空间充足 | 无 | 不涉及 | 不修改布局 |

## 实现约束

### 注释策略

- 注释 schema 3 为何把 shape 作为 header 元数据，并由 header CRC 保护。
- 注释旧 schema 为什么安全回退而不自动擦除。
- 注释 Web 为何必须对已烘焙 RGB565 再应用相同遮罩，以及扩大形状不可恢复。
- 不为枚举赋值、普通字段复制或显而易见分支增加复述式注释。

### 封装边界

- 形状枚举属于 badge record/store 公共数据模型。
- HTTP 数值到错误码、shape 到 JSON 字符串的映射留在 server/protocol 边界。
- Web 的形状规范化和扩大判断使用小型纯函数；不引入新类、状态库或构建依赖。
- 不把工牌和个性化图片协议合并；两者尺寸、记录和 UI 语义不同。

### 最小影响面

- 不改 `partitions.csv`、LVGL 视图、个性化 store/API、Wi-Fi 服务或 app controller。
- 不更改照片背景合成色、圆角半径、RGB565 字节序和图片长度。
- 将旧 schema/envelope 用例改为明确拒绝/回退测试，不保留兼容成功路径。

## 验证设计

### Native 记录与 store

- schema 3 三种 shape 的 44 字节 header、header CRC 和 payload CRC 明确向量。
- schema 1/2 fixture 被拒绝且 store 安全回退默认资料。
- 越界 shape、损坏 shape/CRC 被拒绝。
- 三种形状保存、双槽轮换、重启恢复和写入/读回/commit 失败保持旧快照。

### Native HTTP

- v3 头字段、固定总长以及头/姓名/bio/图片每个分割点的分块解析。
- v1/v2 在 writer 启动前拒绝；v3 只接受 0/1/2，拒绝越界值和非零 reserved。
- `GET /api/profile` 对 default 和三种 shape 返回稳定字符串。
- 既有 token、Busy、超时、断连、额外数据和 profile 更新事件回归。

### Web

- v3 envelope 偏移、长度和 shape 编码。
- profile shape JSON 规范化；非法值作为不兼容响应报错。
- 三种 shape 自动选中并重绘。
- 设备来源的 circle/rounded 反显重新经过对应裁剪。
- `circle -> rounded/square`、`rounded -> square` 禁止保存并要求原图；更严格裁剪和新本地图片不受限。
- 原工牌、三槽个性化和离线资源检查继续通过。

### 目标与人工验证

- 运行全量 Native、Web、分区布局检查、`pio run` 和 `git diff --check`。
- iPhone Safari、Android Chrome 分别保存三种形状，退出并重新进入页面，核对 radio、预览和 LCD。
- 使用旧 schema 2 fixture 验证设备安全回退默认资料且不自动擦除；随后重新配置并验证 schema 3 重启恢复。
- 自动化与目标构建不能替代上述浏览器、LCD 和重启证据。

## 主要风险

| 风险 | 影响 | 规避方式 |
| --- | --- | --- |
| 44 字节 header 偏移或 CRC 范围错误 | 记录无效或图片错位 | 明确 header 向量、静态尺寸断言和三种形状测试 |
| v3 固定头的分块状态错误 | 上传截断或越界 | 固定收满 16 字节后解析，并逐分割点测试 |
| 旧网页仍在浏览器中运行 | version 1/2 保存失败 | 静态资源版本更新、no-store，并返回明确协议错误 |
| 只选中 radio 未重绘 | 黑色外围仍显示 | profile 后顺序加载 photo，并统一调用 redraw |
| 扩大形状被误认为恢复原图 | 保存出 metadata/像素不一致资料 | 设备来源图扩大时禁用保存，要求重新选原图 |
| JSON 缓冲边界不足 | profile 查询失败或截断 | 按最坏转义长度重新计算并测试 |

## 需求追踪

| 需求 | 设计点 |
| --- | --- |
| R-01 | 稳定枚举、44 字节 schema 3 header、store 同事务写入 |
| R-02 | profile JSON、radio 恢复、按形状统一 redraw |
| R-03 | envelope v3、16 字节头、非法形状前置拒绝 |
| R-04 | schema 1/2 回退、envelope 1/2 前置拒绝、单版本矩阵 |
| R-05 | 精确恢复、禁止像素推断、扩大形状保存门禁 |
| R-06 | 最小影响面和完整回归边界 |
| NFR-01 | 44 字节头、原 payload 布局、无分区/RAM 大块变化 |
| NFR-02 | 固件、网页、schema 与 envelope 版本一致性 |
| NFR-03 | Native、HTTP、Web、目标和真机分层验证 |

## 待确认问题

无。用户已确认不保留旧 Flash 记录和旧上传协议兼容，可进入任务拆解与执行确认。
