/* SPDX-License-Identifier: Apache-2.0 */
#ifndef D133_PROTOCOL_H
#define D133_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

int netapp_form(const char *body, char ssid[33], char password[64]);
/* Web OTA trigger form: url + crc(8 hex) + token, all required. */
int netapp_ota_form(const char *body, char url[256], char crc[9], char token[64]);
/* OS-only crc-CPIO reader. No flash writes until the OS header is validated. */
struct netapp_cpio {
    unsigned char header[160];
    size_t used, needed;
    uint32_t left, padding, sum, checksum, os_size, written, capacity;
    unsigned state, entries, kind, done;
    int (*begin)(void *arg, uint32_t size);
    int (*write)(void *arg, const void *data, size_t size);
    void *arg;
};
void netapp_cpio_init(struct netapp_cpio *p, uint32_t capacity);
int netapp_cpio_feed(struct netapp_cpio *p, const void *data, size_t size);
#endif
