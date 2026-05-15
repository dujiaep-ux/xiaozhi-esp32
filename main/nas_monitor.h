#ifndef _NAS_MONITOR_H_
#define _NAS_MONITOR_H_

#include <string>
#include <functional>

// NAS status data structure
struct NasStatus {
    float cpu;
    float memory;
    float temperature;
    std::string uptime;
    bool valid;
};

// NAS monitor class
class NasMonitor {
public:
    static NasMonitor& GetInstance() {
        static NasMonitor instance;
        return instance;
    }

    // Fetch latest status from NAS API
    void FetchStatus();

    // Get latest cached status
    NasStatus GetStatus() const { return status_; }

    // Check if data is valid
    bool IsValid() const { return status_.valid; }

    // Register callback when status updates
    void OnStatusUpdate(std::function<void(const NasStatus&)> callback) {
        status_callback_ = callback;
    }

private:
    NasMonitor();
    ~NasMonitor();
    NasMonitor(const NasMonitor&) = delete;
    NasMonitor& operator=(const NasMonitor&) = delete;

    NasStatus status_;
    std::function<void(const NasStatus&)> status_callback_;
};

// Drawing functions for LVGL
namespace NasMonitorUI {
    // Initialize the NAS monitoring UI screen
    void Init();

    // Update UI with new status
    void Update(const NasStatus& status);

    // Show the NAS monitor screen
    void Show();

    // Hide the NAS monitor screen
    void Hide();

    // Check if NAS screen is visible
    bool IsVisible();
}

// Get disk info by index (0-3)
bool NasMonitorGetDisk(int index, std::string& mount, float& percent, std::string& total, std::string& used);
NasStatus NasMonitorGetStatus();
int NasMonitorGetDiskCount();

#endif // _NAS_MONITOR_H_
