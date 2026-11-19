#!/bin/sh
# 字体转换（在 tools/fonts/ 目录执行;需 Node 环境 + npx lv_font_conv）
# 字体源1（中文）:思源黑体 Google 版（Noto Sans SC）静态 Regular
#   下载:https://github.com/notofonts/noto-cjk/raw/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf
# 字体源2（图标）:LVGL 自带 FontAwesome5,直接用 managed_components 里的（无需下载）
#   managed_components/lvgl__lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff
# 本机可变字体兜底:NotoSansSC-VF.ttf（VF 对 lv_font_conv 支持不稳,优先静态版）
#
# 教训（2026-11-19 真机）:
#   - 64px 数字字体必须含空格——冒号闪烁用 ' ' 替换 ':',字库无空格会画缺字方框
#   - **半角 ASCII 不能靠 --symbols 传**（实测 ':' '/' 丢失→缺字方框）,必须 --range 0x20-0x7E 显式全量
#   - LV_SYMBOL_*(0xFxxx)已合并进 16/24px 中文字体,图标+中文可单 label 混排

set -e
SYMS=$(cat gb2312_level1.txt)
FA=$(cat fa_ranges.txt)   # 由 extract_symbols.py 从 lv_symbol_def.h 生成
FAW=../../managed_components/lvgl__lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff

# 16px 中文全字库+全量 ASCII+图标（辅助文字）
npx lv_font_conv --font NotoSansSC-Regular.otf --size 16 --bpp 4 \
  --range 0x20-0x7E --symbols "$SYMS" --font "$FAW" --range "$FA" \
  --format lvgl --lv-font-name noto_sans_sc_16 --no-compress \
  -o ../../components/app_ui/fonts/font_noto_sc_16.c

# 24px 中文全字库+全量 ASCII+图标（标题/正文）
npx lv_font_conv --font NotoSansSC-Regular.otf --size 24 --bpp 4 \
  --range 0x20-0x7E --symbols "$SYMS" --font "$FAW" --range "$FA" \
  --format lvgl --lv-font-name noto_sans_sc_24 --no-compress \
  -o ../../components/app_ui/fonts/font_noto_sc_24.c

# 64px 数字（主页时间;Noto 数字默认表格数字=等宽 adv_w 568;含空格供冒号闪烁）
npx lv_font_conv --font NotoSansSC-Regular.otf --size 64 --bpp 4 \
  --symbols " 0123456789:%-+" --format lvgl --lv-font-name noto_num_64 --no-compress \
  -o ../../components/app_ui/fonts/font_noto_num_64.c

echo done
