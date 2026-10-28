# The Telegraph edge MCU (XIAO ESP32-S3) for the WANTED engine's ESP-IDF build.

# The log reader is seeded from outside the engine, so a board with no console
# has one wapp it can always start. The build stages it in seeds/.
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/seeds/tg-logs.wasm)
    set(WANTED_EXTRA_SEEDS
        "tg-logs:3=${CMAKE_CURRENT_LIST_DIR}/seeds/tg-logs.wasm")
endif()
