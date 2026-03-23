#pragma once
#include "common/error_info.h"
#include <string>

struct sd_bus;

namespace vinput::cli {

class DbusClient {
public:
    DbusClient();
    ~DbusClient();

    DbusClient(const DbusClient&) = delete;
    DbusClient& operator=(const DbusClient&) = delete;

    // Check if daemon is running (via NameHasOwner, avoids auto-activation)
    bool IsDaemonRunning(std::string* error = nullptr);
    // Get daemon status string (only call if IsDaemonRunning returns true)
    bool GetDaemonStatus(std::string* status, std::string* error = nullptr);

    // Recording control
    bool StartRecording(std::string* error = nullptr);
    bool StartCommandRecording(const std::string& selected_text, std::string* error = nullptr);
    bool StopRecording(const std::string& scene_id, std::string* error = nullptr);
    bool StartAdaptor(const std::string& adaptor_id, std::string* error = nullptr);
    bool StopAdaptor(const std::string& adaptor_id, std::string* error = nullptr);
    bool NotifyError(const vinput::dbus::ErrorInfo& error_info,
                     std::string* error = nullptr);

private:
    sd_bus* bus_ = nullptr;
};

} // namespace vinput::cli
