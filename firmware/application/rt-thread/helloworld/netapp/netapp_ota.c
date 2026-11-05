/* SPDX-License-Identifier: Apache-2.0 */
#include "netapp.h"
#include "protocol.h"
#include <finsh.h>
#include <fal.h>
#include <env.h>
#include <webclient.h>
#include <string.h>
#include <stdlib.h>

struct ota_job { char url[256]; uint32_t crc; };
struct flash_target { const struct fal_partition *part; uint32_t offset; };

static int begin_os(void *arg, uint32_t size)
{
    struct flash_target *t = arg;
    (void)size;
    return fal_partition_erase(t->part, 0, t->part->len) == t->part->len ? 0 : -1;
}
static int write_os(void *arg, const void *data, size_t size)
{
    struct flash_target *t = arg;
    unsigned char check[2048];
    if (size > sizeof(check) || size > t->part->len - t->offset ||
        fal_partition_write(t->part, t->offset, data, size) != size ||
        fal_partition_read(t->part, t->offset, check, size) != size || memcmp(data, check, size)) return -1;
    t->offset += size;
    return 0;
}
static uint32_t crc_update(uint32_t crc, const unsigned char *s, size_t size)
{
    unsigned i;
    while (size--) {
        crc ^= *s++;
        for (i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1)));
    }
    return crc;
}

static void ota_thread(void *arg)
{
    struct ota_job *job = arg;
    struct webclient_session *session = RT_NULL;
    struct flash_target target = {0};
    struct netapp_cpio parser;
    unsigned char *buffer = RT_NULL, magic[4];
    uint32_t crc = 0xffffffffU, received = 0;
    int length = 0, n, ret = -1, env_open = 0;
    rt_tick_t started_at = rt_tick_get();
    char active, *now;
    const char *failure = "OTA failed";
    netapp_set_ota_status("Checking partitions", 0, "");
    if (fw_env_open()) { failure = "Cannot open boot environment"; goto done; }
    env_open = 1; now = fw_getenv("osAB_now");
    if (!now || (strcmp(now, "A") && strcmp(now, "B"))) { failure = "Invalid active OS slot"; goto done; }
    active = *now;
    fw_env_close(); env_open = 0;
    target.part = fal_partition_find(active == 'A' ? "os_r" : "os");
    if (!target.part || !fal_partition_find("os") || !fal_partition_find("os_r")) {
        failure = "Missing A/B partitions";
        rt_kprintf("[netapp] Missing A/B partitions; flash the new base image first.\n"); goto done;
    }
    buffer = rt_malloc(2048); session = webclient_session_create(2048);
    if (!buffer || !session) { failure = "Out of memory"; goto done; }
    netapp_set_ota_status("Downloading", 0, "");
    if (webclient_get(session, job->url) != 200) { failure = "HTTP download failed"; goto done; }
    webclient_set_timeout(session, 10000);
    length = session->content_length;
    if (length < 1024 || (uint32_t)length > target.part->len + 2048) { failure = "Invalid package size"; goto done; }
    netapp_cpio_init(&parser, target.part->len);
    parser.arg = &target; parser.begin = begin_os; parser.write = write_os;
    rt_kprintf("[netapp] OTA downloading %d bytes to inactive %s.\n", length, target.part->name);
    while (received < (uint32_t)length) {
        if ((rt_tick_t)(rt_tick_get()-started_at) > 600*RT_TICK_PER_SECOND) { failure = "OTA timed out"; goto done; }
        size_t wanted = length - received; if (wanted > 2048) wanted = 2048;
        n = webclient_read(session, buffer, wanted);
        if (n <= 0 || n > wanted) { failure = "Download interrupted"; goto done; }
        if (netapp_cpio_feed(&parser, buffer, n)) { failure = "Package or flash error"; goto done; }
        crc = crc_update(crc, buffer, n); received += n;
        netapp_set_ota_status("Downloading / flashing", received * 90U / (uint32_t)length, RT_NULL);
    }
    netapp_set_ota_status("Verifying", 92, RT_NULL);
    if (!parser.done || parser.entries != 2 || parser.written != parser.os_size ||
        (crc ^ 0xffffffffU) != job->crc || fal_partition_read(target.part, 0, magic, 4) != 4 ||
        memcmp(magic, "\xd0\x0d\xfe\xed", 4)) { failure = "CRC or image validation failed"; goto done; }
    /* Commit only after the complete archive, expected CRC and flash readback pass.
     * Never update shared assets, Wi-Fi credentials, or the running OS partition. */
    netapp_set_ota_status("Committing", 97, RT_NULL);
    if (fw_env_open()) { failure = "Cannot open boot environment"; goto done; }
    env_open = 1; now = fw_getenv("osAB_now");
    if (!now || now[0] != active || now[1]) { failure = "Active OS changed"; goto done; }
    if (fw_env_write("osAB_next", active == 'A' ? "B" : "A") ||
        fw_env_write("bootcount", "0") || fw_env_write("upgrade_available", "1") || fw_env_flush()) { failure = "Boot environment commit failed"; goto done; }
    ret = 0;
done:
    if (env_open) fw_env_close();
    if (session) webclient_close(session);
    if (buffer) rt_free(buffer);
    rt_free(job);
    if (!ret) {
        extern void rt_hw_cpu_reset(void);
        netapp_set_ota_status("Rebooting", 100, "");
        rt_kprintf("[netapp] OTA verified; rebooting to new OS.\n");
        rt_thread_mdelay(200); rt_hw_cpu_reset();
    }
    netapp_set_ota_status("Failed", received && length > 0 ? received * 90U / (uint32_t)length : 0, failure);
    rt_kprintf("[netapp] OTA failed; boot selection unchanged unless ENV commit itself failed.\n");
    netapp_ota_unlock();
}

int netapp_ota_start(const char *url, const char *crc_hex)
{
    struct ota_job *job;
    rt_thread_t thread;
    char *end;
    unsigned long crc;
    size_t i;
    if (!url || !crc_hex || strncmp(url, "http://", 7) || strlen(url) >= 256 || strlen(crc_hex) != 8)
        return -RT_EINVAL;
    for (i = 0; i < 8; ++i) if (!((crc_hex[i] >= '0' && crc_hex[i] <= '9') ||
        (crc_hex[i] >= 'a' && crc_hex[i] <= 'f') || (crc_hex[i] >= 'A' && crc_hex[i] <= 'F')))
        return -RT_EINVAL;
    crc = strtoul(crc_hex, &end, 16); if (*end) return -RT_EINVAL;
    if (netapp_ota_lock()) {
        rt_kprintf("[netapp] Requires connected Wi-Fi and no active OTA/setup.\n");
        return -RT_EBUSY;
    }
    netapp_set_ota_status("Starting", 0, "");
    job = rt_calloc(1, sizeof(*job));
    if (!job) { netapp_set_ota_status("Failed", 0, "Out of memory"); netapp_ota_unlock(); return -RT_ENOMEM; }
    strcpy(job->url, url); job->crc = (uint32_t)crc;
    thread = rt_thread_create("netota", ota_thread, job, 8192, 24, 10);
    if (!thread) { rt_free(job); netapp_set_ota_status("Failed", 0, "Cannot start OTA thread"); netapp_ota_unlock(); return -RT_ENOMEM; }
    rt_thread_startup(thread);
    return RT_EOK;
}

static void netapp_ota(int argc, char **argv)
{
    int ret;
    if (argc != 3) goto usage;
    ret = netapp_ota_start(argv[1], argv[2]);
    if (ret == -RT_EINVAL) goto usage;
    if (ret && ret != -RT_EBUSY) rt_kprintf("[netapp] OTA start failed (%d).\n", ret);
    return;
usage:
    rt_kprintf("netapp_ota http://LAN-SERVER/ota.cpio CRC32_HEX\n"
        "Development LAN only: obtain CRC32 from the trusted build computer.\n");
}
MSH_CMD_EXPORT(netapp_ota, Download and verify OS-only A/B OTA package);
