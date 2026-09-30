# 三槽个性化图片验收矩阵

> 状态：自动化基线已完成；8 KiB HTTP 栈候选的核心保存链路已由用户确认通过，其余真机项目按实际结果记录

本文严格区分自动化证据与真实设备观察。状态只使用 `PASS`、`FAIL`、`NOT RUN`；自动化构建不能把 LCD、按键、Flash、手机浏览器或断电项目推断为 `PASS`。

证据不得记录姓名、bio、照片内容、HTTP body、会话 token、`cardid` 原值或其他可恢复设备身份的数据。设备数据保护只记录非敏感摘要是否一致。

## 自动化交付基线

| 项目 | 当前结果 | 证据 |
| --- | --- | --- |
| Native | PASS | 11 组，82/82；新增均匀/高变化像素字节流连续覆盖保存；`pio test -e native` |
| Web | PASS | 19/19；`node --test test/web/badge_web.test.js` |
| 布局脚本 | PASS | 6/6，并验证目标上传清单不覆盖持久区 |
| Target build | PASS | 整理后构建：Flash 1,875,148/3,145,728（59.6%）；RAM 59,420/327,680（18.1%） |
| 固件哈希 | PASS | 整理后构建 SHA-256 `caa365511a2c36ccec7498cc588f251edc0e651ede85141c5bc3446914cc9a1b` |
| HTTP 任务栈修复 | PASS | 保留 512 字节流式块，并将 ESP-IDF HTTP 任务栈由默认 4 KiB 显式提高到 8 KiB；启动日志应显示 `HTTP ready stack=8192` |
| Web 资源嵌入 | PASS | `firmware.bin` 包含版本 `2026092901`、个性化 Canvas 与 slot API 路径 |
| 业务隔离 | PASS | `AI-Passport-Badge.map` 未发现 NimBLE、ANCS、AMS、消息、音乐或旧无线切换业务符号 |
| Diff check | PASS | `git diff --check` |

## 测试对象

| 字段 | 记录 |
| --- | --- |
| 设备型号/编号 | NOT RUN |
| 测试日期 | NOT RUN |
| Git commit 或工作区标识 | NOT RUN |
| 串口设备 | NOT RUN |
| iPhone / iOS / Safari | NOT RUN |
| Android / Chrome | NOT RUN |
| 供电方式 | NOT RUN |

## 分区与普通烧录保护

| 项目 | 步骤与预期 | 状态 | 证据/备注 |
| --- | --- | --- | --- |
| 烧录前基线 | 记录设置、工牌、三槽占用状态和四个持久区域的非敏感摘要 | NOT RUN | — |
| 普通上传地址 | 使用 PlatformIO 普通 upload；输出仅写 `0x0`、`0x8000`、`0x10000` | NOT RUN | 禁止全片擦除 |
| NVS 保护 | 上传前后设置保持，NVS 不被擦除 | NOT RUN | — |
| `badge_data` 保护 | `0x310000/0x40000` 上传前后摘要一致 | NOT RUN | — |
| `cardid` 保护 | `0x356000/0x4000` 上传前后摘要一致 | NOT RUN | 不记录原值 |
| `custom_data` 保护 | `0x35A000/0x100000` 普通升级不被写入 | NOT RUN | — |
| 启动映射 | 启动日志无 custom 分区或 mmap 错误；失败时仅禁用个性化 | NOT RUN | — |

## LCD 与实体按键

| 项目 | 步骤与预期 | 状态 | 证据/备注 |
| --- | --- | --- | --- |
| 0 张 | 三槽全空；长按上/下不进入个性化页 | NOT RUN | — |
| 1 张 | 保存任一槽；长按进入个性化，短按上/下无视觉变化 | NOT RUN | — |
| 2 张稀疏槽 | 仅槽 1、3 有图；短按上/下只在两张间循环 | NOT RUN | — |
| 3 张 | 三槽有图；短按上下按相反方向稳定循环 | NOT RUN | — |
| 全屏像素 | 图片为 240×320、方向与颜色正确、无拉伸、边框、文字或残影 | NOT RUN | — |
| 最近槽恢复 | 离开再进入个性化页，优先显示最近且仍有效的槽 | NOT RUN | — |
| 当前槽清空 | 清空当前槽且仍有其他图；返回后显示有效槽 | NOT RUN | — |
| 最后一槽清空 | 从设置返回时安全回到工牌，个性化页不可进入 | NOT RUN | — |
| 长按隔离 | 长按上/下切业务页，长按确认进设置；短按确认无操作 | NOT RUN | — |
| 息屏唤醒 | 首次按键只唤醒，首帧无误切页或残影 | NOT RUN | — |

## Wi-Fi 与手机浏览器

以下项目分别在 iPhone Safari 与 Android Chrome 执行；每个平台均单独记录结果。

| 项目 | 步骤与预期 | iPhone | Android | 证据/备注 |
| --- | --- | --- | --- | --- |
| 热点与页面 | 从设置启动热点，连接后访问 `http://192.168.4.1/` | NOT RUN | NOT RUN | — |
| 默认入口 | 页面默认显示工牌配置，原姓名/bio/照片流程可用 | NOT RUN | NOT RUN | — |
| 三槽状态 | 个性化配置准确显示三个槽位的空/已保存状态 | NOT RUN | NOT RUN | — |
| 空槽选择 | 切到空槽时画布清空，不沿用其他槽图片 | NOT RUN | NOT RUN | — |
| 已有图片反显 | 按需读取目标槽 RGB565，画布构图与 LCD 一致 | NOT RUN | NOT RUN | — |
| 横竖图编辑 | 选择横图和竖图，拖动、1～4 倍缩放、90° 旋转均不露空白 | NOT RUN | NOT RUN | — |
| 窄屏操作 | 320 px 宽视口无阻断按钮的横向溢出，拖动比例正确 | NOT RUN | NOT RUN | — |
| 保存到槽位 | 明确选择目标槽，保存后不重启并可继续使用 | PASS | NOT RUN | 用户确认 8 KiB HTTP 栈候选功能测试通过 |
| 修复前保存故障 | 选择图片并保存到槽位 1 | FAIL | NOT RUN | 2026-09-29 两次触发 `Stack protection fault`；本地 `1.log` 含设备网络标识，不提交或共享 |
| 512 B 缓冲候选复验 | SHA-256 `cbeb7b…f2657d6`；分别保存原失败图片和简单图片 | FAIL | NOT RUN | 原图仍触发 `A stack overflow in task httpd` 并重启；同为 153,600 字节的简单图片可完成 |
| 8 KiB 栈候选复验 | SHA-256 `771efc…6e439`；使用原失败图片保存且不重启 | PASS | NOT RUN | 用户确认功能测试通过 |
| 整理后构建冒烟 | SHA-256 `caa365…c9a1b`；普通烧录后保存一次原失败图片 | NOT RUN | NOT RUN | 仅做接口清理和 README/许可证收尾，仍需区分构建与真机证据 |
| 覆盖槽位 | 覆盖已有图成功显示新图，其他槽不变 | NOT RUN | NOT RUN | — |
| 未保存切换 | 切换类别或槽位出现确认；取消后保持当前编辑 | NOT RUN | NOT RUN | — |
| 清空取消 | 点击清空，确认文案含槽号；取消后不发 DELETE | NOT RUN | NOT RUN | — |
| 清空成功 | 确认后目标槽变空、画布清除、保存/清空状态正确 | NOT RUN | NOT RUN | — |
| 清空失败 | Busy、断连或存储失败时保留原图片并明确提示 | NOT RUN | NOT RUN | — |
| Mutation 锁定 | 保存或清空期间不能切换配置类别或目标槽 | NOT RUN | NOT RUN | — |
| 按键安全退出 | Wi-Fi 页面任意按键退出，事务到安全边界后热点关闭 | NOT RUN | NOT RUN | — |

## 事务、重启与故障恢复

| 项目 | 步骤与预期 | 状态 | 证据/备注 |
| --- | --- | --- | --- |
| 三槽重启恢复 | 保存三张不同测试图后重启，占用状态与图片均恢复 | NOT RUN | — |
| 覆盖断连 | 上传中断后只显示旧完整图片，不出现半图 | NOT RUN | — |
| 覆盖断电 | 在擦除、写入、读回和 commit 前后可控断电；只恢复旧或新完整图片 | NOT RUN | — |
| 清空断连 | DELETE 中断后重新 GET 可判定状态，重复 DELETE 安全 | NOT RUN | — |
| 清空断电 | 墓碑 commit 前后断电；只出现旧图片或确定空槽 | NOT RUN | — |
| 槽位隔离 | 任一槽保存/覆盖/清空失败，另外两个槽内容不变 | NOT RUN | — |
| 工牌隔离 | 个性化操作前后工牌姓名、bio 和照片不变 | NOT RUN | — |
| 已空幂等清空 | 对空槽重复清空成功且不产生异常写入或状态漂移 | NOT RUN | — |

## 性能与稳定性

至少执行五轮“开启热点 → 手机连接 → 保存或清空 → 按键退出”，并在三槽双 bank 均有有效记录时测量冷启动。

| 项目 | 状态 | 第 0/1/2/3/4/5 轮或证据 |
| --- | --- | --- |
| 冷启动 custom 扫描耗时 | NOT RUN | — |
| 按键切图到有效帧延迟 | NOT RUN | — |
| 无崩溃、重启或 watchdog | NOT RUN | — |
| 普通堆无持续下降 | NOT RUN | — |
| 最大连续块无持续下降 | NOT RUN | — |
| MMU 映射稳定 | NOT RUN | — |
| 任务栈水位稳定 | NOT RUN | — |

## 最终结论

| 字段 | 记录 |
| --- | --- |
| Automated baseline | PASS |
| Device and browser acceptance | 8 KiB 栈候选核心保存链路 PASS；其余矩阵仍有 `NOT RUN` |
| 已知阻塞缺陷 | 已验证核心路径未发现阻塞缺陷；整理后构建冒烟及其余完整验收尚未执行 |
| 验收人/日期 | NOT RUN |
