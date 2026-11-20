#ifndef UI_PAGE_HOURGLASS_H
#define UI_PAGE_HOURGLASS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 沙漏页（一级页 7/9,U5）——设置态档位/运行态漏沙联动;竖放手势启动（本页+IDLE） */
lv_obj_t *ui_hourglass_create(void);
void ui_hourglass_on_show(void);
void ui_hourglass_on_hide(void);

/* 本页在 nav 页表中的下标（app_ui.c k_pages 顺序定稿:主页0..设置8） */
#define UI_PAGE_IDX_HOURGLASS   6

#ifdef __cplusplus
}
#endif

#endif /* UI_PAGE_HOURGLASS_H */
