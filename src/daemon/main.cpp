#include "asr_provider.h"
#include "audio_capture.h"
#include "common/core_config.h"
#include "common/dbus_interface.h"
#include "common/i18n.h"
#include "common/recognition_result.h"
#include "dbus_service.h"
#include "post_processor.h"

#include <poll.h>
#include <signal.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

static std::atomic<bool> g_running{true};

static void signal_handler(int sig) {
  (void)sig;
  g_running = false;
}

namespace {

bool ShouldDisableAsrAtStartup(const CoreConfig &config, bool disable_asr,
                               std::string *reason) {
  if (disable_asr) {
    if (reason) {
      *reason = _("ASR disabled by command line.");
    }
    return true;
  }

  const AsrProvider *provider = ResolveActiveAsrProvider(config);
  if (!provider) {
    if (reason) {
      *reason = _("No active ASR provider configured.");
    }
    return true;
  }

  if (provider->type == vinput::asr::kLocalProviderType &&
      provider->model.empty()) {
    if (reason) {
      *reason = _("No local ASR model configured.");
    }
    return true;
  }

  return false;
}

void LogActiveAsrProvider(const CoreConfig &config) {
  const AsrProvider *provider = ResolveActiveAsrProvider(config);
  if (!provider) {
    fprintf(stderr, "vinput-daemon: ASR provider=(missing)\n");
    return;
  }

  if (provider->type == vinput::asr::kLocalProviderType) {
    fprintf(stderr,
            "vinput-daemon: ASR provider=%s type=%s model=%s lang=%s\n",
            provider->name.c_str(), provider->type.c_str(),
            provider->model.c_str(), config.defaultLanguage.c_str());
    return;
  }

  fprintf(stderr,
          "vinput-daemon: ASR provider=%s type=%s command=%s timeout=%dms\n",
          provider->name.c_str(), provider->type.c_str(),
          provider->command.c_str(), provider->timeoutMs);
}

void LogAsrRequest(const CoreConfig &config, std::size_t sample_count) {
  const AsrProvider *provider = ResolveActiveAsrProvider(config);
  if (!provider) {
    fprintf(stderr, "vinput-daemon: ASR request provider=(missing) samples=%zu\n",
            sample_count);
    return;
  }

  fprintf(stderr,
          "vinput-daemon: ASR request provider=%s type=%s samples=%zu\n",
          provider->name.c_str(), provider->type.c_str(), sample_count);
}

}  // namespace

int main(int argc, char *argv[]) {
  vinput::i18n::Init();
  signal(SIGTERM, signal_handler);
  signal(SIGINT, signal_handler);

  bool disable_asr = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--no-asr") == 0) {
      disable_asr = true;
    }
  }

  auto startup_settings = LoadCoreConfig();
  NormalizeCoreConfig(&startup_settings);
  std::unique_ptr<vinput::asr::Provider> asr;
  std::string asr_disabled_reason;
  disable_asr = ShouldDisableAsrAtStartup(startup_settings, disable_asr,
                                          &asr_disabled_reason);
  if (!disable_asr) {
    LogActiveAsrProvider(startup_settings);
    std::string asr_error;
    asr = vinput::asr::CreateProvider(startup_settings, &asr_error);
    if (!asr) {
      fprintf(stderr, "vinput-daemon: %s\n", asr_error.c_str());
      return 1;
    }
    if (!asr->Init(startup_settings, &asr_error)) {
      fprintf(stderr, "vinput-daemon: %s\n", asr_error.c_str());
      return 1;
    }
  }
  if (disable_asr) {
    fprintf(stderr, "vinput-daemon: running with ASR disabled");
    if (!asr_disabled_reason.empty()) {
      fprintf(stderr, " (%s)", asr_disabled_reason.c_str());
    }
    fprintf(stderr, "\n");
  }

  AudioCapture capture;
  if (!capture.Start()) {
    fprintf(stderr, "vinput-daemon: audio capture start failed, exiting\n");
    return 1;
  }

  DbusService dbus;
  PostProcessor post_processor;

  using vinput::dbus::Status;
  using vinput::dbus::StatusToString;

  // --- Single-slot state (all protected by state_mutex) ---
  struct Order {
    std::vector<int16_t> pcm;
    std::string scene_id;
    bool is_command = false;
    std::string selected_text;
  };

  std::mutex state_mutex;
  std::condition_variable worker_cv;
  Status phase{Status::Idle};
  std::optional<Order> current_order;
  bool current_is_command = false;
  std::string current_selected_text;
  std::atomic<bool> worker_running{true};
  std::thread worker;

  // Helper: set phase under lock, emit outside lock
  auto setPhase = [&](Status new_phase) {
    {
      std::lock_guard<std::mutex> lock(state_mutex);
      phase = new_phase;
    }
    dbus.EmitStatusChanged(StatusToString(new_phase));
  };

  auto resetToIdle = [&]() {
    {
      std::lock_guard<std::mutex> lock(state_mutex);
      phase = Status::Idle;
      current_order.reset();
    }
    dbus.EmitStatusChanged(StatusToString(Status::Idle));
    fprintf(stderr, "vinput-daemon: phase -> idle\n");
  };

  dbus.SetStartHandler([&]() {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (phase != Status::Idle) {
      fprintf(stderr, "vinput-daemon: start rejected (phase: %s)\n",
              StatusToString(phase));
      return DbusService::MethodResult::Failure("Daemon is busy.");
    }
    current_is_command = false;
    current_selected_text.clear();
    auto runtime_settings = LoadCoreConfig();
    capture.SetTargetObject(runtime_settings.captureDevice);
    if (!capture.BeginRecording()) {
      fprintf(stderr, "vinput-daemon: failed to start recording\n");
      return DbusService::MethodResult::Failure("Failed to start recording.");
    }
    phase = Status::Recording;
    dbus.EmitStatusChanged(StatusToString(Status::Recording));
    fprintf(stderr, "vinput-daemon: recording started\n");
    return DbusService::MethodResult::Success();
  });

  dbus.SetStartCommandHandler([&](const std::string &selected_text) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (phase != Status::Idle) {
      fprintf(stderr, "vinput-daemon: command start rejected (phase: %s)\n",
              StatusToString(phase));
      return DbusService::MethodResult::Failure("Daemon is busy.");
    }
    current_is_command = true;
    current_selected_text = selected_text;
    auto runtime_settings = LoadCoreConfig();
    capture.SetTargetObject(runtime_settings.captureDevice);
    if (!capture.BeginRecording()) {
      fprintf(stderr, "vinput-daemon: failed to start command recording\n");
      return DbusService::MethodResult::Failure(
          "Failed to start command recording.");
    }
    phase = Status::Recording;
    dbus.EmitStatusChanged(StatusToString(Status::Recording));
    fprintf(stderr,
            "vinput-daemon: command recording started (selected_text length: "
            "%zu chars)\n",
            selected_text.size());
    return DbusService::MethodResult::Success();
  });

  dbus.SetStopHandler(
      [&](const std::string &scene_id) -> DbusService::MethodResult {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (phase != Status::Recording) {
      fprintf(stderr, "vinput-daemon: stop rejected (phase: %s)\n",
              StatusToString(phase));
      return DbusService::MethodResult::Failure("Recording is not active.");
    }

    capture.EndRecording();
    auto pcm = capture.StopAndGetBuffer();
    if (pcm.empty()) {
      fprintf(stderr,
              "vinput-daemon: recording stopped with empty audio buffer\n");
      phase = Status::Idle;
      current_order.reset();
      dbus.EmitStatusChanged(StatusToString(Status::Idle));
      fprintf(stderr, "vinput-daemon: phase -> idle\n");
      return DbusService::MethodResult::Success();
    }

    if (pcm.size() < vinput::asr::kMinSamplesForInference) {
      fprintf(stderr,
              "vinput-daemon: recording too short, skipping inference: "
              "%zu samples (%.1f ms)\n",
              pcm.size(), static_cast<double>(pcm.size()) * 1000.0 / 16000.0);
      phase = Status::Idle;
      current_order.reset();
      dbus.EmitStatusChanged(StatusToString(Status::Idle));
      fprintf(stderr, "vinput-daemon: phase -> idle\n");
      return DbusService::MethodResult::Success();
    }

    current_order = Order{std::move(pcm), scene_id, current_is_command,
                          current_selected_text};
    current_is_command = false;
    current_selected_text.clear();
    phase = Status::Inferring;
    dbus.EmitStatusChanged(StatusToString(Status::Inferring));
    worker_cv.notify_one();
    fprintf(stderr, "vinput-daemon: recording stopped, queued inference\n");
    return DbusService::MethodResult::Success();
  });

  dbus.SetStatusHandler([&]() -> std::string {
    std::lock_guard<std::mutex> lock(state_mutex);
    return StatusToString(phase);
  });

  if (!dbus.Start()) {
    fprintf(stderr, "vinput-daemon: DBus service start failed, exiting\n");
    return 1;
  }

  worker = std::thread([&]() {
    while (worker_running) {
      Order order;
      {
        std::unique_lock<std::mutex> lock(state_mutex);
        worker_cv.wait(lock, [&]() {
          return current_order.has_value() || !worker_running.load();
        });
        if (!worker_running && !current_order.has_value()) {
          break;
        }
        order = std::move(*current_order);
      }

      try {
        std::string text;
        if (!disable_asr) {
          LogAsrRequest(startup_settings, order.pcm.size());
          auto asr_result = asr->Infer(order.pcm);
          if (!asr_result.ok && !asr_result.error.empty()) {
            fprintf(stderr, "vinput-daemon: ASR provider error: %s\n",
                    asr_result.error.c_str());
            dbus.EmitError(asr_result.error);
          }
          text = std::move(asr_result.text);
        }

        vinput::result::Payload result;
        if (!text.empty()) {
          auto runtime_settings = LoadCoreConfig();
          NormalizeCoreConfig(&runtime_settings);
          vinput::scene::Config scene_config;
          scene_config.activeSceneId = runtime_settings.scenes.activeScene;
          scene_config.scenes = runtime_settings.scenes.definitions;
          if (order.is_command) {
            const auto *cmd_scene = FindCommandScene(runtime_settings);
            if (cmd_scene && cmd_scene->candidate_count > 0 &&
                !cmd_scene->provider_id.empty()) {
              setPhase(Status::Postprocessing);
            }
            vinput::scene::Definition fallback_cmd;
            fallback_cmd.id = std::string(vinput::scene::kCommandSceneId);
            fallback_cmd.builtin = true;
            const auto &command_scene = cmd_scene ? *cmd_scene : fallback_cmd;
            std::string llm_error;
            result = post_processor.ProcessCommand(
                text, order.selected_text, command_scene, runtime_settings,
                &llm_error);
            if (!llm_error.empty()) {
              dbus.EmitError(llm_error);
            }
          } else {
            const auto &scene =
                vinput::scene::Resolve(scene_config, order.scene_id);
            if (scene.candidate_count > 0 && !scene.provider_id.empty() &&
                !scene.prompt.empty()) {
              setPhase(Status::Postprocessing);
            }
            std::string llm_error;
            result = post_processor.Process(text, scene, runtime_settings,
                                            &llm_error);
            if (!llm_error.empty()) {
              dbus.EmitError(llm_error);
            }
          }
        }

        dbus.EmitRecognitionResult(vinput::result::Serialize(result));

      } catch (const std::exception &e) {
        fprintf(stderr, "vinput-daemon: worker exception: %s\n", e.what());
        dbus.EmitError(e.what());
      } catch (...) {
        fprintf(stderr, "vinput-daemon: worker unknown exception\n");
        dbus.EmitError("Unknown error during processing");
      }

      // No matter what, release the slot
      resetToIdle();
    }
  });

  fprintf(stderr, "vinput-daemon: running\n");

  int dbus_fd = dbus.GetFd();
  int notify_fd = dbus.GetNotifyFd();
  while (g_running) {
    struct pollfd fds[2];
    fds[0].fd = dbus_fd;
    fds[0].events = POLLIN;
    fds[1].fd = notify_fd;
    fds[1].events = POLLIN;

    int ret = poll(fds, 2, 1000);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      fprintf(stderr, "vinput-daemon: poll error: %s\n", strerror(errno));
      break;
    }

    if (ret > 0) {
      if (fds[1].revents & POLLIN) {
        dbus.FlushEmitQueue();
      }
      if (fds[0].revents & POLLIN) {
        while (dbus.ProcessOnce()) {
          // process all pending messages
        }
      }
    }
  }

  fprintf(stderr, "vinput-daemon: shutting down\n");
  {
    std::lock_guard<std::mutex> lock(state_mutex);
    worker_running = false;
  }
  worker_cv.notify_all();
  if (worker.joinable()) {
    worker.join();
  }
  if (asr) {
    asr->Shutdown();
  }
  return 0;
}
