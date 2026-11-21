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
#define UI_NAV_ANIM_MS      280     /* 切页动画时长（200 匀速偏快无过程感,280+ease-out 真机定,2026-11-21） */
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

/* ---- 二级页面（页面栈,最深一级;需求 §四:两级导航）----
 * 二级页由功能页创建并持有（常驻,不销毁）;页面自身须提供左上返回按钮调 pop,
 * nav 层补充"起点在左边缘(≤UI_NAV_EDGE_PX)的右滑=返回"手势。
 * 起点计算:当前点 - gesture_sum（该值每次按下清零,lv_indev.c indev_proc_release）。 */
#define UI_NAV_EDGE_PX  40      /* 左边缘手势判定区宽度 */

void app_ui_nav_push(lv_obj_t *scr);   /* 进入二级页（从右滑入,一级页触发 on_hide） */
bool app_ui_nav_pop(void);             /* 返回一级页（向右滑出,触发 on_show）;无二级页返回 false */
bool app_ui_nav_is_secondary(void);    /* 当前是否显示二级页 */

/* 占位页工厂（真页面随 U3~U11 逐页替换;中文字体引入后页面名换中文） */
lv_obj_t *app_ui_nav_placeholder(const char *name, const char *symbol);

/* ---- 竖向手势转发（U3b 主页三槽卡片）----
 * 上下方向手势默认忽略;页面可注册本回调接管（如主页卡片上下滑切换）。
 * 应在 on_show 注册、on_hide 注销;start_x=按下起点 x（槽位判定用）。 */
void app_ui_nav_set_vertical_cb(void (*cb)(lv_dir_t dir, int32_t start_x));

#ifdef __cplusplus
}
#endif

#endif /* APP_UI_NAV_H */
