/* app_motion — 姿态服务（S1.5）
 * 轮询 QMI8658 加速度计 → 横放方向判定（左横/右横）→ 防抖后切换屏幕旋转并广播
 * LL_EV_ORIENTATION。竖放/晃动手势为后续阶段（页面限定,见需求）。
 *
 * S1.5 校准开关：MOTION_AUTO_ROTATE=0 时只打印判定日志不实际旋转——
 * 与诊断页联测,由串口日志的 ax/ay/az 读数一次定死下方两个轴映射宏,
 * 置 1 后方向自适应正式生效。 */

#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"

#include "app_motion.h"
#include "app_event.h"
#include "bsp_imu.h"
#include "bsp_lcd.h"

static const char *TAG = "app_motion";

/* ---- 轴映射（S1.5 真机标定,2026-11-10）----
 * 屏幕长边(640)对应 IMU 的 Y 轴（实测:横拿时 |ay|≈0.7~1.0,掉头反号）。
 * ay>0 = USB 左横拿（画面正）→ ROTATION_90;ay<0 = 掉头 → ROTATION_270。 */
#define ORIENT_LONG_AXIS_IS_X   0                          /* 长边 ∥ IMU Y 轴 */
#define ORIENT_MAP_POS          LV_DISPLAY_ROTATION_90     /* ay+ → 左横(USB 左) */
#define ORIENT_MAP_NEG          LV_DISPLAY_ROTATION_270    /* ay− → 右横 */

#define MOTION_POLL_PERIOD_MS   200     /* 轮询周期 */
#define MOTION_CONFIRM_CNT      3       /* 防抖:连续 N 次判定一致且与当前方向不同才切换 */
#define MOTION_LOG_DIV          50      /* 每 N 次采样打 1 条日志（10s 一条,正式运行低噪） */
#define MOTION_AUTO_ROTATE      1       /* 方向自适应已生效（轴映射已标定） */

/* 横放判定阈值:重力主要落在长边轴上（平放/竖放/斜置不判定） */
#define AXIS_LONG_THR_G         0.70f
#define AXIS_CROSS_THR_G        0.40f

/* 由重力向量判定横放方向。横放返回 true 并给出目标旋转方向 */
static bool accel_to_orientation(float ax, float ay, float az,
                                 lv_display_rotation_t *out)
{
    float along, cross;
#if ORIENT_LONG_AXIS_IS_X
    along = ax; cross = ay;
#else
    along = ay; cross = ax;
#endif
    (void)az;
    if (fabsf(along) < AXIS_LONG_THR_G || fabsf(cross) > AXIS_CROSS_THR_G) {
        return false;   /* 平放/竖放/斜置:不判定 */
    }
    *out = (along > 0) ? ORIENT_MAP_POS : ORIENT_MAP_NEG;
    return true;
}

static void motion_task(void *arg)
{
    int confirm = 0;
    lv_display_rotation_t want = LV_DISPLAY_ROTATION_0;
    int n = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(MOTION_POLL_PERIOD_MS));

        float ax, ay, az;
        if (bsp_imu_read_accel(&ax, &ay, &az) != ESP_OK) {
            continue;
        }

        lv_display_rotation_t target;
        const bool is_landscape = accel_to_orientation(ax, ay, az, &target);

        /* 低频日志:横放时给出轴读数与判定,供 S1.5 轴映射标定 */
        if ((++n % MOTION_LOG_DIV) == 0) {
            ESP_LOGI(TAG, "a=[%.2f %.2f %.2f] %s",
                     (double)ax, (double)ay, (double)az,
                     is_landscape ? (target == LV_DISPLAY_ROTATION_90 ? "LAND-90" : "LAND-270")
                                  : "flat/unknown");
        }

        if (!is_landscape) {
            confirm = 0;
            continue;
        }
        if (target == want) confirm++;
        else { want = target; confirm = 1; }

        if (confirm >= MOTION_CONFIRM_CNT && want != bsp_lcd_get_rotation()) {
#if MOTION_AUTO_ROTATE
            if (bsp_lcd_lock(200)) {
                bsp_lcd_set_rotation(want);
                bsp_lcd_unlock();
            }
            app_event_post(LL_EV_ORIENTATION, &want, sizeof(want), 0);
            ESP_LOGI(TAG, "orientation -> %d", (int)want);
#else
            /* 标定模式:打一条即可,避免刷屏;确认计数复位 */
            ESP_LOGI(TAG, "orientation would -> %d (auto-rotate off, cur=%d)",
                     (int)want, (int)bsp_lcd_get_rotation());
            confirm = 0;
#endif
        }
    }
}

esp_err_t app_motion_init(void)
{
    /* IMU 失败不阻塞整机（姿态功能降级,其余服务不受影响） */
    esp_err_t err = bsp_imu_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "imu init failed, motion service disabled: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    if (xTaskCreate(motion_task, "motion", 3 * 1024, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "create task failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "motion service running (poll %dms, auto-rotate %s)",
             MOTION_POLL_PERIOD_MS, MOTION_AUTO_ROTATE ? "on" : "off");
    return ESP_OK;
}
