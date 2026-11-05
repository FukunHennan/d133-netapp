/* SPDX-License-Identifier: Apache-2.0 */
#include "netapp.h"
#include "ui/ui_app.h"

/* Network workers publish status; this callback is always run by LVGL. */
static void refresh_status(lv_timer_t *timer)
{
    struct netapp_status s;
    ui_app_status_t view;
    (void)timer;
    netapp_get_status(&s);
    view.wifi = s.wifi;
    view.ssid = s.ssid;
    view.ip = s.ip;
    view.ap_ssid = s.ap_ssid;
    view.ap_password = D133_SETUP_PASSWORD;
    view.ota = s.ota;
    view.error = s.error;
    view.ota_percent = s.ota_percent;
    view.online = s.online;
    view.provisioning = s.provisioning;
    ui_app_update(&view);
}

void netapp_ui_init(void)
{
    ui_app_init(netapp_request_setup);
    refresh_status(RT_NULL);
    lv_timer_create(refresh_status, 500, RT_NULL);
}
