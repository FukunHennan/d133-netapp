#include "lvgl.h"
#include "ui_app.h"
#include <SDL.h>
#include <string.h>

static const ui_app_status_t ap_state = {
    .wifi = "Setup AP ready", .ap_ssid = "D133-Setup-A1B2C3",
    .ap_password = "D133setup", .ip = "192.168.4.1",
    .ota = "Idle", .provisioning = 1
};

static int demo_setup(void)
{
    ui_app_update(&ap_state);
    return 0;
}


int main(int argc, char **argv)
{
    const char *snapshot = NULL;
    const char *page = "boot";
    ui_app_status_t state = {0};
    int i;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--snapshot") && i + 1 < argc) snapshot = argv[++i];
        else if (!strcmp(argv[i], "--page") && i + 1 < argc) page = argv[++i];
    }

    lv_init();
    lv_display_t *display = lv_sdl_window_create(480, 272);
    if (!display) return 1;
    lv_sdl_window_set_title(display, "D133 LVGL 9.1 - 480x272");
    lv_sdl_mouse_create();
    ui_app_init(demo_setup);

    if (!strcmp(page, "ap")) {
        ui_app_skip_boot();
        ui_app_update(&ap_state);
    } else if (!strcmp(page, "main")) {
        ui_app_skip_boot();
        state.wifi = "Connected";
        state.ssid = "Home-WiFi";
        state.ip = "192.168.1.106";
        state.ota = "Idle";
        state.online = 1;
        ui_app_set_sensor_text("25.6", "68", "101.3", "示例数据");
        ui_app_update(&state);
    } else if (!strcmp(page, "ota")) {
        ui_app_skip_boot();
        state.wifi = "Connected";
        state.ssid = "Home-WiFi";
        state.ip = "192.168.1.106";
        state.ota = "Downloading / flashing";
        state.ota_percent = 42;
        state.online = 1;
        ui_app_set_sensor_text("25.6", "68", "101.3", "示例数据");
        ui_app_update(&state);
    } else {
        state.wifi = "Connecting";
        state.ssid = "Home-WiFi";
        state.ota = "Idle";
        ui_app_update(&state);
    }

    for (i = 0; ; ++i) {
        lv_timer_handler();
        if (snapshot && i == 30) {
            SDL_Renderer *renderer = lv_sdl_window_get_renderer(display);
            SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 480, 272, 32, SDL_PIXELFORMAT_ARGB8888);
            if (!surface) return 2;
            if (SDL_RenderReadPixels(renderer, NULL, surface->format->format,
                                     surface->pixels, surface->pitch) != 0) return 3;
            int result = SDL_SaveBMP(surface, snapshot);
            SDL_FreeSurface(surface);
            return result == 0 ? 0 : 4;
        }
        SDL_Delay(5);
    }
}
