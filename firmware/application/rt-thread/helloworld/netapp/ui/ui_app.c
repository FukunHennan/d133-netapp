/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_app.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

LV_FONT_DECLARE(ui_font_16);
LV_FONT_DECLARE(ui_font_24);
LV_FONT_DECLARE(ui_font_36);

#define C_BG     0x081829
#define C_PANEL  0x102C49
#define C_PANEL2 0x183956
#define C_EDGE   0x345573
#define C_WHITE  0xF3FAFF
#define C_MUTED  0x96B2CA
#define C_CYAN   0x36D9EC
#define C_BLUE   0x509BFF
#define C_GREEN  0x55D9B0
#define C_AMBER  0xFFC878

typedef struct {
    lv_obj_t *root;
    lv_obj_t *state;
    lv_obj_t *ssid;
    lv_obj_t *password;
    lv_obj_t *ip;
    lv_obj_t *wifi;
    lv_obj_t *wifi_ip;
    lv_obj_t *ota;
    lv_obj_t *ota_bar;
    lv_obj_t *ota_panel;
    lv_obj_t *error;
    lv_obj_t *temp;
    lv_obj_t *humidity;
    lv_obj_t *pressure;
    lv_obj_t *sensor_note;
    lv_obj_t *progress;     /* 启动页进度条填充 */
} ui_screen_t;

/* 启动页固定显示时长（毫秒），与原型 5s 进度动画一致 */
#define BOOT_MIN_MS 5000

static ui_screen_t boot, ap, main_page;
static ui_page_t active_page = UI_PAGE_BOOT;
static int initialized;
static bool boot_elapsed;

static lv_color_t color(uint32_t rgb) { return lv_color_hex(rgb); }

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h,
                     uint32_t fill, uint32_t border, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, color(fill), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, border ? 1 : 0, 0);
    if (border) lv_obj_set_style_border_color(o, color(border), 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_scrollbar_mode(o, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const char *s, int x, int y,
                       int w, const lv_font_t *font, uint32_t rgb)
{
    lv_obj_t *o = lv_label_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_width(o, w);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, color(rgb), 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_DOT);
    lv_label_set_text(o, s);
    return o;
}

static lv_obj_t *page_root(void)
{
    lv_obj_t *root = lv_obj_create(NULL);
    lv_obj_set_size(root, 480, 272);
    lv_obj_set_style_bg_color(root, color(C_BG), 0);
    lv_obj_set_style_bg_grad_color(root, color(0x0D2A4B), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    return root;
}

static void header(lv_obj_t *root, const char *section)
{
    box(root, 18, 17, 7, 23, C_CYAN, 0, 3);
    label(root, "衡山派", 34, 13, 126, &ui_font_24, C_WHITE);
    if (section && *section) label(root, section, 300, 20, 160, &ui_font_16, C_MUTED);
    box(root, 18, 51, 444, 1, C_EDGE, 0, 0);
}

static const char *wifi_text(const char *s)
{
    if (!s) return "正在启动";
    if (!strcmp(s, "Starting")) return "正在启动";
    if (!strcmp(s, "Connecting")) return "正在连接 Wi-Fi";
    if (!strcmp(s, "Getting IP")) return "正在获取 IP 地址";
    if (!strcmp(s, "Opening setup AP")) return "正在启动配网热点";
    if (!strcmp(s, "Setup AP ready")) return "热点已就绪";
    if (!strcmp(s, "Connected")) return "已连接";
    if (!strcmp(s, "Disconnected")) return "连接已断开";
    if (!strcmp(s, "Reconnecting")) return "正在重连 Wi-Fi";
    if (!strcmp(s, "Connection failed")) return "连接失败";
    if (!strcmp(s, "DHCP failed")) return "获取地址失败";
    if (!strcmp(s, "Wi-Fi unavailable")) return "无线模块未就绪";
    if (!strcmp(s, "Setup AP failed")) return "热点启动失败";
    return s;
}

static const char *ota_text(const char *s)
{
    if (!s || !*s || !strcmp(s, "Idle")) return "等待更新";
    if (!strcmp(s, "Starting")) return "正在启动升级";
    if (!strcmp(s, "Checking partitions")) return "检查分区";
    if (!strcmp(s, "Downloading")) return "下载中";
    if (!strcmp(s, "Downloading / flashing")) return "下载并写入";
    if (!strcmp(s, "Verifying")) return "正在校验";
    if (!strcmp(s, "Committing")) return "提交更新";
    if (!strcmp(s, "Rebooting")) return "即将重启";
    if (!strcmp(s, "Failed")) return "升级失败";
    return s;
}

static const char *error_text(const char *s)
{
    if (!s) return "";
    if (!strcmp(s, "Check RTL8189, SDIO and power")) return "检查无线模块和供电";
    if (!strcmp(s, "Connect or save failed")) return "连接或保存失败";
    if (!strcmp(s, "AP or portal failed")) return "热点或配网页面失败";
    if (!strcmp(s, "Cannot start Wi-Fi hotspot")) return "无法启动热点";
    if (!strcmp(s, "Link lost or setup requested")) return "网络已断开";
    if (!strcmp(s, "DHCP start failed")) return "地址分配失败";
    if (!strcmp(s, "Setup already active or offline")) return "当前未联网或正在配网";
    if (!strcmp(s, "OTA active")) return "升级中无法配网";
    return s;
}

/* 空心矩形：只描边不填充，用于图标轮廓 */
static lv_obj_t *outline(lv_obj_t *parent, int x, int y, int w, int h,
                         uint32_t border, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, color(border), 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* CPU 芯片图标：外框 + 内核 + 四边引脚，中心位于 (cx, cy)。
 * 对应原型中的 Lucide CPU 图标，纯图元绘制，无需图片资源。 */
static void icon_cpu(lv_obj_t *parent, int cx, int cy)
{
    int i, p;
    outline(parent, cx - 26, cy - 26, 52, 52, C_CYAN, 6);
    box(parent, cx - 12, cy - 12, 24, 24, C_CYAN, 0, 3);
    for (i = 0; i < 4; i++) {
        p = cx - 18 + i * 11;
        box(parent, p, cy - 34, 3, 8, C_CYAN, 0, 1);   /* 上引脚 */
        box(parent, p, cy + 26, 3, 8, C_CYAN, 0, 1);   /* 下引脚 */
        p = cy - 18 + i * 11;
        box(parent, cx - 34, p, 8, 3, C_CYAN, 0, 1);   /* 左引脚 */
        box(parent, cx + 26, p, 8, 3, C_CYAN, 0, 1);   /* 右引脚 */
    }
}

/* 进度条动画回调：直接把填充条宽度从 0 拉到 229 */
static void boot_fill_anim(void *var, int32_t width)
{
    lv_obj_set_width((lv_obj_t *)var, width);
}

static void start_boot_animation(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, boot.progress);
    lv_anim_set_exec_cb(&a, boot_fill_anim);
    lv_anim_set_values(&a, 0, 229);
    lv_anim_set_time(&a, BOOT_MIN_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

static void boot_timer_cb(lv_timer_t *t)
{
    boot_elapsed = true;
    lv_timer_delete(t);
}

static void create_boot(void)
{
    lv_obj_t *halo;
    boot.root = page_root();
    header(boot.root, "");
    /* 启动页保持极简：不显示网络阶段文案，避免与固定 5s 进度冲突 */

    /* 居中圆形底盘 */
    halo = box(boot.root, 180, 64, 120, 120, 0x123A5A, C_EDGE, 60);
    lv_obj_set_style_bg_grad_color(halo, color(0x0B2545), 0);
    lv_obj_set_style_bg_grad_dir(halo, LV_GRAD_DIR_VER, 0);
    icon_cpu(boot.root, 240, 124);

    /* 底部：状态文字 + 进度条（轨道 + 填充，纯图元绘制） */
    boot.state = label(boot.root, "系统初始化", 186, 196, 108, &ui_font_16, C_MUTED);
    lv_obj_set_style_text_align(boot.state, LV_TEXT_ALIGN_CENTER, 0);

    box(boot.root, 125, 230, 229, 4, 0x2B4B67, 0, 2);
    boot.progress = box(boot.root, 125, 230, 0, 4, C_CYAN, 0, 2);
}

static void create_ap(void)
{
    lv_obj_t *left, *right;
    ap.root = page_root();
    header(ap.root, "网络配置");
    label(ap.root, "连接设备热点", 19, 65, 280, &ui_font_24, C_WHITE);
    ap.state = label(ap.root, "热点已就绪", 19, 99, 278, &ui_font_16, C_CYAN);

    left = box(ap.root, 18, 132, 276, 105, C_PANEL, C_EDGE, 16);
    box(left, 15, 19, 4, 68, C_CYAN, 0, 2);
    label(left, "热点名称", 30, 12, 218, &ui_font_16, C_MUTED);
    ap.ssid = label(left, "D133-Setup-...", 30, 35, 225, &ui_font_16, C_WHITE);
    label(left, "连接密码", 30, 64, 95, &ui_font_16, C_MUTED);
    ap.password = label(left, "D133setup", 124, 64, 138, &ui_font_16, C_WHITE);

    right = box(ap.root, 307, 65, 155, 172, C_PANEL, C_EDGE, 16);
    box(right, 44, 14, 68, 68, 0x174D68, 0x42A8C7, 34);
    label(right, "AP", 60, 28, 48, &ui_font_24, C_CYAN);
    label(right, "手机连接热点", 16, 96, 130, &ui_font_16, C_WHITE);
    label(right, "浏览器打开", 16, 119, 130, &ui_font_16, C_MUTED);
    ap.ip = label(right, "192.168.4.1", 16, 143, 134, &ui_font_16, C_CYAN);
    ap.error = label(ap.root, "", 20, 245, 440, &ui_font_16, C_AMBER);
}

/* 传感器图标：用 LVGL 基础图元绘制，不依赖图片资源和符号字体。 */
static void icon_thermometer(lv_obj_t *card, uint32_t accent)
{
    box(card, 24, 11, 4, 16, accent, 0, 2);       /* 液柱 */
    box(card, 19, 24, 14, 14, accent, 0, 7);      /* 球泡 */
}

static lv_point_precise_t drop_pts[] = {
    {26, 10}, {30, 15}, {33, 20}, {33, 25}, {30, 31}, {26, 34},
    {22, 31}, {19, 25}, {19, 20}, {22, 15}, {26, 10}
};

static void icon_humidity(lv_obj_t *card, uint32_t accent)
{
    lv_obj_t *line = lv_line_create(card);
    lv_line_set_points(line, drop_pts, sizeof(drop_pts) / sizeof(drop_pts[0]));
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_color(line, color(accent), 0);
    lv_obj_set_style_line_rounded(line, true, 0);
}

static lv_point_precise_t gauge_needle[] = {{26, 24}, {33, 16}};

static void icon_pressure(lv_obj_t *card, uint32_t accent)
{
    lv_obj_t *arc = lv_arc_create(card);
    lv_obj_set_pos(arc, 14, 12);
    lv_obj_set_size(arc, 24, 24);
    lv_arc_set_bg_angles(arc, 135, 405);
    lv_arc_set_angles(arc, 135, 405);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color(C_EDGE), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color(accent), LV_PART_INDICATOR);

    lv_obj_t *needle = lv_line_create(card);
    lv_line_set_points(needle, gauge_needle, sizeof(gauge_needle) / sizeof(gauge_needle[0]));
    lv_obj_set_style_line_width(needle, 2, 0);
    lv_obj_set_style_line_color(needle, color(accent), 0);
    lv_obj_set_style_line_rounded(needle, true, 0);
}

static void sensor_card(lv_obj_t *root, int x, const char *unit, uint32_t accent,
                        lv_obj_t **value, void (*draw_icon)(lv_obj_t *, uint32_t))
{
    lv_obj_t *card = box(root, x, 77, 140, 117, C_PANEL, C_EDGE, 17);
    lv_obj_set_style_bg_grad_color(card, color(0x0C2440), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    draw_icon(card, accent);
    *value = label(card, "--", 14, 46, 112, &ui_font_36, C_WHITE);
    label(card, unit, 14, 95, 112, &ui_font_16, accent);
}

static void create_main(void)
{
    lv_obj_t *status;
    main_page.root = page_root();
    header(main_page.root, "传感器状态");
    main_page.wifi = label(main_page.root, "Wi-Fi 已连接", 18, 52, 285,
                           &ui_font_16, C_GREEN);
    /* 封箱设备：右上角常显本机 IP，方便在同一局域网里找到 http://<IP>/ 升级页 */
    main_page.wifi_ip = label(main_page.root, "", 311, 52, 148, &ui_font_16, C_CYAN);
    main_page.sensor_note = label(main_page.root, "传感器尚未接入", 18, 249, 300,
                                  &ui_font_16, C_MUTED);

    sensor_card(main_page.root, 18, "°C", C_AMBER, &main_page.temp, icon_thermometer);
    sensor_card(main_page.root, 170, "%", C_CYAN, &main_page.humidity, icon_humidity);
    sensor_card(main_page.root, 322, "kPa", C_GREEN, &main_page.pressure, icon_pressure);

    /* OTA 进度条：默认隐藏，只在升级过程中出现（见 ui_app_update） */
    status = box(main_page.root, 18, 201, 444, 45, C_PANEL2, C_EDGE, 12);
    main_page.ota_panel = status;
    lv_obj_add_flag(status, LV_OBJ_FLAG_HIDDEN);
    label(status, "系统更新", 12, 5, 88, &ui_font_16, C_MUTED);
    main_page.ota = label(status, "等待更新", 113, 5, 300, &ui_font_16, C_WHITE);
    main_page.ota_bar = lv_bar_create(status);
    lv_obj_set_pos(main_page.ota_bar, 12, 36);
    lv_obj_set_size(main_page.ota_bar, 420, 4);
    lv_bar_set_range(main_page.ota_bar, 0, 100);
    lv_bar_set_value(main_page.ota_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(main_page.ota_bar, color(0x31516B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(main_page.ota_bar, color(C_CYAN), LV_PART_INDICATOR);

    main_page.error = label(main_page.root, "", 18, 249, 440, &ui_font_16, C_AMBER);
}

void ui_app_show(ui_page_t page)
{
    lv_obj_t *target;
    if (!initialized) return;
    if (page == UI_PAGE_AP) target = ap.root;
    else if (page == UI_PAGE_MAIN) target = main_page.root;
    else { page = UI_PAGE_BOOT; target = boot.root; }
    if (active_page == page && lv_screen_active() == target) return;
    active_page = page;
    lv_screen_load(target);
}

void ui_app_skip_boot(void)
{
    boot_elapsed = true;
}

void ui_app_init(int (*request_setup)(void))
{
    if (initialized) return;
    /* 主页不再放「重新配网」按钮：掉线后固件会自动开热点进入配网页；
     * 手动重新配网仍可用串口命令 netapp_setup。回调参数保留以兼容现有接口。 */
    (void)request_setup;
    create_boot();
    create_ap();
    create_main();
    initialized = 1;
    lv_screen_load(boot.root);
    /* 启动页固定 5s：期间进度条匀速填充，到点后才允许切到其它页面 */
    start_boot_animation();
    lv_timer_create(boot_timer_cb, BOOT_MIN_MS, NULL);
}


void ui_app_set_sensor_text(const char *temperature, const char *humidity,
                            const char *pressure, const char *note)
{
    if (!initialized) return;
    lv_label_set_text(main_page.temp, temperature && *temperature ? temperature : "--");
    lv_label_set_text(main_page.humidity, humidity && *humidity ? humidity : "--");
    lv_label_set_text(main_page.pressure, pressure && *pressure ? pressure : "--");
    lv_label_set_text(main_page.sensor_note, note && *note ? note : "传感器尚未接入");
}

void ui_app_update(const ui_app_status_t *s)
{
    const char *phase;
    const char *problem;
    char line[64];
    if (!initialized || !s) return;
    phase = wifi_text(s->wifi);
    problem = error_text(s->error);
    lv_label_set_text(ap.state, phase);
    lv_label_set_text(ap.ssid, s->ap_ssid && *s->ap_ssid ? s->ap_ssid : "D133-Setup-...");
    lv_label_set_text(ap.password, s->ap_password && *s->ap_password ? s->ap_password : "--");
    lv_label_set_text(ap.ip, s->ip && *s->ip ? s->ip : "192.168.4.1");
    lv_label_set_text(ap.error, problem);
    if (s->ssid && *s->ssid) {
        snprintf(line, sizeof(line), "已连接 %.32s", s->ssid);
        lv_label_set_text(main_page.wifi, line);
    } else {
        lv_label_set_text(main_page.wifi, "Wi-Fi 已连接");
    }
    /* OTA 面板默认隐藏；只有升级任务存在时才展开并显示进度条 */
    {
        unsigned percent = s->ota_percent > 100 ? 100 : s->ota_percent;
        int ota_active = s->ota && s->ota[0] && strcmp(s->ota, "Idle") != 0;
        if (ota_active) {
            char ota_line[48];
            snprintf(ota_line, sizeof(ota_line), "%s %u%%", ota_text(s->ota), percent);
            lv_label_set_text(main_page.ota, ota_line);
            lv_bar_set_value(main_page.ota_bar, percent, LV_ANIM_OFF);
            lv_obj_clear_flag(main_page.ota_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(main_page.ota, ota_text(s->ota));
            lv_bar_set_value(main_page.ota_bar, 0, LV_ANIM_OFF);
            lv_obj_add_flag(main_page.ota_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_label_set_text(main_page.error, problem);
    lv_label_set_text(main_page.wifi_ip, s->ip && *s->ip ? s->ip : "");
    if (problem && *problem) {
        lv_obj_clear_flag(main_page.error, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(main_page.sensor_note, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(main_page.error, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(main_page.sensor_note, LV_OBJ_FLAG_HIDDEN);
    }

    /* 启动页固定显示 5s，期间无论网络是否就绪都停留在启动页 */
    if (!boot_elapsed) ui_app_show(UI_PAGE_BOOT);
    else if (s->provisioning) ui_app_show(UI_PAGE_AP);
    else if (s->online && s->ip && *s->ip) ui_app_show(UI_PAGE_MAIN);
    else ui_app_show(UI_PAGE_BOOT);
}
