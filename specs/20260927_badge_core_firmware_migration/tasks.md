# 任务拆解

> 状态：已确认

> 交付范围调整（2026-09-28）：普通构建是必要交付，并保留直接覆盖核心业务逻辑的 C/Unity native 测试。task-01～task-19 为历史实施记录；task-20 已按用户确认的核心实机链路关闭，未覆盖项继续在验收矩阵保持 `NOT RUN`。Node Web 测试、布局/仓库验证工具、构建 Hook、角色副本和工厂镜像不属于迁移规格的精简基线；个性化增强规格后续恢复的 Web 测试和资源依赖脚本按当前工程为准。

## 执行原则

- 每个任务只迁移或调整一个清晰能力，依赖未完成时不得提前接线。
- `_ai-passport-tools_dev/` 始终只读；不得清理、重置、覆盖或在其中生成构建文件。
- 生产源码只能通过白名单加入构建，不创建消息、音乐、BLE、ANCS、AMS 或 `wireless_mode` 空壳。
- 每个任务完成后先运行其聚焦验证，再勾选任务并向 `changelog.md` 追加记录。
- 构建与自动化结果不能代替 task-20 的真实设备验收。

## 任务总览

- [x] task-01: [Repo] 建立 Git 忽略规则和隐私路径门禁
- [x] task-02: [Skeleton] 建立官方基线根工程与来源文件
- [x] task-03: [Build] 建立单一展示固件构建与保护分区配置
- [x] task-04: [BSP] 迁移并裁剪展示固件板级基础能力
- [x] task-05: [BSP] 迁移按键手势和可逆显示生命周期
- [x] task-06: [UI] 迁移文本、字体和公共页面壳
- [x] task-07: [Config] 迁移通用设置存储与兼容读取
- [x] task-08: [Badge Data] 迁移工牌记录、双槽存储和分区适配
- [x] task-09: [Navigation] 建立工牌与空白个性化双页导航
- [x] task-10: [Badge UI] 迁移默认/用户工牌页面
- [x] task-11: [Display] 迁移显示活动、渲染策略和电量调度
- [x] task-12: [Settings] 建立展示固件设置页
- [x] task-13: [Wi-Fi] 迁移无 BLE 状态的热点生命周期
- [x] task-14: [HTTP] 迁移流式上传协议和 HTTP 服务
- [x] task-15: [Web] 迁移离线工牌配置网页
- [x] task-16: [Integration] 建立薄应用入口并完成展示固件接线
- [x] task-17: [Validation] 完善布局、产物和链接隔离检查
- [x] task-18: [Gate] 建立统一验证入口并完成全量自动化
- [x] task-19: [Docs] 完成中文使用、安全烧录和验收文档
- [x] task-20: [Device] 执行展示固件真机验收

## task-01: [Repo] 建立 Git 忽略规则和隐私路径门禁

**追踪需求：** R-01, R-12；NFR-03, NFR-04

**依赖任务：** 无

**修改范围：** `.gitignore`、`tools/check_repo.py`、必要的静态检查测试夹具

**公共能力处理：** 扩展官方 Demo 与旧开发工程已有忽略规则；检索两处 `.gitignore` 后合并为目标根目录规则，不新增第二个忽略文件。

**代码注释要求：** 仅说明容易误伤正式资源的 ignore 例外，以及隐私路径匹配为何不回显完整命中内容。

**完成标准：**

- 忽略 `/_ai-passport-tools_dev/`、`.DS_Store`、`.pio/`、`build/`、`managed_components/`、生成的 `sdkconfig`、IDE 文件、Python 缓存和明确的本地日志/临时产物。
- 保留 `sdkconfig.defaults`、`docs/`、`specs/`、Web 资源和正式测试夹具。
- 静态检查拒绝受版本控制文本中的常见个人主目录绝对路径，不打印完整疑似隐私路径。
- 不删除任何已存在的本地文件或参考目录。

**自动化验证：**

- 测试目标：验证应忽略与必须保留的代表性路径，并验证隐私路径正负样例。
- 测试用例 / 脚本：`git check-ignore -v` 检查参考目录、构建目录、生成配置和保留文件；为 `tools/check_repo.py` 增加临时夹具测试。
- 执行命令：`python3 tools/check_repo.py`，并运行任务中记录的 `git check-ignore -v` 样例。
- 兜底检查：`git status --short` 只确认工作树可见范围，不证明扫描规则完整。

**人工验证关注点：** 检查 `.gitignore` 没有宽泛忽略正式文档、规格、源码、字体许可或网页资源。

**待确认问题：** 无

## task-02: [Skeleton] 建立官方基线根工程与来源文件

**追踪需求：** R-01, R-12；NFR-04

**依赖任务：** task-01

**修改范围：** `CMakeLists.txt`、`LICENSE`、`dependencies.lock` 及最小必要的官方工程文件

**公共能力处理：** 复用官方 Demo 根工程和规范；不复制社区页面、无关 Demo、CI 或品牌二进制资源。

**代码注释要求：** 保留原许可证与来源声明；工程文件中不增加与构建无关的解释性注释。

**完成标准：**

- 根工程名称和角色调整为展示固件，仍以 ESP32-C3、ESP-IDF 5.5.3 为基础。
- 保留官方许可证和依赖锁定来源；不复制非构建必需的 Agent 协作文件。
- 所有文本使用相对路径或通用占位符，不包含本机主目录。
- 参考目录未成为 CMake include、subdirectory 或资源路径。

**自动化验证：**

- 测试目标：验证必要根文件存在、许可证未丢失、工程没有引用本地参考目录或隐私路径。
- 测试用例 / 脚本：扩展 `tools/check_repo.py` 的必需文件、引用和许可证检查。
- 执行命令：`python3 tools/check_repo.py`。
- 兜底检查：本任务尚未具备完整目标源码，静态检查不能证明固件可构建。

**人工验证关注点：** 对照官方文件确认只迁移工程与安全基线，没有无意复制无关产品素材。

**待确认问题：** 无

## task-03: [Build] 建立单一展示固件构建与保护分区配置

**追踪需求：** R-01, R-04, R-10, R-12；NFR-01, NFR-04

**依赖任务：** task-02

**修改范围：** `platformio.ini`、`partitions.csv`、`sdkconfig.defaults`、初始 `main/CMakeLists.txt`、构建期产物命名脚本

**公共能力处理：** 扩展旧开发工程 PlatformIO 固定版本与官方保护布局；只建立 `ai_passport_badge` 和 `native` 环境。

**代码注释要求：** 注释 8 MB Flash、3 MB 应用限制和保护区原因；普通版本锁定不重复注释。

**完成标准：**

- 生产环境固定 PlatformIO、ESP-IDF 5.5.3、8 MB Flash 和 `partitions.csv`。
- 分区保持 app `0x10000/0x300000`、`badge_data@0x310000/0x40000`、`cardid@0x356000/0x4000`。
- `sdkconfig.defaults` 保留 24 KiB LVGL、Wi-Fi 小缓冲等展示需求并关闭 BT/NimBLE。
- 产物角色名称明确，不存在 `ai_passport_phone`、`ams_probe` 或综合固件兼容环境。

**自动化验证：**

- 测试目标：静态解析环境、SDK 配置和分区边界。
- 测试用例 / 脚本：新增最小配置解析测试，覆盖环境唯一性、BT 禁用、分区重叠和 app 大小。
- 执行命令：`python3 tools/check_repo.py` 和布局配置测试命令。
- 兜底检查：在 `app_main` 尚未接入前不以目标构建作为完成标准。

**人工验证关注点：** 核对 `badge_data` 结束地址与 `cardid` 起点，确认没有为个性化新增分区。

**待确认问题：** 无

## task-04: [BSP] 迁移并裁剪展示固件板级基础能力

**追踪需求：** R-01, R-10, R-11；NFR-01, NFR-04

**依赖任务：** task-03

**修改范围：** `components/bsp/CMakeLists.txt`、`components/bsp/idf_component.yml`、展示所需 `include/` 与 `src/` 基础驱动、BSP 许可文件

**公共能力处理：** 扩展官方 BSP；以官方硬件定义和引脚为事实源，选择性迁移旧显示、按键、电池和背光实现，不迁移音频、Radio/BLE Demo 依赖。

**代码注释要求：** 对硬件极性、初始化顺序、锁要求和阻塞边界保留必要注释；不得凭通用 ESP32-C3 经验改写板级常量。

**完成标准：**

- BSP 只暴露 LCD/LVGL、按键、电池/I2C、背光等展示固件需要的接口。
- component manifest 不包含音频 codec 或蓝牙依赖。
- 引脚、总线、显示参数与官方事实源一致。
- 原许可证和第三方声明完整。

**自动化验证：**

- 测试目标：验证 BSP 源清单、依赖和公开头文件不引用已排除能力。
- 测试用例 / 脚本：仓库检查扫描 `bsp_audio`、BT 头文件和不允许的组件依赖，并编译硬件无关 BSP helper。
- 执行命令：`python3 tools/check_repo.py` 与相应 host helper 测试。
- 兜底检查：静态/host 检查不能证明实体硬件驱动正确。

**人工验证关注点：** 本任务不烧录；记录 LCD、按键、电池和背光均待 task-20 实机确认。

**待确认问题：** 无

## task-05: [BSP] 迁移按键手势和可逆显示生命周期

**追踪需求：** R-02, R-11；NFR-01, NFR-02, NFR-05

**依赖任务：** task-04

**修改范围：** `components/bsp/include/`、`components/bsp/src/` 中按键手势、显示 power sequence、display lifecycle 与 LVGL suspend/resume 文件，相关 host 测试

**公共能力处理：** 复用旧开发固件已实现的纯 C 手势与显示生命周期能力，并适配 task-04 的官方 BSP 接口。

**代码注释要求：** 解释同步首帧、面板 sleep、LVGL suspend、背光恢复的顺序，以及按键 callback 只投递事件的原因。

**完成标准：**

- 短按/长按识别由纯逻辑状态机完成，硬件 callback 不阻塞。
- 显示可重复 suspend/resume，唤醒先恢复面板/LVGL并刷新首帧，再恢复保存亮度。
- 失败路径保持黑屏重试或可诊断状态，不删除仍可能被 callback 访问的对象。
- 不通过扩大 LVGL 内存池解决生命周期问题。

**自动化验证：**

- 测试目标：覆盖手势边界、休眠/恢复动作顺序、失败重试和重复调用幂等性。
- 测试用例 / 脚本：迁移并精简 button gesture、display lifecycle、power sequence host 测试。
- 执行命令：`pio test -e native -f test_core`。
- 兜底检查：native 测试只验证状态与调用顺序，不证明 LCD 首帧和实体按键时序。

**人工验证关注点：** task-20 检查长按阈值、无误触、首帧无白屏/残影和背光恢复。

**待确认问题：** 无

## task-06: [UI] 迁移文本、字体和公共页面壳

**追踪需求：** R-02, R-03, R-05, R-09, R-11；NFR-01, NFR-04

**依赖任务：** task-04

**修改范围：** `components/text/`、`components/ui_common/`、字体资源、许可证与生成说明、相关 native 测试

**公共能力处理：** 复用旧开发固件 UTF-8、排版、顶栏、页面容器和中文字体；不同时迁移官方 Demo 菜单/pixel 页面资源。

**代码注释要求：** 保留 UTF-8 边界、缺字替换和字体来源说明；普通 LVGL 样式配置不加复述注释。

**完成标准：**

- 公共 UI 提供顶栏、内容容器、页面显示和仅变化更新接口。
- 中文字体、缺字回退和 UTF-8 清理能力可供工牌/设置复用。
- 字体来源、许可证和再生成说明完整。
- 公共组件不包含业务模式私有头文件。

**自动化验证：**

- 测试目标：覆盖合法/损坏 UTF-8、字节边界、缺字替代和宽度格式化。
- 测试用例 / 脚本：迁移 `text` 纯 C 测试并编译公共 UI 的硬件无关 helper。
- 执行命令：`pio test -e native -f test_core`。
- 兜底检查：host 测试不证明 LCD 字体像素、对齐或对象内存峰值。

**人工验证关注点：** task-20 检查中文、英文、缺字、电量位置和页面视觉一致性。

**待确认问题：** 无

## task-07: [Config] 迁移通用设置存储与兼容读取

**追踪需求：** R-05, R-11；NFR-02, NFR-04, NFR-05

**依赖任务：** task-03

**修改范围：** `main/services/config/settings_store.*`、`settings_nvs.*`、`test/test_settings/`

**公共能力处理：** 扩展旧版本化设置存储；保留旧 schema 的合法通用字段及其他 profile 兼容值，不新建第二套 NVS wrapper。

**代码注释要求：** 解释 schema 迁移、提交后更新和保留未知/profile 字段的兼容意图。

**完成标准：**

- 合法旧记录迁移亮度、息屏等通用字段；损坏或未知记录安全回退默认。
- 保存只在 NVS 完整提交后更新运行值。
- 初始化失败不自动擦除 NVS，不触碰其他 namespace 或 BLE bond。
- 亮度档位为 20/40/60/80/100，默认 60；息屏合法值沿用已确认范围。

**自动化验证：**

- 测试目标：覆盖 schema 1/2/3、损坏长度/值、读写失败、提交语义和保留字段。
- 测试用例 / 脚本：迁移并调整 `test_settings`，加入展示固件只修改通用字段的回归用例。
- 执行命令：`pio test -e native -f test_settings`。
- 兜底检查：native fake backend 不证明真实 NVS 掉电持久化。

**人工验证关注点：** task-20 验证保存、重启、断电以及切换其他固件后的设置恢复。

**待确认问题：** 无

## task-08: [Badge Data] 迁移工牌记录、双槽存储和分区适配

**追踪需求：** R-03, R-04, R-08, R-12；NFR-02, NFR-03, NFR-05

**依赖任务：** task-03

**修改范围：** `main/services/badge/badge_record.*`、`badge_store.*`、`badge_partition.*`、`test/test_badge/`

**公共能力处理：** 复用旧工牌记录和 writer 接口；不把图片并入 settings/NVS，也不新增文件系统。

**代码注释要求：** 保留槽布局、CRC、序号回绕、提交标志、溢出检查和非活动槽事务原因。

**完成标准：**

- 严格支持 200×200 RGB565 LE、80,000 字节图片和 1～48 字节合法 UTF-8 姓名。
- 初始化选择最新合法已提交槽，两槽无效时回退默认快照。
- begin/write/finish/abort 保持单事务、流式 CRC、读回校验和提交后发布。
- 分区 adapter 只接受精确 `badge_data` 分区，所有范围运算先检查。

**自动化验证：**

- 测试目标：覆盖格式/CRC/UTF-8、序号回绕、双槽选择、分块写、Busy、I/O 失败和各掉电阶段。
- 测试用例 / 脚本：迁移 `test_badge` 并增加分区尺寸/边界 fake 测试。
- 执行命令：`pio test -e native -f test_badge`。
- 兜底检查：内存 backend 不能证明真实 Flash 擦写与断电行为。

**人工验证关注点：** task-20 使用真实 Flash 验证中断写入、旧记录回退和跨固件保留。

**待确认问题：** 无

## task-09: [Navigation] 建立工牌与空白个性化双页导航

**追踪需求：** R-02, R-09；NFR-04, NFR-05

**依赖任务：** task-06

**修改范围：** `main/navigation/`、`main/modes/custom/`、`test/test_modes/` 中导航与占位页逻辑

**公共能力处理：** 扩展旧稳定 ID 导航；注册表缩减为两个业务模式和一个系统设置模式，不新建 profile 抽象。

**代码注释要求：** 只注释稳定 ID 与数组位置解耦、设置返回原业务页等关键状态约束。

**完成标准：**

- 长按上/下只在工牌和个性化之间双向循环。
- 长按确认进入设置，退出回到进入前业务页。
- 个性化是独立空白页面，不创建存储、endpoint 或大缓冲。
- 不存在消息激活、通知唤醒、音乐条件宏或四页数量假设。

**自动化验证：**

- 测试目标：覆盖双向循环、稳定 ID 激活、设置进入/返回、短按保持和空注册保护。
- 测试用例 / 脚本：精简 `test_modes` 与导航测试，加入业务数量固定为 2 的断言。
- 执行命令：`pio test -e native -f test_modes`。
- 兜底检查：状态测试不证明实体长按体验和页面像素。

**人工验证关注点：** task-20 核对按键方向、循环顺序、设置返回及空白页无白屏。

**待确认问题：** 无

## task-10: [Badge UI] 迁移默认/用户工牌页面

**追踪需求：** R-03；NFR-01, NFR-04

**依赖任务：** task-06, task-08, task-09

**修改范围：** `main/modes/badge/`、默认图片资源、相关模式测试

**公共能力处理：** 复用旧工牌 mode/view 与公共页面壳；默认资源使用非个人化占位素材，不新增通用图片 decoder。

**代码注释要求：** 解释 LVGL image descriptor 对映射 Flash 快照的生命周期要求，以及为何对象只创建一次。

**完成标准：**

- 默认记录显示默认图片、`AI Passport` 名称和默认文案。
- 用户记录显示图片和姓名，不显示默认文案。
- 图片区域固定、姓名居中；快照变更时更新描述符，不反复创建对象。
- 所有 LVGL 更新在锁内执行，页面 render 不读写 Flash。

**自动化验证：**

- 测试目标：验证默认/用户快照选择、序号变化触发更新和相同快照不重复更新。
- 测试用例 / 脚本：扩展 `test_modes` 的 badge mode 纯逻辑用例；目标编译在 task-16 完成。
- 执行命令：`pio test -e native -f test_modes`。
- 兜底检查：host 测试不能证明颜色、RGB565 字节序、对齐和 LVGL 对象峰值。

**人工验证关注点：** task-20 检查默认/用户工牌、色彩、比例、中文姓名、切页和热更新。

**待确认问题：** 无

## task-11: [Display] 迁移显示活动、渲染策略和电量调度

**追踪需求：** R-11；NFR-01, NFR-02, NFR-05

**依赖任务：** task-05, task-06

**修改范围：** `main/services/display/`、相关 `test/test_core/` 与 `test/test_time/`

**公共能力处理：** 扩展旧变化驱动渲染、display activity 和 battery schedule；删除通知唤醒分支，只保留按键/显式事件唤醒。

**代码注释要求：** 解释 gesture consumption、首帧、采样唯一所有者和失败重试边界。

**完成标准：**

- 无状态变化时不持续请求全屏渲染。
- 息屏、按键唤醒、首帧完成和亮度恢复顺序明确。
- 电量调度初始采用亮屏 30 秒、息屏 300 秒、失败 1 秒重试；SOC 不变不触发重绘。
- 不包含 ANCS/AMS deadline 或通知目标状态。

**自动化验证：**

- 测试目标：覆盖 render request 合并、休眠/唤醒、gesture consume、采样 deadline、失败重试和 SOC 去重。
- 测试用例 / 脚本：迁移并精简 display/time 测试。
- 执行命令：`pio test -e native -f test_core -f test_time`。
- 兜底检查：调度测试不证明真实 CW2017、LCD 功耗或唤醒视觉。

**人工验证关注点：** task-20 检查电量变化、息屏时长、唤醒帧、残影和电流。

**待确认问题：** 无

## task-12: [Settings] 建立展示固件设置页

**追踪需求：** R-05, R-06, R-11；NFR-04, NFR-05

**依赖任务：** task-07, task-09, task-11

**修改范围：** `main/settings/`、设置模式/视图测试

**公共能力处理：** 扩展旧设置页面与公共 UI；移除通知 store、cancel、消息容量和消息过期 UI，不新增第二套配置状态。

**代码注释要求：** 注释草稿提交、Wi-Fi 专用页按键消费和返回原业务页的边界。

**完成标准：**

- 列表只包含 Wi-Fi 热点、自动息屏、屏幕亮度和返回。
- 编辑使用白名单档位，确认后持久化成功才更新，失败给出有限时反馈。
- Wi-Fi 页面状态不出现关闭/恢复蓝牙文案。
- 专用 Wi-Fi 页任意键只请求退出并消费本次完整手势。

**自动化验证：**

- 测试目标：覆盖列表游标、编辑草稿、保存失败、返回、Wi-Fi 启动失败和按键消费。
- 测试用例 / 脚本：调整 `test_settings` 与 `test_modes`。
- 执行命令：`pio test -e native -f test_settings -f test_modes`。
- 兜底检查：native 测试不证明设置布局、中文像素或实体按键节奏。

**人工验证关注点：** task-20 检查四行列表、编辑反馈、Wi-Fi 状态、返回和重启持久化。

**待确认问题：** 无

## task-13: [Wi-Fi] 迁移无 BLE 状态的热点生命周期

**追踪需求：** R-06, R-10；NFR-01, NFR-02, NFR-03, NFR-05

**依赖任务：** task-03, task-08

**修改范围：** `main/services/badge/badge_wifi_state.*`、`badge_wifi_service.*`、`badge_wifi_esp.*`、`test/test_badge_wifi/`

**公共能力处理：** 扩展旧 Wi-Fi 资源管理；删除 `wireless_mode` 及 BLE prepare/restore adapter，保留单所有者和逆序释放。

**代码注释要求：** 解释部分启动失败清理、上传中延迟停止、事件 callback 边界和最多一个客户端的产品约束。

**完成标准：**

- 状态仅包含 OFF、STARTING、WAITING_CLIENT、CLIENT_CONNECTED、STOPPING、ERROR。
- SoftAP 为开放网络，SSID 为 `AI-Passport-XXXX`，最多一个客户端，网关 `192.168.4.1`。
- global/netif/event/Wi-Fi/HTTP 资源按位记录，任何失败逆序释放并可重试。
- 上传期间退出只记录 stop request，事务安全结束后再停服务。
- 源码和接口不包含 BLE stop/start/retry。

**自动化验证：**

- 测试目标：覆盖状态迁移、每个启动步骤故障、逆序清理、连接计数、上传中退出、反馈到期和重复启停。
- 测试用例 / 脚本：重写 `test_badge_wifi` 以删除 BLE 互斥场景并增加无泄漏调用计数断言。
- 执行命令：`pio test -e native -f test_badge_wifi`。
- 兜底检查：fake adapter 不证明真实 SoftAP、socket、netif 或堆释放。

**人工验证关注点：** task-20 检查 SSID、单客户端、连接状态、五轮启停和真实堆趋势。

**待确认问题：** 无

## task-14: [HTTP] 迁移流式上传协议和 HTTP 服务

**追踪需求：** R-07, R-08；NFR-01, NFR-02, NFR-03, NFR-05

**依赖任务：** task-08, task-13

**修改范围：** `main/services/badge/badge_http_protocol.*`、`badge_http_server.*`、`badge_assets.*`、`test/test_badge_http/`

**公共能力处理：** 复用旧 parser/writer/server 边界；HTTP 层不直接操作分区或 LVGL，不新建通用 Web 框架。

**代码注释要求：** 解释 envelope、token、长度上限、分块消费、extra-data 拒绝和 response 完成后的停止安全点。

**完成标准：**

- 只开放明确的 GET 静态资源和 POST 工牌上传路径。
- parser 校验协议版本、token、姓名、固定图片长度和额外数据。
- 请求体流式传给 writer；断开、超时、取消和存储错误执行 abort。
- 并发事务返回 Busy；日志不含姓名、图片、body 或 token。

**自动化验证：**

- 测试目标：覆盖任意分块、零/超长/额外数据、token、Busy、read 错误、断开、超时、取消和响应映射。
- 测试用例 / 脚本：迁移并精简 `test_badge_http`，加入日志/路径白名单静态检查。
- 执行命令：`pio test -e native -f test_badge_http`。
- 兜底检查：host server adapter 不证明真实浏览器、socket 断开时序或任务栈。

**人工验证关注点：** task-20 检查 Safari/Chrome 的成功、错误、断网和重复保存行为。

**待确认问题：** 无

## task-15: [Web] 迁移离线工牌配置网页

**追踪需求：** R-07, R-08；NFR-01, NFR-03, NFR-05

**依赖任务：** task-14

**修改范围：** `main/web/badge/`、`tools/test_badge_web.mjs`、构建期 Web asset 依赖

**公共能力处理：** 复用旧原生 HTML/CSS/JavaScript 和 Canvas 图片处理；不引入 npm 包、CDN 或运行时外网资源。

**代码注释要求：** 注释裁剪坐标、方向/旋转、RGB565 字节序和浏览器内存边界；普通 DOM 更新不写空泛注释。

**完成标准：**

- 支持选择、拖动、缩放、90° 旋转、正方形预览、姓名输入、保存进度和错误反馈。
- 输出严格为 200×200 RGB565 LE，并生成匹配 HTTP envelope。
- Busy/失败后按钮和状态可恢复；成功后热点不关闭并允许再次保存。
- 页面无外部 URL、遥测、远程字体或个性化入口。

**自动化验证：**

- 测试目标：覆盖横竖图、裁剪夹紧、缩放上下限、四次旋转、RGB565 字节序、固定长度、姓名边界和状态恢复。
- 测试用例 / 脚本：迁移并扩展 `tools/test_badge_web.mjs`，扫描外部资源引用。
- 执行命令：`node tools/test_badge_web.mjs`。
- 兜底检查：Node 数学/DOM 测试不证明真实相册 EXIF、Canvas 内存或移动端触控体验。

**人工验证关注点：** task-20 使用 iPhone Safari 与 Android Chrome 分别测试横图、竖图、旋转、缩放和连续保存。

**待确认问题：** 无

## task-16: [Integration] 建立薄应用入口并完成展示固件接线

**追踪需求：** R-01～R-11；NFR-01, NFR-02, NFR-04

**依赖任务：** task-05, task-07, task-10, task-11, task-12, task-13, task-14, task-15

**修改范围：** `main/app_main.c`、`main/CMakeLists.txt`、必要的窄应用事件类型与集成测试

**公共能力处理：** 复用已有单应用上下文和事件队列原则；不迁移综合固件 app_main，不新增运行时 profile 层。

**代码注释要求：** 注释 NVS 不擦除策略、初始化/释放所有权、LVGL 锁、上传事件跨线程边界和唤醒首帧顺序。

**完成标准：**

- 启动顺序为 NVS、BSP、设置/工牌存储、UI/导航、显示调度、Wi-Fi 服务对象。
- 热点默认关闭；应用循环串行消费按键、Wi-Fi、资料更新、电量和渲染事件。
- `app_main.c` 不包含记录解析、HTTP 协议、页面布局或硬件寄存器逻辑。
- CMake 只列展示侧源码与依赖，启动日志明确 `badge/display` 角色。
- 首次完整 `ai_passport_badge` 构建成功。

**自动化验证：**

- 测试目标：验证初始化失败降级、按键优先级、Wi-Fi 手势消费、工牌热更新和显示事件顺序。
- 测试用例 / 脚本：精简旧 integration 测试，使用 fake adapter/queue 验证编排；执行目标构建。
- 执行命令：`pio test -e native -f test_integration`、`pio run -e ai_passport_badge`。
- 兜底检查：构建成功只证明编译/链接，不证明设备启动、LCD、Wi-Fi 或 Flash 正常。

**人工验证关注点：** 本任务不自动烧录；所有设备行为留待 task-20。

**待确认问题：** 无

## task-17: [Validation] 完善布局、产物和链接隔离检查

**追踪需求：** R-04, R-10, R-12；NFR-03, NFR-05

**依赖任务：** task-03, task-08, task-16

**修改范围：** `tools/check_build.py`、`tools/verify_layout.py`、`tools/test_verify_layout.py`、静态隔离脚本及测试

**公共能力处理：** 扩展官方保护布局检查和旧 PlatformIO 构建检查；使用实际 ELF/Map/分区二进制，不仅搜索源码。

**代码注释要求：** 注释镜像偏移、扇区擦除范围和符号拒绝列表为何属于安全边界。

**完成标准：**

- 验证应用头、3 MB 限额、分区 MD5、`badge_data`、`cardid` 和普通分段写入范围。
- 产物命名包含展示角色，不引用其他环境构建目录。
- ELF/Map/依赖检查拒绝 `bt`、NimBLE、ANCS、AMS、消息和音乐符号，并确认工牌/Wi-Fi/HTTP 预期符号存在。
- 失败输出不泄露个人绝对路径。

**自动化验证：**

- 测试目标：覆盖正确布局及 app 越限、分区缺失/重叠、MD5 错误、写入保护区、禁用符号存在等负例。
- 测试用例 / 脚本：扩展 `tools/test_verify_layout.py` 并新增静态隔离脚本测试夹具。
- 执行命令：`python3 tools/test_verify_layout.py`，目标构建后运行 `python3 tools/verify_layout.py .pio/build/ai_passport_badge` 和静态隔离检查。
- 兜底检查：产物检查不证明实际烧录工具未被传入额外擦除参数。

**人工验证关注点：** 在 task-20 烧录前复核实际命令和 resolved offsets，不使用全片擦除。

**待确认问题：** 无

## task-18: [Gate] 建立统一验证入口并完成全量自动化

**追踪需求：** R-01, R-10, R-12；NFR-05

**依赖任务：** task-01～task-17

**修改范围：** `tools/validate.sh`、native source 白名单、必要测试注册与验证文档片段

**公共能力处理：** 扩展官方单入口验证思想和旧工程现有测试；删除 BLE/ANCS/AMS harness 注册，不重复维护第二套命令清单。

**代码注释要求：** 只注释门禁顺序、环境前提和为何某检查必须在目标构建后运行。

**完成标准：**

- `tools/validate.sh` 串行执行仓库/隐私检查、全量 native、布局单测、Web 测试、目标构建、产物布局和链接隔离。
- native source 白名单只含展示侧纯逻辑和必要 host stub。
- 任何一步失败立即返回非零，产物不会被误标为已验证。
- 不并行运行共享 `.pio/build` 的 test 与 build。

**自动化验证：**

- 测试目标：证明完整门禁能从干净生成目录执行，并准确传播失败。
- 测试用例 / 脚本：先逐项运行定位问题，再运行聚合入口；必要时用无害负例确认门禁会失败。
- 执行命令：`./tools/validate.sh`、`git diff --check`。
- 兜底检查：完整门禁仍只是 host/构建证据，不是设备验收。

**人工验证关注点：** 汇报 Build、Host tests、Device tests、Unverified 四个字段，禁止将前三者混写。

**待确认问题：** 无

## task-19: [Docs] 完成中文使用、安全烧录和验收文档

**追踪需求：** R-01, R-05, R-06, R-07, R-09, R-10, R-12；NFR-03, NFR-04

**依赖任务：** task-16, task-17, task-18

**修改范围：** `README.md`、必要的 `docs/*.md`、`specs/20260927_badge_core_firmware_migration/acceptance.md`

**公共能力处理：** 复用官方文档结构；按用户要求只保留中文根 README，只说明展示固件，不复制旧综合固件说明。

**代码注释要求：** 不涉及生产代码；文档命令使用相对路径和占位符。

**完成标准：**

- 中文 README 说明固件角色、双页导航、设置、热点、网页配置、开放网络风险和空白个性化范围，不维护重复英文版本。
- 构建/验证、空白设备发布镜像和已配置设备安全分段烧录分开说明，明确禁止全片擦除。
- 明确消息/音乐/BLE 不在本镜像、资料保留边界和自动化/真机证据区别。
- 文档不含个人绝对路径、过期四页流程或 BLE/Wi-Fi 互斥说明。
- `acceptance.md` 建立可填写的设备、浏览器、断电、跨固件、资源和功耗矩阵，初始结果均保持 NOT RUN。

**自动化验证：**

- 测试目标：验证中文根文档、链接、命令、禁止词/旧流程和隐私路径。
- 测试用例 / 脚本：扩展 `tools/check_repo.py` 的根文档、链接及内容检查。
- 执行命令：`python3 tools/check_repo.py`、`./tools/validate.sh`、`git diff --check`。
- 兜底检查：文档检查不证明用户按说明操作时的真实设备结果。

**人工验证关注点：** 按 README 从新环境走一遍命令，确认没有依赖本地参考目录或未声明工具。

**待确认问题：** 无

## task-20: [Device] 执行展示固件真机验收

> 状态：已完成。用户已完成并确认本轮展示固件核心实机链路，包含启动设置热点、iPhone DHCP、离线网页访问、资料保存和设备图片显示，并明确要求关闭交付任务；未覆盖的 Android、断电、跨固件、完整稳定性和功耗项目仍保持 `NOT RUN`，不作为 PASS，也不再阻塞本次交付关闭。

**追踪需求：** R-02～R-08, R-11, R-12；NFR-01, NFR-02, NFR-03, NFR-05

**依赖任务：** task-18, task-19

**修改范围：** `specs/20260927_badge_core_firmware_migration/acceptance.md`、`changelog.md`；发现缺陷时回到对应任务修复，不在验收记录中掩盖

**公共能力处理：** 不涉及新增公共能力；使用统一验收矩阵记录设备、固件哈希、步骤、实际结果和证据。

**代码注释要求：** 无；若验收暴露代码缺陷，按对应模块约束补充必要注释。

**完成标准：**

- LCD/按键：冷启动工牌、默认/用户资料、双页导航、设置、息屏/唤醒、亮度、电量和中文显示完成记录。
- Wi-Fi/Web：iPhone Safari 与 Android Chrome 完成连接、横竖图、旋转/缩放、连续保存、错误输入、断网和按键退出。
- 存储：保存各阶段断电只恢复完整新/旧记录；普通安全刷写保留 NVS、`badge_data` 和 `cardid`。
- 稳定性：至少五轮热点开启/连接/保存/退出，无崩溃、watchdog 或持续堆下降；记录普通堆、最大连续块、DMA 堆和任务栈。
- 隔离：运行期间不创建 BLE/ANCS/AMS 任务或状态，启动日志明确展示固件角色。
- 功耗：分别记录亮屏、息屏、热点等待和热点传输状态；没有仪器证据时保持未验证。

**自动化验证：**

- 测试目标：烧录前重新确认测试对象与已验证产物一致。
- 测试用例 / 脚本：重新运行完整门禁，记录产物哈希和布局检查结果。
- 执行命令：`./tools/validate.sh`，随后使用 README 中经过复核的安全分段烧录命令。
- 兜底检查：自动化只锁定产物身份，真机结果必须由观察和测量填写。

**人工验证关注点：** 逐项记录 PASS/FAIL/NOT RUN 和证据；任何未执行项目保持未验证，不以“基本正常”代替。

**待确认问题：** 无

## 全局待确认问题

无。
