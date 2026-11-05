/* SPDX-License-Identifier: Apache-2.0 */
#include "netapp.h"
#include "protocol.h"
#include "portal_html.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <finsh.h>
#include <lwip/sockets.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>
#include <lwip/dhcp.h>
#pragma push_macro("IFNAMSIZ")
#undef IFNAMSIZ
#include <wifi/wifi_conf.h>
#pragma pop_macro("IFNAMSIZ")

#define CREDENTIAL_FILE "/data/wifi.cfg"
#define CREDENTIAL_TMP  "/data/wifi.tmp"
#define SETUP_IP "192.168.4.1"
#define DEFAULT_WIFI_SSID "123456789"
#define DEFAULT_WIFI_PASSWORD "123456789"
extern void dhcpd_start(const char *name);
extern void dhcpd_stop(const char *name);

struct credentials { char magic[4], ssid[33], password[64]; };
static struct rt_mutex state_lock;
static int started, online, ota_busy, setup_requested;
static volatile int scan_done;
static char scan_networks[8][33];
static unsigned scan_count;
static struct netapp_status status = { .wifi = "Starting", .ota = "Idle" };
static char ifname[8];
static void wifi_status(const char *phase, const char *ssid, const char *ip, const char *error, int ap)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    rt_strncpy(status.wifi, phase, sizeof(status.wifi) - 1);
    rt_strncpy(status.ssid, ssid ? ssid : "", sizeof(status.ssid) - 1);
    rt_strncpy(status.ip, ip ? ip : "", sizeof(status.ip) - 1);
    if (error) rt_strncpy(status.error, error, sizeof(status.error) - 1);
    status.provisioning = ap;
    status.online = online;
    rt_mutex_release(&state_lock);
}
void netapp_get_status(struct netapp_status *out)
{
    if (!out) return;
    if (!started) { *out = status; return; }
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    *out = status;
    out->online = online;
    rt_mutex_release(&state_lock);
}
void netapp_set_ota_status(const char *phase, unsigned percent, const char *error)
{
    if (!started) return;
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    rt_strncpy(status.ota, phase, sizeof(status.ota) - 1);
    status.ota_percent = percent > 100 ? 100 : percent;
    if (error) rt_strncpy(status.error, error, sizeof(status.error) - 1);
    rt_mutex_release(&state_lock);
}
int netapp_request_setup(void)
{
    int result = -RT_EBUSY;
    if (!started) return -RT_ERROR;
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    if (online && !ota_busy) {
        setup_requested = 1;
        status.error[0] = 0;
        result = RT_EOK;
    } else {
        rt_strncpy(status.error, ota_busy ? "OTA active" : "Setup already active or offline", sizeof(status.error) - 1);
    }
    rt_mutex_release(&state_lock);
    return result;
}

/* Realtek invokes this callback from its own thread; never wait for a lock here. */
static rtw_result_t scan_result(rtw_scan_handler_result_t *result)
{
    unsigned i, len;
    char name[33];
    if (result->scan_complete) { scan_done = 1; return RTW_SUCCESS; }
    len = result->ap_details.SSID.len;
    if (!len || len > 32) return RTW_SUCCESS;
    memcpy(name, result->ap_details.SSID.val, len);
    name[len] = 0;
    for (i = 0; i < len; ++i)
        if ((unsigned char)name[i] < 32 || (unsigned char)name[i] == 127) name[i] = '?';
    if (rt_mutex_take(&state_lock, 0) != RT_EOK) return RTW_SUCCESS;
    for (i = 0; i < scan_count; ++i)
        if (!strcmp(scan_networks[i], name)) break;
    if (i == scan_count && scan_count < 8) {
        rt_strncpy(scan_networks[scan_count], name, sizeof(scan_networks[0]) - 1);
        ++scan_count;
    }
    rt_mutex_release(&state_lock);
    return RTW_SUCCESS;
}

/* Scan while the radio is still in STA mode. Scanning with the setup AP active
 * takes the single radio off-channel and disconnects the phone. */
static void scan_before_portal(void)
{
    unsigned i;
    int ret;
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    scan_done = 0;
    scan_count = 0;
    memset(scan_networks, 0, sizeof(scan_networks));
    rt_strncpy(status.scan_summary, "正在扫描附近网络...", sizeof(status.scan_summary) - 1);
    rt_mutex_release(&state_lock);
    ret = wifi_scan_networks(scan_result, RT_NULL);
    if (ret == RTW_SUCCESS)
        for (i = 0; i < 100 && !scan_done; ++i) rt_thread_mdelay(100);
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    if (ret != RTW_SUCCESS) rt_strncpy(status.scan_summary, "扫描失败，可手动输入 Wi-Fi", sizeof(status.scan_summary) - 1);
    else if (!scan_done) rt_strncpy(status.scan_summary, "扫描超时，可手动输入 Wi-Fi", sizeof(status.scan_summary) - 1);
    else if (!scan_count) rt_strncpy(status.scan_summary, "未发现网络，可手动输入 Wi-Fi", sizeof(status.scan_summary) - 1);
    else rt_snprintf(status.scan_summary, sizeof(status.scan_summary), "已发现 %u 个网络", scan_count);
    rt_mutex_release(&state_lock);
}
static char page[16384];

static void html_escape(const char *src, char out[200])
{
    size_t used = 0;
    while (*src && used < 193) {
        const char *replacement = RT_NULL;
        if (*src == '&') replacement = "&amp;";
        else if (*src == '<') replacement = "&lt;";
        else if (*src == '>') replacement = "&gt;";
        else if (*src == '"') replacement = "&quot;";
        else if (*src == '\'') replacement = "&#39;";
        if (replacement) {
            size_t len = strlen(replacement);
            if (used + len >= 199) break;
            memcpy(out + used, replacement, len); used += len;
        } else out[used++] = *src;
        ++src;
    }
    out[used] = 0;
}

/* JSON string body escaping for the status endpoint. */
static void json_escape(const char *src, char *out, size_t cap)
{
    size_t used = 0;
    while (*src && used + 1 < cap) {
        char c = *src++;
        if (c == '"' || c == '\\') {
            if (used + 2 >= cap) break;
            out[used++] = '\\'; out[used++] = c;
        } else out[used++] = ((unsigned char)c < 32 || c == 127) ? ' ' : c;
    }
    out[used] = 0;
}

static const char *setup_page(void)
{
    char names[8][33], escaped[200], note[200], summary[96];
    unsigned count, i;
    size_t used;
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    count = scan_count;
    memcpy(names, scan_networks, sizeof(names));
    memcpy(summary, status.scan_summary, sizeof(summary));
    rt_mutex_release(&state_lock);
    used = strlen(portal_prefix);
    memcpy(page, portal_prefix, used);
    page[used] = 0;
    if (count) rt_snprintf(note, sizeof(note), "已发现 %u 个网络，点选后填写密码", count);
    else if (summary[0]) html_escape(summary, note);
    else html_escape("未发现网络时可以手动输入名称", note);
    {
        int n = rt_snprintf(page + used, sizeof(page) - used,
                            "<p class='scan-note'>%s</p>", note);
        if (n > 0 && (size_t)n < sizeof(page) - used) used += n;
    }
    for (i = 0; i < count && i < 8 && used < sizeof(page) - 3200; ++i) {
        int n;
        html_escape(names[i], escaped);
        n = rt_snprintf(page + used, sizeof(page) - used,
            "<button class='network' type='button' data-ssid=\"%s\" onclick='chooseNetwork(this)' "
            "aria-pressed='false'><span class='wifi-glyph'>&#x2301;</span>"
            "<span class='network-name'>%s</span><span class='network-arrow'>&#x203A;</span></button>",
            escaped, escaped);
        if (n < 0 || (size_t)n >= sizeof(page) - used) break;
        used += n;
    }
    if (used + sizeof(portal_suffix) < sizeof(page))
        memcpy(page + used, portal_suffix, sizeof(portal_suffix));
    else page[used] = 0;
    return page;
}

int netapp_ota_lock(void)
{
    int ok;
    if (!started) return -RT_ERROR;
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    ok = online && !ota_busy && !setup_requested;
    if (ok) ota_busy = 1;
    rt_mutex_release(&state_lock);
    return ok ? 0 : -RT_EBUSY;
}
void netapp_ota_unlock(void)
{
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER); ota_busy = 0; rt_mutex_release(&state_lock);
}

static int load_credentials(struct credentials *c)
{
    FILE *f = fopen(CREDENTIAL_FILE, "rb");
    int ok;
    if (!f) return -1;
    ok = fread(c, 1, sizeof(*c), f) == sizeof(*c) && fgetc(f) == EOF;
    fclose(f);
    if (!ok || memcmp(c->magic, "WFI1", 4) || !memchr(c->ssid, 0, sizeof(c->ssid)) ||
        !memchr(c->password, 0, sizeof(c->password)) || !c->ssid[0] ||
        (c->password[0] && strlen(c->password) < 8)) return -1;
    return 0;
}

static int save_credentials(const struct credentials *c)
{
    int fd = open(CREDENTIAL_TMP, O_CREAT | O_TRUNC | O_WRONLY, 0600), ret = -1;
    if (fd < 0) return -1;
    if (write(fd, c, sizeof(*c)) == sizeof(*c) && fsync(fd) == 0) ret = 0;
    if (close(fd)) ret = -1;
    if (!ret) ret = rename(CREDENTIAL_TMP, CREDENTIAL_FILE);
    return ret;
}

static struct netif *wifi_netif(void)
{
    struct netif *n, *found = RT_NULL;
    LOCK_TCPIP_CORE();
    for (n = netif_list; n; n = n->next) {
        if (n->name[0] == 'W' && n->name[1] == 'L') { found = n; break; }
    }
    if (found) rt_snprintf(ifname, sizeof(ifname), "WL%u", found->num);
    UNLOCK_TCPIP_CORE();
    return found;
}

static int join(struct netif *n, const struct credentials *c)
{
    unsigned i;
    int ready = 0;
    wifi_status("Connecting", c->ssid, "", "", 0);
    LOCK_TCPIP_CORE(); dhcp_stop(n); UNLOCK_TCPIP_CORE();
    wifi_off(); rt_thread_mdelay(50);
    if (wifi_on(RTW_MODE_STA) < 0 ||
        wifi_connect((char *)c->ssid, c->password[0] ? RTW_SECURITY_WPA2_AES_PSK : RTW_SECURITY_OPEN,
            c->password[0] ? (char *)c->password : RT_NULL, strlen(c->ssid),
            strlen(c->password), 0, RT_NULL) != RTW_SUCCESS) return -1;
    LOCK_TCPIP_CORE();
    netif_set_default(n); netif_set_up(n);
    netif_set_addr(n, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
    ready = dhcp_start(n) == ERR_OK;
    UNLOCK_TCPIP_CORE();
    if (!ready) { wifi_status("DHCP failed", c->ssid, "", "DHCP start failed", 0); return -1; }
    wifi_status("Getting IP", c->ssid, "", "", 0);
    for (i = 0; i < 30; ++i) {
        LOCK_TCPIP_CORE(); ready = dhcp_supplied_address(n); UNLOCK_TCPIP_CORE();
        if (ready) { rt_kprintf("[netapp] Wi-Fi connected; DHCP ready.\n"); return 0; }
        rt_thread_mdelay(1000);
    }
    return -1;
}

static int send_all(int fd, const char *buf, size_t n)
{
    while (n) {
        int sent = lwip_send(fd, buf, n, 0);
        if (sent <= 0) return -1;
        n -= sent; buf += sent;
    }
    return 0;
}
static const char *reason(int status)
{
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 409: return "Conflict";
    case 500: return "Internal Server Error";
    default: return "Error";
    }
}
static void response_ct(int fd, int status, const char *ct, const char *body)
{
    char header[240];
    int n = rt_snprintf(header, sizeof(header), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
        "Content-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
        status, reason(status), ct, (unsigned)strlen(body));
    if (!send_all(fd, header, n)) send_all(fd, body, strlen(body));
}
static void response(int fd, int status, const char *body)
{
    response_ct(fd, status, "text/html; charset=utf-8", body);
}
static void json_reply(int fd, int status, int ok, const char *error)
{
    char body[208], esc[160];
    json_escape(error ? error : "", esc, sizeof(esc));
    rt_snprintf(body, sizeof(body), "{\"ok\":%s,\"error\":\"%s\"}", ok ? "true" : "false", esc);
    response_ct(fd, status, "application/json", body);
}
/* Snapshot of the shared status as JSON for the web OTA page. SSIDs may hold
 * quotes or control bytes, so every string is escaped first. */
static const char *status_json(void)
{
    static char json[640];
    struct netapp_status s;
    char wifi[80], ssid[80], ip[32], ota[80], err[200];
    netapp_get_status(&s);
    json_escape(s.wifi, wifi, sizeof(wifi));
    json_escape(s.ssid, ssid, sizeof(ssid));
    json_escape(s.ip, ip, sizeof(ip));
    json_escape(s.ota, ota, sizeof(ota));
    json_escape(s.error, err, sizeof(err));
    rt_snprintf(json, sizeof(json),
        "{\"wifi\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"ota\":\"%s\",\"ota_percent\":%u,"
        "\"error\":\"%s\",\"online\":%d,\"provisioning\":%d}",
        wifi, ssid, ip, ota, s.ota_percent, err, s.online, s.provisioning);
    return json;
}

static void redirect_to_setup(int fd)
{
    static const char header[] = "HTTP/1.1 302 Found\r\nLocation: http://" SETUP_IP "/\r\n"
        "Content-Length: 0\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    send_all(fd, header, sizeof(header) - 1);
}

/* Read bounded HTTP/1.1 requests, including fragmented TCP headers/bodies.
 * sta selects routing: AP portal (Wi-Fi setup) or STA web (OTA trigger/status). */
static int request(int fd, struct credentials *c, int sta)
{
    char buf[2048], *end = RT_NULL;
    size_t used = 0, header_len = 0, body_len = 0;
    int have_length = 0, have_host = 0, want_body, get_root, get_status;
    rt_tick_t deadline_start = rt_tick_get();
    struct timeval tv = {5, 0};
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    while (used < sizeof(buf)-1) {
        if ((rt_tick_t)(rt_tick_get()-deadline_start) > 10*RT_TICK_PER_SECOND) goto bad;
        int n = lwip_recv(fd, buf+used, sizeof(buf)-1-used, 0);
        if (n <= 0) goto bad;
        if (memchr(buf+used, 0, n)) goto bad;
        used += n; buf[used] = 0;
        if ((end = strstr(buf, "\r\n\r\n")) != RT_NULL) break;
    }
    if (!end) goto bad;
    header_len = end+4-buf;
    want_body = (!sta && (!strncmp(buf, "POST /wifi HTTP/1.1\r\n", 21) ||
                           !strncmp(buf, "POST /wifi HTTP/1.0\r\n", 21))) ||
                (sta && (!strncmp(buf, "POST /ota HTTP/1.1\r\n", 20) ||
                          !strncmp(buf, "POST /ota HTTP/1.0\r\n", 20)));
    get_root = !strncmp(buf, "GET / HTTP/1.1\r\n", 16) || !strncmp(buf, "GET / HTTP/1.0\r\n", 16);
    get_status = sta && (!strncmp(buf, "GET /ota_status HTTP/1.1\r\n", 26) ||
                          !strncmp(buf, "GET /ota_status HTTP/1.0\r\n", 26));
    if (!want_body && strncmp(buf, "GET ", 4)) goto bad;
    if (want_body) {
        char *line = strstr(buf, "\r\n") + 2;
        while (line < end) {
            char *next = strstr(line, "\r\n"), *value, *tail;
            unsigned long length;
            if (!next) goto bad;
            if (!strncasecmp(line, "Host:", 5)) {
                value = line+5; while (*value == ' ') ++value;
                if (have_host++) goto bad;
                if (sta) {
                    if (next == value) goto bad; /* any host is fine on the LAN address */
                } else {
                    if ((size_t)(next-value) != strlen(SETUP_IP) &&
                        (size_t)(next-value) != strlen(SETUP_IP ":80")) goto bad;
                    if (strncmp(value, SETUP_IP, strlen(SETUP_IP)) ||
                        (next-value > strlen(SETUP_IP) && strncmp(value+strlen(SETUP_IP), ":80", 3))) goto bad;
                }
            }
            if (!sta && !strncasecmp(line, "Origin:", 7)) {
                value = line+7; while (*value == ' ') ++value;
                if ((size_t)(next-value) != strlen("http://" SETUP_IP) ||
                    strncmp(value, "http://" SETUP_IP, strlen("http://" SETUP_IP))) goto bad;
            }
            if (!strncasecmp(line, "Transfer-Encoding:", 18)) goto bad;
            if (!strncasecmp(line, "Content-Length:", 15)) {
                if (have_length++) goto bad;
                value = line+15; while (*value == ' ') ++value;
                if (*value < '0' || *value > '9') goto bad;
                length = strtoul(value, &tail, 10);
                if (tail != next || length > 512) goto bad;
                body_len = length;
            }
            line = next+2;
        }
        if (!have_host || !have_length || !body_len || header_len+body_len >= sizeof(buf)) goto bad;
        while (used < header_len+body_len) {
            if ((rt_tick_t)(rt_tick_get()-deadline_start) > 10*RT_TICK_PER_SECOND) goto bad;
            int n = lwip_recv(fd, buf+used, header_len+body_len-used, 0);
            if (n <= 0 || memchr(buf+used, 0, n)) goto bad;
            used += n;
        }
        if (used != header_len+body_len) goto bad;
        buf[used] = 0;
        if (sta) {
            char url[256], crc[9], token[64];
            int ret;
            if (netapp_ota_form(buf+header_len, url, crc, token)) { json_reply(fd, 400, 0, "bad form"); return 0; }
            if (strcmp(token, D133_SETUP_PASSWORD)) { json_reply(fd, 403, 0, "bad token"); return 0; }
            ret = netapp_ota_start(url, crc);
            if (ret == -RT_EINVAL) { json_reply(fd, 400, 0, "url must start with http:// and crc must be 8 hex digits"); return 0; }
            if (ret == -RT_EBUSY) { json_reply(fd, 409, 0, "device offline or another task is active"); return 0; }
            if (ret) { json_reply(fd, 500, 0, "cannot start OTA thread"); return 0; }
            json_reply(fd, 200, 1, "");
            return 0;
        }
        memset(c, 0, sizeof(*c)); memcpy(c->magic, "WFI1", 4);
        if (netapp_form(buf+header_len, c->ssid, c->password)) goto bad;
        response(fd, 200, portal_connecting);
        memset(buf, 0, sizeof(buf)); return 1;
    }
    if (get_root) response(fd, 200, sta ? ota_page : setup_page());
    else if (get_status) response_ct(fd, 200, "application/json", status_json());
    else if (sta) json_reply(fd, 404, 0, "not found");
    else redirect_to_setup(fd);
    return 0;
bad:
    if (sta) json_reply(fd, 400, 0, "bad request");
    else response(fd, 400, portal_error);
    memset(buf, 0, sizeof(buf)); return 0;
}

/* Reply only to A/ANY queries. DHCP already advertises the AP as DNS. */
static void dns_reply(int fd)
{
    unsigned char packet[512];
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    int n = lwip_recvfrom(fd, packet, sizeof(packet), 0, (struct sockaddr *)&peer, &peer_len);
    unsigned i = 12, end, label_len, qtype, qclass;
    if (n < 17 || (packet[2] & 0x80) || packet[4] != 0 || packet[5] != 1) return;
    while (i < (unsigned)n && packet[i]) {
        label_len = packet[i];
        if (label_len > 63 || i + label_len + 1 >= (unsigned)n) return;
        i += label_len + 1;
    }
    if (i + 5 > (unsigned)n) return;
    ++i;
    qtype = ((unsigned)packet[i] << 8) | packet[i + 1];
    qclass = ((unsigned)packet[i + 2] << 8) | packet[i + 3];
    end = i + 4;
    packet[2] = 0x81; packet[3] = 0x80;
    packet[6] = packet[7] = packet[8] = packet[9] = packet[10] = packet[11] = 0;
    if ((qtype == 1 || qtype == 255) && qclass == 1 && end + 16 <= sizeof(packet)) {
        static const unsigned char answer[] = {
            0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4, 192, 168, 4, 1
        };
        packet[7] = 1;
        memcpy(packet + end, answer, sizeof(answer));
        end += sizeof(answer);
    }
    lwip_sendto(fd, packet, end, 0, (struct sockaddr *)&peer, peer_len);
}

static int portal(struct netif *n, struct credentials *c)
{
    int server = -1, dns = -1, client, reuse = 1, result = -1;
    struct sockaddr_in addr;
    char ssid[33];
    size_t passlen = strlen(D133_SETUP_PASSWORD);
    if (passlen < 8 || passlen > 63) return -1;
    rt_snprintf(ssid, sizeof(ssid), "D133-Setup-%02X%02X%02X", n->hwaddr[3], n->hwaddr[4], n->hwaddr[5]);
    scan_before_portal();
    rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
    rt_strncpy(status.ap_ssid, ssid, sizeof(status.ap_ssid) - 1);
    rt_mutex_release(&state_lock);
    wifi_status("Opening setup AP", "", SETUP_IP, "", 1);
    LOCK_TCPIP_CORE(); dhcp_stop(n); UNLOCK_TCPIP_CORE();
    wifi_off(); rt_thread_mdelay(50);
    if (wifi_on(RTW_MODE_AP) < 0 || wifi_start_ap(ssid, RTW_SECURITY_WPA2_AES_PSK,
        D133_SETUP_PASSWORD, strlen(ssid), passlen, 6) != RTW_SUCCESS) {
        wifi_status("Setup AP failed", "", "", "Cannot start Wi-Fi hotspot", 0);
        return -1;
    }
    LOCK_TCPIP_CORE(); dhcpd_start(ifname); UNLOCK_TCPIP_CORE();
    memset(&addr, 0, sizeof(addr)); addr.sin_family = AF_INET; addr.sin_port = htons(80);
    addr.sin_addr.s_addr = inet_addr(SETUP_IP);
    server = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) goto out;
    lwip_setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    if (lwip_bind(server, (struct sockaddr *)&addr, sizeof(addr)) || lwip_listen(server, 1)) goto out;
    dns = lwip_socket(AF_INET, SOCK_DGRAM, 0);
    if (dns >= 0) {
        addr.sin_port = htons(53);
        if (lwip_bind(dns, (struct sockaddr *)&addr, sizeof(addr))) {
            lwip_close(dns); dns = -1;
        }
    }
    wifi_status("Setup AP ready", "", SETUP_IP, dns < 0 ? "DNS server unavailable" : "", 1);
    rt_kprintf("[netapp] Setup hotspot: %s; open http://" SETUP_IP "/\n", ssid);
    for (;;) {
        fd_set read_fds;
        struct timeval timeout = {1, 0};
        int selected;
        FD_ZERO(&read_fds);
        FD_SET(server, &read_fds);
        if (dns >= 0) FD_SET(dns, &read_fds);
        selected = lwip_select((server > dns ? server : dns) + 1, &read_fds, RT_NULL, RT_NULL, &timeout);
        if (selected < 0) break;
        if (dns >= 0 && FD_ISSET(dns, &read_fds)) dns_reply(dns);
        if (FD_ISSET(server, &read_fds)) {
            client = lwip_accept(server, RT_NULL, RT_NULL);
            if (client < 0) break;
            result = request(client, c, 0); lwip_close(client);
            if (result == 1) { rt_thread_mdelay(1000); break; }
        }
    }
out:
    if (dns >= 0) lwip_close(dns);
    if (server >= 0) lwip_close(server);
    LOCK_TCPIP_CORE(); dhcpd_stop(ifname); UNLOCK_TCPIP_CORE();
    if (result != 1) wifi_status("Setup AP failed", "", SETUP_IP, "AP or portal failed", 0);
    return result == 1 ? 0 : -1;
}

/* Online-mode web server: same port 80, OTA trigger/status routes only.
 * Started per connection cycle and closed before the setup portal opens. */
static int sta_server_open(void)
{
    int server, reuse = 1;
    struct sockaddr_in addr;
    server = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) return -1;
    lwip_setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (lwip_bind(server, (struct sockaddr *)&addr, sizeof(addr)) || lwip_listen(server, 2)) {
        lwip_close(server);
        return -1;
    }
    return server;
}

static void network_thread(void *arg)
{
    struct netif *n = RT_NULL;
    struct credentials c;
    unsigned i;
    int configured;
    int default_pending;
    int web = -1;
    (void)arg;
    for (i = 0; i < 30; ++i) {
        n = wifi_netif();
        if (n && wifi_is_up(RTW_STA_INTERFACE)) break;
        rt_thread_mdelay(1000);
    }
    if (!n || i == 30) { wifi_status("Wi-Fi unavailable", "", "", "Check RTL8189, SDIO and power", 0); rt_kprintf("[netapp] RTL Wi-Fi not ready; check module, SDIO and power GPIO.\n"); return; }
    configured = load_credentials(&c) == 0;
    default_pending = configured;
    if (!configured) {
        memset(&c, 0, sizeof(c));
        memcpy(c.magic, "WFI1", 4);
        rt_strncpy(c.ssid, DEFAULT_WIFI_SSID, sizeof(c.ssid) - 1);
        rt_strncpy(c.password, DEFAULT_WIFI_PASSWORD, sizeof(c.password) - 1);
    }
    for (;;) {
        if (!configured && !c.ssid[0]) {
            if (portal(n, &c)) { rt_thread_mdelay(3000); continue; }
        }
        if (join(n, &c)) {
            wifi_status("Connection failed", c.ssid, "", "Wi-Fi authentication or DHCP failed", 0);
            if (default_pending) {
                memset(&c, 0, sizeof(c));
                memcpy(c.magic, "WFI1", 4);
                rt_strncpy(c.ssid, DEFAULT_WIFI_SSID, sizeof(c.ssid) - 1);
                rt_strncpy(c.password, DEFAULT_WIFI_PASSWORD, sizeof(c.password) - 1);
                default_pending = 0;
                configured = 0;
                continue;
            }
            rt_kprintf("[netapp] Wi-Fi connection failed; returning to setup.\n");
            configured = 0; memset(&c, 0, sizeof(c)); continue;
        }
        if (!configured && save_credentials(&c))
            rt_kprintf("[netapp] Connected, but credentials could not be saved.\n");
        default_pending = 0;
        {
            char ip[16];
            LOCK_TCPIP_CORE();
            rt_snprintf(ip, sizeof(ip), "%u.%u.%u.%u", ip4_addr1(netif_ip4_addr(n)), ip4_addr2(netif_ip4_addr(n)), ip4_addr3(netif_ip4_addr(n)), ip4_addr4(netif_ip4_addr(n)));
            UNLOCK_TCPIP_CORE();
            wifi_status("Connected", c.ssid, ip, "", 0);
            web = sta_server_open();
            if (web < 0) rt_kprintf("[netapp] STA web server unavailable; use serial netapp_ota.\n");
            else rt_kprintf("[netapp] Web OTA ready: http://%s/ (token: setup password)\n", ip);
        }
        memset(&c, 0, sizeof(c));
        rt_mutex_take(&state_lock, RT_WAITING_FOREVER); online = 1; rt_mutex_release(&state_lock);
        for (;;) {
            int retry;
            if (web >= 0) {
                fd_set read_fds;
                struct timeval timeout = {1, 0};
                FD_ZERO(&read_fds);
                FD_SET(web, &read_fds);
                if (lwip_select(web + 1, &read_fds, RT_NULL, RT_NULL, &timeout) > 0 && FD_ISSET(web, &read_fds)) {
                    int client = lwip_accept(web, RT_NULL, RT_NULL);
                    if (client >= 0) {
                        request(client, RT_NULL, 1);
                        lwip_close(client);
                    }
                }
            } else rt_thread_mdelay(1000);
            rt_mutex_take(&state_lock, RT_WAITING_FOREVER);
            retry = !ota_busy && (setup_requested || wifi_is_connected_to_ap() != RTW_SUCCESS);
            if (retry) { online = 0; setup_requested = 0; }
            rt_mutex_release(&state_lock);
            if (retry) { wifi_status("Disconnected", "", "", "Link lost or setup requested", 0); break; }
        }
        if (web >= 0) { lwip_close(web); web = -1; }
        /* Explicit provisioning or link loss opens the setup portal. Stored credentials
         * remain intact until a replacement network has passed DHCP. */
        configured = 0;
        memset(&c, 0, sizeof(c));
    }
}

int netapp_start(void)
{
    rt_thread_t thread;
    if (started) return 0;
    rt_mutex_init(&state_lock, "netstate", RT_IPC_FLAG_PRIO);
    thread = rt_thread_create("netapp", network_thread, RT_NULL, 6144, 22, 10);
    if (!thread) return -RT_ENOMEM;
    started = 1;
    return rt_thread_startup(thread);
}

static void netapp_setup(void)
{
    if (netapp_request_setup()) rt_kprintf("[netapp] Setup unavailable while offline or OTA is active.\n");
}
MSH_CMD_EXPORT(netapp_setup, Reopen D133 provisioning hotspot);
