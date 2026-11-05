# 界面预览

这里放**前端预览产物**，不是固件编译输入。

| 文件 | 说明 |
|---|---|
| `device-ui.html` | **设备三页的可标注 HTML 原型**（启动/配网/主页），像素级对齐 `ui_app.c` 的坐标、颜色、字号；带标注模式，点击元素可写修改备注并一键复制 |
| `device-ui-preview.png` | 上面的 HTML 渲染截图（GitHub 里看效果用） |
| `portal-preview.html` | 手机配网页，由 `portal.html` 注入假扫描列表生成的独立页面 |
| `portal-mobile.png` | 手机视图下的渲染截图 |
| `preview_portal.py` | 生成 `portal-preview.html` 的脚本 |
| `design/` | 早期设计稿（`ap-repair-r85.png` 等），与当前实现有差异，仅作历史参考 |

## 设备界面怎么用（推荐给我提需求的方式）

用 Edge/Chrome 打开 `preview/device-ui.html`：

1. 点右上角 **「✏️ 标注模式」**；
2. 点击屏幕上任意元素（例如主页的 `main.temp`），右侧面板会显示它的 ID；
3. 在文本框写修改说明（例如“字号加大 4px、颜色改青色、文案改成室内温度”）；
4. 点「添加到清单」，全部写完后点 **「复制标注清单」**，把复制出来的文本直接发给我即可。

每个元素都有 `data-id`：`boot.state`、`ap.ssid`、`ap.ip`、`main.temp`、`main.temp-unit`、`main.ota-text`、`main.setup-btn` …… 也可以不用标注模式，直接说“把 `main.temp` 的字号加大”这种描述。

> 该文件是纯静态 HTML（无外部依赖），字体直接引用 `fonts_src/_static.ttf`；缩放、标注记录（localStorage）都在本地，不会上传。

## 可编辑的源在哪


页面源码在固件目录里，和固件一起编译：

- `firmware/application/rt-thread/helloworld/netapp/portal.html`（主配网页，含 `<!--NETWORKS-->` 占位）
- `firmware/application/rt-thread/helloworld/netapp/portal_connecting.html`
- `firmware/application/rt-thread/helloworld/netapp/portal_error.html`
- `firmware/application/rt-thread/helloworld/netapp/ota.html`（在线后的 OTA 页面）

## 工作流

1. 改上面的 HTML；
2. 重新生成预览图：
   ```powershell
   python preview\preview_portal.py
   start preview\portal-preview.html
   ```
   （用浏览器开发者工具切到手机视图，或用独立 profile 打开）
3. 重新生成固件内嵌头文件并重编固件：
   ```powershell
   python firmware\application\rt-thread\helloworld\netapp\generate_portal.py
   ```

> 预览页面里的网络列表是假的（`123456789` / `Home-WiFi` / `Guest-2.4G`），真实设备上的列表来自开机前的一次 STA 扫描。
