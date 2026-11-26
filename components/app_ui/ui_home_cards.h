#ifndef UI_HOME_CARDS_H
#define UI_HOME_CARDS_H

#include "lvgl.h"
#include "app_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 主页三槽卡片系统（U3b,2026-11-21 架构先行——卡片视觉待用户设计后填充,
 * 框架接口稳定:卡片=插件,只实现 create/refresh,样式随意换框架不动）
 *
 * 槽: 屏幕三等分(206px),每槽独立上下滑**循环**切换卡片,淡入 150ms;
 *     三槽默认显示 时钟2/天气/倒计时,同一卡片列表,索引独立=自由组合。
 * 手势: nav 层上下方向转发(app_ui_nav_set_vertical_cb),按起点 x 判槽;
 *       左右滑切页不受影响。主页 on_show 注册/on_hide 注销。
 * 数据: 卡片自带 refresh(1s 轮询当前显示卡);电池经 set_battery 推送快照。 */

typedef struct {
    const char *name;                              /* 卡名（日志/调试） */
    lv_obj_t *(*create)(lv_obj_t *parent);         /* 构建卡片内容（往槽容器填控件） */
    void (*refresh)(void);                         /* 刷新数据（1s 轮询当前显示的卡） */
    void (*destroy)(lv_obj_t *card);               /* 卡片层销毁前收尾,可 NULL（清零本卡持有的控件句柄,防悬空） */
} home_card_t;

/* 在主页 screen 上构建三槽+初始卡片（须持 LVGL 锁） */
void home_cards_build(lv_obj_t *screen);

/* 竖向手势入口（注册给 nav）：dir=TOP/BOTTOM,start_x=按下起点 → 判槽切换 */
void home_cards_on_gesture(lv_dir_t dir, int32_t start_x);

/* 1s 数据刷新（主页 timer 调;只刷新各槽当前显示的卡） */
void home_cards_tick(void);

/* 电池快照推送（app_ui 的 on_battery 事件回调转发;更新时钟卡状态行） */
void home_cards_set_battery(const ll_battery_state_t *st);

/* 网络状态钩子（U8 WiFi/S3 蓝牙接入后调用;须持 LVGL 锁）
 * wifi_ok=false: WiFi 图标右下橙色小叹号;bt_ok=false: 蓝牙图标整体隐藏 */
void home_cards_set_net(bool wifi_ok, bool bt_ok);

#ifdef __cplusplus
}
#endif

#endif /* UI_HOME_CARDS_H */
