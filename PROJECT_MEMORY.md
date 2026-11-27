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

### 传感器 / IMU

| # | 日期 | 坑 | 一句话教训 | 详情 |
|---|---|---|---|---|
| 31 | 11-25 | 换板后 IMU 个体零偏 → USB 朝右要倾斜很多才切（0.70g 阈值余量仅 0.007g） | 判定逻辑两方向对称时先查器件个体差异;阈值须留 ≥0.1g 余量盖住芯片零偏容差（±100mg,数据手册 Table 7） | 进度 §8 11-25 |

### 事件 / UI 状态

| # | 日期 | 坑 | 一句话教训 | 详情 |
|---|---|---|---|---|
| 34 | 11-26 | 界面忙时（等 LVGL 锁超过 200ms）网络状态更新被丢弃，叹号偶发不更新且无从查起 | 事件回调里"拿锁失败就放弃"等于静默丢状态；已加 DROPPED 警告日志。**遗留**：未做补送，彻底解决要缓存待更新的状态、由界面一侧重试 | app_ui.c on_net_state |
| 35 | 11-26 | 开机 WiFi 先于界面连上（got ip 4.3s / UI ready 5.4s），网络状态事件无人订阅即丢失 → 时钟卡叹号常驻，滑走滑回也不消 | 事件驱动的界面状态必须"先订阅、再主动查一次初值" | 进度 §8 11-26;app_ui.c app_ui_init |
| 36 | 11-26 | 在界面初始化里又去拿界面的锁：自己等自己 200ms 超时后悄悄跳过，日志打了"已连接"，初值却没写进去 | 界面初始化全程由 main 拿着锁调用，里面不能再拿同一把锁；日志只能证明"查到了"，是否"写进了"要看两行日志的时间差或看屏幕 | app_ui.c app_ui_init;main.c |
| 37 | 11-26 | 时钟卡被滑走销毁后静态控件句柄未清零，home_cards_set_net 仍写已释放对象 → 滑动卡片有概率死机 | 持有控件句柄的卡片必须有 destroy 收尾：删除前清零句柄（仅当句柄确属被删卡片层）；跨模块推送状态的钩子要按"目标可能已不存在"设防 | 进度 §8 11-26;ui_home_cards.c card_clock_destroy |

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
| 21 | 11-19 | 烧录偶发 COM 口打开失败（旧板 COM4,2026-11-25 起的板子为 COM9） | 直接重试即过,勿排查 | 进度 §2 |
| 22 | 11-20 | 文档"变更记录"与"状态区"两类更新不同步（积压 5 天） | 收官三步：追加记录+状态区回扫+关键词 grep | 进度 §7 |
| 23 | 11-20 | "已记录"未落文件 | 说了≠记了,先写文件再声明 | 进度 §7 |
| 32 | 11-25 | git push/fetch 直连 github.com 443 超时 | git 不读 Windows 系统代理;push 加 `-c http.proxy=http://127.0.0.1:7897`（系统代理端口,仅本次命令生效） | 进度 §2 |

### WiFi / 内存挤压（2026-11-21 全天,9 轮战役终通）

| # | 日期 | 坑 | 一句话教训 | 详情 |
|---|---|---|---|---|
| 24 | 11-21 | esp_event_loop_create_default 重复调用→abort | app_event 已建 default loop,组件内不可再调（重复创建即 abort） | 代码注释 |
| 25 | 11-21 | esp_wifi_init 返回 0x101(NO_MEM) | esf 静态 TX 池是**大头（16 个×1.6KB=25.6KB）**,砍 STATIC_TX_BUFFER_NUM 16→6 是最有效一刀 | 本节下文 |
| 26 | 11-21 | LVGL+WiFi 同跑内部 RAM 不够 | 三个方向齐下：DMA 中转缓冲→PSRAM / 删空任务+四任务合并 slow_tick / WiFi 全链裁剪（TX/RX/mgmt/lwIP/保留池） | 本节下文 |
| 27 | 11-21 | WiFi 后启动→motion/timer 任务建栈失败 abort | **先启 WiFi（大块连续 esf 须堆完整时拿走）,后建任务**;所有 xTaskCreate 加降级兜底（降级≠abort） | main.c |
| 28 | 11-21 | net_init 任务 2KB 栈溢出→stack overflow 复位 | esp_wifi_init 调用深,任务栈至少 4KB | app_net.c |
| 29 | 11-21 | motion 栈砍 2KB 后溢出 | 3KB 是底线（IMU 读+浮点+LVGL 锁切旋转）,不动 | app_motion.c |
| 30 | 11-21 | sdkconfig sed 改了但数字不变 | sdkconfig 需 **reconfigure** 才进 sdkconfig.h,直接改 sdkconfig 后 `idf.py reconfigure` | 流程 |
| 33 | 11-26 | esp_wifi_init 峰值凭记忆记成 51K/40K | 内存数字以水位探针实测为准:全链路峰值 25.1~28.6 KiB | 本节下文 |

**WiFi 内存全景（真机实测）**：
- WiFi 驱动常驻内部 RAM = **~27.4KB**（TX 池 9.6K + 驱动任务栈 6.6K + lwIP + phy）——归驱动所有,不随 net_init 任务栈释放,直到 `esp_wifi_deinit()`
- net_init 任务栈 4KB 是临时的,vTaskDelete 自删即回收
- **水位探针实测（2026-11-25/26,app_net `heap[...]` 四点;口径=内部堆当前值与开机以来最低水位）**:esp_wifi_init 占用=init 前后差 **27092×4、23660×2 字节**（六次启动,两次偏低 3432 字节,原因未查）,init 期间无更深瞬时下探;start 期间再瞬时下探 1756~1796 字节;**全链路峰值（init 前内部堆 − 最低水位）=25716~29320 字节（25.1~28.6 KiB）,随 init 占用的启动间差异浮动**——曾记的 51K/40K 不成立;未连 WiFi 时最低水位出现在 start 段,连上的四次中两次连接过程比 start 段再低 1936 字节（got-ip 最低 11231）,两次未创新低;got-ip 后回升到 15703~16495）
- **内部 RAM 总账（大头项,非精确账）**：116KB 可用 ≈ LVGL 任务栈~6K + DMA 保留池 16K + 各任务栈~15K + WiFi 驱动 27.4K + 剩余 ~11.4K（以上相加 75.8K,未含系统任务栈/NVS/netif/碎片约 40K;精确值以真机日志为准,2026-11-23 校准）
- iPhone 热点须开"最大兼容性"（=2.4GHz,ESP32 不支持 5GHz）
- 对完时可 `esp_wifi_stop()+deinit()` 回收 27.4KB,需要时重新 init（~2s 重连）——符合需求 §9 联网降频

**ESP32-S3 内部 SRAM 512KB 完整账本**（map 文件+heap_init 实测,2026-11-21）：

| 区域 | 大小 | 性质 | 可优化？ |
|---|---|---|---|
| ICache（指令缓存） | 16KB | 硬件固定 | ✗ |
| DCache（数据缓存） | 32KB | 硬件固定（PSRAM 必经） | ✗ |
| IRAM 中断向量 | 1KB | 硬件 | ✗ |
| IRAM 代码段 | ~204KB | WiFi 驱动/FreeRTOS 内核/中断处理——必须放 SRAM 才能高速执行 | 极有限 |
| .data+.bss（全局变量） | ~136KB | LVGL ~25K + WiFi/wpa ~20K + mbedcrypto ~15K + lwIP ~12K + FreeRTOS ~8K + libc ~10K + 本项目 ~8K + hal/NVS 等 ~38K | 有限（裁功能才省） |
| 动态堆（malloc/任务栈） | **116KB** | 唯一可自由分配——任务栈+WiFi esf+DMA 保留池全在此 | ✓ 主战场 |
| RTC RAM | 7KB | 深度睡眠 | ✗ |

> **结论：512KB 中 ~390KB 在写第一行代码前就被占死**——真正可自由分配的只有 116KB 动态堆,这也是 WiFi 27KB 打得艰难的根本原因。后续大块数据一律 PSRAM（余 7.7MB）。

**SNTP 对时链路（2026-11-21 全通）**：
- 触发链：每 1h（`CONFIG_LWIP_SNTP_UPDATE_DELAY=3600000`）lwIP SNTP 向 ntp.aliyun.com 发 UDP → NTP 应答 → lwIP 设系统时钟 → `sntp_sync_cb` 回调 → 发 `LL_EV_TIME_SYNCED` 事件（载荷 bsp_rtc_datetime_t）→ app_clock 订阅 → `app_clock_sync_time()` 写 PCF85063 + 更新缓存
- **每次 NTP 应答都写一次 RTC**（校正 RTC 漂移 ±20ppm/天 ~2s;PCF85063 每小时写一次无压力）
- NTP 失败不写（断网跳过,下周期再试）;WiFi 断线重连后不立即查（等下个周期）
- **时间三层降级链**：开机读 RTC → 有效直接用（断电走时 ✓）;无效（首次上电/电池耗尽）→ 尝试 SNTP → WiFi 不在则**编译时刻兜底**（`CLOCK_SET_COMPILE_TIME=1`,精度=烧录到开机间隔,已标注退役留降级）
- U8 正式阶段应对时间隔改 12h（需求 §9 功耗策略,配合 WiFi 动态开关）

**WiFi sdkconfig 裁剪定稿**（sdkconfig.defaults 已固化）：
```
SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y    # lwIP 可迁移部分进 PSRAM
SPIRAM_MALLOC_RESERVE_INTERNAL=16384  # 保留池 32K→16K
ESP_WIFI_STATIC_TX_BUFFER_NUM=6    # 16→6 (esf 池大头,省 16KB)
ESP_WIFI_STATIC_RX_BUFFER_NUM=2    # 4→2
LWIP_MAX_SOCKETS=4                  # 10→4
LWIP_TCPIP_TASK_STACK_SIZE=2048     # 3072→2048
# AMPDU TX/RX 关闭（吞吐无关紧要,对时场景）
# WPA3 关闭
```

**未砍候选项（2026-11-23 记）**：`CONFIG_LWIP_IPV6=y` 仍开着——用户决定**先不动，后面真不够内存时再关**（对时/和风走 IPv4，关掉不影响功能；能省多少未实测）。

---

## 1a. 关键约束（勿违反,违反即返工）

| 约束 | 内容 | 来源 |
|---|---|---|
| **主页 v1 勿删** | `components/app_ui/app_ui.c` 的主页 v1（S2.4 深色版）是**夜间模式基准样式**,任何重构（含 U3 主页 v2）不得删除/重写——U3 做法=加新不删旧,拆日间（设计稿三栏）/夜间（v1）双样式切换 | 用户 2026-11-19/11-20 两次明确要求;需求 V0.8 §7 |
| **开机图已定稿** | `boot_logo.c` 用户实物素材（已裁装饰边）,勿改;换图方法见 AI 记忆/文件头注释 | 用户 2026-11-19 |
| **WiFi=临时硬编码** | 当前 SSID/密码写死在 user_config.h（WIFI_TMP_*）,**只是对时临时方案**——正式配网（扫描/设置页/NVS 存储/SmartConfig 等）归 U8 天气阶段;用后即弃,勿在其上叠加正式功能 | 用户 2026-11-21 |
| **公司电脑不留私人痕迹** | git 提交邮箱用 noreply 地址（历史已重写） | 项目惯例 |
| **PSRAM 栈任务零 flash 驱动接触** | 工作台（栈）在 PSRAM 的任务不得调用任何会走到 flash 驱动的 API,`esp_flash_*`、`esp_partition_*`、NVS、文件系统,**读也不行**;读写设置交给内部栈任务,其缓冲也须在内部 RAM。依据是 flash 操作期间缓存关闭,PSRAM 与 flash 同不可访问。JS 方案脚本任务的硬约束 | ESP-IDF 官方文档 External RAM·限制;进度 §6.8 |

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
