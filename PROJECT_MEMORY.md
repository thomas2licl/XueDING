# PROJECT_MEMORY — LocalLife 全局坑与教训

> **定位**：项目级"踩坑总账"。新人/新会话接手**先读此文件**，再按指针深入。
> **规则**：修完一个坑 → 在 §索引 加一行（日期/一句话教训/详情指针）；同类坑只补差异。
> 与其他文档的关系：进度文档 §6 是**完整技术档案**（本文件只做索引）；AI 助手的私有记忆（如有）不得作为唯一载体——**一切教训以本文件与 §6 为准**。

---

## 1. 索引（按类别）

### 硬件契约 / 显示管线

| # | 日期 | 坑 | 一句话教训 | 详情 |
|---|---|---|---|---|
| 1 | 11-10 | AXS15231B QSPI 只支持从 y=0 顺序流写（RASET 无效,0x3C 续写） | 任意窗口直写必花屏——驱动契约先实测再设计 flush | 进度 §6.3 |
| 2 | 11-10 | 整帧 PSRAM 列式旋转 148ms/帧 | tile 16×16 转置+DMA 乒乓 → ~20ms/帧 | 进度 §6.3 |
| 3 | 11-20 | LVGL 双缓冲 DIRECT 的 sync_areas 漏拷 → 随机闪旧像素 | flush 同步等 DMA 时双缓冲零收益,直接单缓冲根治 | 进度 §6.3 |
| 4 | 11-20 | 270 方向短前缀与 90 行为不对称 | 前缀刷统一全量 10 块,~10ms 代价换一致性 | 进度 §6.3 |
| 5 | 11-13 | bootloader 期 ~1s 花屏（背光 CTRL 硬件上拉） | app 软件无法触及,三方案挂起中 | 进度 §6.5 |

### 触摸 / 输入

| # | 日期 | 坑 | 教训 | 详情 |
|---|---|---|---|---|
| 6 | 11-13 | 触摸芯片低频/边沿式报点（按住不报,拖动 200~400ms/帧） | 10ms 采样+空报文 30ms 去抖;空报文≠抬起 | 进度 §6.4 |
| 7 | 11-13 | 空报文立即判 RELEASED → 状态振荡 → 点击被吞数秒 | 去抖窗口必须够长 | 进度 §6.4 |
| 8 | 11-19 | 去抖计数器 uint8 回绕 → 静置每 2.56s 假 PRESSED（可致误点击） | "计满保持"语义的计数器必须钳位 | 进度 §6.4 |
| 9 | 11-20 | 拖动报点间隔>去抖窗口 → 一次长滑拆成多次手势连切多页 | 手势层加 350ms 冷却（一滑一页） | nav 代码注释 |

### LVGL 9.5 集成（全表见 §2）

| # | 日期 | 坑 | 教训 |
|---|---|---|---|
| 10 | 11-10 | indev 自动旋转坐标 | read_cb 给物理坐标,手动映射=二次旋转 |
| 11 | 11-10 | 屏上 perf monitor 自刷新循环 | 关掉,用串口统计 |
| 12 | 11-10 | label transform_scale 饿死 IDLE 看门狗 | 放大文字用字体文件,勿用 transform |
| 13 | 11-20 | lv_event_t/lv_indev_t 结构体私有化 | 只用公开 API;拿不到的字段靠事件自记 |
| 14 | 11-20 | lv_refr_now 在 esp_event 任务调用→复位 | 仅 LVGL 任务上下文可调 |
| 15 | 11-20 | canvas RGB565 直写网页色→错色（深色变蓝） | 逐像素写必须位域转换 |

### 字体

| # | 日期 | 坑 | 教训 | 详情 |
|---|---|---|---|---|
| 16 | 11-19 | 64px 数字字体缺空格 → 冒号闪烁缺字方框 | 字符集按功能审查（闪烁=' '+' :'） | tools/fonts/README |
| 17 | 11-19 | --symbols 传半角 ASCII 丢字符 | 半角一律 --range 0x20-0x7E | tools/fonts/README |
| 18 | 11-19 | LV_SYMBOL 不在中文字库 | FontAwesome（lvgl 包自带 woff）按码位合并 | tools/fonts/README |
| 19 | 11-19 | 生成文件 include 路径编译错 | CMake 加 LV_LVGL_H_INCLUDE_SIMPLE | tools/fonts/README |

### 工具链 / 流程

| # | 日期 | 坑 | 教训 | 详情 |
|---|---|---|---|---|
| 20 | 11-19 | Git Bash 直接跑 idf.py 被 MSYSTEM 拒 | PowerShell 包装一条龙（现成命令） | 进度 §2 |
| 21 | 11-19 | 烧录偶发 COM4 打开失败 | 直接重试即过,勿排查 | 进度 §2 |
| 22 | 11-20 | 文档"变更记录"与"状态区"两类更新不同步（积压 5 天） | 收官三步：追加记录+状态区回扫+关键词 grep | 进度 §7 |
| 23 | 11-20 | "已记录"未落文件 | 说了≠记了,先写文件再声明 | 进度 §7 |

---

## 1a. 关键约束（勿违反,违反即返工）

| 约束 | 内容 | 来源 |
|---|---|---|
| **主页 v1 勿删** | `components/app_ui/app_ui.c` 的主页 v1（S2.4 深色版）是**夜间模式基准样式**,任何重构（含 U3 主页 v2）不得删除/重写——U3 做法=加新不删旧,拆日间（设计稿三栏）/夜间（v1）双样式切换 | 用户 2026-11-19/11-20 两次明确要求;需求 V0.8 §7 |
| **开机图已定稿** | `boot_logo.c` 用户实物素材（已裁装饰边）,勿改;换图方法见 AI 记忆/文件头注释 | 用户 2026-11-19 |
| **公司电脑不留私人痕迹** | git 提交邮箱用 noreply 地址（历史已重写） | 项目惯例 |

---

## 2. LVGL 9.5 集成坑全表（15 条,项目内权威版本）

改 LVGL 移植层/深度集成前必读；每条都经源码或真机实证：

1. **indev 自动旋转**：`lv_indev.c` indev_pointer_proc 自动调 `lv_display_rotate_point` 转 read_cb 的点 → read_cb 必须给**物理面板坐标**（本项目 `ph=(ty, 639-tx)`），手动映射会二次旋转
2. **DIRECT 缓冲同步**：双缓冲下 LVGL 在两块间 memcpy 上帧脏区（sync_areas）——flush 里 in-place 改渲染缓冲会污染后续帧 → 交换必须在独立缓冲（tile 旋转已遵循）
3. **屏上 perf monitor**（CONFIG_LV_USE_PERF_MONITOR）自我维持刷新循环——静态页+全帧刷架构下的空载刷新源,已关；性能观测用串口 flush perf
4. **事件不自动冒泡**：LV_EVENT_PRESSED 只发 hit-test 对象 → 需 `LV_OBJ_FLAG_EVENT_BUBBLE`；GESTURE 同理——全局手势统一走 **indev 级事件**最干净
5. **screen 默认可滚动**：手指微动触发回弹动画 → 静态页必须 clear SCROLLABLE
6. **结构体 9.5 私有化**：`lv_event_t`/`lv_indev_t` 不透明——`e->code`/`indev->pointer.*` 编译报 incomplete type；用公开 API（lv_event_get_code/lv_indev_get_point/lv_indev_get_gesture_dir）,拿不到的（按下起点）订阅 indev 级 PRESSED 事件自记
7. **`LV_SCREEN_LOAD_ANIM_MOVE_LEFT` 语义**（lv_display.c 源码）：新屏 x 从 +W→0（从右滑入）——左滑→下一页用 MOVE_LEFT;枚举前缀 `LV_SCREEN_LOAD_ANIM_*`
8. **滚动控件优先于手势**：`indev_gesture()` 遇 scroll_obj 直接 return——"滑动控件不吃切页手势"天然保证；列表配 `set_scroll_dir(cont, LV_DIR_VER)` 锁向
9. **lv_font_conv 三坑**：--symbols 传半角丢字符（用 --range 0x20-0x7E）;数字字体要含空格;LV_SYMBOL 要合并 FontAwesome
10. **双缓冲 sync_areas 漏拷**：特定脏区组合漏拷→陈旧像素闪旧画面（曾误判渲染/transition/前缀问题绕三圈）——flush 同步等 DMA 的架构直接**单缓冲**零成本根治
11. **canvas RGB565 位域**：网页色直接截断成 uint16 会错色（0x080A0E→深蓝;白色碰巧正确掩盖）——`(r>>3)<<11|(g>>2)<<5|(b>>3)`
12. **lv_refr_now 上下文限制**：仅 LVGL 任务可调,esp_event 任务调用会复位；跨任务更新=持锁改对象+invalidate
13. **label transform_scale 走图层渲染**：每帧 malloc 大缓冲,LVGL 占满 core0 饿死 IDLE0 看门狗——放大文字用自定义字体文件
14. **GESTURE 判定与低频报点兼容**：累计位移>50px 触发,单点大位移反而易命中——但报点间隔>去抖窗口时一滑拆多手势,手势层加冷却
15. **主题 transition（80ms）**：按钮状态渐变本身无害——按键闪先查 buffer 层（sync 漏拷曾伪装成 transition 问题）
16. **单缓冲 DIRECT 下 screen 动画的 invalidate 唤不醒渲染**（2026-11-21 破案,半下午）：触摸切页 0 帧动画,但无触摸的程序切页正常、与其他动画(页点淡入)并发时正常——**screen anim 自身的 invalidate 无法唤醒 refr,普通 anim 的可以**。修复=动画期间 33ms"渲染心跳"(立即首拍+周期 invalidate 两屏+自停);**心跳只可踢 1~2 屏**——首版踢 3 屏全屏把 LVGL 任务打爆、触摸饿死。定位手段：①程序化复现(定时自动切页,剥离触摸变量) ②LVGL 动画生命周期打点(scr_load_anim/anim_completed 的 printf) ③用户观察是重要线索但需日志校准(时间窗口错位曾致两轮空采集)
17. **lv_timer_t 也是私有结构**（user_data 不可直接访问）——计数用文件级变量;同族:lv_event_t/lv_indev_t(教训 6)

---

*维护记录：2026-11-20 建立（汇总进度 §6 档案+tools/fonts+私有记忆中仅存于 AI 侧的 LVGL 条目）。*
