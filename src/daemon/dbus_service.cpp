#include "dbus_service.h"
#include "common/dbus_interface.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

using namespace vinput::dbus;

DbusService::DbusService() = default;

DbusService::~DbusService() {
  if (slot_) {
    sd_bus_slot_unref(slot_);
  }
  if (bus_) {
    sd_bus_flush_close_unref(bus_);
  }
  if (notify_fd_ >= 0) {
    close(notify_fd_);
  }
}

namespace {

int ReplyWithMethodResult(sd_bus_message *m, sd_bus_error *error,
                          const DbusService::MethodResult &result,
                          const char *reply_signature) {
  if (!result.ok) {
    return sd_bus_error_set(error, kErrorOperationFailed, result.message.c_str());
  }

  if (!reply_signature || reply_signature[0] == '\0') {
    return sd_bus_reply_method_return(m, "");
  }
  return sd_bus_reply_method_return(m, reply_signature, result.payload.c_str());
}

} // namespace

static const sd_bus_vtable vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD(kMethodStartRecording, "", "", &DbusService::handleStartRecording,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(kMethodStartCommandRecording, "s", "",
                  &DbusService::handleStartCommandRecording,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(kMethodStopRecording, "s", "s", &DbusService::handleStopRecording,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(kMethodGetStatus, "", "s", &DbusService::handleGetStatus,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(kMethodStartAdaptor, "s", "", &DbusService::handleStartAdaptor,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD(kMethodStopAdaptor, "s", "", &DbusService::handleStopAdaptor,
                  SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_SIGNAL(kSignalRecognitionResult, "s", 0),
    SD_BUS_SIGNAL(kSignalStatusChanged, "s", 0),
    SD_BUS_SIGNAL(kSignalDaemonError, kErrorInfoSignature, 0),
    SD_BUS_VTABLE_END,
};

bool DbusService::Start(std::string *error) {
  notify_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (notify_fd_ < 0) {
    if (error) {
      *error = std::string("failed to create eventfd: ") + strerror(errno);
    }
    return false;
  }

  int ret = sd_bus_open_user(&bus_);
  if (ret < 0) {
    if (error) {
      *error = std::string("failed to open user bus: ") + strerror(-ret);
    }
    return false;
  }

  ret = sd_bus_add_object_vtable(bus_, &slot_, kObjectPath, kInterface, vtable,
                                 this);
  if (ret < 0) {
    if (error) {
      *error = std::string("failed to add D-Bus vtable: ") + strerror(-ret);
    }
    return false;
  }

  ret = sd_bus_request_name(bus_, kBusName, 0);
  if (ret < 0) {
    if (error) {
      *error = std::string("failed to request D-Bus name: ") + strerror(-ret);
    }
    return false;
  }

  fprintf(stderr, "vinput: DBus service started on %s\n", kBusName);
  return true;
}

int DbusService::GetFd() const { return sd_bus_get_fd(bus_); }

int DbusService::GetNotifyFd() const { return notify_fd_; }

bool DbusService::ProcessOnce() {
  int ret = sd_bus_process(bus_, nullptr);
  if (ret < 0) {
    fprintf(stderr, "vinput: sd_bus_process failed: %s\n", strerror(-ret));
    return false;
  }
  return ret > 0;
}

void DbusService::FlushEmitQueue() {
  // Drain the eventfd counter
  uint64_t val;
  (void)read(notify_fd_, &val, sizeof(val));

  std::vector<PendingEmit> local;
  {
    std::lock_guard<std::mutex> lock(emit_mutex_);
    local.swap(emit_queue_);
  }

  for (const auto &item : local) {
    if (item.type == PendingEmit::Type::Result) {
      sd_bus_emit_signal(bus_, kObjectPath, kInterface,
                         kSignalRecognitionResult, "s", item.payload.c_str());
    } else if (item.type == PendingEmit::Type::Status) {
      sd_bus_emit_signal(bus_, kObjectPath, kInterface, kSignalStatusChanged,
                         "s", item.payload.c_str());
    } else {
      sd_bus_emit_signal(bus_, kObjectPath, kInterface, kSignalDaemonError,
                         kErrorInfoSignature, item.error.code.c_str(),
                         item.error.subject.c_str(), item.error.detail.c_str(),
                         item.error.raw_message.c_str());
    }
  }
}

void DbusService::EmitRecognitionResult(const std::string &text) {
  {
    std::lock_guard<std::mutex> lock(emit_mutex_);
    emit_queue_.push_back({PendingEmit::Type::Result, text, {}});
  }
  uint64_t val = 1;
  (void)write(notify_fd_, &val, sizeof(val));
}

void DbusService::EmitStatusChanged(const std::string &status) {
  {
    std::lock_guard<std::mutex> lock(emit_mutex_);
    emit_queue_.push_back({PendingEmit::Type::Status, status, {}});
  }
  uint64_t val = 1;
  (void)write(notify_fd_, &val, sizeof(val));
}

void DbusService::EmitError(const vinput::dbus::ErrorInfo &error) {
  {
    std::lock_guard<std::mutex> lock(emit_mutex_);
    emit_queue_.push_back({PendingEmit::Type::Error, {}, error});
  }
  uint64_t val = 1;
  (void)write(notify_fd_, &val, sizeof(val));
}

void DbusService::SetStartHandler(std::function<MethodResult()> handler) {
  start_handler_ = std::move(handler);
}

void DbusService::SetStartCommandHandler(
    std::function<MethodResult(const std::string &)> handler) {
  start_command_handler_ = std::move(handler);
}

void DbusService::SetStopHandler(
    std::function<MethodResult(const std::string &scene_id)> handler) {
  stop_handler_ = std::move(handler);
}

void DbusService::SetStatusHandler(std::function<std::string()> handler) {
  status_handler_ = std::move(handler);
}

void DbusService::SetStartAdaptorHandler(
    std::function<MethodResult(const std::string &)> handler) {
  start_adaptor_handler_ = std::move(handler);
}

void DbusService::SetStopAdaptorHandler(
    std::function<MethodResult(const std::string &)> handler) {
  stop_adaptor_handler_ = std::move(handler);
}

int DbusService::handleStartRecording(sd_bus_message *m, void *userdata,
                                      sd_bus_error *error) {
  auto *self = static_cast<DbusService *>(userdata);
  MethodResult result;
  if (self->start_handler_) {
    result = self->start_handler_();
  }
  return ReplyWithMethodResult(m, error, result, "");
}

int DbusService::handleStartCommandRecording(sd_bus_message *m, void *userdata,
                                             sd_bus_error *error) {
  auto *self = static_cast<DbusService *>(userdata);
  const char *selected_text = "";
  int ret = sd_bus_message_read(m, "s", &selected_text);
  if (ret < 0) {
    fprintf(stderr, "vinput: failed to read StartCommandRecording text: %s\n",
            strerror(-ret));
    return ret;
  }

  MethodResult result;
  if (self->start_command_handler_) {
    result = self->start_command_handler_(selected_text ? selected_text : "");
  }
  return ReplyWithMethodResult(m, error, result, "");
}

int DbusService::handleStopRecording(sd_bus_message *m, void *userdata,
                                     sd_bus_error *error) {
  auto *self = static_cast<DbusService *>(userdata);
  const char *scene_id = "";
  int ret = sd_bus_message_read(m, "s", &scene_id);
  if (ret < 0) {
    fprintf(stderr, "vinput: failed to read StopRecording scene id: %s\n",
            strerror(-ret));
    return ret;
  }

  MethodResult result;
  if (self->stop_handler_) {
    result = self->stop_handler_(scene_id ? scene_id : "");
  }
  return ReplyWithMethodResult(m, error, result, "s");
}

int DbusService::handleGetStatus(sd_bus_message *m, void *userdata,
                                 sd_bus_error *error) {
  (void)error;
  auto *self = static_cast<DbusService *>(userdata);
  std::string status = kStatusIdle;
  if (self->status_handler_) {
    status = self->status_handler_();
  }
  return sd_bus_reply_method_return(m, "s", status.c_str());
}

int DbusService::handleStartAdaptor(sd_bus_message *m, void *userdata,
                                    sd_bus_error *error) {
  auto *self = static_cast<DbusService *>(userdata);
  const char *adaptor_id = "";
  int ret = sd_bus_message_read(m, "s", &adaptor_id);
  if (ret < 0) {
    fprintf(stderr, "vinput: failed to read StartAdaptor id: %s\n",
            strerror(-ret));
    return ret;
  }

  MethodResult result;
  if (self->start_adaptor_handler_) {
    result = self->start_adaptor_handler_(adaptor_id ? adaptor_id : "");
  }
  return ReplyWithMethodResult(m, error, result, "");
}

int DbusService::handleStopAdaptor(sd_bus_message *m, void *userdata,
                                   sd_bus_error *error) {
  auto *self = static_cast<DbusService *>(userdata);
  const char *adaptor_id = "";
  int ret = sd_bus_message_read(m, "s", &adaptor_id);
  if (ret < 0) {
    fprintf(stderr, "vinput: failed to read StopAdaptor id: %s\n",
            strerror(-ret));
    return ret;
  }

  MethodResult result;
  if (self->stop_adaptor_handler_) {
    result = self->stop_adaptor_handler_(adaptor_id ? adaptor_id : "");
  }
  return ReplyWithMethodResult(m, error, result, "");
}
