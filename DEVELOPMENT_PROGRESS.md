# 开发进度文档 — LocalLife 智能桌面生活终端

> 基准日期：2026-11-10
> 开发板：微雪 ESP32-S3-Touch-LCD-3.49-V2
> 代码基线：厂商官方例程 `ESP-IDF/10_LVGL_V9_Test`（未修改的原始状态）
> 需求文档：`需求/需求文档.md`（LocalLife V0.1）

---

## 1. 项目概述

| 项 | 内容 |
|---|---|
| 项目目标 | LocalLife — 本地优先的智能桌面生活终端（智能时钟 / 天气助手 / 语音助手 / 提醒事项 / 声音中心 / 图片相册 / 氛围灯与夜间模式） |
| 硬件平台 | 微雪 ESP32-S3-Touch-LCD-3.49-V2（ESP32-S3R8，3.49" 172×640 触摸长条屏） |
| 软件框架 | ESP-IDF + LVGL V9 |
| 起点 | 厂商 LVGL V9 测试例程（当前运行 `lv_demo_widgets()` 演示） |
| 代码库根 | `esp32-s3-touch-lcd-3.49-v2-master`（含 Arduino 例程、固件、原理图 `schematic/`） |
| 需求状态 | V0.1 已确定产品功能范围；未锁定软件架构、API、音频格式、硬件扩展等实现细节 |

### LocalLife 七大功能模块（详见需求文档）

1. **智能时钟** — 时间/日期/星期显示、闹钟（单次/每日/自定义星期）、贪睡、夜间时钟模式
2. **天气助手** — 和风天气、区县级地点管理、逐小时预报、通勤天气提醒、语音播报
3. **语音助手** — 离线唤醒词 + 固定命令词识别、离线中文 TTS（第一阶段不做自由对话）
4. **提醒事项** — 语音创建、精确到时分、周期性提醒、屏幕+语音通知
5. **声音中心** — 自然白噪音、助眠噪音、专注背景音、睡眠定时
6. **图片相册** — 蓝牙同步手机图片、轮播、桌面相框
7. **氛围灯与夜间模式** — 亮度调节、息屏定时、睡眠渐暗

**场景联动**：早晨模式、工作专注模式、睡眠模式。

**设计原则**：本地优先、离线可用、模块化、网络中断不影响时钟/闹钟/本地播放等核心功能。

### 代码库内可参考的官方例程（`ESP-IDF/` 目录）

| 例程 | 用途（对 LocalLife 项目的参考价值） |
|---|---|
| 01_ADC_Test | ADC 采集 |
| 02_I2C_PCF85063 | 板载 RTC（时钟掉电走时基准）★★ |
| 03_I2C_QMI8658 | 板载 IMU（翻转亮屏等交互可参考） |
| 04_SD_Card | SD 卡（音频文件、图片存储）★★ |
| 05_WIFI_AP / 06_WIFI_STA | WiFi（和风天气 API 联网）★★ |
| 07_BATT_PWR_Test | 电池电量 |
| 08_Audio_Test | 音频输出（白噪音/TTS 播放）★★ |
| 09_LVGL_V8_Test / **10_LVGL_V9_Test** | 显示（本项目基线） |
| 11_FactoryProgram | 出厂程序 |

---

## 2. 环境与依赖（以 `dependencies.lock` 为准）

| 组件 | 版本 |
|---|---|
| ESP-IDF | 5.5.4（target: esp32s3） |
| lvgl/lvgl | 9.5.0（组件管理器，public 依赖） |
| espressif/esp_lcd_axs15231b | 1.0.1~1 |
| espressif/esp_io_expander_tca9554 | 2.0.3 |
| espressif/esp_lcd_touch | 1.2.1 |

**依赖管理策略（2026-11-10 起）：** `managed_components/` 全量入库（vendor 化，约 180MB / 4780 文件），
clone 后**无需联网即可构建**；对 LVGL 的二次开发（改 demo、裁剪）直接改该目录，git 可追踪 diff。
升级依赖用 `idf.py update-dependencies`，会产生大 diff，属预期。

`sdkconfig.defaults` 要点：16MB Flash、Octal PSRAM 80MHz、自定义分区表（factory 8M）、
`CONFIG_LV_OS_FREERTOS=y`、开启 Montserrat 12/16 字体、benchmark/stress/music demos。

> 注：引入离线语音识别（ESP-SR）、中文 TTS 后，分区表与 PSRAM 分配需重新评估。

### 本机构建环境（EIM 安装，2026-11-10 验证）

| 项 | 路径 |
|---|---|
| ESP-IDF 本体 | `E:\espressif\.espressif\v5.5.4\esp-idf` |
| 工具链 / Python venv | `C:\Espressif\tools` |
| 激活脚本（PowerShell） | `. C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1` |

构建流程：PowerShell 激活 → `idf.py build`；烧录：`idf.py -p COM<x> flash monitor`。
注意：从 Git Bash 调 PowerShell 跑 idf.py 会因 MSYSTEM 变量被拒（"MSys/Mingw no longer supported"），需在纯 PowerShell/ESP-IDF 终端中执行。

---

## 3. 硬件资源与引脚（摘自 `main/user_config.h`）

### 显示
- 面板：**AXS15231B**，QSPI（SPI3_HOST，quad 模式，40 MHz），RGB565
- 原生分辨率：172(H) × 640(V)（竖屏长条）
- 引脚：CS=GPIO9，PCLK=GPIO10，DATA0~3=GPIO11~14，TE=GPIO21
- 复位/背光使能：经 TCA9554 IO 扩展器（LCD_RST=PP5，BL_EN=PP1）
- 背光调光：LEDC PWM，GPIO42（50 kHz，8 bit）→ 对应需求「屏幕亮度调节 / 夜间低亮度 / 渐暗」

### 触摸
- 独立 I2C 总线（I2C_NUM_1）：SDA=GPIO17，SCL=GPIO18，300 kHz
- 器件地址 0x3B，自定义协议（11 字节命令 `B5 AB A5 5A ...` 读 32 字节报文）
- 触摸中断经 TCA9554 PP0

### IO 扩展 / 传感器总线
- TCA9554 挂在 I2C_NUM_0：SDA=GPIO47，SCL=GPIO48
- TCA9554 引出：TOUCH_INT(PP0)、BL_EN(PP1)、IMU_INT1/2(PP2/3)、RTC_INT(PP4，可作闹钟中断)、LCD_RST(PP5)、SYS_EN(PP6)、NS_MODE(PP7)

### 板载其他器件（LocalLife 相关）
- RTC：PCF85063（I2C_NUM_0，走时/闹钟中断）
- IMU：QMI8658（I2C_NUM_0）
- 音频：功放/扬声器（详见例程 08_Audio_Test 与原理图 `schematic/`）
- 麦克风：待确认（离线语音识别的前置硬件，需查原理图确认板载 MIC 及其接口 —— **待办**）

### 可配置开关（`user_config.h`）
- `Rotated`：软件旋转（当前 `USER_DISP_ROT_NONO` 不旋转；设为 `USER_DISP_ROT_90` 后启用 LVGL 软件旋转 90° + 触摸坐标换算）
- `Backlight_Testing`：背光亮度循环测试（当前关闭）

---

## 4. 代码结构

```
10_LVGL_V9_Test/                # git 仓库根（2026-11-10 初始化，进度随阶段提交）
├── CMakeLists.txt              # 工程入口
├── partitions.csv              # nvs 0x6000 / phy 0x1000 / factory 8M
├── sdkconfig.defaults          # 配置默认值（完整 sdkconfig 为生成物，已忽略）
├── dependencies.lock           # 组件管理器锁定版本
├── .gitignore / .gitattributes # 忽略 build/sdkconfig/.claude 等；行尾统一 LF
├── 需求/
│   └── 需求文档.md              # LocalLife V0.1 产品功能需求
├── main/
│   ├── main.c                  # 全部初始化 + LVGL 移植层
│   ├── user_config.h           # 引脚定义与功能开关
│   └── idf_component.yml       # 声明 lvgl/axs15231b/tca9554 依赖
├── components/
│   ├── i2c_bsp/                # I2C 总线初始化（总线0:外设, 总线1:触摸）+ 读写封装
│   └── lcd_bl_pwm_bsp/         # 背光 LEDC PWM
└── managed_components/         # 构建依赖 vendor 化全量入库（git 管理）
    ├── lvgl__lvgl/             # LVGL 9.5.0，含 demos/；二次开发改这里，diff 可追溯
    └── espressif__*/           # axs15231b / esp_lcd_touch / tca9554 等 5 个组件
```

> 工作区另有 `open_picture.jpg` 暂缓入库（用户待定）。

### main.c 关键实现（对后续开发重要）

- **渲染缓冲**：双全帧 PSRAM 缓冲（172×640×2B ×2）+ `LV_DISPLAY_RENDER_MODE_FULL`；另有 DMA 内部 RAM 中转缓冲（172×64 行/块）
- **刷屏流程**：flush 回调里把全帧按 172×64 分 10 块，逐块 `memcpy` 到 DMA buf → `esp_lcd_panel_draw_bitmap`，用二值信号量等每块 DMA 完成回调
- **旋转**：启用 `Rotated` 时用 `lv_draw_sw_rotate` 先旋转到 PSRAM `lvgl_dest` 缓冲再分块发送（软件旋转，占 CPU）
- **任务模型**：LVGL 任务锁互斥量，`lv_timer_handler` 跑在 pinned core 0、8KB 栈、优先级 2；tick 由 esp_timer 每 5ms 提供
- **演示切换**：`app_main` 末尾 `lv_demo_widgets()` 生效，`lv_demo_music/stress/benchmark` 已注释可切换

---

## 5. 开发进度

| # | 阶段 | 状态 | 完成日期 | 备注 |
|---|------|------|----------|------|
| 1 | 例程解压、结构梳理 | ✅ 完成 | 2026-11-10 | 厂商原始代码，未做修改 |
| 2 | 本进度文档建立 | ✅ 完成 | 2026-11-10 | |
| 3 | LocalLife 需求文档 V0.1 | ✅ 完成 | 2026-11-10 | 见 `需求/需求文档.md`；项目定位由 TPMS 显示验证转向桌面生活终端 |
| 4 | 编译 + 烧录验证例程跑通 | ✅ 完成 | 2026-11-10 | 编译 + 烧录 + 屏显/触摸验证通过（widgets demo）；profile 页已个性化 |
| 5 | 软件架构设计（本地优先、模块化） | ⬜ 未开始 | | 任务划分、存储与分区规划 |
| 6 | 关键技术预研 | 🔶 进行中 | | 2026-11-10 完成音频硬件/音频链路/ESP-SR/TTS 四项（结论见 §6.1），全部可行；剩余：和风天气 API、BLE 传图、SNTP+RTC |
| 7 | 智能时钟 + 闹钟（核心，离线可用） | ⬜ 未开始 | | SNTP + PCF85063；夜间时钟模式 |
| 8 | 声音中心（白噪音/助眠/专注） | ⬜ 未开始 | | 依赖音频链路预研结论 |
| 9 | 天气助手（和风天气 + 通勤提醒） | ⬜ 未开始 | | 依赖 WiFi；含地点管理与缓存策略 |
| 10 | 提醒事项（语音/触摸创建） | ⬜ 未开始 | | |
| 11 | 语音助手（唤醒词 + 命令词 + TTS 播报） | ⬜ 未开始 | | 依赖预研结论；与各模块联动 |
| 12 | 图片相册（同步 + 轮播） | ⬜ 未开始 | | BLE 手机端方案待定，可先 SD 卡导入 |
| 13 | 氛围灯与夜间模式、场景联动 | ⬜ 未开始 | | 早晨/专注/睡眠三场景 |

> 状态图例：✅ 完成 · 🔶 进行中 · ⬜ 未开始 · ⏸ 搁置
>
> 排序说明：时钟/闹钟为平时主形态且完全离线，优先落地；音频链路是声音中心与 TTS 播报的共同前置，预研优先级高；天气依赖外网，靠后但仍属第一版核心体验。

---

## 6. 待办 / 问题清单

### 6.1 技术预研结论（2026-11-10，语音与音频方向）

**① 板载音频硬件（来源：原理图 `schematic/` + 例程 08 `board_cfg.txt`）— 全齐备 ✅**

| 链路 | 器件 | 说明 |
|---|---|---|
| 播放 | ES8311（DAC）→ NS4150B（功放）→ 喇叭 | TTS 播报、白噪音输出 |
| 录音 | ES7210（ADC）← MIC1 + MIC2（双模拟麦） | 唤醒词/命令词输入；双麦可用 AFE 波束降噪 |

- I2S 引脚：mclk=GPIO7、bclk=15、ws=46、din=6（录音）、dout=45（播放）；codec 控制走 I2C 总线0（47/48，与 TCA9554 同总线）
- **结论：语音输入/输出硬件无缺口，离线语音识别（需求§3）具备硬前提**

**② 音频软件链路（例程 08_Audio_Test）— 官方已铺好，移植成本低 ✅**

- 架构：`espressif/esp_codec_dev ~1.3.4`（统一 codec 抽象）+ 微雪 `codec_board` 组件（解析 `board_cfg.txt`，板型 `S3_LCD_3_49`）+ `audio_bsp`
- 播放：`esp_codec_dev_write()`，24kHz/2ch/16bit 裸 PCM（例程用 `EMBED_FILES` 把 canon.pcm 嵌进固件）
- 录音：`esp_codec_dev_read()`，24kHz/2ch；音量 `esp_codec_dev_set_out_vol()`、录音增益 `set_in_gain()` 均有 API
- 移植到本工程：拷 `codec_board` + `audio_bsp` 两个组件，`idf_component.yml` 加 `espressif/esp_codec_dev` 依赖即可
- 白噪音资源：少量可 EMBED 嵌入；批量音频建议放 SD 卡（板载 TF 卡槽，见例程 04）

**③ ESP-SR 离线语音识别 — 可行 ✅（需 PSRAM，本板 8MB Octal 满足）**

- 流水线：AFE（AEC 回声消除/降噪/双麦波束）→ WakeNet（唤醒）→ MultiNet（命令词）
- MultiNet 原生支持**中文拼音命令词**，最多 200 条，运行时可增删改 → 覆盖需求"查询时间/天气、控制音乐、设闹钟"等固定命令
- 资源占用（官方 benchmark）：MultiNet5 Q8 ≈ 16KB RAM + **2310KB Flash**；WakeNet ≈ 180KB 内存
- **版本坑：esp-sr 1.3.3 有命令词 bug，须用 ≥1.4.2**
- 唤醒词：官方免费词（如 "Hi,乐鑫"）直接可用；完全自定义需乐鑫生成（收费），第一阶段用免费词
- 参考：[ESP-SR 命令词文档](https://docs.espressif.com/projects/esp-sr/zh_CN/latest/esp32s3/speech_command_recognition/README.html)、[官方 Benchmark](https://docs.espressif.com/projects/esp-sr/zh_CN/latest/esp32s3/benchmark/README.html)、[乐鑫语音方案页](https://www.espressif.com/zh-hans/solutions/audio-solutions/esp-skainet/overview)

**④ 离线中文 TTS — 可行 ✅（乐鑫 esp-tts，集成在 esp-sr 内）**

- 中文 UTF-8 文本输入，流式合成（`esp_tts_stream_synthesize()`），本地完成不依赖网络
- 模型编进固件，约 **1.5~7MB**；默认音色（如 Yoon），**语速 0~9 档可调**（需求"语速/音量可调"初步满足，音量走播放通道）
- 音色自然度待实测；替代方案：第三方 LingTTS（神经网络，更自然但更大）
- 参考：[ESP-SR TTS 文档](https://docs.espressif.com/projects/esp-sr/zh_CN/latest/esp32s3/speech_synthesis/readme.html)

**⑤ 综合评估（Flash 预算与风险）**

- 当前 app 0.8MB + MultiNet5 Q8 2.3MB + WakeNet ~0.2MB + TTS 2~7MB + 白噪音资源 ≈ **6~10MB** → factory 分区需从 8MB 扩到 **10~12MB**（16MB Flash 总量充足，改 `partitions.csv` 即可，架构阶段处理）
- 风险点：ESP-SR AFE 与 esp_codec_dev 的采样率/格式对接（集成时验证）；TTS 音色效果需真机试听；自定义唤醒词收费（先用免费词规避）
- **总体结论：语音全链路（双麦拾音→唤醒→命令词→TTS 播报）硬件软件均可行，无阻碍项，可进入软件架构设计（#5）**

> 剩余预研项（不阻塞架构设计，可并行/延后）：和风天气 API 接口验证、BLE 图片同步方案、SNTP+PCF85063 时间策略。

### 6.2 待办清单

**环境与基线**
- [x] 确认本机 ESP-IDF 版本与例程要求（5.5.4）一致，完成首次 `idf.py build`（2026-11-10）
- [x] 烧录验证通过：`idf.py -p COM<x> flash monitor`，widgets demo 屏显/触摸正常（2026-11-10）

**技术选型（预研项）**
- [x] 麦克风硬件确认：ES7210 + 双模拟 MIC（2026-11-10，见 §6.1①）
- [x] 音频链路：ES8311+NS4150B 播放；esp_codec_dev 方案移植成本低；白噪音少量 EMBED / 批量走 SD 卡（见 §6.1②）
- [x] 离线语音识别：ESP-SR（WakeNet+MultiNet5 Q8），需 ≥1.4.2、需 PSRAM ✅（见 §6.1③）
- [x] 离线中文 TTS：乐鑫 esp-tts，中文流式合成、语速 0~9 档 ✅（见 §6.1④）
- [ ] 和风天气 API：账号申请、区县级天气接口验证、离线缓存策略
- [ ] BLE 图片同步方案：手机端 App 依赖较重，评估第一阶段降级为 SD 卡导入的可行性
- [ ] 时间基准策略：SNTP 网络对时 + PCF85063 掉电走时，RTC_INT 作闹钟中断源

**设计决策**
- [ ] 决定屏幕方向：竖屏原生 172×640 还是软件旋转（注意软件旋转的性能开销）
- [ ] 功能优先级确认与 V0.1 → V0.2 需求细化（各模块界面原型）
- [ ] （待补充）

---

## 7. 变更记录

| 日期 | 变更内容 |
|------|----------|
| 2026-11-10 | 创建文档；梳理厂商例程结构、环境依赖、硬件引脚；确认代码为未修改原始状态 |
| 2026-11-10 | 新增 LocalLife 需求文档 V0.1（`需求/需求文档.md`），项目定位由 TPMS 显示验证调整为本地优先的智能桌面生活终端；更新项目概述、进度规划、待办清单与技术预研项 |
| 2026-11-10 | git 仓库初始化（初始提交含厂商原始代码 + 需求/进度文档）；首次 `idf.py build` 编译通过（产物 bin 801KB），记录本机 EIM 构建环境；待烧录验证 |
| 2026-11-10 | 依赖 vendor 化：`managed_components/` 全量入库（lvgl 9.5.0 等 6 组件，约 180MB / 4780 文件），仓库共 4798 文件，clone 后可离线构建 |
| 2026-11-10 | 烧录验证通过，进度 #4 完成。首次修改厂商代码：widgets demo profile 页个性化（头像换为 open_picture.jpg 圆形裁切 ARGB8888、名字 XiaoXUE、简介 "This is a gift from HangZhou, to XiaoXue"），提交 f792132 / 1fad4ea |
| 2026-11-10 | 技术预研（语音与音频方向）完成：确认 ES7210 双 MIC + ES8311/NS4150B 音频硬件、esp_codec_dev 链路移植方案、ESP-SR 与 esp-tts 可行性及资源占用；结论见 §6.1，#6 转 🔶 |
