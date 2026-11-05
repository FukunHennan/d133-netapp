/* SPDX-License-Identifier: Apache-2.0 */
#ifndef D133_UI_APP_H
#define D133_UI_APP_H

#include "lvgl.h"

typedef enum {
    UI_PAGE_BOOT,
    UI_PAGE_AP,
    UI_PAGE_MAIN
} ui_page_t;

typedef struct {
    const char *wifi;
    const char *ssid;
    const char *ip;
    const char *ap_ssid;
    const char *ap_password;
    const char *ota;
    const char *error;
    unsigned ota_percent;
    int online;
    int provisioning;
} ui_app_status_t;

/* All functions run on the LVGL thread. The status strings are copied into labels. */
void ui_app_init(int (*request_setup)(void));
void ui_app_update(const ui_app_status_t *status);
void ui_app_set_sensor_text(const char *temperature, const char *humidity,
                            const char *pressure, const char *note);
void ui_app_show(ui_page_t page);

/* 跳过启动页固定 5s 停留（桌面模拟器截图用，固件侧不需要调用） */
void ui_app_skip_boot(void);

#endif
