# D133 LVGL 界面调试流程

适用范围：衡山派 D133EBS（JLC）+ luban-lite V1.0 + LVGL 9.1 的手写界面。AiDoGUI Designer 那条路已经退出编译（旧导出只留在历史目录里），不属于当前流程。

## 1. 界面代码只有一份

- 实现：`firmware/application/rt-thread/helloworld/netapp/ui/ui_app.c`，三页（启动 / AP 配网 / 传感器主页），480×272，颜色用文件顶部 `C_BG`、`C_PANEL`、`C_CYAN` 等常量。
- 对外接口：`ui_app.h` 的 `ui_app_init()`、`ui_app_update()`、`ui_app_set_sensor_text()`、`ui_app_show()`，约定全部在 LVGL 线程调用；状态用 `ui_app_status_t` 快照传入，界面不直接碰网络/RTOS。
- 中文字体：同目录 `ui_font_16.c`（约 138 KB 源文件）、`ui_font_24.c`（约 254 KB）、`ui_font_36.c`（约 18 KB），由 `generate_fonts.py` 生成，不手改。
- 桌面模拟器编译的就是这几个文件本身，不是副本，所以电脑上看到的布局和板子上跑的是同一份代码。

## 2. 桌面模拟器：先看图，再上板

`simulator/` 用 CMake + Ninja + MinGW 直接编译 SDK 里的 LVGL 9.1（`packages/artinchip/lvgl-ui/lvgl_v9/lvgl`）和 SDL（`packages/third-party/awtk-ui/awtk/3rd/SDL`）。

```powershell
cd simulator
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' -DDIRECTX=OFF
cmake --build build --parallel 4
.\build\d133_preview.exe --page boot
.\build\d133_preview.exe --page ap
.\build\d133_preview.exe --page main
```

窗口 480×272，鼠标点击当触摸，用来验证页面切换、OTA 进度条这类交互。`main.c` 里的网络和传感器数值是示例数据，只为把三页填满；板子上走 `netapp_get_status()`，传感器在驱动接入前显示 `--`。加 `--page ota` 可预览升级中的进度条。

**颜色深度必须与板端一致（16 bpp）**：`simulator/lv_conf.h` 里 `LV_COLOR_DEPTH 16`，`CMakeLists.txt` 里 `CACHE_LINE_SIZE=32`。用 32 bpp 预览会掩盖板端 RGB565 的渐变色带。

## 3. 无窗口快照：把渲染结果落成图片

改完布局不必开窗口，直接出图逐像素核对：

```powershell
cd simulator
$env:SDL_VIDEODRIVER = 'dummy'
Start-Process -FilePath .\build\d133_preview.exe -ArgumentList '--page','ap','--snapshot','screenshots\ap.bmp' -NoNewWindow -Wait
Remove-Item Env:SDL_VIDEODRIVER
python -c "from PIL import Image; Image.open(r'screenshots\ap.bmp').save(r'screenshots\ap.png')"
```

`--snapshot` 在第 30 帧从 SDL renderer 读像素存 BMP。BMP 是 522 KB，转 PNG 后约 19 KB，方便查看或贴图。两个环境细节：用 `python`（3.8+，不是 Store 占位符的 `python3`）；非交互 shell 里要用 `Start-Process -Wait`，否则进程可能来不及写文件。

## 4. 中文字体闭环

界面文字一改就要重新生成字形，否则新增汉字显示成空心方框（而且 `LV_LOG_LEVEL_ERROR` 不会打日志）：

```powershell
python firmware\application\rt-thread\helloworld\netapp\ui\generate_fonts.py
```

脚本扫描 `ui_app.c` 和 `simulator/main.c` 里出现的汉字和 `°`，用 `npx lv_font_conv` 从 `fonts_src/_static.ttf`（Noto Sans SC 静态字体）生成 16/24/36 三档 C 数组；36 号只带数字和符号，给主页大号读数用。需要 Node.js，首次会联网下载 `lv_font_conv`。生成后模拟器和固件都要重编。

字体是编译进镜像的数组：当前三档合计约 62 KiB（占 OS 段 3.8%），不是体积瓶颈；但字体在 OS 段，改字库必须随 OTA 下发。

## 5. 上板后的界面链路

- `netapp_ui.c` 建一个 500 ms 的 `lv_timer`，每次取网络和 OTA 状态快照调 `ui_app_update()`：配网中显示 AP 页，在线且拿到 IP 显示主页，其余显示启动页。
- 界面刷新只在 LVGL 线程，业务线程只发布状态。出现卡顿或撕裂先查是不是从别的线程直接调了 `ui_app_*`。
- 串口 shell 的 `netapp_setup` 可在在线状态手动重新打开配网（界面上不再放「重新配网」按钮，掉线时固件会自动开热点进入配网页），OTA 过程中会被拒绝。
- 主页读数 `--` 是传感器驱动未接入的预期状态，不是渲染问题。

## 6. 固件编译与烧录

```powershell
cd d133-netapp
.\setup.ps1 -SkipClone            # 把最新源码/字库/页面拷进 SDK
.\luban-lite\tools\netapp\build.ps1
```

产物在 `luban-lite\output\d13x_JLC_rt-thread_helloworld_netapp\images\`。烧录用 **AiBurn GUI 的 “Burn online”**；命令行 `upgcmd image` 在 fwc 重枚举后会报 `No device is connected`，是工具重连缺陷，不是镜像问题。板子 USB 是 `33C3:6677`（网上常说的 `3277:0010` 是笔记本摄像头）。详见 [flashing.md](flashing.md)。

## 7. 配网网页的独立预览（不属于 LVGL）

`portal.html` 改完运行 `generate_portal.py` 生成固件内嵌的 `portal_html.h`；`preview/preview_portal.py` 渲染一份带假扫描列表的 `preview/portal-preview.html`，用独立 Edge/Chrome 手机视图打开看效果。这一步和 LVGL 无关，但和界面改动属于同一次闭环。

## 8. 一轮改动的固定顺序

1. 改 `ui_app.c`；有新增汉字先跑 `generate_fonts.py`。
2. `cmake --build build --parallel 4` 重编模拟器。
3. 三页各出一张 `--snapshot` PNG，肉眼核对布局、文字溢出和配色。
4. 需要验证交互再开窗口用鼠标点一遍。
5. `setup.ps1 -SkipClone` + `build.ps1` 出镜像，AiBurn GUI 烧录。
6. 板上串口看日志，确认页面切换和 500 ms 状态刷新正常。
