# 项目上下文

> 状态：已确认

## 项目定位

- 当前工作区根目录是目标“个性展示固件”仓库，目前 Git 尚无提交，只有未跟踪的 `.DS_Store` 和参考目录 `_ai-passport-tools_dev/`。
- `_ai-passport-tools_dev/firmware` 是最后开发的综合固件，已包含工牌、消息、音乐、空白个性化页、设置、Wi-Fi/HTTP、BLE/ANCS/AMS 和无线互斥切换；该目录存在大量已暂存及未跟踪改动，只作为迁移来源，不在本规格 Phase 0～3 中修改。
- `_ai-passport-tools_dev/ai-passport-main` 是官方 Demo 快照，可作为新仓库的工程骨架、BSP、验证脚本、文档规范和受保护 Flash 布局参考。
- `_ai-passport-tools_dev/specs/20260925_badge_wifi_configuration/dual-firmware-plan.md` 是拆分方向的最终参考：展示固件仅承载工牌、个性化、Wi-Fi/HTTP/Web；手机伴侣固件承载消息、音乐、BLE/ANCS/AMS。本仓库只实现前者，不再实现单固件运行时无线切换。

## 技术栈

- 芯片与硬件：ESP32-C3、8 MB Flash、无 PSRAM、ST7789P3 240×320 LCD、ADC 实体按键、I2C 电量计和 LEDC 背光。
- SDK：ESP-IDF 5.5.3。旧开发固件由 PlatformIO `platformio/espressif32@6.13.0` 驱动 ESP-IDF；官方 Demo 使用原生 ESP-IDF/CMake 和统一验证脚本。
- UI：LVGL 9、`espressif/esp_lvgl_port`；旧开发固件另有 `ui_common`、16 px 中文字体、文本模块和变化驱动渲染。
- 网络与存储：ESP-IDF SoftAP、`esp_netif`、`esp_event`、`esp_http_server`、NVS、原始 Flash 分区、内嵌 HTML/CSS/JavaScript。
- 测试：旧开发固件通过 PlatformIO native + Unity、Python/C harness、Node Web 测试和布局脚本覆盖纯逻辑；官方 Demo 提供 host C 测试、静态检查、固件构建和合并镜像验证入口。

## 架构与目录

### 目标仓库

- 当前尚无生产代码。Phase 2 需要确定从官方 Demo 建立骨架后选择性迁移模块的落盘结构。
- 目标生产代码不能依赖 `_ai-passport-tools_dev/` 的相对路径；参考目录不是构建输入，也不能作为发布包的一部分。

### 官方 Demo 参考

- `ai-passport-main/main/`：单一 Demo 应用、页面和 `ui_pixel` 主题。
- `ai-passport-main/components/bsp/`：显示、按键、电池、无线等硬件能力；官方约定可复用硬件能力放 BSP，页面、状态机、资源和应用任务放 `main`。
- `ai-passport-main/tools/validate.sh` 与 `tools/verify_firmware.py`：静态、host、固件及保护布局验证。
- 官方工程要求非 LVGL 线程访问对象时持有 `bsp_lvgl_lock()`，按键回调不得执行网络、存储等阻塞操作。

### 旧开发固件参考

- `firmware/main/app_main.c`：综合固件启动、服务接线、按键/邮箱/网络事件循环、显示活动和电量调度；当前职责过多，不应整文件搬入。
- `firmware/main/modes/badge/`：工牌模式、默认资料与 LVGL 视图。
- `firmware/main/modes/custom/`：个性化空白占位模式。
- `firmware/main/services/badge/`：工牌记录、双槽存储、分区适配、Wi-Fi 状态/服务、HTTP 协议/服务、Web 资源和应用输入边界。
- `firmware/main/services/config/`：版本化设置记录及 NVS 适配。
- `firmware/main/services/display/`：显示活动、变化驱动渲染和电量采样调度。
- `firmware/main/navigation/`、`firmware/main/settings/`：稳定页面 ID、业务页循环和设置页状态。
- `firmware/components/text/`、`firmware/components/ui_common/`：UTF-8/排版、公共页面壳、主题和字体。
- `firmware/main/services/ble/`、`services/notifications/`、`services/music/`、`modes/message/`、`modes/music/` 及 `services/wireless/` 不属于本固件迁移范围。

## 当前可复用能力

| 能力 | 现有实现 | 迁移判断 |
| --- | --- | --- |
| 板级显示、按键、电池和背光 | 官方 BSP 与旧开发 BSP 改进 | 以官方 BSP 为基线，逐项对照迁移旧固件已验证的显示休眠/恢复、背光和电量能力，不整目录盲目覆盖 |
| 工牌记录与双槽事务存储 | `services/badge/badge_record.*`、`badge_store.*`、`badge_partition.*` | 优先迁移；纯 C 核心已有固定边界并适合 host 测试 |
| 工牌页面 | `modes/badge/*` | 迁移行为与数据接口；视觉层需在 Phase 2 决定适配官方主题还是迁移旧 `ui_common` 页面壳 |
| Wi-Fi/HTTP 生命周期 | `badge_wifi_state.*`、`badge_wifi_service.*`、`badge_wifi_esp.*`、`badge_http_*` | 删除 BLE 准备/恢复语义后复用，不迁移 `wireless_mode` |
| 离线网页 | `main/web/badge/` | 迁移并保留浏览器端裁剪、缩放、旋转和 RGB565 转换；目标工程不得从参考目录运行时加载资源 |
| 设置持久化 | `services/config/settings_store.*`、`settings_nvs.*` | 复用 schema 兼容能力；展示固件 UI 只保留热点、息屏、亮度和返回等相关项 |
| 导航 | `navigation/*`、`modes/custom/*` | 复用稳定 ID 和状态机思想，目标注册表只包含工牌与空白个性化页 |
| 省电与渲染 | `services/display/*`、旧 BSP 显示改进 | 按依赖逐项迁移；不得通过扩大 LVGL 24 KiB 内存池掩盖对象树或生命周期问题 |
| 自动化 | native、Web、布局和目标构建脚本 | 迁移与展示固件有关的测试并接入目标仓库统一入口；BLE/ANCS/AMS 测试不迁入 |

## 数据与 Flash 约束

- 主应用位于 `0x10000`，最大 `0x300000` 字节；受保护 `cardid` 固定为 `0x356000/0x4000`。
- 旧开发固件已将 `badge_data` 放在 `0x310000/0x40000`，保存两个 `0x20000` 工牌槽；该分区结束于 `0x350000`，不得与 `cardid` 重叠。
- 工牌记录当前固定为 200×200、RGB565 LE、80,000 字节图片，姓名最多 48 个 UTF-8 字节，包含 schema、序号、长度、CRC 和提交标志。
- 普通开发烧录不得执行全片擦除，不得覆盖 NVS、`badge_data` 或 `cardid`。是否沿用官方合并镜像发布方式与旧工程 PlatformIO 分段烧录方式，需要在设计阶段统一，且必须区分空白设备与已配置设备。
- 展示固件不得清除旧综合固件留下的 BLE bond/NVS 未识别字段，以便以后切换到手机伴侣固件时恢复。

## 运行时与交互约束

- 默认进入工牌页；业务页只包含“工牌”和“个性化”，长按上/下循环切换，长按确认进入设置。
- 个性化本期保持独立空白占位，不定义记录、上传接口或动态内容模型。
- 设置只展示本 profile 有意义的项目；消息过期和消息容量不应出现在展示固件设置中。
- 热点必须由设置页物理触发，默认不开机启动；开放、无密码、最多一个客户端，地址为 `192.168.4.1`，按设备任意实体键退出专用 Wi-Fi 页。
- 展示固件没有 BLE 生命周期，Wi-Fi 启停路径不得保留“停止 BLE、恢复 BLE”阶段或错误文案。
- HTTP/Wi-Fi 回调不得直接操作 LVGL；页面 render 只读取稳定快照，不执行网络、Flash 写入或图片转换。
- 配置上传流式写入非活动槽，完整验证和提交后才发布；失败、断连或断电继续使用上一份有效工牌。
- 日志不得输出姓名、照片、HTTP body、token、密钥或其他个人数据。
- 仓库文档、源码、配置、脚本和日志样例不得写入本机用户主目录等隐私绝对路径；命令使用仓库相对路径、占位符或运行时解析的项目根目录。
- 实现阶段需增加根 `.gitignore`，至少排除参考目录、macOS 元数据、PlatformIO/ESP-IDF 构建产物、生成的 `sdkconfig`、Managed Components、IDE 配置和 Python 缓存。

## 关键风险

- 根仓库为空，官方 Demo 与旧开发固件的构建体系、目录结构、UI 风格和验证入口不同；若直接复制综合固件再删功能，容易残留 BLE 依赖和过时互斥状态。
- 旧开发目录有用户现存改动，迁移必须复制经过明确审查的文件或逻辑，不能通过清理、重置或覆盖该目录取得“干净基线”。
- ESP32-C3 无 PSRAM；字体、LVGL、Wi-Fi、HTTP、内嵌网页和 80 KB 图片同时存在时仍需限制内部 RAM、连续块和任务栈峰值。
- 只隐藏消息/音乐页面不足以完成拆分；最终 ELF/Map 和组件依赖中必须不存在 `bt`、NimBLE、ANCS、AMS、消息和音乐业务符号。
- 官方完整合并镜像与已配置设备的安全分段烧录适用场景不同；发布物验证通过不能替代对已配置设备保护区的烧录范围检查。
- host 测试、目标构建和布局脚本不能证明 LCD 色彩、实体键、手机热点连接、Safari/Chrome 操作、断电恢复、内存趋势或功耗。
