#include "vinput.h"
#include "common/dbus_interface.h"
#include "common/error_info.h"
#include "common/i18n.h"
#include "common/recognition_result.h"
#include "common/string_utils.h"

#include "notifications_public.h"
#include <dbus_public.h>
#include <fcitx-utils/dbus/matchrule.h>
#include <fcitx-utils/dbus/message.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>

#include <cstdio>
#include <string>
#include <tuple>

using namespace vinput::dbus;

namespace {

constexpr const char *kSystemdBusName = "org.freedesktop.systemd1";
constexpr const char *kSystemdPath = "/org/freedesktop/systemd1";
constexpr const char *kSystemdManagerInterface =
    "org.freedesktop.systemd1.Manager";
constexpr const char *kSystemdRestartUnit = "RestartUnit";
constexpr uint64_t kSystemdCallTimeoutUsec = 5 * 1000 * 1000;
constexpr const char *kDaemonUnitName = "vinput-daemon.service";
constexpr const char *kReplaceMode = "replace";
constexpr uint64_t kDaemonCallTimeoutUsec = 5 * 1000 * 1000;
constexpr uint64_t kStatusSyncIntervalUsec = 200 * 1000;

std::string RecordingPreeditText() { return _("... Recording ..."); }

std::string CommandingPreeditText() { return _("... Commanding ..."); }

std::string InferringPreeditText() { return _("... Recognizing ..."); }

std::string PostprocessingPreeditText() { return _("... Postprocessing ..."); }

std::string AppendDetail(std::string summary, const std::string &detail) {
  if (detail.empty()) {
    return summary;
  }
  summary += "\n";
  summary += detail;
  return summary;
}

std::string RenderErrorMessage(const vinput::dbus::ErrorInfo &error) {
  using namespace vinput::dbus;

  if (error.code == kErrorCodeLocalAsrModelConfigMissing) {
    return error.subject.empty()
               ? _("Local ASR model configuration is missing. Please configure a model first.")
               : vinput::str::FmtStr(
                     _("Local ASR model configuration is missing for provider '%s'. Please configure a model first."),
                     error.subject);
  }
  if (error.code == kErrorCodeLocalAsrModelTypeMissing) {
    return error.subject.empty()
               ? _("Local ASR model metadata is missing model_type.")
               : vinput::str::FmtStr(
                     _("Local ASR model metadata is missing model_type for provider '%s'."),
                     error.subject);
  }
  if (error.code == kErrorCodeLocalAsrModelInvalidPath) {
    return error.subject.empty()
               ? _("Local ASR model metadata contains an invalid path.")
               : vinput::str::FmtStr(
                     _("Local ASR model metadata contains an invalid path for provider '%s'."),
                     error.subject);
  }
  if (error.code == kErrorCodeLocalAsrModelTokensMissing) {
    return error.subject.empty()
               ? _("Local ASR model tokens file is missing.")
               : vinput::str::FmtStr(
                     _("Local ASR model tokens file is missing for provider '%s'."),
                     error.subject);
  }
  if (error.code == kErrorCodeLocalAsrModelFilesMissing) {
    return error.subject.empty()
               ? _("Local ASR model files are missing.")
               : vinput::str::FmtStr(
                     _("Local ASR model files are missing for provider '%s'."),
                     error.subject);
  }
  if (error.code == kErrorCodeLocalAsrModelRootResolveFailed) {
    return error.subject.empty()
               ? _("Failed to resolve the local ASR model directory.")
               : vinput::str::FmtStr(
                     _("Failed to resolve the local ASR model directory for provider '%s'."),
                     error.subject);
  }
  if (error.code == kErrorCodeLocalAsrModelParseFailed) {
    return error.subject.empty()
               ? _("Failed to parse local ASR model metadata.")
               : vinput::str::FmtStr(
                     _("Failed to parse local ASR model metadata for provider '%s'."),
                     error.subject);
  }
  if (error.code == kErrorCodeLocalAsrUnsupportedModelType) {
    return error.subject.empty()
               ? _("The local ASR model type is not supported.")
               : vinput::str::FmtStr(
                     _("The local ASR model type is not supported for provider '%s'."),
                     error.subject);
  }
  if (error.code == kErrorCodeLocalAsrRecognizerCreateFailed) {
    return error.subject.empty()
               ? _("Failed to initialize the local ASR recognizer.")
               : vinput::str::FmtStr(
                     _("Failed to initialize the local ASR recognizer for provider '%s'."),
                     error.subject);
  }
  if (error.code == kErrorCodeVadCreateFailed) {
    return error.subject.empty()
               ? _("Failed to initialize VAD.")
               : vinput::str::FmtStr(
                     _("Failed to initialize VAD for provider '%s'."),
                     error.subject);
  }
  if (error.code == kErrorCodeLocalAsrModelCheckFailed) {
    return error.subject.empty()
               ? _("Local ASR model check failed.")
               : vinput::str::FmtStr(
                     _("Local ASR model check failed for provider '%s'."),
                     error.subject);
  }
  if (error.code == kErrorCodeLocalAsrProviderInitFailed) {
    return error.subject.empty()
               ? _("Failed to initialize the local ASR provider.")
               : vinput::str::FmtStr(
                     _("Failed to initialize the local ASR provider '%s'."),
                     error.subject);
  }
  if (error.code == kErrorCodeAudioCaptureLoopNotInitialized) {
    return _("Audio capture is not initialized.");
  }
  if (error.code == kErrorCodePipeWireThreadLoopCreateFailed) {
    return _("Failed to create the PipeWire thread loop.");
  }
  if (error.code == kErrorCodePipeWireThreadLoopStartFailed) {
    return AppendDetail(_("Failed to start the PipeWire thread loop."),
                        error.detail);
  }
  if (error.code == kErrorCodePipeWirePropertiesAllocFailed) {
    return _("Failed to allocate PipeWire properties.");
  }
  if (error.code == kErrorCodePipeWireStreamCreateFailed) {
    return _("Failed to create the PipeWire stream.");
  }
  if (error.code == kErrorCodePipeWireStreamConnectFailed) {
    return AppendDetail(_("Failed to connect the PipeWire stream."),
                        error.detail);
  }
  if (error.code == kErrorCodeDbusEventfdCreateFailed) {
    return AppendDetail(_("Failed to initialize daemon notifications."),
                        error.detail);
  }
  if (error.code == kErrorCodeDbusUserBusOpenFailed) {
    return AppendDetail(_("Failed to connect to the user D-Bus."),
                        error.detail);
  }
  if (error.code == kErrorCodeDbusVtableAddFailed) {
    return AppendDetail(_("Failed to register the daemon D-Bus interface."),
                        error.detail);
  }
  if (error.code == kErrorCodeDbusNameRequestFailed) {
    return AppendDetail(_("Failed to acquire the daemon D-Bus name."),
                        error.detail);
  }
  if (error.code == kErrorCodeStartRecordingFailed) {
    return _("Failed to start recording.");
  }
  if (error.code == kErrorCodeStartCommandRecordingFailed) {
    return _("Failed to start command recording.");
  }
  if (error.code == kErrorCodeAsrProviderStartFailed) {
    return error.subject.empty()
               ? _("ASR provider failed to start.")
               : vinput::str::FmtStr(_("ASR provider '%s' failed to start."),
                                     error.subject);
  }
  if (error.code == kErrorCodeAsrProviderTimeout) {
    return error.subject.empty()
               ? _("ASR provider timed out.")
               : vinput::str::FmtStr(_("ASR provider '%s' timed out."),
                                     error.subject);
  }
  if (error.code == kErrorCodeAsrProviderFailed) {
    return error.subject.empty()
               ? _("ASR provider failed.")
               : vinput::str::FmtStr(_("ASR provider '%s' failed."),
                                     error.subject);
  }
  if (error.code == kErrorCodeAsrProviderNoText) {
    return error.subject.empty()
               ? _("ASR provider returned no text.")
               : vinput::str::FmtStr(_("ASR provider '%s' returned no text."),
                                     error.subject);
  }
  if (error.code == kErrorCodeLlmRequestFailed) {
    return AppendDetail(_("LLM request failed."), error.detail);
  }
  if (error.code == kErrorCodeLlmHttpFailed) {
    return _("LLM request returned an HTTP error.");
  }
  if (error.code == kErrorCodeProcessingUnknown) {
    return _("Unknown error during processing.");
  }
  if (error.code == kErrorCodeDaemonStartFailed) {
    return _("Failed to start the daemon.");
  }
  if (error.code == kErrorCodeDaemonRestartFailed) {
    return _("Failed to restart the daemon.");
  }

  if (!error.raw_message.empty()) {
    return error.raw_message;
  }
  if (!error.detail.empty()) {
    return error.detail;
  }
  return _("Unknown error.");
}

} // namespace

void VinputEngine::setupDBusWatcher() {
  if (!bus_)
    return;

  fcitx::dbus::MatchRule result_rule(kBusName, kObjectPath, kInterface,
                                     kSignalRecognitionResult);

  result_slot_ = bus_->addMatch(result_rule, [this](fcitx::dbus::Message &msg) {
    onRecognitionResult(msg);
    return true;
  });

  fcitx::dbus::MatchRule status_rule(kBusName, kObjectPath, kInterface,
                                     kSignalStatusChanged);

  status_slot_ = bus_->addMatch(status_rule, [this](fcitx::dbus::Message &msg) {
    onStatusChanged(msg);
    return true;
  });

  fcitx::dbus::MatchRule error_rule(kBusName, kObjectPath, kInterface,
                                    kSignalDaemonError);

  error_slot_ =
      bus_->addMatch(error_rule, [this](fcitx::dbus::Message &msg) {
        onDaemonError(msg);
        return true;
      });
}

void VinputEngine::callStartRecording() {
  if (!bus_)
    return;
  auto msg = bus_->createMethodCall(kBusName, kObjectPath, kInterface,
                                    kMethodStartRecording);
  auto reply = msg.call(kDaemonCallTimeoutUsec);
  if (!reply || reply.isError()) {
    fprintf(stderr, "vinput: StartRecording rejected by daemon\n");
    syncFrontendWithDaemonStatus(session_ ? session_->ic : status_ic_,
                                 false);
  }
}

void VinputEngine::callStartCommandRecording(const std::string &selected_text) {
  if (!bus_)
    return;
  auto msg = bus_->createMethodCall(kBusName, kObjectPath, kInterface,
                                    kMethodStartCommandRecording);
  msg << selected_text;
  auto reply = msg.call(kDaemonCallTimeoutUsec);
  if (!reply || reply.isError()) {
    fprintf(stderr, "vinput: StartCommandRecording rejected by daemon\n");
    syncFrontendWithDaemonStatus(session_ ? session_->ic : status_ic_,
                                 true);
  }
}

void VinputEngine::callStopRecording(const std::string &scene_id) {
  if (!bus_)
    return;
  auto msg = bus_->createMethodCall(kBusName, kObjectPath, kInterface,
                                    kMethodStopRecording);
  msg << scene_id;
  auto reply = msg.call(kDaemonCallTimeoutUsec);
  if (!reply || reply.isError()) {
    fprintf(stderr, "vinput: StopRecording rejected by daemon\n");
    syncFrontendWithDaemonStatus(session_ ? session_->ic : status_ic_,
                                 session_ ? session_->command_mode : false);
  }
}

void VinputEngine::ensureStatusSync() {
  const bool needs_sync = session_.has_value() || status_ic_ != nullptr;
  if (!needs_sync) {
    stopStatusSyncIfIdle();
    return;
  }

  if (!status_sync_event_) {
    status_sync_event_ = instance_->eventLoop().addTimeEvent(
        CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + kStatusSyncIntervalUsec, 0,
        [this](fcitx::EventSourceTime *event, uint64_t) {
          syncFrontendWithDaemonStatus();
          if (!(session_.has_value() || status_ic_ != nullptr)) {
            return false;
          }
          event->setNextInterval(kStatusSyncIntervalUsec);
          return true;
        });
    return;
  }

  status_sync_event_->setTime(fcitx::now(CLOCK_MONOTONIC) +
                              kStatusSyncIntervalUsec);
  status_sync_event_->setEnabled(true);
}

void VinputEngine::stopStatusSyncIfIdle() {
  if (status_sync_event_ && !(session_.has_value() || status_ic_ != nullptr)) {
    status_sync_event_->setEnabled(false);
  }
}

void VinputEngine::enterRecordingState(fcitx::InputContext *ic,
                                       const fcitx::Key &trigger,
                                       bool command_mode) {
  if (!ic) {
    return;
  }
  if (status_ic_ && status_ic_ != ic) {
    clearPreedit(status_ic_);
  }
  if (!session_) {
    session_.emplace(Session{Session::Phase::Recording, ic, trigger,
                             std::chrono::steady_clock::now(), command_mode});
  } else {
    session_->phase = Session::Phase::Recording;
    session_->ic = ic;
    session_->trigger = trigger;
    session_->command_mode = command_mode;
  }
  status_ic_ = ic;
  updatePreedit(ic, command_mode ? CommandingPreeditText() : RecordingPreeditText());
  ensureStatusSync();
}

void VinputEngine::enterBusyState(fcitx::InputContext *ic, bool command_mode,
                                  const std::string &preedit_text) {
  if (!ic) {
    return;
  }
  if (status_ic_ && status_ic_ != ic) {
    clearPreedit(status_ic_);
  }
  if (!session_) {
    session_.emplace(Session{Session::Phase::Busy, ic, fcitx::Key(),
                             std::chrono::steady_clock::now(), command_mode});
  } else {
    session_->phase = Session::Phase::Busy;
    session_->ic = ic;
    session_->trigger = fcitx::Key();
    session_->command_mode = command_mode;
  }
  status_ic_ = ic;
  updatePreedit(ic, preedit_text);
  ensureStatusSync();
}

void VinputEngine::finishFrontendSession(fcitx::InputContext *fallback_ic) {
  auto *ic = session_ ? session_->ic
                      : (fallback_ic ? fallback_ic : status_ic_);
  session_.reset();
  if (status_ic_ == ic) {
    status_ic_ = nullptr;
  }
  if (ic) {
    clearPreedit(ic);
  }
  stopStatusSyncIfIdle();
}

std::string VinputEngine::queryDaemonStatus() const {
  if (!bus_) {
    return {};
  }

  auto msg =
      bus_->createMethodCall(kBusName, kObjectPath, kInterface, kMethodGetStatus);
  auto reply = msg.call(kDaemonCallTimeoutUsec);
  if (!reply || reply.isError()) {
    return {};
  }

  std::string status;
  reply >> status;
  return status;
}

void VinputEngine::syncFrontendWithDaemonStatus(fcitx::InputContext *fallback_ic,
                                                bool prefer_command_mode) {
  const std::string status = queryDaemonStatus();
  auto *ic = session_ ? session_->ic
                      : (fallback_ic ? fallback_ic : status_ic_);
  if (!ic) {
    return;
  }

  if (status == kStatusRecording) {
    enterRecordingState(ic, session_ ? session_->trigger : fcitx::Key(),
                        session_ ? session_->command_mode : prefer_command_mode);
    return;
  }

  if (status == kStatusInferring) {
    enterBusyState(ic, session_ ? session_->command_mode : prefer_command_mode,
                   InferringPreeditText());
    return;
  }

  if (status == kStatusPostprocessing) {
    enterBusyState(ic, session_ ? session_->command_mode : prefer_command_mode,
                   PostprocessingPreeditText());
    return;
  }

  finishFrontendSession(ic);
}

void VinputEngine::restartDaemon() {
  if (!bus_) {
    fprintf(
        stderr,
        "vinput: cannot restart vinput-daemon because DBus is unavailable\n");
    return;
  }

  auto msg =
      bus_->createMethodCall(kSystemdBusName, kSystemdPath,
                             kSystemdManagerInterface, kSystemdRestartUnit);
  msg << kDaemonUnitName << kReplaceMode;

  auto reply = msg.call(kSystemdCallTimeoutUsec);
  if (!reply) {
    fprintf(stderr,
            "vinput: failed to restart vinput-daemon via systemd user bus\n");
    return;
  }

  if (reply.isError()) {
    fprintf(stderr, "vinput: systemd restart failed: %s: %s\n",
            reply.errorName().c_str(), reply.errorMessage().c_str());
  }
}

void VinputEngine::onRecognitionResult(fcitx::dbus::Message &msg) {
  std::string payload_text;
  msg >> payload_text;

  const bool has_session = session_.has_value();
  const bool is_command = has_session && session_->command_mode;
  auto *ic = has_session ? session_->ic : status_ic_;

  if (!ic) {
    return;
  }

  hideResultMenu();

  const auto payload = vinput::result::Parse(payload_text);
  finishFrontendSession(ic);

  if (!has_session) {
    return;
  }

  if (payload.commitText.empty()) {
    return;
  }

  int llm_count = 0;
  for (const auto &c : payload.candidates) {
    if (c.source == vinput::result::kSourceLlm) ++llm_count;
  }
  if (llm_count > 1) {
    // Save command mode for result menu interaction
    result_is_command_ = is_command;
    showResultMenu(ic, payload);
    return;
  }

  if (is_command) {
    auto &surrounding = ic->surroundingText();
    if (surrounding.isValid() && surrounding.cursor() != surrounding.anchor()) {
      int cursor = surrounding.cursor();
      int anchor = surrounding.anchor();
      int from = std::min(cursor, anchor);
      int len = std::abs(cursor - anchor);
      ic->deleteSurroundingText(from - cursor, len);
    }
  }

  ic->commitString(payload.commitText);
}

void VinputEngine::onStatusChanged(fcitx::dbus::Message &msg) {
  std::string status;
  msg >> status;

  auto *ic = session_ ? session_->ic : status_ic_;
  if (!ic)
    return;

  if (status == kStatusRecording) {
    enterRecordingState(ic, session_ ? session_->trigger : fcitx::Key(),
                        session_ ? session_->command_mode : false);
  } else if (status == kStatusInferring) {
    enterBusyState(ic, session_ ? session_->command_mode : false,
                   InferringPreeditText());
  } else if (status == kStatusPostprocessing) {
    enterBusyState(ic, session_ ? session_->command_mode : false,
                   PostprocessingPreeditText());
  } else {
    finishFrontendSession(ic);
  }
}

void VinputEngine::onDaemonError(fcitx::dbus::Message &msg) {
  std::tuple<std::string, std::string, std::string, std::string> payload;
  msg >> payload;
  auto error = vinput::dbus::MakeErrorInfo(
      std::move(std::get<0>(payload)), std::move(std::get<1>(payload)),
      std::move(std::get<2>(payload)), std::move(std::get<3>(payload)));

  if (error.empty()) {
    return;
  }

  auto *ic = session_ ? session_->ic : status_ic_;
  hideResultMenu();
  finishFrontendSession(ic);

  notifyError(error);
}

void VinputEngine::notifyError(const vinput::dbus::ErrorInfo &error) {
  if (error.empty()) {
    return;
  }

  std::string title = _("Voice Input");
  std::string message = RenderErrorMessage(error);

  auto *notifications =
      instance_->addonManager().addon("notifications", true);
  if (notifications) {
    notifications->call<fcitx::INotifications::sendNotification>(
        "fcitx5-vinput", 0, "dialog-error",
        title, message, std::vector<std::string>{},
        5000, fcitx::NotificationActionCallback{},
        fcitx::NotificationClosedCallback{});
  } else {
    fprintf(stderr, "vinput: %s: %s\n", title.c_str(), message.c_str());
  }
}

void VinputEngine::notifyError(const std::string &message) {
  notifyError(vinput::dbus::MakeRawError(message));
}

void VinputEngine::updatePreedit(fcitx::InputContext *ic,
                                 const std::string &text) {
  if (!ic)
    return;
  fcitx::Text preedit;
  preedit.append(text);
  ic->inputPanel().setPreedit(preedit);
  ic->inputPanel().setClientPreedit(preedit);
  ic->updatePreedit();
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}

void VinputEngine::clearPreedit(fcitx::InputContext *ic) {
  if (!ic)
    return;
  fcitx::Text empty;
  ic->inputPanel().setPreedit(empty);
  ic->inputPanel().setClientPreedit(empty);
  ic->updatePreedit();
  ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
}
