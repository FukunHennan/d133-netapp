# 架构说明

## 启动与线程

```
main()  (prio 10, 2 KB)
 ├─ netapp_start() → 建 "netapp" 线程 (prio 22, 6 KB)
 │    └─ 等 Wi-Fi → 读凭据 → join+DHCP → 在线 / 开热点配网 → 掉线重开
 └─ (SDK) LVGL 线程 (prio 20, 8 KB 静态栈) → aic_ui_init()
      └─ D133_NETAPP 下改道 netapp_ui_init()
           ├─ ui_app_init(callback)  三页控件树
           └─ 500 ms lv_timer 读状态快照 → ui_app_update()
netota 线程 (prio 24, 8 KB) 仅 OTA 期间存在
tshell (prio 20, 4 KB) / lwIP TCP (prio 10, 2 KB)
```

**约定：界面只在 LVGL 线程刷新**。网络线程只写 `struct netapp_status`（一把 `state_lock`），LVGL 定时器读快照，按钮回调也只调用 `netapp_request_setup()` 这种短临界区接口。

## 网络状态机（netapp.c）

```
等 Wi-Fi 就绪(≤30s)
  → 有已存凭据? join+DHCP(≤30s)
       成功 → 在线（起 :80 OTA 服务端）
       失败 → 用默认 SSID 123456789 再试一次
  → 无凭据或都失败 → 开热点配网
       D133-Setup-<MAC后3字节>，WPA2，口令 = D133_SETUP_PASSWORD(默认 D133setup)
       scan(STA 态，≤10s) → wifi_off → wifi_on(AP) → start_ap → dhcpd_start
       :80 HTTP 配网页 + :53 DNS 应答(192.168.4.1) + HTTP 探测重定向
       收到 POST /wifi → 校验表单 → 回 connecting 页 → 关 socket/DHCPD → join
  → 在线期间每 1 s 检查链路；掉线或用户点「重新配网」→ 回配网
```

保存：`/data/wifi.cfg`（LittleFS，明文，`magic + ssid + password`），临时文件 fsync 后 rename。**不会打印路由器密码**。

## OTA（netapp_ota.c + protocol.c）

```
网页 POST /ota 或串口 netapp_ota <url> <crc32>
  → 校验 url/CRC 格式、加锁（要求在线且无其它任务）
  → 读 osAB_now，选非活动分区（A→os_r，B→os）
  → HTTP GET 流式下载（2 KB 缓冲，10 s 超时，总超时 600 s）
  → 边下载边解析 CRC-CPIO：只接受 ota_info.bin + d13x_os.itb 两个条目
  → 每 2 KB 写入后立即回读比对；容量/名称/顺序/长度全部校验
  → 全部通过后校验：CPIO 结构 + CRC32 + FIT 魔数
  → 重新打开 env 确认 osAB_now 未变
  → 写 osAB_next / bootcount=0 / upgrade_available=1 + flush
  → 重启到新槽
```

下载中断、校验失败、写 flash 失败都不提交 env，可重试；只有完整校验通过才切启动槽。

## 分区与内存

- 16 MiB NOR：`256k spl / 128k env / 128k env_r / 3328k os / 3328k os_r / 8m rodata / 1m data`
- OS 段当前 1632 KiB / 3328 KiB（49%）；`ota.cpio` 1672 KB 同样远低于上限
- PSRAM 8 MiB 放代码/数据/帧缓冲；SRAM 1 MiB 主要用于 bootloader 和中断栈

## 已知风险（按上板影响排序）

| 风险 | 说明 |
|---|---|
| LVGL 线程栈 8 KB | `lv_rt_thread_port.c` 用 `LV_MAIN_THREAD_STACK_SIZE=4096*2` 建栈，Kconfig 的 `LPKG_LVGL_THREAD_STACK_SIZE=32768` 未被 v9 port 使用；4 bpp 中文绘制+多页面下有溢出风险，建议改 16–32 KB 并用 `list_thread` 看水位 |
| LVGL 堆 64 KB | 板端 `lv_conf.h` 未设 `LV_MEM_SIZE`，取 LVGL 默认 64 KB；而 UI 每 500 ms 无条件重设约 10 个 label，`lv_label_set_text` 每次 free+malloc+invalidate → 长期堆碎片/无用重绘。建议同值跳过或改 `lv_label_set_text_static` |
| 无有效回滚 | bootloader 在 `aic_get_os_to_startup()` 里加载新 OS 后立即清 `upgrade_available`，`bootcount/bootlimit` 永不触发；「能启动但随后崩」只能 USB 重刷 |
| 掉线不重试凭据 | 链路一断直接开热点等人配网，现场断网需人工干预；建议先对已存凭据重试 N 次 |
| 配网页失败路径 | `portal()` 在 bind/listen 失败时直接 return，没有 `wifi_off`，下次会先扫描再关 AP，可能踢掉手机 |
| rodata 92% | 7.7 MB 是 SDK demo 资源，界面不读；关闭 `CONFIG_AIC_USING_FS_IMAGE_0` 可大幅瘦身 |
| 色深 | 板端 RGB565 16 bpp（模拟器已对齐），深色渐变仍有轻微色带，属预期 |
| OTA 安全 | 明文 HTTP + 明文口令 + 无签名；仅可信局域网联调用 |
