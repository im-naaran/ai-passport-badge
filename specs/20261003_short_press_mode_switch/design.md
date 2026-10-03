# 技术设计文档

> 状态：已确认

## 设计目标

将当前“工牌模式 + 一个内部切槽的个性化模式”调整为四个平级展示项：一个工牌项和三个固定槽位个性化项。现有 `navigation` 继续负责循环、跳过不可用项、设置进入/返回和生命周期，不在按键层增加业务特判。

完整逻辑顺序固定为：

```text
工牌(id=4)
  -> 个性化槽位1(保留现有个性化 id=2)
  -> 个性化槽位2(新增稳定 id)
  -> 个性化槽位3(新增稳定 id)
  -> 工牌
```

空个性化槽位通过各自的 `is_available()` 被跳过。模式 ID 只表示稳定展示项身份，不写入 Flash，也不改变槽位编号、HTTP API 或存储格式。

## 采用方案与原因

### 方案：每个个性化槽位注册为独立模式描述符

- `mode_registry_t.business` 从两个条目扩展为四个条目，顺序固定为工牌、槽位 1、槽位 2、槽位 3。
- 创建三个 `custom_mode_t`，每个实例绑定同一个 `custom_store_t` 和一个固定逻辑槽位。
- 每个个性化描述符通过固定槽位快照判断可用性并提供渲染数据，不再在模式内部响应上/下键切换槽位。
- 三个个性化描述符共享一个 `custom_view`，只保留一个全屏图片页面和一个 LVGL 图片对象。
- 短按上/下由 `navigation` 在四个展示项间移动；设置仍作为独立系统模式。

选择该方案的原因：

1. 直接复用现有稳定 ID、`is_available()`、空项跳过、生命周期和 `return_id`，与“拍平”语义一致。
2. 导航层只认识通用 `mode_t`，无需依赖个性化 store 或硬编码槽位规则。
3. 每个槽位拥有稳定身份，设置返回不会受动态可用项数量和数组压缩影响。
4. 共享视图避免为三张 Flash 映射图片创建三套 LVGL 页面对象，也保留现有图片缓存失效处理。

### 未采用方案

- **在单个个性化模式内增加边界返回动作**：需要让页面局部处理知道何时越过工牌边界，并扩展 `mode_action_t` 表达前后跳转；模式内外仍保留两套循环规则，容易出现方向和空槽处理不一致。
- **由 `app_controller` 直接判断工牌和槽位**：会把业务 ID、store 和导航细节泄漏到输入协调层，破坏现有职责边界。
- **运行时压缩成可用项数组**：上传或清空后索引会漂移，设置返回必须额外保存类型和槽位；固定四项加动态可用性更简单可靠。

## 模块与数据流

### 初始化

```text
custom_store 初始化并恢复 3 个槽位快照
  -> app_main 创建 badge descriptor
  -> app_main 为 slot 0..2 创建 3 个 custom descriptor
  -> mode_registry 按固定顺序组装 business[4]
  -> navigation_init 选择首个可用项（工牌）
  -> badge_view 创建工牌页面
  -> custom_view 创建一个共享全屏图片页面，并绑定 3 个 custom descriptor
  -> settings_view 创建设置页面
```

`navigation_init()` 始终从固定数组的第一个可用项开始。工牌位于索引 0 且始终可用，因此冷启动和重启必定先显示工牌；个性化槽位的历史状态和上传事件都不能主动改变启动项。

### 短按上/下

```text
BSP_CLICK
  -> app_controller 先检查唤醒消费和热点专页
  -> navigation_key(short, UP/DOWN)
  -> adjacent_available 在 business[4] 中反向/正向扫描
  -> 跳过未占用槽位
  -> leave 当前项、更新 index、enter 目标项
  -> 请求一次渲染
```

### 长按和确认

| 当前状态 | 手势 | 结果 |
|---|---|---|
| 展示项 | 短按上/下 | 切换到前/后一个可用展示项 |
| 展示项 | 短按确认 | no-op |
| 展示项 | 长按上/下 | no-op |
| 展示项 | 长按确认 | 记录当前稳定 ID，进入设置 |
| 设置 | 短按上/下/确认 | 转发给现有 `settings_page` |
| 设置 | 长按任意键 | no-op；热点专页仍由 controller 优先消费任意手势 |

### 个性化数据更新

```text
HTTP 保存/清空成功
  -> 应用事件 PERSONALIZATION_UPDATED
  -> navigation_reconcile_active()
       当前项仍可用：保持当前项
       当前个性化项已失效：leave 当前项并切到工牌
  -> 请求渲染
```

- 上传空槽只改变该描述符的可用性，不自动切换当前画面。
- 覆盖当前槽位时描述符身份不变，共享视图根据图片指针/sequence 更新缓存。
- 清空非当前槽位时当前项不变。
- 清空当前槽位时统一回退工牌，避免渲染失效 Flash 快照。

## 接口与状态设计

### `custom_mode_t`

由“一个模式持有可变当前槽位”改为“一个展示项绑定固定槽位”：

```c
typedef struct {
    custom_store_t *store;
    uint8_t slot;
    void *view;
} custom_mode_t;
```

- 移除 `current_slot` 和 `current_slot_known`。
- `custom_mode_snapshot()` 直接读取绑定槽位。
- `is_available()` 只检查绑定槽位快照是否占用。
- descriptor 不再提供 `handle_key`；展示切换统一由 navigation 处理。
- descriptor 工厂接收槽位和稳定 ID，集中校验 `slot < CUSTOM_SLOT_COUNT`。

### `mode_registry_t`

```c
typedef struct {
    mode_t business[1 + CUSTOM_SLOT_COUNT];
    mode_t system_settings;
} mode_registry_t;
```

- 初始化接口接收一个 badge descriptor、三个 custom descriptors 和 settings descriptor。
- 工牌固定放在索引 0，三个个性化项按逻辑槽位升序放在索引 1～3。
- 保留所有 ID 唯一性验证；最终仍由 `navigation_init()` 做完整兜底检查。

### `navigation_key()`

输入分流调整为：

1. 如果设置激活：仅短按转发 `settings_page`；`MODE_RETURN` 时按稳定 `return_id` 返回。
2. 如果展示项激活且收到长按确认：进入设置并记录当前展示项 ID。
3. 如果展示项激活且收到短按上/下：调用现有相邻可用项扫描并循环切换。
4. 其他展示项手势返回 no-op。

建议将返回值由 `void` 调整为 `bool`，语义为“需要渲染”：

- 有效展示切换、进入/退出设置或设置短按处理返回 `true`。
- 唯一工牌时短按上/下、展示页短按确认、长按上/下返回 `false`。
- `app_controller` 仅在返回 `true` 时设置 `APP_ACTION_NAVIGATION | APP_ACTION_RENDER`，避免 no-op 手势触发多余刷新。

### 活动项协调

新增通用能力：

```c
bool navigation_reconcile_active(navigation_t *navigation);
```

- 展示项仍可用时返回 `false`。
- 当前展示项失效时，优先切换到固定首个可用项；在本项目中始终是工牌，并返回 `true`。
- 设置激活时不立即改变页面；设置返回继续通过 `return_id` 校验并回退首个可用项。
- `app_controller_personalization_updated()` 调用该函数，但无论是否切换仍请求渲染，以便覆盖当前槽位时刷新新 sequence。

### 共享 `custom_view`

- `custom_view_init()` 扩展为接收三个 custom descriptors，创建一次 root/image/descriptor 状态，并把同一个 view 指针绑定到三个 `custom_mode_t`。
- 三个 descriptors 使用同一个 render 回调；回调从当前 descriptor context 的固定槽位获取 snapshot。
- 从一个个性化槽位切到另一个时，继续使用 `lv_image_cache_drop()` 清除旧 descriptor 缓存，然后替换映射指针和 sequence。
- 工牌仍使用独立 `badge_view`，不会被转换成个性化 RGB565 全屏视图。

## 公共能力复用评估

| 检索范围 | 可复用能力 | 当前差距 | 决策 |
|---|---|---|---|
| `main/navigation/navigation.*` | 稳定 ID、循环扫描、可用性过滤、生命周期、设置返回 | 全局切换只在长按发生，且缺少活动项失效协调 | 扩展输入分流并新增通用 reconcile |
| `main/navigation/mode.*` | descriptor、`is_available`、render/enter/leave | 无需新增类型；一个槽位可直接表示为一个 descriptor | 复用，不扩展 `mode_t` |
| `main/navigation/mode_registry.*` | 固定业务顺序和 ID 冲突保护 | 仅容纳一个 custom descriptor | 扩展为固定四项注册表 |
| `main/modes/custom/*` | store 快照、Flash 映射渲染和图片缓存处理 | 当前实例内部保存并切换槽位 | 改为固定槽位 descriptor，三个实例共享 view |
| `main/app_controller.*` | 唤醒、热点优先级、集中渲染请求 | 个性化更新不协调失效活动项，导航 no-op 也渲染 | 复用优先级，接入导航返回值和 reconcile |
| BSP 手势层 | 长短按互斥、释放不补 click | 无 | 完全复用，不修改 |
| custom store | 槽位快照、占用状态、sequence | 无 | 完全复用，不新增展示序列缓存 |

不新建第二套展示导航服务，不把 custom store 注入 navigation，也不改变公共存储或 HTTP 接口。

## 实现约束

- 模式 ID 使用命名常量；保留现有工牌 ID 和第一个个性化 ID，新增两个不与设置 ID 冲突的稳定 ID。
- 对“固定四项顺序”“稳定 ID 用于设置返回”“活动槽位清空回退工牌”补充业务意图注释。
- 不为简单索引转换新增通用容器或动态内存集合。
- 共享 custom view 的创建和绑定集中完成，不允许三个 descriptor 各自分配页面。
- HTTP/Flash 回调仍只投递事件；导航协调和 LVGL 渲染继续在应用主循环执行。
- 不修改图片存储、网页资源、分区、按键 BSP、设置持久化和 UI 布局。
- 保留用户现有暂存内容，不执行 `git add` 或提交。

## 测试设计

### `test/test_modes`

- 注册表包含固定四个展示项，顺序和 ID 唯一。
- 每个 custom descriptor 只对其绑定槽位报告可用并返回对应 snapshot。
- 0 个个性化：短按上/下保持工牌，无 leave/enter。
- 1、2、3 个及稀疏槽位：短按上下按固定顺序双向循环并跳过空槽。
- 展示页短按确认、长按上/下均 no-op；长按确认进入设置。
- 从每个展示项进入设置并返回原稳定 ID。
- 设置期间返回目标失效时回退工牌。
- `navigation_reconcile_active()` 对当前有效、非当前槽删除和当前槽删除分别验证不切换/不切换/回退工牌。

### `test/test_integration`

- 导航有效变化产生 `APP_ACTION_NAVIGATION | APP_ACTION_RENDER`，展示页 no-op 不请求导航渲染。
- 当前个性化槽位被清空后，个性化更新事件回退工牌并请求渲染。
- 当前槽位覆盖时保持展示项并请求渲染。
- 息屏首次短按只唤醒，不切换展示项。
- 热点专页短按或长按只退出热点，不执行展示导航。

### 回归与构建

- 串行运行 PlatformIO 全量 native 测试，保留 custom store、HTTP、settings、core 和 Web 现有覆盖。
- 运行目标固件构建，确认 descriptor/共享 view 接口能在 ESP-IDF/LVGL 环境编译链接。
- 运行 `git diff --check`。
- 自动化不能替代真机 LCD、实体按键、息屏唤醒和热点退出验收。

## 人工验收关注点

1. 分别准备 0、1、3 张个性化图片，确认上下方向、循环顺序和空槽跳过。
2. 设备保存三张个性化图片后重启，确认首屏仍是工牌，未按键前不得显示个性化图片。
3. 从工牌及每张个性化图片长按确认进入设置，操作栏目后返回原展示项。
4. 显示某个性化槽位时通过网页覆盖、清空，确认覆盖留在当前项、清空回退工牌且无残影。
5. 息屏后短按一次只唤醒，再短按才切换；长按上/下无动作且释放不补切换。
6. 热点页面按任意键只退出热点并回到设置列表。

## 主要风险与规避

| 风险 | 影响 | 规避方式 |
|---|---|---|
| 三个 custom descriptor 各自创建 LVGL 页面 | 对象和堆占用增加，缓存状态分散 | 三个 descriptor 共享一个 custom view |
| 动态空槽使数组下标漂移 | 设置返回错误图片 | 固定四项数组并使用稳定 ID，不压缩数组 |
| 清空当前槽后仍渲染旧映射 | 失效内容或显示错误 | 更新事件先 reconcile，再执行渲染 |
| 简单反转 long_press 条件 | 设置短按被全局导航抢占 | 明确区分 settings_active 和展示态分流 |
| no-op 手势仍触发刷新 | 多余 LCD 刷新和功耗 | navigation 返回是否需要渲染 |
| 新增 ID 与现有 ID 冲突 | 初始化失败或返回错误项 | 命名常量、注册表测试和 navigation 唯一性校验 |

## 待确认问题

无。
