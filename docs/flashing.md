# 烧录与上板验证

## 结论先说

**用 AiBurn 图形界面的 “Burn online”，不要用命令行 `upgcmd image`。**

同一块板、同一个镜像：GUI 6/6 成功（每次 60–64 s），CLI 6/6 失败。失败点固定在烧录进度 0.25% 左右：

```
Upgrade fwc: image.updater.psram, size 28688 ...
__do_fwc_upgrade()371: It is executable fwc, reconnect the upg device
__open_usb_dev_locked()280: USB device not found!   (重试 10 次，每次 2 s)
image_do_upgrade_inner()619: No device is connected
```

设备加载可执行 fwc（PSRAM 初始化）后会重新枚举，GUI 通过 WinEvent 在 0.8–1.0 s 内重连上并继续，而 CLI 版本只按未初始化的 `Bus:Port 255` 轮询。**这是工具缺陷，不是镜像、线缆、驱动或板子的问题**——0.25% 时目标分区还一个字节都没写。

## 环境

- AiBurn 2.1.0（`aic-tools/AiBurn-2.1.0_Setup.zip`），会安装 WHQL WinUSB 驱动
- 板子 USB 标识：`VID_33C3&PID_6677`
  > 注意：网上/旧文档里常说的 `VID_3277&PID_0010` 不是板子，而是笔记本内置摄像头。用错 VID 会得出“设备没重新枚举”的错误结论。

## 步骤

1. 打开 AiBurn → 选择板卡/芯片（D13x、JLC 配置）→ 加载
   `luban-lite\output\d13x_JLC_rt-thread_helloworld_netapp\images\d13x_JLC_v1.1.0.img`；
2. 点 **Burn online**，等待 “Burn online successfully!”；
3. 打开串口（UART0，115200-8-N-1）看启动日志；
4. 依次确认：热点 `D133-Setup-XXXXXX` 出现 → 手机连热点打开 `http://192.168.4.1/` → 提交 Wi-Fi → 重启后自动回连 → 主页显示。

成功证据请以 `C:\ArtInChip\AiBurn\log\AiBurn_log_*.log` 里的 `Burn online successfully!` 为准，不要拿 `flash-*.log`（CLI）里的 0.25% 报错判断镜像有问题。

## 镜像与分区

16 MiB NOR，合计正好 16 MiB：

| 分区 | 大小 | 用途 | 当前占用 |
|---|---|---|---|
| spl | 256 KiB | bootloader | 188 KB（72%） |
| env / env_r | 128 KiB ×2 | 引导环境（冗余） | ~4 KB |
| os | 3328 KiB | 应用（A 槽） | 1632 KiB（49%） |
| os_r | 3328 KiB | 应用（B 槽，OTA 目标） | 空 |
| rodata | 8 MiB | 只读资源（FatFS） | 7.36 MiB（**92%，SDK demo 资源，界面未使用**） |
| data | 1 MiB | 持久数据（LittleFS，`/data/wifi.cfg`） | 按分区格式化 |

- **首次部署必须整片烧录**这次的新镜像：旧固件没有 `os_r` 分区，不能直接 OTA，否则分区表不匹配。
- 整片重刷可能清掉已保存的 Wi-Fi 凭据，需要重新配网。
- `rodata.fatfs` 里 7.7 MB 是 SDK `demo_hub` 的 344 个资源文件，netapp 界面不读取；在 defconfig 里关闭 `CONFIG_AIC_USING_FS_IMAGE_0` 可把镜像从 10.9 MB 降到约 3.2 MB，烧录时间也会相应缩短。

## OTA（可选）

首次烧录成功以后，OS 升级走局域网：

```powershell
# 1. 电脑起静态服务器（必须提供 Content-Length）
cd luban-lite\output\d13x_JLC_rt-thread_helloworld_netapp\images
python -m http.server 8000 --bind <电脑局域网 IP>

# 2. 生成可信校验值和命令
python ..\..\..\tools\netapp\ota_manifest.py --url http://<电脑IP>:8000/ota.cpio ota.cpio
```

设备在线后串口会打印 `Web OTA ready: http://<设备IP>/`，也可以直接在网页里填地址 + 8 位 CRC32 + 口令（默认热点密码）。串口方式：

```text
netapp_ota http://<电脑IP>:8000/ota.cpio <8位CRC32>
```

> 安全边界：明文 HTTP、无签名、CRC32 只防传输损坏，不防恶意篡改；仅限可信局域网使用。

## 常见问题

| 现象 | 处理 |
|---|---|
| `No device is connected` | 用 AiBurn GUI；CLI 只用 `upgcmd -l` 探测设备 |
| 烧完不进系统 | 确认整片镜像而不是 OTA 包；串口看 bootloader 日志 |
| 串口无输出 | 确认 UART0、115200；AiBurn.ini 里 `read_device_log=0` 不读设备日志 |
| 设备反复重启 | 检查 `osAB_now/osAB_next`；当前版本无健康确认回滚，坏固件只能重刷 |
