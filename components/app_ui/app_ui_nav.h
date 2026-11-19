#ifndef APP_UI_NAV_H
#define APP_UI_NAV_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_ui_nav — 横滑页面导航框架（U1,UI 跟踪文档 §1,2026-11-19 定稿）
 *
 * 一级页面平级横滑：手势触发 + 整页 MOVE 动画,线性不循环（到头即停）。
 * 页面常驻不销毁（auto_del=false）,隐藏页挂起刷新 timer、显示页恢复
 * （on_show/on_hide 钩子;动画类页面必须使用,静态页可空置）。
 * 页点指示器置于 top layer（非 clickable,不挡触摸、不随切页滑动）。
 *
 * 机制要点（LVGL 9.5 源码级确认,勿凭记忆改）：
 *  - 手势=经典 gesture（indev.c indev_gesture:累计位移>50px 触发一次;
 *    正被滚动控件消费时不触发——"滑动控件优先"由 LVGL 机制天然保证）;
 *    GESTURE 事件发手指所按对象、不冒泡到 screen（需 LV_OBJ_FLAG_GESTURE_BUBBLE）,
 *    故本框架订阅 indev 级事件（lv_indev_add_event_cb）统一接收。
 *  - MOVE_LEFT=新屏从右滑入（lv_display.c:新屏 x +W→0,旧屏 0→-W）,
 *    即手指左滑→下一页用 MOVE_LEFT,右滑→上一页用 MOVE_RIGHT。
 *  - 调用方须已持 LVGL 锁（init/gesture 回调均在 LVGL 任务上下文）。 */

#define UI_PAGE_COUNT       9       /* 页面顺序定稿见 UI 跟踪 §1,占位页随功能逐页替换 */
#define UI_NAV_ANIM_MS      200     /* 切页动画时长（交互规范 §6.1:180~220ms） */
#define UI_NAV_ANIM_BUSY_MS (UI_NAV_ANIM_MS + 50)  /* 动画窗口内忽略新手势 */

typedef struct {
    const char *name;                          /* 页名（日志/占位页显示） */
    lv_obj_t *(*create)(void);                 /* 创建独立 screen（parent=NULL） */
    void (*on_show)(void);                     /* 显示时恢复（timer resume 等,可选） */
    void (*on_hide)(void);                     /* 隐藏时挂起（timer pause 等,可选） */
} ui_page_def_t;

/* 注册页面表并瞬时载入第 0 页（开机后首次进入,无动画） */
void app_ui_nav_init(const ui_page_def_t *pages, int count);

/* 当前页索引（0 起） */
int app_ui_nav_current(void);

/* 程序切页（语音/场景联动用;方向语义同手势:animate=true 用 MOVE 动画） */
void app_ui_nav_goto(int idx, bool animate);

/* 占位页工厂（真页面随 U3~U11 逐页替换;中文字体引入后页面名换中文） */
lv_obj_t *app_ui_nav_placeholder(const char *name, const char *symbol);

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_NAV_H */
