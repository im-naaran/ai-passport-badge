# 变更记录

- 2026-09-30：启动 `badge_profile_shape_persistence` 规格；完成 Phase 0 项目上下文分析，确认当前黑色外围来自 RGB565 无 Alpha 与形状未持久化，而非反显解码新增黑框。
- 2026-09-30：完成 Phase 1 需求草案；范围包括形状事务持久化、profile 查询恢复、新旧 schema/envelope 兼容、旧图片手动确认和不可逆裁剪提示，等待用户确认。
- 2026-09-30：Phase 1 需求经用户确认；进入 Phase 2，完成 schema 3 payload、envelope version 3、profile API、Web 反显状态、旧版本兼容与验证策略设计，等待用户确认。
- 2026-09-30：用户明确项目仍处于开发阶段，不要求兼容旧 Flash schema 1/2 或旧上传 envelope version 1/2；需求与设计收敛为单一 schema 3 / envelope v3，旧记录安全回退默认工牌且不自动擦除，旧请求在 Flash 事务前拒绝。用户要求继续，修订后的 Phase 2 设计视为确认并进入 Phase 3。
- 2026-09-30：完成 Phase 3 任务草案，共 6 个任务，依次覆盖 schema 3 record、事务 store、envelope v3/API、Web 形状恢复、集成回归和独立真机验收，等待用户确认。
- 2026-09-30：按“不兼容旧格式后应进一步精简代码”的复核，优化设计：schema 3 直接使用 44 字节 header 保存 shape/reserved，payload 保持 `name + bio + image`，避免 store 插入特殊 shape 字节；Web 用单一 `sourceShape` 表达来源，并在设备反显图扩大形状时禁止保存、要求重新选择原图，避免 metadata 与像素不一致。
- 2026-09-30：用户确认任务拆解并授权执行 task-01～03；进入 Phase 4，范围限定为 record、store 和 HTTP，不进入 Web、集成或真机验收。
- 2026-09-30：完成 task-01：工牌记录收敛为 44 字节 schema 3 header，增加稳定 shape 枚举与校验，移除 schema 1/2 成功读取分支；旧记录测试确认回退默认资料且不擦除。
- 2026-09-30：完成 task-02：shape 接入默认资料、上传 meta、双槽事务和快照恢复；三种形状、槽位轮换、重启恢复及各失败回退由 `test_badge` 8 个用例覆盖。
- 2026-09-30：完成 task-03：上传协议收敛为固定 16 字节 envelope v3，严格校验 shape/reserved，v1/v2 在 writer 前拒绝；`GET /api/profile` 返回稳定 shape 字符串，非法形状映射为 `invalid_shape`。`test_badge_http` 15 个用例和 `test_personalization_http` 6 个用例通过。
- 2026-09-30：完成 task-04：Web 改用 16 字节 envelope v3，严格恢复 profile shape 和对应 radio；设备反显图重新应用已保存遮罩，使用单一 `sourceShape` 阻止不可逆扩大保存，本地重新选图后解除限制。Web 自动化增加到 21 个用例。
- 2026-09-30：完成 task-05：复用既有 `.incbin` 依赖并把静态资源版本更新为 `2026093001`；Native 11 组 85/85、Web 21/21、布局脚本 6/6、ESP32-C3 目标构建、普通上传保护、资源嵌入和 diff check 均形成自动化证据。固件 Flash 1,877,312 字节、RAM 59,436 字节，`firmware.bin` 1,877,712 字节，SHA-256 为 `b0b5d2254e7457b3752898f52c5bd0728a8bc505d9e70ebf9823b7d53a435f78`。
- 2026-09-30：按用户要求不执行 task-06 真机验证；新增独立 `acceptance.md`，iPhone、Android、LCD、真实 Flash、重启、断连/断电和稳定性项目全部保持 `NOT RUN`。
- 2026-09-30：用户随后确认功能验证通过，并明确所有 task 均可完成；task-06 标记完成，验收文档记录整体用户结论，同时不补写未提供的设备、系统和浏览器版本。
- 2026-09-30：完成代码收尾审查：把 schema 3 header 的 payload CRC/header CRC/commit 偏移收敛为单一公共定义并增加编译期布局断言，移除 Web 重复 shape 等级表，合并测试重复条件。整理后 Native 85/85、Web 21/21、布局 6/6、目标构建、资源嵌入和 diff check 重新执行；最终固件 SHA-256 为 `162cfe05e1a869bb71ed324c090f977ccc8912baae44ed99a1dfc9d0fbababfc`。
