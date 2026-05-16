#include "nas_monitor.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <ctime>

static const char* TAG = "NasMonitorUI";

static lv_obj_t* nas_screen = nullptr;
static lv_obj_t* cpu_arc = nullptr;
static lv_obj_t* mem_arc = nullptr;
static lv_obj_t* cpu_label = nullptr;
static lv_obj_t* mem_label = nullptr;
static lv_obj_t* temp_bar = nullptr;
static lv_obj_t* temp_label = nullptr;
static lv_obj_t* uptime_label = nullptr;
static lv_obj_t* clock_label = nullptr;

// Disk widgets
static lv_obj_t* disk_bar[4] = {nullptr};
static lv_obj_t* disk_label[4] = {nullptr};
static lv_obj_t* disk_pct_label[4] = {nullptr};

static esp_timer_handle_t s_update_timer = nullptr;
static bool s_screen_visible = false;

// Draw arc/ring gauge
static void draw_ring_gauge(lv_obj_t* parent, int x, int y, int radius, int thickness, int value, int max_value, lv_obj_t*& arc_obj, lv_obj_t*& label) {
    // Create arc for background (full ring)
    arc_obj = lv_arc_create(parent);
    lv_obj_set_size(arc_obj, radius * 2, radius * 2);
    lv_obj_set_pos(arc_obj, x - radius, y - radius);
    lv_arc_set_range(arc_obj, 0, max_value);
    lv_arc_set_value(arc_obj, max_value); // Full background
    lv_arc_set_rotation(arc_obj, 135);
    lv_arc_set_bg_angles(arc_obj, 0, 270);
    lv_obj_remove_style(arc_obj, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_color(arc_obj, lv_color_make(220, 220, 220), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_obj, thickness, LV_PART_INDICATOR);

    // Create arc for value (foreground)
    lv_obj_t* value_arc = lv_arc_create(parent);
    lv_obj_set_size(value_arc, radius * 2, radius * 2);
    lv_obj_set_pos(value_arc, x - radius, y - radius);
    lv_arc_set_range(value_arc, 0, max_value);
    lv_arc_set_value(value_arc, value);
    lv_arc_set_rotation(value_arc, 135);
    lv_arc_set_bg_angles(value_arc, 0, 270);
    lv_obj_remove_style(value_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_color(value_arc, lv_color_make(80, 80, 80), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(value_arc, thickness, LV_PART_INDICATOR);

    // Label below
    label = lv_label_create(parent);
    lv_obj_set_pos(label, x - 20, y + radius - 10);
    lv_obj_set_size(label, 40, 16);
    lv_label_set_text(label, "0%");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

void NasMonitorUI::Init() {
    ESP_LOGI(TAG, "Initializing NAS Monitor UI");

    // Create the NAS screen
    nas_screen = lv_obj_create(NULL);
    lv_obj_set_size(nas_screen, 400, 300);
    lv_obj_set_style_bg_color(nas_screen, lv_color_make(255, 255, 255), 0);

    // Title bar
    lv_obj_t* title = lv_label_create(nas_screen);
    lv_obj_set_pos(title, 10, 8);
    lv_label_set_text(title, "NAS MONITOR");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(title, lv_color_make(80, 80, 80), 0);

    // Clock
    clock_label = lv_label_create(nas_screen);
    lv_obj_set_pos(clock_label, 300, 8);
    lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_12, 0);
    lv_label_set_text(clock_label, "--:--");

    // Divider line
    lv_obj_t* line1 = lv_line_create(nas_screen);
    static lv_point_t points[] = {{10, 28}, {390, 28}};
    lv_line_set_points(line1, points, 2);
    lv_obj_set_style_line_color(line1, lv_color_make(180, 180, 180), 0);
    lv_obj_set_style_line_width(line1, 1, 0);

    // CPU ring (left)
    draw_ring_gauge(nas_screen, 100, 90, 38, 8, 0, 100, cpu_arc, cpu_label);

    // CPU text label
    lv_obj_t* cpu_title = lv_label_create(nas_screen);
    lv_obj_set_pos(cpu_title, 70, 55);
    lv_obj_set_size(cpu_title, 60, 16);
    lv_label_set_text(cpu_title, "CPU");
    lv_obj_set_style_text_font(cpu_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(cpu_title, LV_TEXT_ALIGN_CENTER, 0);

    // Memory ring (right)
    draw_ring_gauge(nas_screen, 300, 90, 38, 8, 0, 100, mem_arc, mem_label);

    // Memory text label
    lv_obj_t* mem_title = lv_label_create(nas_screen);
    lv_obj_set_pos(mem_title, 270, 55);
    lv_obj_set_size(mem_title, 60, 16);
    lv_label_set_text(mem_title, "MEM");
    lv_obj_set_style_text_font(mem_title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(mem_title, LV_TEXT_ALIGN_CENTER, 0);

    // Vertical divider
    lv_obj_t* vline = lv_line_create(nas_screen);
    static lv_point_t vpoints[] = {{200, 40}, {200, 130}};
    lv_line_set_points(vline, vpoints, 2);
    lv_obj_set_style_line_color(vline, lv_color_make(200, 200, 200), 0);
    lv_obj_set_style_line_width(vline, 1, 0);

    // Temperature section
    lv_obj_t* temp_title = lv_label_create(nas_screen);
    lv_obj_set_pos(temp_title, 10, 145);
    lv_label_set_text(temp_title, "TEMP");
    lv_obj_set_style_text_font(temp_title, &lv_font_montserrat_10, 0);

    // Temperature bar background
    lv_obj_t* temp_bg = lv_bar_create(nas_screen);
    lv_obj_set_pos(temp_bg, 55, 145);
    lv_obj_set_size(temp_bg, 230, 12);
    lv_bar_set_range(temp_bg, 0, 100);
    lv_bar_set_value(temp_bg, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(temp_bg, lv_color_make(220, 220, 220), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(temp_bg, lv_color_make(240, 240, 240), LV_PART_MAIN);

    temp_label = lv_label_create(nas_screen);
    lv_obj_set_pos(temp_label, 295, 145);
    lv_label_set_text(temp_label, "--C");
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_10, 0);

    // Divider line 2
    lv_obj_t* line2 = lv_line_create(nas_screen);
    static lv_point_t points2[] = {{10, 168}, {390, 168}};
    lv_line_set_points(line2, points2, 2);
    lv_obj_set_style_line_color(line2, lv_color_make(180, 180, 180), 0);

    // DISK USAGE title
    lv_obj_t* disk_title = lv_label_create(nas_screen);
    lv_obj_set_pos(disk_title, 10, 175);
    lv_label_set_text(disk_title, "DISK USAGE");
    lv_obj_set_style_text_font(disk_title, &lv_font_montserrat_10, 0);

    // Divider line 3
    lv_obj_t* line3 = lv_line_create(nas_screen);
    static lv_point_t points3[] = {{10, 190}, {390, 190}};
    lv_line_set_points(line3, points3, 2);
    lv_obj_set_style_line_color(line3, lv_color_make(200, 200, 200), 0);

    // Disk rows (4 disks)
    for (int i = 0; i < 4; i++) {
        int row_y = 200 + i * 24;

        // Disk label
        disk_label[i] = lv_label_create(nas_screen);
        lv_obj_set_pos(disk_label[i], 10, row_y);
        lv_obj_set_size(disk_label[i], 50, 14);
        lv_label_set_text(disk_label[i], "Disk");
        lv_obj_set_style_text_font(disk_label[i], &lv_font_montserrat_10, 0);

        // Progress bar background
        lv_obj_t* bar_bg = lv_bar_create(nas_screen);
        lv_obj_set_pos(bar_bg, 60, row_y + 2);
        lv_obj_set_size(bar_bg, 220, 10);
        lv_bar_set_range(bar_bg, 0, 100);
        lv_bar_set_value(bar_bg, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar_bg, lv_color_make(220, 220, 220), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(bar_bg, lv_color_make(240, 240, 240), LV_PART_MAIN);
        disk_bar[i] = bar_bg;

        // Percentage label
        disk_pct_label[i] = lv_label_create(nas_screen);
        lv_obj_set_pos(disk_pct_label[i], 290, row_y);
        lv_obj_set_size(disk_pct_label[i], 40, 14);
        lv_label_set_text(disk_pct_label[i], "--%");
        lv_obj_set_style_text_font(disk_pct_label[i], &lv_font_montserrat_10, 0);
    }

    // Bottom line
    lv_obj_t* line4 = lv_line_create(nas_screen);
    static lv_point_t points4[] = {{10, 295}, {390, 295}};
    lv_line_set_points(line4, points4, 2);
    lv_obj_set_style_line_color(line4, lv_color_make(180, 180, 180), 0);

    // Uptime label
    uptime_label = lv_label_create(nas_screen);
    lv_obj_set_pos(uptime_label, 10, 280);
    lv_label_set_text(uptime_label, "UP: --");
    lv_obj_set_style_text_font(uptime_label, &lv_font_montserrat_9, 0);

    ESP_LOGI(TAG, "NAS Monitor UI initialized");
}

static void update_timer_callback(void* arg) {
    if (!s_screen_visible) return;

    NasStatus status = NasMonitorGetStatus();
    NasMonitorUI::Update(status);
}

void NasMonitorUI::Update(const NasStatus& status) {
    if (!nas_screen || !s_screen_visible) return;

    // Update CPU
    if (cpu_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f%%", status.cpu);
        lv_label_set_text(cpu_label, buf);
    }

    // Update Memory
    if (mem_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f%%", status.memory);
        lv_label_set_text(mem_label, buf);
    }

    // Update Temperature
    if (temp_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1fC", status.temperature);
        lv_label_set_text(temp_label, buf);
    }

    // Update disks
    for (int i = 0; i < 4; i++) {
        std::string mount, total, used;
        float pct = 0;
        if (NasMonitorGetDisk(i, mount, pct, total, used)) {
            if (disk_label[i]) {
                // Shorten mount path
                std::string short_mount = mount;
                if (mount.length() > 8) {
                    short_mount = mount.substr(0, 8);
                }
                lv_label_set_text(disk_label[i], short_mount.c_str());
            }
            if (disk_bar[i]) {
                lv_bar_set_value(disk_bar[i], (int)pct, LV_ANIM_OFF);
            }
            if (disk_pct_label[i]) {
                char buf[16];
                snprintf(buf, sizeof(buf), "%.0f%%", pct);
                lv_label_set_text(disk_pct_label[i], buf);
            }
        }
    }

    // Update uptime
    if (uptime_label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "UP: %s", status.uptime.c_str());
        lv_label_set_text(uptime_label, buf);
    }

    // Update clock
    if (clock_label) {
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        char time_buf[16];
        strftime(time_buf, sizeof(time_buf), "%H:%M", tm_info);
        lv_label_set_text(clock_label, time_buf);
    }
}

void NasMonitorUI::Show() {
    if (!nas_screen) return;

    s_screen_visible = true;
    // Load the NAS screen but keep it transparent initially
    lv_scr_load(nas_screen);

    // Start update timer (every 5 seconds)
    if (!s_update_timer) {
        esp_timer_create_args_t args = {
            .callback = update_timer_callback,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "nas_update_timer"
        };
        esp_timer_create(&args, &s_update_timer);
    }
    esp_timer_start_periodic(s_update_timer, 5000000); // 5 seconds

    // Initial fetch
    NasMonitor::GetInstance().FetchStatus();
}

void NasMonitorUI::Hide() {
    s_screen_visible = false;
    if (s_update_timer) {
        esp_timer_stop(s_update_timer);
    }
}

bool NasMonitorUI::IsVisible() {
    return s_screen_visible;
}
