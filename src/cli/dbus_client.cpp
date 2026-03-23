#include "cli/dbus_client.h"
#include "common/dbus_interface.h"
#include <systemd/sd-bus.h>

namespace vinput::cli {

DbusClient::DbusClient() {
    int r = sd_bus_open_user(&bus_);
    if (r < 0) {
        bus_ = nullptr;
    }
}

DbusClient::~DbusClient() {
    if (bus_) {
        sd_bus_unref(bus_);
    }
}

bool DbusClient::IsDaemonRunning(std::string* error) {
    if (!bus_) {
        if (error) *error = "D-Bus not connected";
        return false;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus_,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "NameHasOwner",
        &err, &reply, "s",
        vinput::dbus::kBusName);

    if (r < 0) {
        if (error) {
            *error = err.message ? err.message : "D-Bus call failed";
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
        return false;
    }

    int result = 0;
    sd_bus_message_read(reply, "b", &result);
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);

    return result != 0;
}

bool DbusClient::GetDaemonStatus(std::string* status, std::string* error) {
    if (!bus_) {
        if (error) *error = "D-Bus not connected";
        return false;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus_,
        vinput::dbus::kBusName,
        vinput::dbus::kObjectPath,
        vinput::dbus::kInterface,
        vinput::dbus::kMethodGetStatus,
        &err, &reply, "");

    if (r < 0) {
        if (error) {
            *error = err.message ? err.message : "D-Bus call failed";
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
        return false;
    }

    const char* str = nullptr;
    sd_bus_message_read(reply, "s", &str);
    if (status && str) *status = str;

    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);

    return true;
}

bool DbusClient::StartRecording(std::string* error) {
    if (!bus_) {
        if (error) *error = "D-Bus not connected";
        return false;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus_,
        vinput::dbus::kBusName,
        vinput::dbus::kObjectPath,
        vinput::dbus::kInterface,
        vinput::dbus::kMethodStartRecording,
        &err, &reply, "");

    if (r < 0) {
        if (error) {
            *error = err.message ? err.message : "D-Bus call failed";
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
        return false;
    }

    if (reply) sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return true;
}

bool DbusClient::StartCommandRecording(const std::string& selected_text, std::string* error) {
    if (!bus_) {
        if (error) *error = "D-Bus not connected";
        return false;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus_,
        vinput::dbus::kBusName,
        vinput::dbus::kObjectPath,
        vinput::dbus::kInterface,
        vinput::dbus::kMethodStartCommandRecording,
        &err, &reply, "s", selected_text.c_str());

    if (r < 0) {
        if (error) {
            *error = err.message ? err.message : "D-Bus call failed";
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
        return false;
    }

    if (reply) sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return true;
}

bool DbusClient::StopRecording(const std::string& scene_id, std::string* error) {
    if (!bus_) {
        if (error) *error = "D-Bus not connected";
        return false;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus_,
        vinput::dbus::kBusName,
        vinput::dbus::kObjectPath,
        vinput::dbus::kInterface,
        vinput::dbus::kMethodStopRecording,
        &err, &reply, "s", scene_id.c_str());

    if (r < 0) {
        if (error) {
            *error = err.message ? err.message : "D-Bus call failed";
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
        return false;
    }

    if (reply) sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return true;
}

bool DbusClient::StartAdaptor(const std::string& adaptor_id, std::string* error) {
    if (!bus_) {
        if (error) *error = "D-Bus not connected";
        return false;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus_,
        vinput::dbus::kBusName,
        vinput::dbus::kObjectPath,
        vinput::dbus::kInterface,
        vinput::dbus::kMethodStartAdaptor,
        &err, &reply, "s", adaptor_id.c_str());

    if (r < 0) {
        if (error) {
            *error = err.message ? err.message : "D-Bus call failed";
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
        return false;
    }

    if (reply) sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return true;
}

bool DbusClient::StopAdaptor(const std::string& adaptor_id, std::string* error) {
    if (!bus_) {
        if (error) *error = "D-Bus not connected";
        return false;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus_,
        vinput::dbus::kBusName,
        vinput::dbus::kObjectPath,
        vinput::dbus::kInterface,
        vinput::dbus::kMethodStopAdaptor,
        &err, &reply, "s", adaptor_id.c_str());

    if (r < 0) {
        if (error) {
            *error = err.message ? err.message : "D-Bus call failed";
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
        return false;
    }

    if (reply) sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return true;
}

bool DbusClient::NotifyError(const vinput::dbus::ErrorInfo& error_info,
                             std::string* error) {
    if (!bus_) {
        if (error) *error = "D-Bus not connected";
        return false;
    }

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus_,
        vinput::dbus::kFcitxBusName,
        vinput::dbus::kNotifierObjectPath,
        vinput::dbus::kNotifierInterface,
        vinput::dbus::kMethodNotifyError,
        &err, &reply, vinput::dbus::kErrorInfoSignature,
        error_info.code.c_str(),
        error_info.subject.c_str(),
        error_info.detail.c_str(),
        error_info.raw_message.c_str());

    if (r < 0) {
        if (error) {
            *error = err.message ? err.message : "D-Bus call failed";
        }
        sd_bus_error_free(&err);
        if (reply) sd_bus_message_unref(reply);
        return false;
    }

    if (reply) sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    return true;
}

} // namespace vinput::cli
