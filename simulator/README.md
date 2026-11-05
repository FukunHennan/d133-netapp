# D133 LVGL 桌面预览器

在 Windows 桌面上用 SDL 窗口跑固件里**同一份** LVGL 9.1 和**同一份**手写界面源码（`firmware/application/rt-thread/helloworld/netapp/ui/`，不是副本），用来在不上板的情况下看布局、验证点击交互、批量出图。

- 窗口 480×272，鼠标点击模拟触摸
- 无窗口截图：`--snapshot` 把渲染结果存成 BMP，适合逐像素核对
- 颜色深度 16 bpp、`CACHE_LINE_SIZE=32`，与板端 RGB565 一致（不要改回 32，否则渐变在模拟器上看不出板端的色带）

## 依赖

| 工具 | 版本 | 安装（winget） |
|---|---|---|
| MinGW-w64 GCC | ≥ 13（x86_64） | `winget install BrechtSanders.WinLibs.POSIX.UCRT` |
| CMake | ≥ 3.16 | `winget install Kitware.CMake` |
| Ninja | 任意 | `winget install Ninja-build.Ninja` |
| luban-lite SDK | V1.0 (`f5be1e4c`) | 在仓库根目录运行 `.\setup.ps1`，会克隆到 `../luban-lite` |

LVGL 和 SDL 的源码不单独下载，直接编译 SDK 里自带的那两份，所以必须先有 SDK。

## 构建

```powershell
cd simulator
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' -DDIRECTX=OFF
cmake --build build --parallel 4
```

首次构建会顺带编译 SDL2 和整个 LVGL，约 2–4 分钟；之后增量编译只要几秒。
SDK 不在默认位置时用 `-DLUBAN_LITE_DIR=<SDK 路径>` 指定。

## 运行

```powershell
.\build\d133_preview.exe --page boot   # 启动页
.\build\d133_preview.exe --page ap     # 配网热点页
.\build\d133_preview.exe --page main   # 传感器主页
```

关闭窗口退出。`main.c` 里的网络和传感器数值是示例数据，只用于把三页填满。

## 无窗口截图

```powershell
$env:SDL_VIDEODRIVER = 'dummy'
Start-Process -FilePath .\build\d133_preview.exe `
    -ArgumentList '--page','ap','--snapshot','screenshots\ap.bmp' -NoNewWindow -Wait
Remove-Item Env:SDL_VIDEODRIVER
python -c "from PIL import Image; Image.open(r'screenshots\ap.bmp').save(r'screenshots\ap.png')"
```

`--snapshot` 在第 30 帧从 SDL renderer 读像素保存，动画/渐入已经稳定。SDL 的 dummy 驱动不会创建窗口，因此适合脚本和 CI。

## 注意

- 模拟器只编 `ui_app.c` + 三份字体，**不编 `netapp_ui.c`**：500 ms 状态刷新、自动切页这条链路只能在板子上验证。
- 改中文文案后先重新生成字体，否则新汉字是空心方框：
  ```powershell
  python ..\firmware\application\rt-thread\helloworld\netapp\ui\generate_fonts.py
  ```
  然后重新构建。
- LVGL 堆（`LV_MEM_SIZE`）在桌面上给 1 MB，板端默认只有 64 KB，内存问题只能在板上复现。
- 截图为历史版本时，注意对比源文件时间戳（`ui_app.c` / 字体 / exe）。
