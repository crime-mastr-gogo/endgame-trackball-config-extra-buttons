zephyr_library()
# Compatibility for efogdev/zmk 50d57079: direct/composite scanners call
# pm_device_runtime_* but omit device_runtime.h. Supply declarations (and
# disabled-runtime inline stubs) to C translation units without enabling
# runtime PM or modifying downloaded dependencies. Remove when upstream fixes it.
zephyr_compile_options($<$<COMPILE_LANGUAGE:C>:-includezephyr/pm/device_runtime.h>)
zephyr_library_sources(optimizations.c)
zephyr_library_sources(efogtech_trackball_0.c)
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fmacro-prefix-map=${CMAKE_SOURCE_DIR}=.")
