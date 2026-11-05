# 衡山派 AP 配网与 OS OTA

## 当前实现

面向当前 JLC / D133EBS、RTL8189 SDIO、RT-Thread 工程。沿用本机现有配置的 PC.7 Wi-Fi 电源脚和 SDMC 引脚；官方硬件资料也说明板卡兼容 RTL8733BS，因此实际模块如果不是 RTL8189，需在 menuconfig 修改驱动选择。不能仅凭板名证明具体模块型号。

启动后等待 RTL 网卡初始化。先尝试已保存的凭据；若不存在或连接失败，尝试默认路由器 SSID **`123456789`**、密码 **`123456789`**。仍无法连接时，设备先在 STA 模式扫描附近网络并缓存列表，再启动 WPA2 热点 `D133-Setup-xxxxxx`。默认**设备热点**密码为 `D133setup`，可在 `D133_SETUP_PASSWORD` 修改。手机连上热点后，访问 **http://192.168.4.1/**，可在手机配网网页点选已发现的网络，也可手动输入 SSID 和密码。开发板 AP 画面仍显示热点名称、热点密码与访问地址。支持 WPA2 或开放网络；未实现 WEP、WPA3-only、企业认证。

配网页面仅在 AP 模式绑定 192.168.4.1:80；同一阶段在 UDP 53 回答 A/ANY 查询为 192.168.4.1，对手机联网探测 HTTP 请求重定向至配网页面。设备热点的 DHCP 已将 AP 地址作为 DNS 发给客户端。手机是否自动弹出配网页仍取决于系统的探测行为；HTTPS 请求无法被透明重定向。切换 STA 前关闭 HTTP、DNS 和 DHCP 服务。TCP 分片、POST 长度、URL 编码和重复字段均检查。连接并取得 DHCP 地址后，凭据写入 `/data/wifi.cfg`，临时文件 fsync 后 rename；保存失败时保持已建立的连接并输出诊断。不会打印路由器密码。凭据是明文 LittleFS 文件，未实现 Flash 加密。掉线会重新开放配网热点；现有凭据保留到新网络连接且保存成功。

串口 shell 命令 `netapp_setup` 可在在线状态重新打开配网（界面已不提供「重新配网」按钮：连接正常时不需要，掉线时固件会先用已保存凭据重试，失败才自动开热点进入配网页）；OTA 过程中会拒绝切换网络。网页 OTA 的升级口令是独立配置项 `D133_OTA_TOKEN`（默认 `D133ota`，与配网热点密码分开），主页右上角常显本机 IP 方便封箱后找到 `http://<IP>/`。屏幕显示当前 Wi-Fi 状态、SSID、热点名称、OTA 阶段/进度/错误，状态由网络和 OTA 线程发布，LVGL 定时器负责刷新。

界面现由 `application/rt-thread/helloworld/netapp/ui/ui_app.c` 手写实现，包含启动、AP 配网和传感器主页，使用 SDK 的 LVGL 9.1。`netapp_ui.c` 将网络状态快照交给界面，配网中显示 AP 页，在线且获得 IP 后显示主页，其余状态显示启动页。手机配网网页在 `application/rt-thread/helloworld/netapp/portal.html` 编辑，运行 `python generate_portal.py` 生成固件内嵌的 `portal_html.h`。扫描在开启 AP 前执行一次；AP 运行时不再发起扫描，以免单无线芯片离开热点信道导致手机断开。RTW 回调不等待互斥锁。若扫描失败，网页仍允许手动输入 SSID。当前传感器驱动尚未接入，主页读数显示 `--`。旧的 `aido_export` 文件保留在工作目录，但不参与编译。SDK 同级的 `lvgl_simulator` 可在电脑上运行三页 LVGL 界面；模拟器的传感器数字是示例数据。

## 编译

在 SDK 根目录运行：

```powershell
.\tools\netapp\build.ps1
```

脚本使用 SDK 自带工具，只影响当前进程的环境变量。先编译 Bootloader，再加载专用配置 `d13x_JLC_rt-thread_helloworld_netapp_defconfig` 编译应用。状态屏接管当前 LVGL demo 入口，用户 LED 应用继续运行；原始 helloworld 配置没有被覆盖。注意 JLC 的板级分区文件是共享的，其他 JLC 配置重新编译也会使用新分区布局。

输出：

- `output/d13x_JLC_rt-thread_helloworld_netapp/images/d13x_JLC_v1.1.0.img`：首次 USB 烧录的基础镜像。
- 同目录 `ota.cpio`：以后通过网络升级 OS 的升级包。
- `artifacts/baseline/d13x_JLC_v1.0.0.img`：编译前保留的旧镜像。

## Flash 布局与首次部署

16 MiB NOR：`spl` 256 KiB、`env` 128 KiB、`env_r` 128 KiB、`os` 3328 KiB、`os_r` 3328 KiB、`rodata` 8 MiB、`data` 1 MiB，合计恰好 16 MiB。界面资源当前能容纳在 8 MiB 内；后续新增资源超限时构建应报错，不能直接扩大而挤占其他分区。

旧板上固件没有 `os_r`，**必须先通过 USB 烧录这次新生成的完整基础镜像**，不能向旧分区直接 OTA。首次重新分区会写入新的 data 镜像，旧配置/数据要先备份。当前状态屏镜像已编译，尚未烧录到板上；完整重刷可能清除已经保存的 Wi-Fi 凭据，需要重新配网。

OTA 只更新 OS，**不更新 rodata、data、Bootloader 或分区表**，所以未来固件须兼容保留的界面资源与数据格式；改变这些内容需另做迁移方案。`ota-subimgs.cfg` 只包含 `ota_info.bin` 和 `d13x_os.itb`。RODATA 不做 A/B，因而没有把 16 MiB Flash 当作可容纳两整套 LVGL 资源的空间。

## OTA 使用

### 网页触发（推荐联调方式）

1. 设备配网在线后，串口会打印 `Web OTA ready: http://<设备IP>/`；浏览器打开该地址即是升级页面。
2. 用构建电脑按下面第 2、3 步准备好 `ota.cpio` 的 CRC32 和 HTTP 下载服务。
3. 在页面填写升级包地址、8 位 CRC32 和升级口令（即设备热点密码 `D133_SETUP_PASSWORD`，默认 `D133setup`），点"开始升级"。页面每 1.5 秒轮询 `/ota_status` 显示阶段与进度，校验通过后设备自动重启。

在线模式的 HTTP 服务与配网页共用 80 端口，仅提供三个路由：`GET /`（升级页面）、`GET /ota_status`（状态 JSON）、`POST /ota`（触发升级）。`POST /wifi` 只在配网热点模式有效。口令不匹配返回 403，设备离线或已有任务在跑返回 409。这是开发联调版的最小防护：无签名、无 HTTPS，仅限可信局域网。

### 串口触发

1. 设备完成配网，电脑和开发板处于同一可信局域网。
2. 用构建电脑计算包的校验值：

   ```powershell
   python tools/netapp/ota_manifest.py output/d13x_JLC_rt-thread_helloworld_netapp/images/ota.cpio --url http://电脑局域网IP:8000/ota.cpio
   ```

3. 在该 images 目录提供静态 HTTP 下载服务（服务端必须提供 Content-Length），例如仅用于局域网联调的 `python -m http.server 8000 --bind 电脑局域网IP`。
4. 在板卡串口 shell 执行脚本打印的 `netapp_ota http://.../ota.cpio 八位CRC32`。CRC32 必须取自可信构建电脑，不要使用下载服务器返回的未验证校验值。

单独 OTA 线程顺序下载，约 2 KiB 接收缓冲区，不依赖 TF 卡或全包内存。先确认 `osAB_now`，仅向另一个 FAL OS 分区写入。限定 CPIO 条目名、顺序、长度、容量和 OS 校验和；每次写入立即回读比对。整个包完整结束、CRC32 匹配、FIT 魔数正确后，才一次性写入下一启动分区并重启。下载中断或内容校验失败时不会提交启动切换，可再次执行 OTA 重试。SDK 示例 `test_ota` 在本配置中不编译，避免绕过校验入口。

这是 **开发联调版 HTTP OTA**。CRC32/CPIO 校验和及回读检查用于检测传输损坏，不提供发布者身份认证、抗恶意篡改或防降级。未接入 HTTPS、签名验签、云端版本检查或生产密钥管理；没有自动定时升级。SDK Bootloader 在加载新固件时即清除升级标志，本实现不宣称具备应用健康确认后的自动回滚。生产 OTA 需要补齐这些机制。

升级期间不要通过其他 shell 命令直接修改 ENV、重连 Wi-Fi 或写 Flash。断电恢复/反复 A-B 升级仍须上板验证，尤其是 ENV 备用副本切换；不以编译通过替代掉电测试。

## 验证

主机协议测试（本机已有 GCC）：

```powershell
gcc -std=c99 -Wall -Wextra -Werror -I application/rt-thread/helloworld/netapp tools/netapp/test_protocol.c application/rt-thread/helloworld/netapp/protocol.c -o tools/netapp/test_protocol.exe
.\tools\netapp\test_protocol.exe
.\tools\netapp\test_protocol.exe output/d13x_JLC_rt-thread_helloworld_netapp/images/ota.cpio
```

覆盖 257 种 TCP 数据分片尺寸、中文/特殊字符表单、重复参数、超长 SSID、截断包、坏包、路径穿越、共享资源升级拒绝与分区容量超限，以及网页 OTA 表单的必需字段、重复字段、超长 CRC 与 NUL 注入。实际 OTA 包也用同一 C 解析器检查。硬件仍需验证：首次启动热点、手机获取 IP、提交错误密码后回到热点、保存后重启重连、A→B→A 两次升级（网页与串口各一次）、断网和掉电恢复。

## 官方依据

- [衡山派主页](https://wiki.lckfb.com/zh-hans/hspi-d133ebs/)
- [无线网络：SDMC、Realtek、lwIP 与内核配置](https://wiki.lckfb.com/zh-hans/hspi-d133ebs/rtos-sdk/peripherals-migration/wireless-network.html)
- [OTA 设计：备用分区、环境变量和 CPIO 包](https://wiki.lckfb.com/zh-hans/hspi-d133ebs/rtos-sdk/application-scenarios/ota-scheme/design-specification.html)
- SDK 实际接口：`wifi_conf.h`、`dhcp_server_raw.c`、`env.h`、FAL 与 `mkcpio.py`。
