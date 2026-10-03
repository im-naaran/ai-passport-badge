# 实施任务清单

> 状态：已确认

## 执行约束

- 按任务编号串行执行；任务修改范围存在接口和测试交叉，不并行修改。
- 每次只实现当前已批准任务，完成聚焦验证后再更新任务状态与 `changelog.md`。
- 不修改按键 BSP、手势阈值、存储格式、HTTP/Web API、分区或设置内容。
- 不执行 `git add`、commit、全片擦除或设备烧录。
- 自动化结果与 LCD、实体按键、热点和息屏真机验收分开记录。

---

- [x] task-01: [Navigation] 调整短按展示循环和长按设置语义

> 状态：已完成。展示态改为短按上/下循环可用项，长按确认进入设置，其他展示手势为 no-op；新增活动项失效协调，`test_modes` 记录 11/11 succeeded。

**追踪需求：** R-01、R-02、R-03、R-04、R-06、NFR-01、NFR-02

**依赖任务：** 无

**修改范围：**

- `main/navigation/navigation.h`
- `main/navigation/navigation.c`
- `test/test_modes/test_main.c`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展。复用现有 `available()`、`adjacent_available()`、稳定 ID、`return_id` 和模式生命周期；增加活动项可用性协调，不新建第二套导航器，也不让 navigation 依赖 custom store。

**代码注释要求：** 注释展示态与设置态为何采用不同输入分流、长按上/下为何被消费为 no-op，以及稳定 ID 回退为何不能使用动态数组位置；普通条件判断不添加重复代码含义的注释。

**实现内容：**

1. 将展示态短按上/下改为在所有可用 business descriptors 间反向/正向循环。
2. 保留展示态长按确认进入设置；展示态短按确认和长按上/下返回 no-op。
3. 设置激活时仅转发短按上/下/确认给现有设置模式；按 `MODE_RETURN` 恢复稳定返回 ID。
4. 让 `navigation_key()` 返回是否需要渲染，避免唯一工牌和其他 no-op 手势触发导航刷新。
5. 新增 `navigation_reconcile_active()`：活动展示项失效时切换到首个可用项；设置激活时不抢占设置页面。
6. 保持初始化从索引 0 的首个可用项开始，明确测试工牌作为固定首项时冷启动默认显示工牌。

**完成标准：**

- 假模式组成四项序列时，短按上下按固定顺序循环并跳过不可用项。
- 只有第一项可用时，短按上/下不调用 leave/enter，返回无需渲染。
- 长按上/下和展示页短按确认不改变状态；长按确认进入设置。
- 设置短按行为、稳定 ID 返回和失效目标回退保持正确。
- reconcile 只在当前展示项失效时执行一次生命周期切换。

**自动化验证：**

- `pio test -e native -f test_modes`
- 用例直接断言默认 index、双向顺序、可用性跳过、生命周期计数、返回值和 reconcile；该测试不证明实体按键与 LCD 表现。

**人工验证关注点：** 暂不进行；状态机真机行为在 task-04 汇总验证。

**待确认问题：** 无。

---

- [x] task-02: [Modes/UI] 注册四个展示项并共享个性化视图

> 状态：已完成。注册工牌与三个固定槽位 descriptor，空槽独立判定可用性，三个个性化项共享一个 LVGL 页面；`test_modes` 记录 11/11 succeeded，目标构建返回 SUCCESS。

**追踪需求：** R-01、R-02、R-05、NFR-01、NFR-02

**依赖任务：** task-01

**修改范围：**

- `main/modes/custom/custom_mode.h`
- `main/modes/custom/custom_mode.c`
- `main/modes/custom/custom_view.h`
- `main/modes/custom/custom_view.c`
- `main/navigation/mode_registry.h`
- `main/navigation/mode_registry.c`
- `main/app_main.c`
- `test/test_modes/test_main.c`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 扩展并复用。将现有 custom descriptor、slot snapshot、`is_available`、Flash 映射渲染和 LVGL cache drop 改造成三个固定槽位实例；共享一个 custom view，不新增公共 UI 框架或完整图片 RAM 缓存。

**代码注释要求：** 注释固定注册顺序决定用户导航顺序、三个 descriptor 共享视图以限制 LVGL 对象/堆占用、以及 descriptor 的稳定 ID 与物理槽位编号为何分离。

**实现内容：**

1. 将 `custom_mode_t` 改为绑定固定 `slot`，移除运行时 `current_slot/current_slot_known` 和局部上/下处理。
2. 为三个槽位创建稳定且唯一的 descriptor ID；保留工牌 ID、原首个个性化 ID和设置 ID不变。
3. 将注册表扩展为固定四项：工牌、槽位 1、槽位 2、槽位 3；由每个槽位自己的 `is_available()` 控制空槽跳过。
4. 在 `app_main` 创建三个 custom mode 实例，并按固定顺序初始化 navigation。
5. 扩展 `custom_view_init()`，只创建一个全屏图片页面并绑定三个 descriptors；切换槽位时继续正确丢弃旧 LVGL 图片缓存。
6. 保证含任意已占用槽位的启动流程仍默认选择索引 0 的工牌，不恢复上次个性化项。

**完成标准：**

- 注册表始终包含四个顺序固定、ID 唯一的展示项和一个独立设置项。
- 每个 custom descriptor 只读取和判断自己的槽位。
- 三个 descriptor 使用同一个 custom view/root/image，未分配三个完整页面。
- 启动首项始终是工牌；三槽均有内容时短按顺序为工牌、1、2、3。
- 目标固件可通过新的初始化和 view 接口编译链接。

**自动化验证：**

- `pio test -e native -f test_modes`
- `pio run -e ai_passport_badge`
- 聚焦测试覆盖 descriptor/槽位绑定、稀疏槽、固定注册顺序和默认工牌；目标构建只证明 ESP-IDF/LVGL 接口可编译链接。

**人工验证关注点：** task-04 检查工牌与共享个性化视图切换无残影，三张图片不会错误复用同一槽内容。

**待确认问题：** 无。

---

- [x] task-03: [Controller] 协调动态槽位更新与渲染请求

> 状态：已完成。controller 仅为有效导航请求刷新，个性化更新会协调失效活动项；上传不自动跳转、覆盖保持当前项、清空当前项回退工牌，`test_integration` 记录 6/6 succeeded。

**追踪需求：** R-02、R-05、R-06、NFR-01、NFR-02

**依赖任务：** task-01、task-02

**修改范围：**

- `main/app_controller.c`
- `main/app_controller.h`（仅在接口确有需要时）
- `test/test_integration/test_main.c`
- 本规格 `tasks.md`、`changelog.md`

**公共能力处理：** 复用。沿用 controller 的唤醒消费、热点优先级和集中 render policy；调用 task-01 的 navigation 返回值与 reconcile，不在 controller 中识别具体模式 ID或槽位。

**代码注释要求：** 注释个性化更新为何必须先协调活动项再渲染，以及覆盖当前图片即使不切页也必须重绘；现有唤醒与热点优先级注释保留。

**实现内容：**

1. 仅当 `navigation_key()` 表示需要渲染时返回 `APP_ACTION_NAVIGATION | APP_ACTION_RENDER`；展示页 no-op 返回 `APP_ACTION_NONE`。
2. `app_controller_personalization_updated()` 先调用 `navigation_reconcile_active()`，再统一请求渲染。
3. 清空当前槽位时回退工牌；清空非当前槽位和上传新槽时保持当前展示项。
4. 覆盖当前槽位时保持当前稳定 ID，但仍请求渲染以显示新 sequence。
5. 保持息屏首次手势消费和热点专页任意键退出的优先级高于导航。

**完成标准：**

- 有效短按切换产生导航和渲染 action，no-op 手势不产生导航渲染。
- 清空当前个性化槽位后 active item 为工牌；其他槽变化不造成自动跳转。
- 上传图片不会从工牌自动进入个性化项。
- 覆盖当前图片会触发渲染但不改变展示项。
- 唤醒和热点现有集成行为无回归。

**自动化验证：**

- `pio test -e native -f test_integration`
- 用例覆盖 action bits、当前/非当前槽变化、上传不自动跳转、覆盖重绘、息屏唤醒和热点退出；不把这些主机用例描述为实机通过。

**人工验证关注点：** task-04 使用实体按键和网页上传/清空验证事件顺序及屏幕结果。

**待确认问题：** 无。

---

- [x] task-04: [Docs/Validation] 同步说明并完成自动化与真机验收清单

> 状态：已完成。README 已同步默认工牌和短按扁平循环；代码审查移除旧槽位导航 API 后，全量 native 记录 88/88 succeeded，Web 21/21、目标构建及 `git diff --check` 均成功完成；用户随后确认功能测试无问题。

**追踪需求：** R-01～R-06、NFR-01、NFR-02

**依赖任务：** task-01、task-02、task-03

**修改范围：**

- `README.md`
- 新增 `specs/20261003_short_press_mode_switch/acceptance.md`
- 本规格 `tasks.md`、`changelog.md`
- 仅在验证发现本功能回归时修改对应测试或实现文件，并回到相关任务记录原因

**公共能力处理：** 不涉及新增公共能力。README 复用当前简洁项目结构，只替换过时的长按/两层导航说明；验收记录复用项目既有自动化与真机证据分离格式。

**代码注释要求：** 无生产逻辑修改时不涉及；若验证发现缺陷，按对应任务的注释约束修复。

**实现内容：**

1. 更新 README：启动默认工牌，短按上/下在工牌和已上传个性化图片间循环，长按确认进入设置。
2. 创建 acceptance 文档，分别记录聚焦测试、全量 native、目标构建和静态检查结果。
3. 提供真机矩阵：默认工牌、0/1/3 张及稀疏槽顺序、上下循环、设置返回、上传不跳转、覆盖当前、清空当前、息屏唤醒、热点退出和长按 no-op。
4. 串行执行完整自动化，避免共享 `.pio/build` 的并发构建互相干扰。

**完成标准：**

- README 与最终实现一致，不再描述“长按切模式、进入个性化后短按切图”。
- 聚焦和全量自动化结果有命令、计数与边界说明。
- 真机未执行项明确标为 `NOT RUN`，不得用构建或 native 结果代替。
- 工作树无空白错误，未暂存或提交本轮修改。

**自动化验证：**

- `pio test -e native`
- `node --test test/web/badge_web.test.js`
- `pio run -e ai_passport_badge`
- `git diff --check`
- 全量 native 验证状态机与纯逻辑，Web 测试作为未改协议/页面的回归，目标构建仅证明编译链接和资源边界。

**人工验证关注点：**

- 重启后首屏必须是工牌；没有任何按键时不可自动进入个性化。
- 短按下依次工牌→有效个性化槽位升序→工牌，短按上反向；空槽必须跳过。
- 展示每个个性化槽位时图片内容对应正确且无旧图残影。
- 任一展示项长按确认进入设置，设置内部按键不变，返回原展示项；若该槽被清空则回工牌。
- 上传新槽不自动切页，覆盖当前槽刷新图片，清空当前槽立即回工牌。
- 息屏首次手势只唤醒；热点专页任意键只退出热点；长按上/下无动作且释放不补切换。

**待确认问题：** 无。
