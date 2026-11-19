# 中文字体再生成（U2,2026-11-19）

产出：`components/app_ui/fonts/` 下三个 LVGL 字体 C 文件。
**字体源文件（NotoSansSC-Regular.otf,8.3MB）不入库**——OFL 许可可自由分发,但为控仓库体积;
再生成时重新下载（URL 见 gen_fonts.sh）。

| 文件 | 规格 | 用途 |
|---|---|---|
| font_noto_sc_16.c | 16px/bpp4/GB2312 一级 3755 字+94 符号+60 个 LVGL 图标 | 辅助文字（后续页面） |
| font_noto_sc_24.c | 24px/bpp4/同上 | 页面标题/正文（图标+中文单 label 混排 ✓） |
| font_noto_num_64.c | 64px/bpp4/空格+0-9 : % - + | 主页时间大数字（**数字等宽 adv_w=568,时间跳动不抖**） |

**坑位记录（2026-11-19 真机）**：①64px 字体缺空格 → 冒号闪烁（':'↔' '）时缺字方框——数字字体也要含空格;②**半角 ASCII 经 --symbols 传参实测会丢字符**（':' '/' 缺失 → 闹钟时间 "13:00" 冒号方框）——半角一律走 `--range 0x20-0x7E`;③LV_SYMBOL 图标不在中文字库 → 已把 FontAwesome（managed_components 自带 woff）按 lv_symbol_def.h 全部 60 个码位合并进 16/24px。

flash 占用：三文件合计 ~2MB（factory 8MB 分区充足;音频扩分区时一并重估）。

## 再生成步骤

```bash
# 1) 生成 GB2312 一级字符表（3755 字+UI 符号）
python gen_gb2312.py            # → gb2312_level1.txt

# 2) 转换（需 Node + npx lv_font_conv;字体源先放当前目录）
./gen_fonts.sh
```

变宽需求（新增字号）照 gen_fonts.sh 里三条命令的模板改 --size/--lv-font-name 即可;
改动字符集（加二级字库/生僻字）改 gen_gb2312.py 区位范围重跑。
