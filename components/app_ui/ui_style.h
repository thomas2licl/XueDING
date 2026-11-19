#ifndef UI_STYLE_H
#define UI_STYLE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UI 视觉规范共享定义（XueDING 交互规范 V1.1 §三,所有页面统一引用）
 * 2026-11-19 起逐页对齐设计稿;改色值只改这里。 */

/* ---- 配色（交互规范 §3.1 基础色彩） ---- */
#define UI_COL_BG      0x080A0E     /* 主背景 */
#define UI_COL_PANEL   0x111722     /* 次级背景（选中区/局部底色） */
#define UI_COL_FG      0xF3F5F7     /* 主文字 */
#define UI_COL_DIM     0xA0A9B6     /* 次级文字 */
#define UI_COL_WEAK    0x626D7B     /* 弱化文字/未选中页点 */
#define UI_COL_BRAND   0x3988E8     /* 品牌蓝（选中/重点交互） */
#define UI_COL_SKY     0x65A8FF     /* 浅蓝（天气图标/动态） */
#define UI_COL_GREEN   0x42C98A     /* 正常绿 */
#define UI_COL_ORANGE  0xF2A64A     /* 提醒橙 */
#define UI_COL_RED     0xF06464     /* 警告红 */

/* ---- 字体（U2,fonts/ 目录） ---- */
LV_FONT_DECLARE(noto_sans_sc_16);
LV_FONT_DECLARE(noto_sans_sc_24);
LV_FONT_DECLARE(noto_num_64);

/* ---- 布局常量（交互规范 §2.1） ---- */
#define UI_MARGIN_X    12           /* 左右安全边距 */
#define UI_MARGIN_Y    8            /* 上下安全边距 */

/* 常用组合快捷（页面内直接 set） */
#define UI_FONT_TEXT   (&noto_sans_sc_16)    /* 辅助文字 */
#define UI_FONT_TITLE  (&noto_sans_sc_24)    /* 标题/正文 */

#ifdef __cplusplus
}
#endif

#endif /* UI_STYLE_H */
