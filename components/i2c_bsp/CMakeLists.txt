idf_component_register(
  SRCS "i2c_bsp.c"
  REQUIRES driver
  PRIV_REQUIRES bsp_common
  INCLUDE_DIRS "./")
