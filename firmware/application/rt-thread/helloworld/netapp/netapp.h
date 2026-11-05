/* SPDX-License-Identifier: Apache-2.0 */
#ifndef D133_NETAPP_H
#define D133_NETAPP_H
#include <rtthread.h>
struct netapp_status {
    char wifi[32];
    char ssid[33];
    char ip[16];
    char ap_ssid[33];
    char ota[32];
    char error[80];
    char scan_summary[96];
    unsigned ota_percent;
    int online;
    int provisioning;
};
void netapp_get_status(struct netapp_status *out);
int netapp_request_setup(void);
void netapp_set_ota_status(const char *phase, unsigned percent, const char *error);
int netapp_ota_lock(void);
void netapp_ota_unlock(void);
/* Validate url/crc and spawn the OTA thread. 0 on success, -RT_EINVAL/-RT_EBUSY/-RT_ENOMEM. */
int netapp_ota_start(const char *url, const char *crc_hex);
int netapp_start(void);
#endif
