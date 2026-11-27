/* psram_stack_test — 实验存档:任务栈放 PSRAM 是否可行（JS 方案"生死线",进度 §6.2/§6.8）
 *
 * 状态:①③ 已实测通过（2026-11-27,数字与原始日志见进度 §6.8"实测记录"）;测试代码已从固件
 * 移除,本文件不参与编译。②(别的任务写 flash,PST_TEST_FLASH)与 ④(XIP from PSRAM)未做,留待复用。
 *
 * 重新启用(临时,测完照旧移除):
 *   1. 本文件拷到 main/
 *   2. main/CMakeLists.txt:SRCS 加 "psram_stack_test.c";开 PST_TEST_FLASH 时 REQUIRES 再加 spi_flash
 *   3. main.c:声明 void psram_stack_test_start(void);,在 app_main 末尾调用
 *   4. 负载满负荷占 core 1 约 14s,必然触发任务看门狗告警(IDLE1 饿死,仅打印不复位),属预期
 *
 * 自包含:只创建自己的测试任务(全部钉在 core 1,不占 LVGL/WiFi 所在的 core 0),不调用
 * 任何业务模块、不订阅事件;开机 PST_START_MS 后开跑,打完汇总后任务自删、内存归还。
 *
 * 依据(ESP-IDF 官方文档 External RAM·限制,查证档案见进度 §6.8):flash cache 禁用
 * 期间(写 flash)片外 RAM 不可访问,故 IDF 默认不把任务栈放片外 RAM。sdkconfig 已开
 * CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM,须用 xTaskCreateStatic 自备片外栈缓冲。
 * PSRAM 栈任务全程零 flash 驱动接触;PST_TEST_FLASH=1 时的写 flash 由另一个内部栈
 * 任务代做(模拟"别的任务写 flash")。
 *
 * 测:① 正常跑(同一份负载 PSRAM 栈/内部栈各跑一遍,结果须一致)
 *     ② [默认关] 别的任务写 flash 期间本任务是否只是被安全暂停、停多久
 *     ③ 栈实际占用(高水位)、负载耗时比、任务切换往返延迟 */

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_memory_utils.h"

#define PST_TEST_FLASH   0   /* 1=加测②;需同时在 main/CMakeLists.txt 的 REQUIRES 加 spi_flash */
#if PST_TEST_FLASH
#include "esp_flash.h"
#include "esp_rom_sys.h"
#endif

static const char *TAG = "psram_stack";

#define PST_STACK_PSRAM  (16 * 1024)   /* PSRAM 栈:故意比业务任务大,量真实占用 */
#define PST_STACK_INT    (4 * 1024)    /* 内部栈(对照任务):内部 RAM 只剩十来 KB,省着用 */
#define PST_STACK_INT_S  (2 * 1024)    /* 内部栈(只做乒乓的小任务) */
#define PST_CORE         1             /* 钉在 core 1;core 0 跑 LVGL/WiFi */
#define PST_PRIO         2             /* 与 slow_tick 同级 */
#define PST_START_MS     8000          /* 等界面/WiFi 起稳再测 */
#define PST_ITERS        200000
#define PST_PING_N       500
#define PST_FILL         0xA5          /* 与 FreeRTOS 栈填充字节一致 → 高水位可测 */
#define PST_CAP_INT      (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#if PST_TEST_FLASH
#define PST_FLASH_ADDR   0x00900000    /* 分区表之外(nvs/phy/factory 止于 0x810000),不碰任何数据 */
#define PST_FLASH_LEN    0x1000        /* 一个 4KB 扇区:擦除=最长的关 cache 窗口 */
#define PST_FLASH_N      5
#define PST_FLASH_GAP_MS 500
#define PST_CHK_N        2000
#endif

/* 工作负载:递归+浮点+局部数组+格式化(不用 libm),故意把栈用深;两任务跑同一份代码 */
static float pst_workload(int depth, int n, char *buf, size_t len)
{
    volatile float acc[32];                       /* 128B 在栈上 */
    for (int i = 0; i < 32; i++) acc[i] = (float)(n + i) * 0.00390625f;
    float s = 0.0f;
    for (int i = 0; i < 32; i++) s += acc[i] * acc[(i * 7) & 31];
    if (depth > 0) {
        s += pst_workload(depth - 1, n + 1, buf, len);
    } else {
        snprintf(buf, len, "%08x", (unsigned)(s * 1024.0f) & 0xFFFFFFFFu);
    }
    return s;
}

static float pst_bench(int64_t *us_out)
{
    char buf[32];
    const int64_t t0 = esp_timer_get_time();
    float sum = 0.0f;
    for (int n = 0; n < PST_ITERS; n++) {
        sum += pst_workload(6, n, buf, sizeof(buf));
    }
    *us_out = esp_timer_get_time() - t0;
    return sum;
}

typedef struct { int64_t min, sum, max; } pst_ping_t;

static StaticTask_t s_tcb_a, s_tcb_b, s_tcb_c;
static StackType_t *s_stk_a, *s_stk_b, *s_stk_c;
static TaskHandle_t s_h_a, s_h_b, s_h_c;
static float s_sum_a, s_sum_b;
static int64_t s_us_a, s_us_b;
static unsigned s_hwm_a, s_hwm_b;
static pst_ping_t s_ping_ab, s_ping_cb;
#if PST_TEST_FLASH
static StaticTask_t s_tcb_d;
static StackType_t *s_stk_d;
static TaskHandle_t s_h_d;
static int64_t s_flash_op_us, s_stall_us;
static bool s_chk_ok;
static uint32_t s_flash_word;
#endif

/* B:内部栈对照任务——同一份负载;再分别与 A、C 做乒乓 */
static void pst_task_b(void *arg)
{
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);      /* 等 A 跑完负载 */
    s_sum_b = pst_bench(&s_us_b);
    s_hwm_b = uxTaskGetStackHighWaterMark(NULL);
    xTaskNotifyGive(s_h_a);

    for (int i = 0; i < PST_PING_N; i++) {        /* 与 A(PSRAM 栈)乒乓 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xTaskNotifyGive(s_h_a);
    }
    for (int i = 0; i < PST_PING_N; i++) {        /* 与 C(内部栈)乒乓 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xTaskNotifyGive(s_h_c);
    }
    for (;;) vTaskDelay(portMAX_DELAY);           /* 等 A 删除 */
}

/* C:内部栈小任务——只与 B 乒乓,给出"内部栈对内部栈"的切换基线 */
static void pst_task_c(void *arg)
{
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);      /* 等 A 放行 */
    s_ping_cb = (pst_ping_t){ INT64_MAX, 0, 0 };
    for (int i = 0; i < PST_PING_N; i++) {
        const int64_t t0 = esp_timer_get_time();
        xTaskNotifyGive(s_h_b);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const int64_t dt = esp_timer_get_time() - t0;
        if (dt < s_ping_cb.min) s_ping_cb.min = dt;
        if (dt > s_ping_cb.max) s_ping_cb.max = dt;
        s_ping_cb.sum += dt;
    }
    xTaskNotifyGive(s_h_a);
    for (;;) vTaskDelay(portMAX_DELAY);           /* 等 A 删除 */
}

#if PST_TEST_FLASH
/* D:内部栈任务——替"别的任务"写 flash(分区表外的空闲扇区) */
static void pst_task_d(void *arg)
{
    (void)arg;
    uint32_t *buf = heap_caps_malloc(PST_FLASH_LEN, PST_CAP_INT);
    for (int i = 0; buf && i < PST_FLASH_N; i++) {
        vTaskDelay(pdMS_TO_TICKS(PST_FLASH_GAP_MS));
        buf[0] = s_flash_word++;                  /* 每次内容不同 */
        const int64_t t0 = esp_timer_get_time();
        const esp_err_t e1 = esp_flash_erase_region(esp_flash_default_chip, PST_FLASH_ADDR, PST_FLASH_LEN);
        const esp_err_t e2 = (e1 == ESP_OK)
            ? esp_flash_write(esp_flash_default_chip, buf, PST_FLASH_ADDR, PST_FLASH_LEN) : e1;
        const int64_t dt = esp_timer_get_time() - t0;
        if (dt > s_flash_op_us) s_flash_op_us = dt;
        ESP_LOGI(TAG, "D: flash op %d/%d %s (%lld ms)", i + 1, PST_FLASH_N,
                 (e2 == ESP_OK) ? "ok" : esp_err_to_name(e2), (long long)(dt / 1000));
    }
    heap_caps_free(buf);
    xTaskNotifyGive(s_h_a);                       /* D:写完了 */
    for (;;) vTaskDelay(portMAX_DELAY);           /* 等 A 删除 */
}
#endif

/* A 自删后自己的 PSRAM 栈无人释放 → 交给一次性 esp_timer 回收 */
static void pst_free_stk_a(void *arg)
{
    (void)arg;
    heap_caps_free(s_stk_a);
    s_stk_a = NULL;
}

/* A:PSRAM 栈任务——主角 */
static void pst_task_a(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(PST_START_MS));      /* 等界面/WiFi 起稳 */

    int probe = 0;
    ESP_LOGI(TAG, "A: stack in PSRAM? %s (sp=%p)",
             esp_ptr_external_ram(&probe) ? "YES" : "NO", (void *)&probe);

    /* ①③ 同一份负载 */
    s_sum_a = pst_bench(&s_us_a);
    s_hwm_a = uxTaskGetStackHighWaterMark(NULL);
    xTaskNotifyGive(s_h_b);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);      /* 等 B 跑完 */

    /* ③ 切换延迟:PSRAM 栈(A) <-> 内部栈(B) */
    s_ping_ab = (pst_ping_t){ INT64_MAX, 0, 0 };
    for (int i = 0; i < PST_PING_N; i++) {
        const int64_t t0 = esp_timer_get_time();
        xTaskNotifyGive(s_h_b);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const int64_t dt = esp_timer_get_time() - t0;
        if (dt < s_ping_ab.min) s_ping_ab.min = dt;
        if (dt > s_ping_ab.max) s_ping_ab.max = dt;
        s_ping_ab.sum += dt;
    }
    xTaskNotifyGive(s_h_c);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);      /* 等 C 完成 */

    vTaskDelete(s_h_b);                           /* 对照任务用完即删,内部栈立即归还 */
    vTaskDelete(s_h_c);
    heap_caps_free(s_stk_b);
    heap_caps_free(s_stk_c);

#if PST_TEST_FLASH
    /* ②:建 D 写 flash;本任务每 1ms 一拍量最大停顿,每 50 拍复算一次校验和 */
    s_stk_d = heap_caps_malloc(PST_STACK_INT, PST_CAP_INT);
    if (s_stk_d) {
        memset(s_stk_d, PST_FILL, PST_STACK_INT);
        s_h_d = xTaskCreateStaticPinnedToCore(pst_task_d, "pst_d", PST_STACK_INT, NULL,
                                              PST_PRIO, s_stk_d, &s_tcb_d, PST_CORE);
    }
    if (s_h_d) {
        char buf[32];
        float base = 0.0f;
        for (int n = 0; n < PST_CHK_N; n++) base += pst_workload(6, n, buf, sizeof(buf));
        s_chk_ok = true;
        int64_t last = esp_timer_get_time();
        for (uint32_t beat = 1;; beat++) {
            esp_rom_delay_us(1000);
            const int64_t now = esp_timer_get_time();
            if (now - last > s_stall_us) s_stall_us = now - last;
            last = now;
            if (beat % 50 == 0) {
                float s = 0.0f;
                for (int n = 0; n < PST_CHK_N; n++) s += pst_workload(6, n, buf, sizeof(buf));
                if (s != base) s_chk_ok = false;
                if (ulTaskNotifyTake(pdTRUE, 0)) break;   /* D 写完了 */
            }
        }
        vTaskDelete(s_h_d);
        heap_caps_free(s_stk_d);
    }
#endif

    ESP_LOGI(TAG, "==== RESULT ====");
    ESP_LOGI(TAG, "[1] workload x%d: PSRAM-stack %lld ms | internal-stack %lld ms | ratio %.2f",
             PST_ITERS, (long long)(s_us_a / 1000), (long long)(s_us_b / 1000),
             s_us_b ? (double)s_us_a / (double)s_us_b : 0.0);
    ESP_LOGI(TAG, "[1] results identical? %s", (s_sum_a == s_sum_b) ? "yes" : "NO");
    ESP_LOGI(TAG, "[3] stack used (high-water): PSRAM-stack %u / %u B | internal-stack %u / %u B",
             (unsigned)(PST_STACK_PSRAM - s_hwm_a), (unsigned)PST_STACK_PSRAM,
             (unsigned)(PST_STACK_INT - s_hwm_b), (unsigned)PST_STACK_INT);
    ESP_LOGI(TAG, "[3] notify round-trip us (min/avg/max): PSRAM<->int %lld/%.1f/%lld | int<->int %lld/%.1f/%lld",
             (long long)s_ping_ab.min, (double)s_ping_ab.sum / PST_PING_N, (long long)s_ping_ab.max,
             (long long)s_ping_cb.min, (double)s_ping_cb.sum / PST_PING_N, (long long)s_ping_cb.max);
#if PST_TEST_FLASH
    ESP_LOGI(TAG, "[2] survived %d x erase+write: longest flash op %lld ms | longest stall of A %lld ms | checksum %s",
             PST_FLASH_N, (long long)(s_flash_op_us / 1000), (long long)(s_stall_us / 1000),
             s_chk_ok ? "ok" : "MISMATCH");
#endif
    ESP_LOGI(TAG, "==== DONE ====");

    esp_timer_handle_t t = NULL;
    const esp_timer_create_args_t ta = { .callback = pst_free_stk_a, .name = "pst_free" };
    if (esp_timer_create(&ta, &t) == ESP_OK) {
        esp_timer_start_once(t, 500ULL * 1000);           /* 500 ms */
    }
    vTaskDelete(NULL);
}

void psram_stack_test_start(void)
{
    s_stk_a = heap_caps_malloc(PST_STACK_PSRAM, MALLOC_CAP_SPIRAM);
    s_stk_b = heap_caps_malloc(PST_STACK_INT, PST_CAP_INT);
    s_stk_c = heap_caps_malloc(PST_STACK_INT_S, PST_CAP_INT);
    if (!s_stk_a || !s_stk_b || !s_stk_c) {
        ESP_LOGE(TAG, "alloc failed (a=%p b=%p c=%p) - test skipped", s_stk_a, s_stk_b, s_stk_c);
        heap_caps_free(s_stk_a); heap_caps_free(s_stk_b); heap_caps_free(s_stk_c);
        return;
    }
    memset(s_stk_a, PST_FILL, PST_STACK_PSRAM);   /* 先铺 0xA5,高水位才量得准 */
    memset(s_stk_b, PST_FILL, PST_STACK_INT);
    memset(s_stk_c, PST_FILL, PST_STACK_INT_S);

    s_h_b = xTaskCreateStaticPinnedToCore(pst_task_b, "pst_b", PST_STACK_INT, NULL,
                                          PST_PRIO, s_stk_b, &s_tcb_b, PST_CORE);
    s_h_c = xTaskCreateStaticPinnedToCore(pst_task_c, "pst_c", PST_STACK_INT_S, NULL,
                                          PST_PRIO, s_stk_c, &s_tcb_c, PST_CORE);
    s_h_a = xTaskCreateStaticPinnedToCore(pst_task_a, "pst_a", PST_STACK_PSRAM, NULL,
                                          PST_PRIO, s_stk_a, &s_tcb_a, PST_CORE);
    if (!s_h_a || !s_h_b || !s_h_c) {
        ESP_LOGE(TAG, "task create failed (a=%p b=%p c=%p)", s_h_a, s_h_b, s_h_c);
        if (s_h_b) vTaskDelete(s_h_b);
        if (s_h_c) vTaskDelete(s_h_c);
        heap_caps_free(s_stk_a); heap_caps_free(s_stk_b); heap_caps_free(s_stk_c);
        return;
    }
    ESP_LOGI(TAG, "test tasks created (core %d, start in %d ms)", PST_CORE, PST_START_MS);
}
