# 手机配网页预览

这里放的是**前端预览产物**，不是固件编译输入。

| 文件 | 说明 |
|---|---|
| `portal-preview.html` | 由 `portal.html` 注入假扫描列表生成的独立页面，可直接用浏览器打开 |
| `portal-mobile.png` | 手机视图下的渲染截图 |
| `preview_portal.py` | 生成 `portal-preview.html` 的脚本 |
| `design/` | 早期设计稿（`ap-repair-r85.png` 等），与当前实现有差异，仅作历史参考 |

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
