# -*- coding: utf-8 -*-
"""生成 GB2312 一级字库字符表（3755 常用字,区位码 16-55 区）+ UI 常用符号。
输出:gb2312_level1.txt（一行,lv_font_conv --symbols 直接喂）。"""

# GB2312 一级汉字:区位 16 区(0xB0)~55 区(0xD7 前段),每区 94 位
chars = []
for qu in range(0xB0, 0xD8):            # 0xB0~0xD7 = 区 16~55
    for wei in range(0xA1, 0xFF):       # 位 1~94（0xA1~0xFE）
        try:
            ch = bytes([qu, wei]).decode('gb2312')
            chars.append(ch)
        except UnicodeDecodeError:
            pass

# UI 常用中文标点/符号（GB2312 一级不含全角数字/部分标点,补齐）
extras = "，。、：；！？（）【】《》·—…“”‘’％℃°+-×÷＝/:.0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz "
chars += [c for c in extras if c not in chars]

text = ''.join(chars)
with open(r'C:\Users\77166\AppData\Local\Temp\gb2312_level1.txt', 'w', encoding='utf-8') as f:
    f.write(text)
print(f"total {len(chars)} chars, level1={len([c for c in chars if ord(c) > 0x4DBF])}")
