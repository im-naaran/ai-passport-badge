# 技术设计

> 状态：已确认

> 交付范围调整（2026-09-27）：当前工程收敛为生产源码、构建配置、静态资源、许可证、中文使用说明，以及直接覆盖核心业务逻辑的 C/Unity native 测试。Node Web 测试和其他 Python 自动化门禁仅作为历史设计记录，不再代表精简后的当前文件清单。

## 方案概述

采用“官方工程骨架 + 展示侧选择性迁移”的方案，不从综合固件复制完整工程后再删除功能。

目标仓库使用官方 Demo 的根目录结构、硬件 BSP 边界、受保护 Flash 布局、许可证和验证原则；构建统一使用 PlatformIO 驱动固定版本 ESP-IDF 5.5.3，并保留旧开发固件中与核心逻辑直接相关的 native/Unity 测试。旧开发代码只按文件和能力白名单迁移，所有目标源文件直接落在目标仓库，不能引用参考目录。

选择该方案的原因：

1. 官方骨架提供板级事实、BSP 责任边界、受保护 `cardid` 规则和可维护的根目录结构。
2. 旧展示侧代码已经包含工牌双槽存储、流式上传、离线网页、设置和省电能力，重写会扩大回归面。
3. 白名单迁移可在最初的 CMake 源清单中排除 BLE、消息和音乐，比复制综合固件后反向清理更容易证明链接隔离。
4. PlatformIO 继续固定 Espressif32、ESP-IDF 和 native 测试依赖，同时底层仍是标准 ESP-IDF/CMake，不引入第二套生产源码。

不采用的方案：

| 方案 | 不采用原因 |
| --- | --- |
| 直接复制综合固件并隐藏消息/音乐页面 | `app_main.c`、CMake、sdkconfig 和测试仍耦合 BLE/ANCS/AMS，难以证明链接层隔离 |
| 保留运行时 BLE/Wi-Fi 互斥 | 展示固件不需要 BLE；保留 stop/restart、callout 和恢复状态只会增加内存和生命周期风险 |
| 从官方 Demo 重新实现全部工牌能力 | 会重复实现已有的事务存储、网页协议和浏览器图片处理，扩大掉电与兼容性回归范围 |
| 同时维护原生 ESP-IDF与 PlatformIO 两套独立构建流程 | 容易出现依赖、sdkconfig、产物和验证漂移；统一由 PlatformIO 调用 ESP-IDF CMake |

## 涉及模块与目标目录

```text
./
├── .gitignore
├── CMakeLists.txt
├── LICENSE
├── platformio.ini
├── partitions.csv
├── sdkconfig.defaults
├── components/
│   ├── bsp/
│   ├── text/
│   └── ui_common/
├── main/
│   ├── CMakeLists.txt
│   ├── app_main.c
│   ├── navigation/
│   ├── modes/{badge,custom}/
│   ├── services/{badge,config,display}/
│   ├── settings/
│   └── web/badge/
├── test/
├── tools/
├── docs/
└── specs/
```

### 迁移来源规则

| 目标内容 | 基线来源 | 处理方式 |
| --- | --- | --- |
| 根 CMake、许可证、板型与保护布局原则 | 官方 Demo | 复制并按展示固件范围更新；保留上游许可与来源说明，不复制非构建必需的协作说明 |
| PlatformIO、native 测试组织、固定版本依赖 | 旧开发固件 | 迁移后改为根目录布局和单一 `ai_passport_badge` 环境 |
| BSP | 官方 Demo + 旧开发 BSP | 以官方硬件定义为事实源，逐项移植旧按键手势、背光、LCD 休眠/恢复；不迁移音频和 BLE 相关依赖 |
| `text`、`ui_common` | 旧开发固件 | 按工牌、设置、顶栏实际依赖迁移；保留字体许可证和生成说明 |
| 工牌、配置、显示、导航和设置模块 | 旧开发固件 | 白名单迁移并删除消息/音乐/无线互斥接口 |
| Web 资源和测试 | 旧开发固件 | 迁移到目标路径并修改测试定位，不从参考目录读取 |
| 官方 Demo 页面、音频、Radio/BLE Demo | 官方 Demo | 不进入生产源清单；只保留必要的硬件实现参考 |
| 消息、音乐、BLE、ANCS、AMS、`wireless_mode` | 旧开发固件 | 不迁移，不保留空壳、兼容宏或无调用源文件 |

## 构建与依赖设计

### 构建环境

- 生产环境命名为 `ai_passport_badge`，固定 `platformio/espressif32@6.13.0` 和 `platformio/framework-espidf@3.50503.0`。
- native 环境继续使用固定的 `platformio/native@1.2.1` 与 Unity 2.6.1，仅编译硬件无关的白名单源文件。
- `main/CMakeLists.txt` 只注册展示固件源文件，`REQUIRES` 限于实际使用的 `bsp`、`text`、`ui_common`、`nvs_flash`、`esp_partition`、`esp_wifi`、`esp_netif`、`esp_event`、`esp_http_server` 和 `esp_timer`。
- 不声明 `bt`，`sdkconfig.defaults` 明确关闭蓝牙相关配置，并保留 Wi-Fi 小缓冲配置、24 KiB LVGL 池、8 MB Flash 与 3 MB 应用限制。
- 生产产物复制/命名为包含 `badge` 或 `display` 角色的文件；启动日志输出相同角色。

### 单一验证入口

`tools/validate.sh` 作为完整入口，顺序执行：

1. 仓库静态检查与隐私路径扫描。
2. native/Unity 展示侧测试。
3. Python 布局测试。
4. Node Web 测试。
5. `ai_passport_badge` 目标构建。
6. 构建产物布局检查和 ELF/Map 静态隔离检查。

单项命令仍可在开发中直接运行，但 README 和 CI 只把 `tools/validate.sh` 作为交付门禁，避免两套命令序列漂移。

## Git 与隐私路径设计

### `.gitignore`

实现阶段第一个工程变更建立根 `.gitignore`，至少包含：

- `/_ai-passport-tools_dev/` 与 `.DS_Store`。
- `/.pio/`、`/build/`、`/managed_components/`。
- `/sdkconfig`、`/sdkconfig.old` 和生成的 profile sdkconfig；保留 `sdkconfig.defaults`。
- `.idea/`、`.vscode/`、`__pycache__/`、`*.py[cod]`。
- 本地日志、串口转储和临时测试产物的明确模式；不使用会误忽略正式 `docs/`、`specs/`、Web 资源或测试夹具的宽泛规则。

新增后用 `git check-ignore -v` 验证代表性路径；不删除被忽略的本地参考目录和文件。

### 隐私路径防护

- 文档和示例命令只使用仓库相对路径或 `<repo>`、`<device-port>` 等占位符。
- 脚本通过自身位置、PlatformIO 的项目目录变量或 Git 根目录解析路径，不硬编码开发者主目录。
- 仓库静态检查扫描 macOS/Linux/Windows 常见用户主目录绝对路径模式，并允许工具运行时在临时输出中使用系统生成路径，但不得把这些内容写入受版本控制文件。
- 检查失败时只报告文件和行号，不回显完整疑似隐私路径。

## 应用组成与生命周期

### 薄启动入口

`app_main.c` 只负责依赖顺序和主事件循环，不包含业务协议实现：

1. 初始化 NVS；失败时不自动擦除。
2. 初始化 BSP、按键队列、LCD/LVGL、背光和电量访问。
3. 初始化设置存储、工牌分区与默认/用户快照。
4. 创建公共 UI、工牌页、空白个性化页和设置页。
5. 初始化导航、显示活动、渲染策略和电量调度。
6. 初始化 Wi-Fi/HTTP 服务对象，但不在启动时开启热点。
7. 进入单一应用循环，串行消费按键、Wi-Fi 小事件、保存完成、电量结果和渲染请求。

任何步骤失败都进入可显示、可诊断的降级路径；不得通过擦除 NVS 或保护分区自愈。

### 所有权

| 资源 | 唯一所有者 | 约束 |
| --- | --- | --- |
| LVGL 对象 | 应用/UI 上下文 | 只在持有 LVGL 锁时创建、更新和显示 |
| 工牌 Flash 写事务 | `badge_store` | 单事务；HTTP 只通过 writer 接口访问 |
| Wi-Fi/netif/event/HTTP | `badge_wifi_service` | 严格正序创建、逆序释放；失败可重试 |
| 设置 NVS | `settings_store` | 提交成功后更新运行值；不擦除未知 namespace |
| 电量计 I2C 访问 | battery task | 其他模块只消费结果 queue |
| 导航状态 | `navigation` | 业务注册表固定为工牌和个性化两个稳定 ID |

## 模块设计

### 导航和页面

- `mode_registry_t` 只持有 `badge`、`custom`、`settings` 和 `business[2]`。
- 工牌保持稳定 ID；个性化使用独立稳定 ID。移除消息激活、通知唤醒和音乐条件宏。
- `custom` 本期只显示顶栏与空内容容器，不分配未来图片、文本记录或上传缓冲。
- 页面对象在启动时一次创建，`enter/exit` 只切换可见性和局部状态。

### 设置

- `settings_t` 的持久化解码保留旧 schema 兼容所需字段，但展示侧 UI 只注册 Wi-Fi、自动息屏、亮度和返回。
- 消息容量和过期字段若为兼容旧记录而保留在底层类型中，只作为不展示、不修改的保留字段；保存通用设置不得把它们重置为无效值。
- Wi-Fi 页面不再展示“关闭蓝牙”“恢复蓝牙”，状态只来自简化后的展示侧 Wi-Fi 状态机。

### 工牌存储

继续使用现有记录核心：

- `badge_record`：格式、UTF-8、CRC、提交标志和序号比较。
- `badge_store`：选择最新合法槽、非活动槽写入、流式 CRC、读回验证和快照发布。
- `badge_partition`：只绑定名为 `badge_data` 且大小精确匹配的分区；所有范围运算先验证再调用 Flash API。
- 默认资源编译进应用，只作为两槽均无效时的只读回退。

### Wi-Fi 与 HTTP

简化 `badge_wifi_state_t`：

```text
OFF -> STARTING -> WAITING_CLIENT <-> CLIENT_CONNECTED
                 -> STOPPING -> OFF
任何启动/运行错误 -> ERROR -> STOPPING/OFF
```

- 删除 `PREPARING_BLE` 和 `RESTORING_BLE`，以及对应 callback、退避和 UI 文案。
- `badge_wifi_service` 负责 global/netif/event/Wi-Fi/HTTP 资源位，启动失败按已设置的资源位逆序清理。
- 上传期间的 stop request 只置位；HTTP 响应完成或事务 abort/finish 后再执行资源释放。
- 客户端计数限制为 0/1；额外连接由 SoftAP 配置拒绝，不在应用中扩展数组。

HTTP 路径白名单：

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| GET | `/`、显式静态资源路径 | 返回内嵌页面资源 |
| POST | 工牌上传 endpoint | 流式提交姓名和 RGB565 图片 |

未列出的路径或方法返回受控错误，不提供目录列表、诊断转储或持久数据读取接口。

### 显示、电量与省电

- 迁移变化驱动渲染、显示活动和电量调度纯逻辑；只有状态变化或明确请求时渲染。
- BSP 逐项迁移 ST7789/LVGL 可逆 suspend/resume 和同步首帧逻辑，保持 LVGL 锁与背光恢复顺序。
- 电量采样使用亮屏 30 秒、息屏 300 秒、失败 1 秒重试作为初始策略；唤醒请求可触发立即采样，SOC 未变化不请求重绘。
- 本期不宣称实现 PM/DFS、Tickless、Modem-sleep 或其他未迁移省电能力。

## 数据流与状态流

### 工牌配置

```text
实体设置入口
  -> Wi-Fi service 启动 SoftAP/HTTP
  -> 手机浏览器本地裁剪与 RGB565 转换
  -> HTTP parser 流式校验
  -> badge_store 写非活动槽并校验提交
  -> 应用事件队列发布 PROFILE_UPDATED
  -> 工牌快照切换 + render request
  -> LVGL 锁内更新工牌视图
```

### 按键与显示

```text
BSP 按键回调 -> 非阻塞按键队列 -> 应用循环
  ├─ Wi-Fi 专用页：任意键只请求退出
  ├─ 业务页长按上/下：双页导航
  ├─ 长按确认：进入设置
  └─ 息屏状态：先消费为唤醒手势，再决定是否导航
```

## 关键接口与类型调整

| 接口/类型 | 设计处理 |
| --- | --- |
| `mode_registry_init` | 删除通知、音乐服务和 cancel 参数，只注入工牌 store 与设置 store |
| `settings_page_t` | 保留 Wi-Fi service adapter；删除通知 store/cancel 及消息类编辑页依赖 |
| `badge_wifi_state_t` | 删除 BLE 准备/恢复状态，保留启动、等待、连接、停止、错误 |
| `badge_wifi_service_t` | 不接收 BLE 生命周期 callback，只拥有 Wi-Fi/HTTP 资源 |
| `badge_http_writer_t` | 原样保留 begin/write/finish/abort 边界，避免 HTTP 层感知分区细节 |
| `badge_profile_snapshot_t` | 原样保留只读快照；LVGL 不持有临时上传 buffer |
| `settings_t` | 保留 schema 向后读取能力，UI 仅公开通用字段 |
| `display_activity_t` / `battery_schedule_t` | 复用纯逻辑接口，硬件访问仍由 BSP/电量 task 隔离 |

## 公共能力复用评估

| 能力类型 | 检索范围 | 现有实现 | 差距 | 决策 | 影响 |
| --- | --- | --- | --- | --- | --- |
| BSP | 官方 `components/bsp`、旧开发 BSP | 官方硬件完整；旧版增加手势与显示生命周期 | 官方仍含音频/BT 依赖，旧版需与官方事实源对照 | 扩展 | 以官方硬件定义为准，移植展示所需增强并去除无关依赖 |
| 工牌存储 | 旧 `services/badge` 与测试 | 已有版本化双槽和流式写入 | 目标路径和构建注册需调整 | 复用 | 保持记录兼容和测试用例 |
| Wi-Fi 状态 | 旧 badge Wi-Fi 与 `wireless_mode` | 资源管理可用，但状态含 BLE 准备/恢复 | 展示固件不需要双无线协调 | 扩展 | 简化状态与 adapter，不迁移 `wireless_mode` |
| 导航 | 旧 `navigation`/registry | 稳定 ID 与模式回调可用 | 当前注册表含消息、音乐 | 扩展 | 缩减为两个业务模式，不新建第二套导航 |
| 设置 | 旧 config/settings | schema、NVS、亮度/息屏已存在 | UI 与类型仍含消息字段 | 扩展 | UI 去消息化，底层保留必要兼容字段 |
| UI 与文本 | 旧 `ui_common`/`text`、官方 `ui_pixel` | 两套视觉基础都存在 | 同时引入会增加资源和对象 | 复用旧展示 UI | 只迁移工牌已使用的 `ui_common`/字体；不并存官方 Demo 菜单主题 |
| 显示调度 | 旧 display 服务与 BSP 改进 | 已有变化驱动渲染、休眠恢复、电量调度 | 需移除通知唤醒分支 | 扩展 | 保留按键唤醒和通用调度，删除通知依赖 |
| 构建验证 | 官方 validate、旧 PlatformIO/tools | 各自覆盖部分场景 | 命令和路径不同，缺静态隔离/隐私扫描 | 扩展 | 合并为根 `tools/validate.sh` 单入口 |
| Git 忽略 | 官方与旧 `.gitignore` | 分别覆盖 ESP-IDF、PlatformIO 和参考目录 | 目标根目录尚无文件 | 新建 | 首个实现任务创建并用 `git check-ignore` 验证 |

## 实现约束

### 注释策略

- 只为资源所有权、Flash 原子提交、溢出边界、异步退出、显示唤醒顺序和兼容旧 schema 等非显而易见逻辑写注释。
- 不为普通赋值、简单状态分支或函数名已表达的行为添加复述性注释。
- 迁移自官方或旧开发工程的代码保留原许可证、来源说明及必要版权信息。

### 封装边界

- 不为“展示 profile”新增运行时抽象层：目标仓库只有一个生产 profile，源清单已经表达边界。
- 协议、状态机、记录格式和调度保持硬件无关，便于 native 测试；ESP-IDF adapter 保持薄层。
- `app_main.c` 只编排，不吸收 store、HTTP、导航或页面内部逻辑。
- 单次使用的简单判断保留在调用点，不新增通用 helper。

### 最小影响面

- 参考目录只读且被忽略，不修改其 Git 状态。
- 不迁移或创建手机伴侣代码空壳。
- 个性化只实现现有占位视图，不预建存储、endpoint 或大缓冲。
- 不修改 `cardid` 地址和大小，不为个性化预留新分区。
- 不增加依赖版本，优先沿用官方/旧工程已经固定的组件版本。

## 自动化验证设计

### 逻辑测试

- `test_badge`：记录边界、UTF-8、CRC、序号回绕、槽选择、写入/校验/提交失败。
- `test_badge_http`：分块边界、token、长度、Busy、断开、超时、取消、额外数据和存储错误。
- `test_badge_wifi`：简化状态迁移、部分启动失败逆序清理、客户端状态、上传中退出和重试。
- `test_settings`：旧 schema 读取、通用字段保存、无效记录默认、未知/保留字段不被破坏。
- `test_modes` / `test_integration`：双业务页循环、设置返回、空白个性化、Wi-Fi 按键消费和热更新。
- `test_display`：变化驱动渲染、息屏/唤醒、首帧顺序和电量调度。

### Web 测试

- Node 测试导入可测试的裁剪/像素转换函数，覆盖横竖图、缩放上下限、旋转、RGB565 字节序和固定长度。
- DOM/协议测试覆盖姓名边界、按钮 Busy、成功后可再次提交、服务端错误映射和无外网资源。

### 构建与静态隔离

- 目标构建输出 RAM、Flash 和镜像大小，并运行 `verify_layout.py`。
- 解析 ELF/Map/组件依赖，断言不存在 `bt`、NimBLE、ANCS、AMS、消息或音乐符号，且存在 Wi-Fi/HTTP/工牌预期符号。
- 布局单测覆盖应用越限、分区 MD5 错误、`badge_data`/`cardid` 缺失、重叠及烧录范围越界。
- 仓库检查覆盖未跟踪生成物规则、引用参考目录、外部 Web URL、隐私绝对路径和许可证文件。

### 人工验证边界

自动化不能替代以下实机项目：LCD 色彩和首帧、实体按键、真实 SoftAP、Safari/Chrome 手感、Flash 断电、跨固件资料保留、连续启停堆趋势和功耗。实现任务必须保留单独验收清单，不在构建成功后勾选这些结果。

## 主要风险

| 风险 | 影响 | 规避方式 |
| --- | --- | --- |
| 官方 BSP 与旧 BSP 漂移 | 覆盖硬件修复或带入无关音频依赖 | 以官方硬件定义为事实源，按功能逐项移植并做 BSP 差异审查 |
| 设置 schema 误删旧字段 | 切回其他固件后配置丢失 | 解码/编码测试覆盖旧记录，只更新展示固件拥有的通用字段 |
| CMake 漏源或残留 `bt` | 构建失败或拆分名义完成但链接仍大 | 源白名单、组件依赖检查和 ELF/Map 负断言三层约束 |
| Wi-Fi 启动失败泄漏 | 多轮启停后堆下降或无法重试 | 资源位记录、逆序清理、故障注入测试和真机五轮验收 |
| 上传退出竞态 | 写半条记录或停止服务时崩溃 | stop request 延迟到事务安全边界，store 单所有者和中断测试 |
| 两套 UI 资源并存 | Flash/RAM 增长、风格不一致 | 本期只使用旧展示 UI 公共层，不迁移官方 Demo 页面主题资源 |
| 隐私路径进入仓库 | 泄露本机用户名和目录结构 | 相对路径规范、静态扫描、失败不回显完整路径 |
| 合并镜像误刷已配置设备 | 覆盖持久数据或身份区 | 区分发布合并产物与已配置设备分段烧录，布局工具检查实际写入范围 |

## 需求追踪

| 需求 | 设计点 |
| --- | --- |
| R-01 | 官方根结构、白名单迁移、单一 `ai_passport_badge` 构建、根 `.gitignore` |
| R-02 | `business[2]`、稳定 ID、双页导航与设置返回 |
| R-03 | 工牌模式/视图、默认资源、只读快照和一次创建 |
| R-04 | `badge_record`、`badge_store`、`badge_partition` 与保护布局 |
| R-05 | 精简设置 UI、通用字段持久化和旧 schema 兼容 |
| R-06 | 简化 Wi-Fi 状态机、单所有者资源生命周期和延迟停止 |
| R-07 | 内嵌 Web、浏览器图片处理与 endpoint 白名单 |
| R-08 | HTTP 流式 parser、writer 接口、Busy 与事务提交 |
| R-09 | 空白 `custom` 页，不建立未来数据模型 |
| R-10 | CMake 白名单、无 `bt` 依赖、sdkconfig 禁用和 ELF/Map 检查 |
| R-11 | display activity/render policy/battery schedule 与 BSP suspend/resume |
| R-12 | 单一验证入口、布局/烧录检查、相对路径规范和正式文档 |

## 待确认问题

### 阻塞性问题

无。

### 非阻塞假设

- A-01：统一构建入口采用 PlatformIO；若以后要直接支持原生 `idf.py`，只能复用同一 CMake/配置源，不新增漂移的第二套依赖声明。
- A-02：目标 UI 沿用旧开发固件的简洁顶栏和页面壳，不同时迁移官方 Demo 的菜单/pixel 页面资源；官方 Demo 主要提供工程、BSP 和安全约束基线。
- A-03：根 `.gitignore` 忽略本地参考目录但不删除它；迁移完成后可由用户自行决定是否保留本地参考文件。
- A-04：隐私路径扫描针对受版本控制的文本文件，不扫描 `.git`、构建目录、二进制资源或本地参考目录。
