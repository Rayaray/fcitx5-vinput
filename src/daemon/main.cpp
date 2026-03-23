#include "asr_provider.h"
#include "audio_capture.h"
#include "common/adaptor_manager.h"
#include "common/core_config.h"
#include "common/dbus_interface.h"
#include "common/i18n.h"
#include "common/process_utils.h"
#include "common/recognition_result.h"
#include "common/string_utils.h"
#include "dbus_service.h"
#include "post_processor.h"

#include <poll.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> g_running{true};

static void signal_handler(int sig) {
  (void)sig;
  g_running = false;
}

namespace {

std::string ReadAvailableText(int fd) {
  std::string text;
  char buffer[4096];
  while (true) {
    const ssize_t n = read(fd, buffer, sizeof(buffer));
    if (n > 0) {
      text.append(buffer, static_cast<std::size_t>(n));
      continue;
    }
    if (n < 0 && errno == EINTR) {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    }
    break;
  }
  return text;
}

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

class AdaptorSupervisor {
public:
  explicit AdaptorSupervisor(DbusService *dbus) : dbus_(dbus) {}

  ~AdaptorSupervisor() {
    Shutdown();
    if (wake_fd_ >= 0) {
      close(wake_fd_);
    }
  }

  bool Start(std::string *error) {
    wake_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wake_fd_ < 0) {
      if (error) {
        *error = std::string("failed to create adaptor wake fd: ") +
                 strerror(errno);
      }
      return false;
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      running_ = true;
      stop_requested_ = false;
    }

    try {
      thread_ = std::thread([this]() { Run(); });
    } catch (const std::exception &e) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        stop_requested_ = true;
      }
      if (error) {
        *error =
            std::string("failed to start adaptor supervisor thread: ") + e.what();
      }
      close(wake_fd_);
      wake_fd_ = -1;
      return false;
    }

    if (error) {
      error->clear();
    }
    return true;
  }

  void Shutdown() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_) {
        return;
      }
      running_ = false;
      stop_requested_ = true;
    }
    Wake();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  DbusService::MethodResult StartAdaptor(const std::string &adaptor_id) {
    return SubmitRequest(Request::Type::Start, adaptor_id);
  }

  DbusService::MethodResult StopAdaptor(const std::string &adaptor_id) {
    return SubmitRequest(Request::Type::Stop, adaptor_id);
  }

private:
  struct ManagedAdaptor {
    std::string id;
    pid_t pid = -1;
    int stderr_fd = -1;
    std::string stderr_buffer;
  };

  struct Request {
    enum class Type { Start, Stop };

    Type type = Type::Start;
    std::string adaptor_id;
    DbusService::MethodResult result;
    bool done = false;
    std::condition_variable cv;
  };

  DbusService::MethodResult SubmitRequest(Request::Type type,
                                          const std::string &adaptor_id) {
    auto request = std::make_shared<Request>();
    request->type = type;
    request->adaptor_id = adaptor_id;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!running_ || stop_requested_) {
        return DbusService::MethodResult::Failure(
            "adaptor supervisor is not running");
      }
      pending_requests_.push_back(request);
    }

    Wake();

    std::unique_lock<std::mutex> lock(mutex_);
    request->cv.wait(lock, [&]() { return request->done; });
    return request->result;
  }

  void Wake() {
    if (wake_fd_ < 0) {
      return;
    }
    uint64_t value = 1;
    (void)write(wake_fd_, &value, sizeof(value));
  }

  void DrainWakeFd() {
    if (wake_fd_ < 0) {
      return;
    }
    uint64_t value = 0;
    while (read(wake_fd_, &value, sizeof(value)) == sizeof(value)) {
    }
  }

  void EmitNotification(std::string_view text) {
    const std::string notification = vinput::str::TrimAsciiWhitespace(text);
    if (notification.empty()) {
      return;
    }
    dbus_->EmitError(vinput::dbus::MakeRawError(notification));
  }

  void FlushAdaptorBuffer(ManagedAdaptor &adaptor, bool flush_partial) {
    size_t start = 0;
    while (true) {
      const size_t end = adaptor.stderr_buffer.find('\n', start);
      if (end == std::string::npos) {
        break;
      }
      std::string line = adaptor.stderr_buffer.substr(start, end - start);
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      if (!line.empty()) {
        fprintf(stderr, "vinput-daemon: adaptor[%s] stderr: %s\n",
                adaptor.id.c_str(), line.c_str());
        EmitNotification(line);
      }
      start = end + 1;
    }

    adaptor.stderr_buffer.erase(0, start);
    if (flush_partial) {
      std::string line = vinput::str::TrimAsciiWhitespace(adaptor.stderr_buffer);
      if (!line.empty()) {
        fprintf(stderr, "vinput-daemon: adaptor[%s] stderr: %s\n",
                adaptor.id.c_str(), line.c_str());
        EmitNotification(line);
      }
      adaptor.stderr_buffer.clear();
    }
  }

  DbusService::MethodResult HandleStartRequest(const std::string &adaptor_id) {
    std::string error;
    auto info = vinput::adaptor::FindById(adaptor_id, &error);
    if (!info.has_value()) {
      return DbusService::MethodResult::Failure(
          error.empty() ? "adaptor not found" : error);
    }
    if (adaptors_.find(adaptor_id) != adaptors_.end() ||
        vinput::adaptor::IsRunning(*info)) {
      return DbusService::MethodResult::Failure("adaptor is already running");
    }

    auto runtime_settings = LoadCoreConfig();
    NormalizeCoreConfig(&runtime_settings);
    const auto spec = vinput::adaptor::BuildCommandSpec(*info, runtime_settings);
    vinput::process::SpawnedProcess process;
    if (!vinput::process::SpawnForMonitoring(spec, info->path.parent_path(),
                                             &process, &error)) {
      return DbusService::MethodResult::Failure(
          error.empty() ? "failed to start adaptor" : error);
    }

    usleep(250000);
    int status = 0;
    if (waitpid(process.pid, &status, WNOHANG) == process.pid) {
      std::string stderr_text = ReadAvailableText(process.stderr_fd);
      close(process.stderr_fd);
      process.stderr_fd = -1;
      stderr_text = vinput::str::TrimAsciiWhitespace(stderr_text);
      if (stderr_text.empty()) {
        stderr_text = "adaptor exited immediately";
      }
      return DbusService::MethodResult::Failure(stderr_text);
    }

    if (!vinput::adaptor::WritePidFile(adaptor_id, process.pid, &error)) {
      kill(process.pid, SIGTERM);
      (void)waitpid(process.pid, nullptr, 0);
      close(process.stderr_fd);
      process.stderr_fd = -1;
      return DbusService::MethodResult::Failure(
          error.empty() ? "failed to persist adaptor pid" : error);
    }

    adaptors_.emplace(adaptor_id, ManagedAdaptor{
                                     .id = adaptor_id,
                                     .pid = process.pid,
                                     .stderr_fd = process.stderr_fd,
                                     .stderr_buffer = {},
                                 });
    fprintf(stderr, "vinput-daemon: adaptor started id=%s pid=%d\n",
            adaptor_id.c_str(), static_cast<int>(process.pid));
    return DbusService::MethodResult::Success();
  }

  DbusService::MethodResult HandleStopRequest(const std::string &adaptor_id) {
    std::string error;
    auto info = vinput::adaptor::FindById(adaptor_id, &error);
    if (!info.has_value()) {
      return DbusService::MethodResult::Failure(
          error.empty() ? "adaptor not found" : error);
    }

    auto it = adaptors_.find(adaptor_id);
    pid_t tracked_pid = -1;
    if (it != adaptors_.end()) {
      tracked_pid = it->second.pid;
      if (it->second.stderr_fd >= 0) {
        close(it->second.stderr_fd);
        it->second.stderr_fd = -1;
      }
      adaptors_.erase(it);
    }

    if (!vinput::adaptor::Stop(*info, &error)) {
      return DbusService::MethodResult::Failure(
          error.empty() ? "failed to stop adaptor" : error);
    }

    if (tracked_pid > 0) {
      (void)waitpid(tracked_pid, nullptr, 0);
    }

    fprintf(stderr, "vinput-daemon: adaptor stopped id=%s\n", adaptor_id.c_str());
    return DbusService::MethodResult::Success();
  }

  void ProcessPendingRequests() {
    std::vector<std::shared_ptr<Request>> requests;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      requests.swap(pending_requests_);
    }

    for (const auto &request : requests) {
      DbusService::MethodResult result;
      if (request->type == Request::Type::Start) {
        result = HandleStartRequest(request->adaptor_id);
      } else {
        result = HandleStopRequest(request->adaptor_id);
      }

      {
        std::lock_guard<std::mutex> lock(mutex_);
        request->result = std::move(result);
        request->done = true;
      }
      request->cv.notify_one();
    }
  }

  void ReapExitedAdaptors() {
    std::vector<std::string> exited_ids;
    for (auto &[id, adaptor] : adaptors_) {
      int status = 0;
      if (waitpid(adaptor.pid, &status, WNOHANG) != adaptor.pid) {
        continue;
      }

      if (adaptor.stderr_fd >= 0) {
        adaptor.stderr_buffer += ReadAvailableText(adaptor.stderr_fd);
      }
      FlushAdaptorBuffer(adaptor, true);
      if (adaptor.stderr_fd >= 0) {
        close(adaptor.stderr_fd);
        adaptor.stderr_fd = -1;
      }

      fprintf(stderr, "vinput-daemon: adaptor exited id=%s code=%d\n",
              adaptor.id.c_str(),
              WIFEXITED(status)
                  ? WEXITSTATUS(status)
                  : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1));
      exited_ids.push_back(id);
    }

    for (const auto &id : exited_ids) {
      vinput::adaptor::RemovePidFile(id);
      adaptors_.erase(id);
    }
  }

  void PollAdaptorsOnce() {
    std::vector<pollfd> fds;
    fds.reserve(1 + adaptors_.size());
    fds.push_back({.fd = wake_fd_, .events = POLLIN, .revents = 0});

    std::vector<std::string> adaptor_ids;
    adaptor_ids.reserve(adaptors_.size());
    for (const auto &[id, adaptor] : adaptors_) {
      if (adaptor.stderr_fd < 0) {
        continue;
      }
      fds.push_back({.fd = adaptor.stderr_fd,
                     .events = static_cast<short>(POLLIN | POLLHUP | POLLERR),
                     .revents = 0});
      adaptor_ids.push_back(id);
    }

    const int ret = poll(fds.data(), static_cast<nfds_t>(fds.size()), 1000);
    if (ret < 0) {
      if (errno != EINTR) {
        fprintf(stderr, "vinput-daemon: adaptor poll error: %s\n",
                strerror(errno));
      }
      return;
    }

    if (ret > 0 && (fds[0].revents & POLLIN)) {
      DrainWakeFd();
      ProcessPendingRequests();
    }

    for (size_t i = 0; i < adaptor_ids.size(); ++i) {
      auto it = adaptors_.find(adaptor_ids[i]);
      if (it == adaptors_.end()) {
        continue;
      }

      ManagedAdaptor &adaptor = it->second;
      const pollfd &fd = fds[i + 1];
      if ((fd.revents & (POLLIN | POLLHUP | POLLERR)) == 0 ||
          adaptor.stderr_fd < 0) {
        continue;
      }

      adaptor.stderr_buffer += ReadAvailableText(adaptor.stderr_fd);
      const bool closing_stderr = (fd.revents & (POLLHUP | POLLERR)) != 0;
      FlushAdaptorBuffer(adaptor, closing_stderr);
      if (closing_stderr) {
        close(adaptor.stderr_fd);
        adaptor.stderr_fd = -1;
      }
    }

    ReapExitedAdaptors();
  }

  void ShutdownAdaptors() {
    for (auto &[id, adaptor] : adaptors_) {
      if (adaptor.stderr_fd >= 0) {
        close(adaptor.stderr_fd);
        adaptor.stderr_fd = -1;
      }
      if (adaptor.pid > 0) {
        kill(adaptor.pid, SIGTERM);
        (void)waitpid(adaptor.pid, nullptr, 0);
      }
      vinput::adaptor::RemovePidFile(id);
    }
    adaptors_.clear();
  }

  void Run() {
    while (true) {
      ProcessPendingRequests();
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_requested_) {
          break;
        }
      }
      PollAdaptorsOnce();
    }

    ProcessPendingRequests();
    ShutdownAdaptors();
  }

  DbusService *dbus_ = nullptr;
  int wake_fd_ = -1;
  std::thread thread_;
  std::mutex mutex_;
  bool running_ = false;
  bool stop_requested_ = false;
  std::vector<std::shared_ptr<Request>> pending_requests_;
  std::map<std::string, ManagedAdaptor> adaptors_;
};

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
  std::string capture_error;
  if (!capture.Start(&capture_error)) {
    if (capture_error.empty()) {
      capture_error = "audio capture start failed";
    }
    fprintf(stderr, "vinput-daemon: %s\n", capture_error.c_str());
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
  AdaptorSupervisor adaptor_supervisor(&dbus);

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
    std::string error;
    if (!capture.BeginRecording(&error)) {
      std::string message = "Failed to start recording.";
      if (!error.empty()) {
        message = "Failed to start recording: " + error;
      }
      fprintf(stderr, "vinput-daemon: %s\n", message.c_str());
      return DbusService::MethodResult::Failure(message);
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
    std::string error;
    if (!capture.BeginRecording(&error)) {
      std::string message = "Failed to start command recording.";
      if (!error.empty()) {
        message = "Failed to start command recording: " + error;
      }
      fprintf(stderr, "vinput-daemon: %s\n", message.c_str());
      return DbusService::MethodResult::Failure(message);
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

  dbus.SetStartAdaptorHandler(
      [&](const std::string &adaptor_id) -> DbusService::MethodResult {
    return adaptor_supervisor.StartAdaptor(adaptor_id);
  });

  dbus.SetStopAdaptorHandler(
      [&](const std::string &adaptor_id) -> DbusService::MethodResult {
    return adaptor_supervisor.StopAdaptor(adaptor_id);
  });

  std::string dbus_error;
  if (!dbus.Start(&dbus_error)) {
    if (dbus_error.empty()) {
      dbus_error = "DBus service start failed";
    }
    fprintf(stderr, "vinput-daemon: %s\n", dbus_error.c_str());
    return 1;
  }

  std::string adaptor_error;
  if (!adaptor_supervisor.Start(&adaptor_error)) {
    fprintf(stderr, "vinput-daemon: %s\n", adaptor_error.c_str());
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
            dbus.EmitError(vinput::dbus::MakeRawError(asr_result.error));
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
              dbus.EmitError(vinput::dbus::ClassifyErrorText(llm_error));
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
              dbus.EmitError(vinput::dbus::ClassifyErrorText(llm_error));
            }
          }
        }

        dbus.EmitRecognitionResult(vinput::result::Serialize(result));

      } catch (const std::exception &e) {
        fprintf(stderr, "vinput-daemon: worker exception: %s\n", e.what());
        dbus.EmitError(vinput::dbus::MakeRawError(e.what()));
      } catch (...) {
        fprintf(stderr, "vinput-daemon: worker unknown exception\n");
        dbus.EmitError(vinput::dbus::MakeErrorInfo(
            vinput::dbus::kErrorCodeProcessingUnknown, {}, {},
            "Unknown error during processing"));
      }

      // No matter what, release the slot
      resetToIdle();
    }
  });

  fprintf(stderr, "vinput-daemon: running\n");

  int dbus_fd = dbus.GetFd();
  int notify_fd = dbus.GetNotifyFd();
  while (g_running) {
    pollfd fds[2] = {
        {.fd = dbus_fd, .events = POLLIN, .revents = 0},
        {.fd = notify_fd, .events = POLLIN, .revents = 0},
    };

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
  adaptor_supervisor.Shutdown();
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
