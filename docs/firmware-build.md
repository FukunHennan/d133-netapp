# 编译固件（luban-lite + netapp）

本项目不把 SDK 放进仓库，而是用 `setup.ps1` 把项目文件叠加到一个固定版本的 luban-lite 上。

## 依赖

| 项 | 要求 | 说明 |
|---|---|---|
| Windows | 10/11 x64 | SDK 自带 Xuantie RISC-V 工具链，无需单独安装编译器 |
| Git | ≥ 2.30 | 克隆 SDK、应用 patch |
| Python | 3.8+（SDK 自带 Python 3.8） | `build.ps1` 会用 SDK 内的 Python |
| 磁盘 | ≥ 6 GB 空闲 | SDK 约 1.9 GB + 构建产物 |
| 网络 | 首次需访问 gitee.com | 克隆 luban-lite |

## 1. 取得 SDK 并叠加项目

```powershell
cd d133-netapp
.\setup.ps1                # 克隆 SDK 到 .\luban-lite，应用 patch，拷入项目文件
```

脚本做四件事：

1. `git clone https://gitee.com/lcsc/luban-lite.git .\luban-lite`，切到 `f5be1e4c`（衡山派发布版 V1.0）；
2. 应用 `firmware/patches/0001-d133-netapp-sdk-changes.patch`，改的是 8 个 SDK 文件：
   - `application/rt-thread/helloworld/Kconfig`、`main.c`：接入 netapp 启动入口
   - `packages/artinchip/lvgl-ui/aic_ui.c`：把 LVGL demo 入口改道到 `netapp_ui_init()`
   - `packages/artinchip/ota/SConscript`：排除会绕过校验的 SDK 示例 `test_ota.c`
   - `packages/artinchip/ota/absystem.c`：避免 `/data` 被 FAT 重复挂载
   - `packages/third-party/lwip/lwipopts.h`：配网服务重开时允许 `SO_REUSE`
   - `target/d13x/JLC/pack/image_cfg.json`、`ota-subimgs.cfg`：加入 `os_r` 分区、版本号 1.1.0、OTA 只带 OS
3. 拷入 `application/rt-thread/helloworld/netapp/`、defconfig、`tools/netapp/`、设计文档、`fonts_src/`；
4. `-Build` 时直接编译。

脚本可重复执行：检测到 patch 已应用会自动跳过。

## 2. 编译

```powershell
.\luban-lite\tools\netapp\build.ps1            # 或 .\setup.ps1 -Build
```

脚本先编 bootloader（`d13x_JLC_baremetal_bootloader_defconfig`），再编应用（`d13x_JLC_rt-thread_helloworld_netapp_defconfig`），最后校验 `img`/`ota.cpio` 的时间戳不早于 `d13x.elf`，避免拿旧包去烧。

产物在 `luban-lite\output\d13x_JLC_rt-thread_helloworld_netapp\images\`：

| 文件 | 用途 |
|---|---|
| `d13x_JLC_v1.1.0.img` | 首次/整片 USB 烧录 |
| `ota.cpio` | 局域网网页或串口 OTA（只含 OS） |
| `ota_info.bin` / `d13x_os.itb` | CPIO 内的 OTA 元数据和 OS 镜像 |
| `d13x.map` / `d13x.detail.csv` | 体积分析（排查镜像超限用） |

## 3. 重新生成内嵌页面（改了 portal.html 时）

```powershell
python .\luban-lite\application\rt-thread\helloworld\netapp\generate_portal.py
```

它会用同目录的 4 个 HTML 重新生成 `portal_html.h`（固件内嵌页面），然后重新编译。

## 4. 重新生成中文字体（改了界面文案时）

```powershell
python .\luban-lite\application\rt-thread\helloworld\netapp\ui\generate_fonts.py
```

需要 Node.js（首次会 `npx lv_font_conv` 下载工具）。脚本扫描 `ui_app.c` 和模拟器 `main.c` 里出现的全部汉字/`°`，生成 16/24/36 px 字库。**新增汉字不重跑就会显示成空心方框**。

也可以在仓库根目录跑同一脚本（它向上查找 `fonts_src/_static.ttf` 和模拟器 `main.c`），生成后再 `setup.ps1 -SkipClone` 覆盖到 SDK。

## 5. 常见问题

| 现象 | 原因 / 处理 |
|---|---|
| `git apply` 失败 | SDK 不是 V1.0 或工作区被改过；删掉 `luban-lite` 重新 clone |
| `scons: command not found` | 直接运行 `build.ps1`，它会设置 SDK 自带工具链和 Python 的环境变量 |
| 镜像超 3328 KiB | 看 `d13x.summary.csv` / `d13x.detail.csv` 找大对象；当前 OS 段约 1632 KiB，余量约 1.7 MiB |
| 改了中文显示方框 | 没重跑字体脚本，或新字不在生成范围内 |
| 烧录报 `No device is connected` | upgcmd 的已知重连缺陷，改用 AiBurn GUI，见 [flashing.md](flashing.md) |
| 提示 Python 是 Microsoft Store 占位符 | 用 `python`（3.8+）而不是 `python3`；SDK 的 `build.ps1` 内部已处理 |
