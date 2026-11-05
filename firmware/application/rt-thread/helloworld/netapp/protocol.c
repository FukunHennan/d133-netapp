/* SPDX-License-Identifier: Apache-2.0 */
#include "protocol.h"
#include <string.h>

static int hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int decode(const char *s, size_t n, char *out, size_t cap)
{
    size_t i, j = 0;
    for (i = 0; i < n; ++i) {
        int c = (unsigned char)s[i];
        if (c == '%') {
            int a, b;
            if (i + 2 >= n || (a = hex(s[i+1])) < 0 || (b = hex(s[i+2])) < 0) return -1;
            c = (a << 4) | b; i += 2;
        } else if (c == '+') c = ' ';
        if (!c || c < 32 || c == 127 || j + 1 >= cap) return -1;
        out[j++] = (char)c;
    }
    out[j] = 0;
    return 0;
}

int netapp_form(const char *body, char ssid[33], char password[64])
{
    unsigned fields = 0;
    memset(ssid, 0, 33); memset(password, 0, 64);
    while (*body) {
        const char *end = strchr(body, '&'), *eq;
        size_t n = end ? (size_t)(end - body) : strlen(body);
        eq = memchr(body, '=', n);
        if (!eq) return -1;
        if (eq - body == 4 && !memcmp(body, "ssid", 4)) {
            if (fields & 1 || decode(eq + 1, n - 5, ssid, 33)) return -1;
            fields |= 1;
        } else if (eq - body == 8 && !memcmp(body, "password", 8)) {
            if (fields & 2 || decode(eq + 1, n - 9, password, 64)) return -1;
            fields |= 2;
        } else return -1;
        if (!end) break;
        body = end + 1;
        if (!*body) return -1;
    }
    return fields == 3 && *ssid && (!*password || strlen(password) >= 8) ? 0 : -1;
}

int netapp_ota_form(const char *body, char url[256], char crc[9], char token[64])
{
    unsigned fields = 0;
    memset(url, 0, 256); memset(crc, 0, 9); memset(token, 0, 64);
    while (*body) {
        const char *end = strchr(body, '&'), *eq;
        size_t n = end ? (size_t)(end - body) : strlen(body);
        eq = memchr(body, '=', n);
        if (!eq) return -1;
        if (eq - body == 3 && !memcmp(body, "url", 3)) {
            if (fields & 1 || decode(eq + 1, n - 4, url, 256)) return -1;
            fields |= 1;
        } else if (eq - body == 3 && !memcmp(body, "crc", 3)) {
            if (fields & 2 || decode(eq + 1, n - 4, crc, 9)) return -1;
            fields |= 2;
        } else if (eq - body == 5 && !memcmp(body, "token", 5)) {
            if (fields & 4 || decode(eq + 1, n - 6, token, 64)) return -1;
            fields |= 4;
        } else return -1;
        if (!end) break;
        body = end + 1;
        if (!*body) return -1;
    }
    return fields == 7 && *url && *crc && *token ? 0 : -1;
}

static int field(const unsigned char *s, uint32_t *v)
{
    unsigned i; *v = 0;
    for (i = 0; i < 8; ++i) {
        int x = hex(s[i]);
        if (x < 0) return -1;
        *v = (*v << 4) | (unsigned)x;
    }
    return 0;
}

void netapp_cpio_init(struct netapp_cpio *p, uint32_t capacity)
{
    memset(p, 0, sizeof(*p)); p->needed = 110; p->capacity = capacity;
}

int netapp_cpio_feed(struct netapp_cpio *p, const void *data, size_t size)
{
    const unsigned char *s = data;
    while (size) {
        size_t n;
        if (p->done) { if (*s++) return -1; --size; continue; }
        if (p->state == 0) {
            n = p->needed - p->used; if (n > size) n = size;
            memcpy(p->header + p->used, s, n); p->used += n; s += n; size -= n;
            if (p->used < p->needed) continue;
            if (p->needed == 110) {
                uint32_t namesize, mode;
                if (memcmp(p->header, "070702", 6) ||
                    field(p->header + 14, &mode) || field(p->header + 94, &namesize) ||
                    field(p->header + 54, &p->left) || field(p->header + 102, &p->checksum) ||
                    namesize < 1 || namesize > 32) return -1;
                if (p->entries < 2 && (mode & 0170000) != 0100000) return -1;
                p->needed = (110 + namesize + 3) & ~(size_t)3;
                p->kind = namesize; continue;
            }
            if (p->header[110 + p->kind - 1] ||
                strlen((char *)p->header + 110) != p->kind - 1) return -1;
            if (p->entries == 0) {
                if (strcmp((char *)p->header+110, "ota_info.bin") || p->left != 512) return -1;
            } else if (p->entries == 1) {
                if (strcmp((char *)p->header+110, "d13x_os.itb") ||
                    p->left < 4 || p->left > p->capacity) return -1;
                p->os_size = p->left;
                if (p->begin && p->begin(p->arg, p->left)) return -1;
            } else if (p->entries == 2) {
                if (strcmp((char *)p->header+110, "TRAILER!!!") || p->left) return -1;
                p->done = 1; continue;
            } else return -1;
            p->padding = (4 - (p->left & 3)) & 3;
            p->sum = 0; p->state = 1;
        } else if (p->state == 1) {
            n = p->left; if (n > size) n = size;
            if (p->entries == 1) {
                size_t i;
                if (p->write && p->write(p->arg, s, n)) return -1;
                for (i = 0; i < n; ++i) p->sum += s[i];
                p->written += n;
            }
            p->left -= n; s += n; size -= n;
            if (!p->left) {
                /* SDK mkcpio patches ota_info size after computing its checksum.
                 * Metadata is ignored for flash addressing; validate OS checksum. */
                if (p->entries == 1 && p->sum != p->checksum) return -1;
                p->state = 2;
            }
        } else {
            n = p->padding; if (n > size) n = size;
            s += n; size -= n; p->padding -= n;
            if (!p->padding) {
                ++p->entries; p->state = 0; p->used = 0; p->needed = 110;
            }
        }
    }
    return 0;
}
