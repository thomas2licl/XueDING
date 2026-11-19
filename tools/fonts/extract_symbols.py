# -*- coding: utf-8 -*-
"""从 lv_symbol_def.h 提取全部 LV_SYMBOL 图标码位,输出 fa_ranges.txt（gen_fonts.sh 用）。"""
import re

src = r"..\..\managed_components\lvgl__lvgl\src\font\lv_symbol_def.h"
text = open(src, encoding='utf-8').read()

# 行格式: #define LV_SYMBOL_XXX "\xEF\x80\x81" /*61441, 0xF001*/
codes = sorted(set(int(m, 16) for m in re.findall(r'/\*\d+, (0xF[0-9A-Fa-f]+)\*/', text)))

# 合并连续段,缩短 --range 参数
ranges, start, prev = [], None, None
for c in codes:
    if start is None:
        start = prev = c
    elif c == prev + 1:
        prev = c
    else:
        ranges.append((start, prev))
        start = prev = c
ranges.append((start, prev))

with open("fa_ranges.txt", "w") as f:
    f.write(",".join(f"0x{a:X}" if a == b else f"0x{a:X}-0x{b:X}" for a, b in ranges))
print(f"{len(codes)} symbols -> fa_ranges.txt")
