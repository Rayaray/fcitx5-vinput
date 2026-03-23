#pragma once

#include "common/error_info.h"

#include <string>

namespace vinput::dbus {

constexpr const char *kFcitxBusName = "org.fcitx.Fcitx5";
constexpr const char *kBusName = "org.fcitx.Vinput";
constexpr const char *kObjectPath = "/org/fcitx/Vinput";
constexpr const char *kInterface = "org.fcitx.Vinput.Service";
constexpr const char *kNotifierObjectPath = "/org/fcitx/Fcitx5/Vinput";
constexpr const char *kNotifierInterface = "org.fcitx.Fcitx5.Vinput1";

constexpr const char *kMethodStartRecording = "StartRecording";
constexpr const char *kMethodStartCommandRecording = "StartCommandRecording";
constexpr const char *kMethodStopRecording = "StopRecording";
constexpr const char *kMethodGetStatus = "GetStatus";
constexpr const char *kMethodStartAdaptor = "StartAdaptor";
constexpr const char *kMethodStopAdaptor = "StopAdaptor";
constexpr const char *kMethodNotifyError = "NotifyError";

constexpr const char *kSignalRecognitionResult = "RecognitionResult";
constexpr const char *kSignalStatusChanged = "StatusChanged";
constexpr const char *kSignalDaemonError = "DaemonError";

constexpr const char *kErrorOperationFailed =
    "org.fcitx.Vinput.Error.OperationFailed";
constexpr const char *kStatusIdle = "idle";
constexpr const char *kStatusRecording = "recording";
constexpr const char *kStatusInferring = "inferring";
constexpr const char *kStatusPostprocessing = "postprocessing";
constexpr const char *kStatusError = "error";

enum class Status { Idle, Recording, Inferring, Postprocessing, Error };

inline const char *StatusToString(Status s) {
  switch (s) {
  case Status::Idle:
    return kStatusIdle;
  case Status::Recording:
    return kStatusRecording;
  case Status::Inferring:
    return kStatusInferring;
  case Status::Postprocessing:
    return kStatusPostprocessing;
  case Status::Error:
    return kStatusError;
  }
  return "unknown";
}

inline Status StringToStatus(const std::string &s) {
  if (s == kStatusIdle)
    return Status::Idle;
  if (s == kStatusRecording)
    return Status::Recording;
  if (s == kStatusInferring)
    return Status::Inferring;
  if (s == kStatusPostprocessing)
    return Status::Postprocessing;
  if (s == kStatusError)
    return Status::Error;
  return Status::Idle;
}

} // namespace vinput::dbus
