# D133 联网小屏 v1.1.0

衡山派 D133EBS 联网小屏：AP 配网 + OS A/B 无线 OTA + LVGL 9.1 界面。

## 下载

| 文件 | 大小 | 用途 |
|---|---|---|
| `d13x_JLC_v1.1.0.img` | 10,905,600 B | 出厂/返修 **USB 整片烧录**（AiBurn GUI → Burn online） |
| `ota.cpio` | 1,666,048 B | **无线/网页 OTA** 升级包（只升级 OS） |
| `SHA256SUMS.txt` | — | 下载后校验 |
| `ota_manifest.json` | — | OTA 包 CRC32/SHA256 |
| `size-report.csv` | — | 镜像体积明细（OS 分区占用 49%） |

校验：

```powershell
Get-FileHash .\d13x_JLC_v1.1.0.img -Algorithm SHA256
Get-FileHash .\ota.cpio -Algorithm SHA256
```

## 本版本内容

- **无线 OTA 可用**：设备联网后主页右上角显示本机 IP，浏览器打开 `http://<IP>/` 即可升级，无需串口；
- **掉线自恢复**：链路中断先用已存凭据重连（掉线 6 次×5 s、开机 3 次），失败才开热点；
- **OTA 独立口令**：新增 `CONFIG_D133_OTA_TOKEN`（默认 `D133ota`，与热点密码分离）；
- **界面更新**：启动页 CPU 图标 + 固定 5 秒进度；主页传感器图标、趋势图卡；OTA 进度条仅升级时出现；
- **A/B 分区**：OTA 只写非活动 OS 槽，下载/校验/回读通过后才切换启动槽。

## 烧录

1. 下载 `d13x_JLC_v1.1.0.img` 并校验 SHA256；
2. 板子进入 USB 升级模式（按住 BOOT 上电/复位）；
3. AiBurn（2.1.0）→ 选择镜像 → **Burn online**。

> 命令行 `upgcmd image` 在可执行 fwc 重枚举后会报 `No device is connected`（工具缺陷），请以 GUI 结果为准。

## 无线升级（v1.1.0 → 后续版本）

1. 设备联网，记下屏幕右上角 IP；
2. 电脑在 `ota.cpio` 目录起 HTTP 服务（端口如 8001）；
3. 浏览器打开 `http://<设备IP>/`，填写包地址、CRC32（本版 `E4AF3CAC`）、口令（`D133ota`，部署前请修改）；
4. 屏幕显示进度，完成后自动重启。

完整流程见仓库 `docs/deployment.md`。

## 已知限制

- OTA 为 HTTP 明文 + 静态口令，无签名/防降级，仅限可信局域网；
- 暂无健康确认回滚：坏固件只能 USB 重刷，量产前建议补齐；
- 传感器驱动未接入，主页读数显示 `--`；
- 镜像含 7.7 MB SDK demo 资源（界面未使用），可关闭以缩小镜像。

## 版本信息

- git tag：`v1.1.0`（提交 `1b1333e`）
- SDK：luban-lite `f5be1e4c`（衡山派发布版 V1.0）
- 适用硬件：衡山派 D133EBS（JLC）、480×272 RGB565、RTL8189 SDIO
