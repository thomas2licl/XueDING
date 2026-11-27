/* psram_stack_test — 实验存档:任务栈放 PSRAM 是否可行（JS 方案"生死线",进度 §6.2/§6.8）
 *
 * 状态（2026-11-27,本板 COM9 实测;数字与原始日志见进度 §6.8"实测记录"）:
 *   ① 正常跑        通过:PSRAM 栈任务与内部栈对照任务跑同一份负载,结果逐位相同,慢约 1%
 *   ② 别的任务写 flash 通过:核 0 写、核 1 写（写时屏幕暂停）各 5 次;掐在界面一帧的中段写
 *                     两遍分别 5 次、8 次撞上——PSRAM 栈任务最长被停 28ms,数据与栈完好,
 *                     界面不卡不崩,被撞的帧只慢约 26ms
 *   ③ 栈占用/切换   通过:该负载峰值 ~3.2KB;任务切换往返 30µs,与内部栈无差别
 *   未做:④ XIP from PSRAM。"PSRAM 栈任务自己调 flash 驱动必崩"是官方文档结论,未拿板子撞。
 * 测试代码已从固件移除,本文件不参与编译。
 *
 * 重新启用（临时,测完照旧移除）:
 *   1. 本文件拷到 main/
 *   2. main/CMakeLists.txt:SRCS 加 "psram_stack_test.c",REQUIRES 加 spi_flash（bsp_lcd 已有）
 *   3. main.c:声明 void psram_stack_test_start(void);,在 app_main 末尾调用
 *   4. 开机 8s 后起跑:完整跑约 50s,只跑加长段约 16s。每次都会真写 flash（空白扇区 0x00900000）
 *
 * 跑法由 PST_ONLY_PHASE3 选:
 *   0 = 完整跑 ① 正常跑 + ② 别的任务写 flash（四段）+ ③ 栈占用/切换延迟
 *   1 = 只跑加长段:写 flash 每次都掐在界面一帧的中段,专看"写 flash 撞上屏幕往面板搬数据"会怎样
 *
 * 自包含:只创建自己的测试任务（A=PSRAM 栈主角,B/C=①③ 的内部栈对照,D=写 flash 的内部栈
 * 任务）,不调用业务模块、不订阅事件。唯一借用的是界面的锁 bsp_lcd_lock():
 *   - screen_paused 段:写 flash 期间拿住它,让屏幕暂停刷新,以隔离"撞上屏幕 DMA（缓冲在 PSRAM）"
 *   - wait_busy 段:反过来,等界面锁被占（界面正在干活）之后再等 PST_BUSY_LEAD_MS 毫秒才动手——
 *     一帧的活 = 读触摸 → 画图 → 往屏幕搬数据,刚开工时还没开始搬
 *
 * 实验结束后:本文件、main.c 的启动行、main/CMakeLists.txt 的源文件项与 spi_flash 依赖一并移除。
 *
 * flash 写入位置 0x00900000:分区表（nvs/phy_init/factory,factory 止于 0x810000）之外、
 * 16MB flash 之内的空白区;每次擦除+写入同一个 4KB 扇区,读回核对。
 *
 * 依据（ESP-IDF 官方文档 External RAM·限制,查证档案见进度 §6.8）:flash cache 禁用期间片外
 * RAM 不可访问,故 IDF 默认不把任务栈放片外 RAM。sdkconfig 已开
 * CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM,须用 xTaskCreateStatic 自备片外栈缓冲。
 * 本实验里写 flash 的一律是内部栈任务 D——"PSRAM 栈任务自己调 flash 驱动必崩"不在实测范围,
 * 那条是官方文档明确的结论,不拿板子去撞。 */

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_memory_utils.h"
#include "esp_flash.h"

#include "bsp_lcd.h"

static const char *TAG = "psram_stack";

#define PST_ONLY_PHASE3  0             /* 见文件头;0=完整跑,1=只跑加长段 */

#define PST_STACK_PSRAM  (16 * 1024)   /* A 的栈:PSRAM,故意比业务任务大 */
#define PST_STACK_INT    (4 * 1024)    /* B:内部栈对照任务 */
#define PST_STACK_INT_S  (2 * 1024)    /* C:只做乒乓的小任务 */
#define PST_STACK_D      (3 * 1024)    /* D:写 flash 的内部栈任务 */
#define PST_CORE         1             /* A/B/C 钉在 core 1;core 0 跑 LVGL/WiFi */
#define PST_PRIO         2             /* 与 slow_tick 同级 */
#define PST_D_PRIO       3             /* D 比 A 高:到点立刻插进去写 */
#define PST_START_MS     8000          /* 等界面/WiFi 起稳再测 */
#define PST_ITERS        200000
#define PST_PING_N       500
#define PST_FILL         0xA5          /* 与 FreeRTOS 栈填充字节一致 → 高水位可测 */
#define PST_CAP_INT      (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

/* ② 写 flash */
#define PST_FLASH_ADDR   0x00900000    /* 分区表之外的空白扇区 */
#define PST_FLASH_LEN    0x1000        /* 一个 4KB 扇区:擦除=最长的关 cache 窗口 */
#define PST_FLASH_RD     1024          /* 读回核对只比对前 1KB（省内部 RAM） */
#define PST_FLASH_GAP_MS 500
#define PST_BUSY_WAIT_MS 300           /* wait_busy 段:最多等界面忙这么久 */
#define PST_BUSY_LEAD_MS 12            /* wait_busy 段:发现界面忙之后,再等这么久才动手 */
#define PST_MID_TAIL_MS  10            /* 写完后界面还要忙 ≥ 这么久 = 写的时候它在帧的中段 */
#define PST_CHK_N        400           /* 一块 = 400 次负载 ≈ 15ms */
#define PST_CHUNK_BUSY   4             /* 每 4 轮睡 10ms:IDLE 能跑,D 能插进来 */
#define PST_GUARD_MAGIC  0x600D0000u

typedef struct { int core; bool screen_paused; bool wait_busy; int ops; } pst_phase_t;
#if PST_ONLY_PHASE3
static const pst_phase_t PST_PHASES[] = {
    /* 核 0 写,不管屏幕,掐在界面一帧的中段写:与"往屏幕搬数据"必撞 */
    { 0, false, true, 12 },
};
#else
static const pst_phase_t PST_PHASES[] = {
    { 0, true,  false, 5 },     /* 核 0 写,写时屏幕暂停 */
    { 1, true,  false, 5 },     /* 核 1 写（A 同核）,写时屏幕暂停 */
    { 0, false, false, 3 },     /* 核 0 写,不管屏幕:真实场景 */
    { 0, false, true,  12 },    /* 核 0 写,掐在界面一帧的中段:与"往屏幕搬数据"必撞 */
};
#endif
#define PST_PHASE_N ((int)(sizeof(PST_PHASES) / sizeof(PST_PHASES[0])))

/* 工作负载:递归+浮点+局部数组+格式化（不用 libm）,故意把栈用深;各任务跑同一份代码 */
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

#if !PST_ONLY_PHASE3
static float pst_bench(int64_t *us_out)
{
    char buf[32];
    const int64_t t0 = esp_timer_get_time();
    float sum = 0.0f;
    for (int n = 0; n < PST_ITERS; n++) {
        sum += pst_workload(6, n, buf, sizeof(buf));
        if ((n % 20000) == 19999) vTaskDelay(1);  /* 定期让出 CPU,免得饿死 IDLE 触发看门狗 */
    }
    *us_out = esp_timer_get_time() - t0;
    return sum;
}
#endif

/* ② 用的小块负载:同一块先后算两遍,两遍结果必须相同（中间被暂停过也不许变） */
static float pst_chunk(uint32_t k, char *buf)
{
    float s = 0.0f;
    for (int i = 0; i < PST_CHK_N; i++) {
        s += pst_workload(6, (int)((k * 131u + (uint32_t)i) & 0xFFFFFu), buf, 32);
    }
    return s;
}

#if !PST_ONLY_PHASE3
typedef struct { int64_t min, sum, max; } pst_ping_t;
#endif

static StaticTask_t s_tcb_a, s_tcb_d;
static StackType_t *s_stk_a, *s_stk_d;
static TaskHandle_t s_h_a, s_h_d;
#if !PST_ONLY_PHASE3
static StaticTask_t s_tcb_b, s_tcb_c;
static StackType_t *s_stk_b, *s_stk_c;
static TaskHandle_t s_h_b, s_h_c;
static float s_sum_a, s_sum_b;
static int64_t s_us_a, s_us_b;
static unsigned s_hwm_a, s_hwm_b;
static pst_ping_t s_ping_ab, s_ping_cb;

/* B:内部栈对照任务——同一份负载;再分别与 A、C 做乒乓 */
static void pst_task_b(void *arg)
{
    (void)arg;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);      /* 等 A 跑完负载 */
    s_sum_b = pst_bench(&s_us_b);
    s_hwm_b = uxTaskGetStackHighWaterMark(NULL);
    xTaskNotifyGive(s_h_a);

    for (int i = 0; i < PST_PING_N; i++) {        /* 与 A（PSRAM 栈）乒乓 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xTaskNotifyGive(s_h_a);
    }
    for (int i = 0; i < PST_PING_N; i++) {        /* 与 C（内部栈）乒乓 */
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
#endif /* !PST_ONLY_PHASE3 */

/* ② 的阶段状态（A 设置,D 读）与结果（D 写,A 读） */
static pst_phase_t s_ph;
static int64_t s_er_us, s_wr_us;      /* 本阶段最长的一次擦除/写入 */
static esp_err_t s_d_err;
static int s_ops_ok;                  /* 擦+写+读回核对全部通过的次数 */
static int s_busy_hits;               /* wait_busy 段:动手那一刻界面确实在忙的次数 */
static int s_mid_hits;                /* 其中写完后界面还要忙 ≥PST_MID_TAIL_MS 的次数（撞在帧的中段） */
static uint32_t s_flash_word = 0x5A000000u;

/* D:内部栈任务——替"别的任务"写 flash（分区表外的空白扇区）。
 * screen_paused:写之前先拿界面锁——此刻屏幕一定不在刷新（没有从 PSRAM 出发的 DMA）。
 * wait_busy:相反,等界面锁被占（界面正在干活）、再过 PST_BUSY_LEAD_MS 毫秒才动手。 */
static void pst_task_d(void *arg)
{
    (void)arg;
    uint32_t *buf = heap_caps_malloc(PST_FLASH_LEN, PST_CAP_INT);   /* 写 flash 期间缓冲须在内部 RAM */
    uint32_t *rd  = heap_caps_malloc(PST_FLASH_RD, PST_CAP_INT);
    if (!buf) {
        ESP_LOGE(TAG, "D: no internal memory for flash buffer, phase skipped");
        xTaskNotifyGive(s_h_a);
        for (;;) vTaskDelay(portMAX_DELAY);
    }

    int busy_hits = 0;
    int mid_hits = 0;
    for (int i = 0; i < s_ph.ops; i++) {
        vTaskDelay(pdMS_TO_TICKS(PST_FLASH_GAP_MS));
        for (int w = 0; w < PST_FLASH_LEN / 4; w++) buf[w] = s_flash_word + (uint32_t)w;   /* 每次内容不同 */

        bool busy = false;
        if (s_ph.wait_busy) {
            const int64_t tw = esp_timer_get_time();
            while (esp_timer_get_time() - tw < PST_BUSY_WAIT_MS * 1000LL) {
                if (bsp_lcd_lock(0)) {
                    bsp_lcd_unlock();              /* 界面锁没人占=界面闲着,再等 */
                    vTaskDelay(1);
                    continue;
                }
                /* 界面锁被占:先别动手,等它干过"读触摸、画图",进入"往屏幕搬数据"再写 */
                vTaskDelay(pdMS_TO_TICKS(PST_BUSY_LEAD_MS));
                if (!bsp_lcd_lock(0)) {
                    busy = true;                   /* 还忙着=一帧长的活 */
                    break;
                }
                bsp_lcd_unlock();                  /* 已经忙完（短活）,重新等 */
            }
        }
        if (busy) busy_hits++;

        const bool locked = s_ph.screen_paused && bsp_lcd_lock(-1);
        const int64_t t0 = esp_timer_get_time();
        esp_err_t e = esp_flash_erase_region(esp_flash_default_chip, PST_FLASH_ADDR, PST_FLASH_LEN);
        const int64_t t1 = esp_timer_get_time();
        if (e == ESP_OK) {
            e = esp_flash_write(esp_flash_default_chip, buf, PST_FLASH_ADDR, PST_FLASH_LEN);
        }
        const int64_t t2 = esp_timer_get_time();
        bool same = (rd == NULL);                 /* 没有读回缓冲时视作跳过核对 */
        if (e == ESP_OK && rd) {
            e = esp_flash_read(esp_flash_default_chip, rd, PST_FLASH_ADDR, PST_FLASH_RD);
            same = (e == ESP_OK) && (memcmp(rd, buf, PST_FLASH_RD) == 0);
        }
        const int64_t t3 = esp_timer_get_time();
        if (locked) bsp_lcd_unlock();

        /* 写完之后界面还要忙多久:还长 = 写的时候它正在一帧的中段（搬数据） */
        int tail_ms = -1;
        if (busy) {
            const int64_t tt = esp_timer_get_time();
            while (esp_timer_get_time() - tt < 200000) {
                if (bsp_lcd_lock(0)) {
                    bsp_lcd_unlock();
                    break;
                }
                vTaskDelay(1);
            }
            tail_ms = (int)((esp_timer_get_time() - tt) / 1000);
            if (tail_ms >= PST_MID_TAIL_MS) mid_hits++;
        }

        if (t1 - t0 > s_er_us) s_er_us = t1 - t0;
        if (t2 - t1 > s_wr_us) s_wr_us = t2 - t1;
        if (e != ESP_OK) s_d_err = e;
        if (e == ESP_OK && same) s_ops_ok++;
        s_flash_word++;

        char hitnote[64] = "";
        if (s_ph.wait_busy) {
            snprintf(hitnote, sizeof(hitnote), " | %s, still busy %d ms after",
                     busy ? "UI busy at start" : "UI idle (no hit)", tail_ms);
        }
        ESP_LOGI(TAG, "D: op %d/%d (core %d, %s): erase %lld ms, write %lld ms, read-back %s%s%s",
                 i + 1, s_ph.ops, s_ph.core,
                 s_ph.screen_paused ? "screen paused" : "screen free",
                 (long long)((t1 - t0) / 1000), (long long)((t2 - t1) / 1000),
                 rd ? (same ? "match" : "DIFF") : "skipped",
                 hitnote,
                 (t3 - t2 > 20000) ? " | slow read-back" : "");
        if (e != ESP_OK) ESP_LOGE(TAG, "D: flash op failed: %s", esp_err_to_name(e));
    }
    s_busy_hits = busy_hits;
    s_mid_hits = mid_hits;

    heap_caps_free(buf);
    heap_caps_free(rd);
    xTaskNotifyGive(s_h_a);                       /* D:写完了 */
    for (;;) vTaskDelay(portMAX_DELAY);           /* 等 A 删除 */
}

static bool pst_d_start(int core)
{
    s_stk_d = heap_caps_malloc(PST_STACK_D, PST_CAP_INT);
    if (!s_stk_d) return false;
    memset(s_stk_d, PST_FILL, PST_STACK_D);
    s_h_d = xTaskCreateStaticPinnedToCore(pst_task_d, "pst_d", PST_STACK_D, NULL,
                                          PST_D_PRIO, s_stk_d, &s_tcb_d, core);
    return s_h_d != NULL;
}

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

#if !PST_ONLY_PHASE3
    /* ①③ 同一份负载 */
    s_sum_a = pst_bench(&s_us_a);
    s_hwm_a = uxTaskGetStackHighWaterMark(NULL);
    xTaskNotifyGive(s_h_b);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);      /* 等 B 跑完 */

    /* ③ 切换延迟:PSRAM 栈（A） <-> 内部栈（B） */
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
#endif /* !PST_ONLY_PHASE3 */

    /* ② 别的任务写 flash 期间,本任务（PSRAM 栈）是否安然无恙 */
    {
        ESP_LOGI(TAG, "[2] start: A stack in PSRAM? %s",
                 esp_ptr_external_ram(&probe) ? "YES" : "NO");

        volatile uint32_t guard[8];               /* 就在 A 的 PSRAM 栈上:跨暂停必须原样 */
        for (int i = 0; i < 8; i++) guard[i] = PST_GUARD_MAGIC + (uint32_t)i;
        char buf[32];
        bool guard_bad = false;
        bool all_ok = true;

        for (int p = 0; p < PST_PHASE_N; p++) {
            const pst_phase_t ph = PST_PHASES[p];
            s_ph = ph;
            s_er_us = s_wr_us = 0;
            s_d_err = ESP_OK;
            s_ops_ok = 0;
            s_busy_hits = 0;
            s_mid_hits = 0;

            int64_t tmin = INT64_MAX;
            for (int k = 0; k < 10; k++) {         /* 没人打扰时一块要多久 */
                const int64_t t0 = esp_timer_get_time();
                (void)pst_chunk(0, buf);
                const int64_t dt = esp_timer_get_time() - t0;
                if (dt < tmin) tmin = dt;
            }

            int64_t over = 0;                     /* 一块之内被非自愿暂停的最长时间 */
            unsigned bad = 0;                     /* 先后两遍结果不一致的轮数 */
            unsigned rounds = 0;
            if (!pst_d_start(ph.core)) {
                ESP_LOGE(TAG, "[2] phase %d: could not create flash writer, skipped", p + 1);
                all_ok = false;
                continue;
            }

            bool notified = false;
            for (uint32_t it = 0; !notified; it++) {
                if ((it % PST_CHUNK_BUSY) == PST_CHUNK_BUSY - 1) vTaskDelay(pdMS_TO_TICKS(10));
                const int64_t t0 = esp_timer_get_time();
                const float s1 = pst_chunk(it, buf);
                const int64_t t1 = esp_timer_get_time();
                const float s2 = pst_chunk(it, buf);
                const int64_t t2 = esp_timer_get_time();
                if (s1 != s2) bad++;
                if (t1 - t0 - tmin > over) over = t1 - t0 - tmin;
                if (t2 - t1 - tmin > over) over = t2 - t1 - tmin;
                for (int i = 0; i < 8; i++) {
                    if (guard[i] != PST_GUARD_MAGIC + (uint32_t)i) guard_bad = true;
                }
                rounds++;
                notified = (ulTaskNotifyTake(pdTRUE, 0) != 0);   /* D 写完了 */
            }

            vTaskDelete(s_h_d);
            heap_caps_free(s_stk_d);
            s_stk_d = NULL;

            vTaskDelay(pdMS_TO_TICKS(300));        /* 给界面一点时间把积压的刷新做完 */
            const bool ui_ok = bsp_lcd_lock(500);  /* 界面任务若卡在等 DMA,这把锁就拿不到 */
            if (ui_ok) bsp_lcd_unlock();

            char note[64] = "";
            if (ph.wait_busy) {
                snprintf(note, sizeof(note), ", UI busy at start %d/%d, mid-frame %d/%d",
                         s_busy_hits, ph.ops, s_mid_hits, ph.ops);
            }
            const bool ok = (s_ops_ok == ph.ops) && (bad == 0) && !guard_bad && ui_ok;
            if (!ok) all_ok = false;
            ESP_LOGI(TAG, "[2] phase %d/%d: flash writer on core %d, %s | ops ok %d/%d "
                          "(erase <= %lld ms, write <= %lld ms)%s%s%s | A paused <= %lld ms over %u rounds | "
                          "A data %s | A stack guard %s | UI %s",
                     p + 1, PST_PHASE_N, ph.core,
                     ph.screen_paused ? "screen paused during write"
                                      : (ph.wait_busy ? "written mid-frame (forced collision with panel transfer)"
                                                      : "screen NOT paused (real world)"),
                     s_ops_ok, ph.ops,
                     (long long)(s_er_us / 1000), (long long)(s_wr_us / 1000),
                     (s_d_err != ESP_OK) ? ", flash error " : "",
                     (s_d_err != ESP_OK) ? esp_err_to_name(s_d_err) : "",
                     note,
                     (long long)(over / 1000), rounds,
                     (bad == 0) ? "consistent" : "CORRUPTED",
                     guard_bad ? "BROKEN" : "intact",
                     ui_ok ? "responsive" : "NO RESPONSE in 500ms (UI task hung?)");
        }
        ESP_LOGI(TAG, "[2] ==== A (PSRAM stack) came through %d phase(s): %s ====",
                 PST_PHASE_N, all_ok ? "alive and consistent" : "PROBLEM, see lines above");
    }

    ESP_LOGI(TAG, "==== RESULT ====");
#if !PST_ONLY_PHASE3
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
#endif
    ESP_LOGI(TAG, "[3] PSRAM-stack high-water after phase(s): %u / %u B used",
             (unsigned)(PST_STACK_PSRAM - uxTaskGetStackHighWaterMark(NULL)), (unsigned)PST_STACK_PSRAM);
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
#if !PST_ONLY_PHASE3
    s_stk_b = heap_caps_malloc(PST_STACK_INT, PST_CAP_INT);
    s_stk_c = heap_caps_malloc(PST_STACK_INT_S, PST_CAP_INT);
    if (!s_stk_a || !s_stk_b || !s_stk_c) {
        ESP_LOGE(TAG, "alloc failed (a=%p b=%p c=%p) - test skipped", s_stk_a, s_stk_b, s_stk_c);
        heap_caps_free(s_stk_a); heap_caps_free(s_stk_b); heap_caps_free(s_stk_c);
        return;
    }
    memset(s_stk_b, PST_FILL, PST_STACK_INT);
    memset(s_stk_c, PST_FILL, PST_STACK_INT_S);
#else
    if (!s_stk_a) {
        ESP_LOGE(TAG, "alloc failed (a=%p) - test skipped", s_stk_a);
        heap_caps_free(s_stk_a);
        return;
    }
#endif
    memset(s_stk_a, PST_FILL, PST_STACK_PSRAM);   /* 先铺 0xA5,高水位才量得准 */

#if !PST_ONLY_PHASE3
    s_h_b = xTaskCreateStaticPinnedToCore(pst_task_b, "pst_b", PST_STACK_INT, NULL,
                                          PST_PRIO, s_stk_b, &s_tcb_b, PST_CORE);
    s_h_c = xTaskCreateStaticPinnedToCore(pst_task_c, "pst_c", PST_STACK_INT_S, NULL,
                                          PST_PRIO, s_stk_c, &s_tcb_c, PST_CORE);
#endif
    s_h_a = xTaskCreateStaticPinnedToCore(pst_task_a, "pst_a", PST_STACK_PSRAM, NULL,
                                          PST_PRIO, s_stk_a, &s_tcb_a, PST_CORE);
#if !PST_ONLY_PHASE3
    if (!s_h_a || !s_h_b || !s_h_c) {
        ESP_LOGE(TAG, "task create failed (a=%p b=%p c=%p)", s_h_a, s_h_b, s_h_c);
        if (s_h_b) vTaskDelete(s_h_b);
        if (s_h_c) vTaskDelete(s_h_c);
        heap_caps_free(s_stk_a); heap_caps_free(s_stk_b); heap_caps_free(s_stk_c);
        return;
    }
#else
    if (!s_h_a) {
        ESP_LOGE(TAG, "task create failed (a=%p)", s_h_a);
        heap_caps_free(s_stk_a);
        return;
    }
#endif
    ESP_LOGI(TAG, "test tasks created (core %d, start in %d ms)", PST_CORE, PST_START_MS);
}
