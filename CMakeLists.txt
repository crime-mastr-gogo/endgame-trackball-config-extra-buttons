add_subdirectory(vendor/adaptive-feedback)
target_sources(app PRIVATE src/feedback.c src/controls.c)
zephyr_include_directories(include)
