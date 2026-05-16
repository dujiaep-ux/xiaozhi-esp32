#include "nas_monitor.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <cJSON.h>

static const char* TAG = "NasMonitor";

// NAS API配置 - 请修改为你的NAS地址
#define NAS_API_HOST "192.168.31.61"
#define NAS_API_PORT 8899

static NasMonitor* s_instance = nullptr;
static esp_timer_handle_t s_fetch_timer = nullptr;

// Disk info for UI display
struct DiskInfo {
    std::string mount;
    float percent;
    std::string total;
    std::string used;
};

static NasStatus s_cached_status;
static std::vector<DiskInfo> s_disks;

static void nas_fetch_timer_callback(void* arg) {
    if (s_instance) {
        s_instance->FetchStatus();
    }
}

NasMonitor::NasMonitor() : status_({}) {
    status_.valid = false;
    s_instance = this;
}

NasMonitor::~NasMonitor() {
    if (s_fetch_timer) {
        esp_timer_stop(s_fetch_timer);
        esp_timer_delete(s_fetch_timer);
        s_fetch_timer = nullptr;
    }
    s_instance = nullptr;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    static char* response_data = nullptr;
    static int response_len = 0;

    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->user_data) {
                char** buf = (char**)evt->user_data;
                response_data = (char*)realloc(response_data, response_len + evt->data_len + 1);
                memcpy(response_data + response_len, evt->data, evt->data_len);
                response_len += evt->data_len;
                response_data[response_len] = 0;
            }
            break;
        case HTTP_EVENT_ON_ERROR:
            ESP_LOGW(TAG, "HTTP error event");
            break;
        default:
            break;
    }
    return ESP_OK;
}

void NasMonitor::FetchStatus() {
    char* response = nullptr;

    char url[128];
    snprintf(url, sizeof(url), "http://%s:%d/api/status", NAS_API_HOST, NAS_API_PORT);

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.user_data = &response;
    config.timeout_ms = 5000;
    config.disable_auto_redirect = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGW(TAG, "Failed to init HTTP client");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        int len = esp_http_client_get_content_length(client);

        if (status == 200 && len > 0 && response) {
            cJSON* json = cJSON_Parse(response);
            if (json) {
                NasStatus new_status;
                new_status.valid = true;

                cJSON* cpu = cJSON_GetObjectItem(json, "cpu");
                if (cpu && cJSON_IsNumber(cpu)) {
                    new_status.cpu = (float)cpu->valuedouble;
                }

                cJSON* memory = cJSON_GetObjectItem(json, "memory");
                if (memory && cJSON_IsNumber(memory)) {
                    new_status.memory = (float)memory->valuedouble;
                }

                cJSON* temp = cJSON_GetObjectItem(json, "temperature");
                if (temp && cJSON_IsNumber(temp)) {
                    new_status.temperature = (float)temp->valuedouble;
                }

                cJSON* uptime = cJSON_GetObjectItem(json, "uptime");
                if (uptime && cJSON_IsString(uptime)) {
                    new_status.uptime = uptime->valuestring;
                }

                // Parse disks
                s_disks.clear();
                cJSON* disks = cJSON_GetObjectItem(json, "disks");
                if (disks && cJSON_IsArray(disks)) {
                    int disk_count = cJSON_GetArraySize(disks);
                    for (int i = 0; i < disk_count && i < 4; i++) {
                        cJSON* disk = cJSON_GetArrayItem(disks, i);
                        if (disk) {
                            DiskInfo info;
                            cJSON* mount = cJSON_GetObjectItem(disk, "mount");
                            if (mount && cJSON_IsString(mount)) {
                                info.mount = mount->valuestring;
                            }
                            cJSON* pct = cJSON_GetObjectItem(disk, "percent");
                            if (pct && cJSON_IsString(pct)) {
                                std::string pct_str = pct->valuestring;
                                info.percent = atof(pct_str.c_str());
                            }
                            cJSON* total = cJSON_GetObjectItem(disk, "total");
                            if (total && cJSON_IsString(total)) {
                                info.total = total->valuestring;
                            }
                            cJSON* used = cJSON_GetObjectItem(disk, "used");
                            if (used && cJSON_IsString(used)) {
                                info.used = used->valuestring;
                            }
                            s_disks.push_back(info);
                        }
                    }
                }

                status_ = new_status;
                s_cached_status = new_status;

                if (status_callback_) {
                    status_callback_(new_status);
                }

                cJSON_Delete(json);
            }
        }
    } else {
        ESP_LOGW(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        status_.valid = false;
    }

    free(response);
    esp_http_client_cleanup(client);
}

// Get disk info by index (0-3)
bool NasMonitorGetDisk(int index, std::string& mount, float& percent, std::string& total, std::string& used) {
    if (index < 0 || index >= (int)s_disks.size()) {
        return false;
    }
    mount = s_disks[index].mount;
    percent = s_disks[index].percent;
    total = s_disks[index].total;
    used = s_disks[index].used;
    return true;
}

NasStatus NasMonitorGetStatus() {
    return s_cached_status;
}

int NasMonitorGetDiskCount() {
    return s_disks.size();
}
