/* LocalLife 定制 bootloader 钩子（§6.5 方案 B,2026-11-14）
 * 根治开机 bootloader 期花屏：AP3032 背光芯片 CTRL 脚 100K 上拉 + IO42(FB 调光脚)
 * 浮空,使背光自上电第一毫秒即亮;bootloader 阶段（~1s）面板 RAM 为随机数据 → 花屏。
 * 本钩子在 bootloader 初始化完成后、加载 app 前把 IO42 配为输出低——LED 电流归零,
 * 背光熄灭,直到 app 的 bsp_lcd 启动背光 PWM 正常调光。ROM→bootloader 早期（钩子前）
 * 仍有百毫秒级微亮,远优于原 ~1s 花屏。
 * 环境限制：bootloader 阶段只能用 hal/寄存器级 API,不能用完整 driver。 */

#include "esp_log.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"

#define BL_OFF_GPIO_NUM    42    /* AP3032 FB 调光脚,低=LED 电流归零=背光灭 */

/* 链接器强制符号：让本组件（静态库）连同符号被链入 bootloader。
 * 见组件 CMakeLists 注释——bootloader 的钩子为 weak,链接器默认会忽略本库。 */
void bootloader_hooks_include(void)
{
}

void bootloader_after_init(void)
{
    /* 实验2:FB 拉高（恒流环路反馈脚,高于基准→芯片降输出→背光灭?）
     * 实验1（拉低）实测无效——背光不灭,印证 FB 是调光反馈而非使能 */
    gpio_ll_func_sel(&GPIO, BL_OFF_GPIO_NUM, 0);
    gpio_ll_output_enable(&GPIO, BL_OFF_GPIO_NUM);
    gpio_ll_set_level(&GPIO, BL_OFF_GPIO_NUM, 1);
    ESP_LOGI("ll_boot", "GPIO42 forced HIGH (fb-backdrive experiment)");
}
