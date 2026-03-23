#pragma once

#include "common/error_info.h"

#include <systemd/sd-bus.h>
#include <sys/eventfd.h>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

class DbusService {
public:
  struct MethodResult {
    bool ok = true;
    std::string message;
    std::string payload;

    static MethodResult Success(std::string payload = {}) {
      MethodResult result;
      result.payload = std::move(payload);
      return result;
    }

    static MethodResult Failure(std::string message) {
      MethodResult result;
      result.ok = false;
      result.message = std::move(message);
      return result;
    }
  };

  DbusService();
  ~DbusService();

  bool Start(std::string *error = nullptr);
  int GetFd() const;
  int GetNotifyFd() const;
  bool ProcessOnce();
  void FlushEmitQueue(); // main thread only
  void EmitRecognitionResult(const std::string &text);
  void EmitStatusChanged(const std::string &status);
  void EmitError(const vinput::dbus::ErrorInfo &error);

  void SetStartHandler(std::function<MethodResult()> handler);
  void SetStartCommandHandler(
      std::function<MethodResult(const std::string &)> handler);
  void SetStopHandler(
      std::function<MethodResult(const std::string &scene_id)> handler);
  void SetStatusHandler(std::function<std::string()> handler);
  void SetStartAdaptorHandler(
      std::function<MethodResult(const std::string &adaptor_id)> handler);
  void SetStopAdaptorHandler(
      std::function<MethodResult(const std::string &adaptor_id)> handler);

  static int handleStartRecording(sd_bus_message *m, void *userdata,
                                  sd_bus_error *error);
  static int handleStartCommandRecording(sd_bus_message *m, void *userdata,
                                         sd_bus_error *error);
  static int handleStopRecording(sd_bus_message *m, void *userdata,
                                 sd_bus_error *error);
  static int handleGetStatus(sd_bus_message *m, void *userdata,
                             sd_bus_error *error);
  static int handleStartAdaptor(sd_bus_message *m, void *userdata,
                                sd_bus_error *error);
  static int handleStopAdaptor(sd_bus_message *m, void *userdata,
                               sd_bus_error *error);

private:
  sd_bus *bus_ = nullptr;
  sd_bus_slot *slot_ = nullptr;
  int notify_fd_ = -1;

  struct PendingEmit {
    enum class Type { Result, Status, Error };
    Type type;
    std::string payload;
    vinput::dbus::ErrorInfo error;
  };
  std::mutex emit_mutex_;
  std::vector<PendingEmit> emit_queue_;

  std::function<MethodResult()> start_handler_;
  std::function<MethodResult(const std::string &)> start_command_handler_;
  std::function<MethodResult(const std::string &scene_id)> stop_handler_;
  std::function<std::string()> status_handler_;
  std::function<MethodResult(const std::string &adaptor_id)>
      start_adaptor_handler_;
  std::function<MethodResult(const std::string &adaptor_id)>
      stop_adaptor_handler_;
};
