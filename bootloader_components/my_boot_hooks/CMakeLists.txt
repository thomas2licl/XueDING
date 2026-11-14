idf_component_register(SRCS "hooks.c" REQUIRES hal soc esp_rom)

# LocalLife 定制 bootloader 钩子组件（§6.5 方案 B,根治 bootloader 期花屏）。
# 本目录（bootloader_components/）下的组件会被编译链接进 bootloader 而非 app
# ——官方机制,见 examples/custom_bootloader/bootloader_hooks。
# hooks.c 中的 bootloader_hooks_include() 空函数满足链接器 "-u" 强制符号,
# 使 bootloader 的 weak 钩子被本实现覆盖。
