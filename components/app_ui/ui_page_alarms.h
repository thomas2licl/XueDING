#ifndef UI_PAGE_ALARMS_H
#define UI_PAGE_ALARMS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 闹钟页（一级页 3/9,U4）——列表+开关+新建入口;新建闹钟为二级页（页面内自持） */
lv_obj_t *ui_alarms_create(void);
void ui_alarms_on_show(void);       /* 显示时重读闹钟列表 */

#ifdef __cplusplus
}
#endif

#endif /* UI_PAGE_ALARMS_H */
