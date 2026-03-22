#include "vinput.h"
#include "common/dbus_interface.h"
#include "common/i18n.h"
#include "common/recognition_result.h"

#include "notifications_public.h"
#include <dbus_public.h>
#include <fcitx-utils/dbus/matchrule.h>
#include <fcitx-utils/dbus/message.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>

#include <cstdio>
#include <string>

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
  std::string error_message;
  msg >> error_message;

  if (error_message.empty()) {
    return;
  }

  auto *ic = session_ ? session_->ic : status_ic_;
  hideResultMenu();
  finishFrontendSession(ic);

  notifyError(error_message);
}

void VinputEngine::notifyError(const std::string &message) {
  if (message.empty()) {
    return;
  }

  auto *notifications =
      instance_->addonManager().addon("notifications", true);
  if (notifications) {
    notifications->call<fcitx::INotifications::sendNotification>(
        "fcitx5-vinput", 0, "dialog-error",
        _("Voice Input"), message, std::vector<std::string>{},
        5000, fcitx::NotificationActionCallback{},
        fcitx::NotificationClosedCallback{});
  } else {
    fprintf(stderr, "vinput: %s\n", message.c_str());
  }
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
