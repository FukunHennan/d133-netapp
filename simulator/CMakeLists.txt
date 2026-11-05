cmake_minimum_required(VERSION 3.16)
project(d133_lvgl_preview C)

set(CMAKE_C_STANDARD 99)

# The desktop preview compiles the SDK's own LVGL 9.1 and SDL, plus the exact
# UI files that go into the firmware (single source of truth).
set(LUBAN_LITE_DIR "${CMAKE_CURRENT_LIST_DIR}/../luban-lite" CACHE PATH
    "luban-lite SDK checkout (V1.0 / f5be1e4c)")
set(LVGL_DIR "${LUBAN_LITE_DIR}/packages/artinchip/lvgl-ui/lvgl_v9/lvgl" CACHE PATH
    "LVGL 9.1 sources inside the SDK")
set(SDL_DIR "${LUBAN_LITE_DIR}/packages/third-party/awtk-ui/awtk/3rd/SDL" CACHE PATH
    "SDL2 sources inside the SDK")
set(UI_DIR "${CMAKE_CURRENT_LIST_DIR}/../firmware/application/rt-thread/helloworld/netapp/ui"
    CACHE PATH "handwritten UI sources shared with the firmware")

foreach(dir LVGL_DIR SDL_DIR UI_DIR)
    if(NOT EXISTS "${${dir}}")
        message(FATAL_ERROR "${dir} does not exist: ${${dir}}\n"
                            "Clone luban-lite next to this repository, or pass -DLUBAN_LITE_DIR=<path>.")
    endif()
endforeach()

set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)

add_subdirectory("${SDL_DIR}" "${CMAKE_BINARY_DIR}/sdl2")
target_compile_options(SDL2-static PRIVATE -Wno-error=declaration-after-statement)
add_subdirectory("${LVGL_DIR}" "${CMAKE_BINARY_DIR}/lvgl")
target_include_directories(lvgl PUBLIC "${CMAKE_CURRENT_LIST_DIR}" "${SDL_DIR}/include")
# CACHE_LINE_SIZE must match the board (Kconfig: CONFIG_CACHE_LINE_SIZE=32).
target_compile_definitions(lvgl PUBLIC CACHE_LINE_SIZE=32 SDL_PixelFormatEnum=Uint32)
target_link_libraries(lvgl PUBLIC SDL2-static)

add_executable(d133_preview main.c
    "${UI_DIR}/ui_app.c"
    "${UI_DIR}/ui_font_16.c"
    "${UI_DIR}/ui_font_24.c"
    "${UI_DIR}/ui_font_36.c")
target_link_libraries(d133_preview PRIVATE mingw32 SDL2main lvgl SDL2-static)
target_include_directories(d133_preview PRIVATE "${CMAKE_CURRENT_LIST_DIR}" "${UI_DIR}")
