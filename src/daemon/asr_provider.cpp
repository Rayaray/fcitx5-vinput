#include "asr_provider.h"

#include "asr_engine.h"
#include "common/model_manager.h"
#include "common/process_utils.h"
#include "common/string_utils.h"

#include <cstddef>
#include <span>
#include <memory>

namespace vinput::asr {

namespace {

std::string FormatProviderError(std::string detail, std::string_view fallback) {
  detail = vinput::str::TrimAsciiWhitespace(detail);
  if (detail.empty()) {
    detail = std::string(fallback);
  }
  return detail;
}

class LocalProvider : public Provider {
public:
  bool Init(const CoreConfig &config, std::string *error) override {
    const ::AsrProvider *provider = ResolveActiveLocalAsrProvider(config);
    if (!provider) {
      if (error) {
        *error = "Active ASR provider is not a local provider.";
      }
      return false;
    }

    provider_name_ = provider->name;
    model_name_ = provider->model;

    ModelManager model_mgr(ResolveModelBaseDir(config).string(), model_name_);
    std::string model_error;
    if (!model_mgr.EnsureModels(&model_error)) {
      if (error) {
        *error = "Local ASR model check failed for provider '" +
                 provider_name_ + "'";
        if (!model_error.empty()) {
          *error += ": " + model_error;
        } else {
          *error += ".";
        }
      }
      return false;
    }

    ModelInfo model_info = model_mgr.GetModelInfo();
    model_type_ = model_info.model_type;

    AsrConfig asr_config;
    asr_config.language = config.defaultLanguage;
    asr_config.hotwords_file = config.hotwordsFile;
    asr_config.normalize_audio = config.asr.normalizeAudio;
    asr_config.vad_enabled = config.asr.vad.enabled;
    asr_config.vad_model_path = VINPUT_VAD_MODEL_PATH;
    std::string engine_error;
    if (!engine_.Init(model_info, asr_config, &engine_error)) {
      if (error) {
        *error = "Failed to initialize local ASR provider '" +
                 provider_name_ + "'";
        if (!engine_error.empty()) {
          *error += ": " + engine_error;
        } else {
          *error += ".";
        }
      }
      return false;
    }

    return true;
  }

  Result Infer(const std::vector<int16_t> &pcm_data) override {
    Result result;
    if (!engine_.IsInitialized()) {
      result.ok = false;
      result.error = "ASR provider is not initialized.";
      return result;
    }
    result.text = engine_.Infer(pcm_data);
    return result;
  }

  void Shutdown() override { engine_.Shutdown(); }

private:
  AsrEngine engine_;
  std::string provider_name_;
  std::string model_name_;
  std::string model_type_;
};

class CommandProvider : public Provider {
public:
  bool Init(const CoreConfig &config, std::string *error) override {
    const ::AsrProvider *provider = ResolveActiveAsrProvider(config);
    if (!provider || provider->type != kCommandProviderType) {
      if (error) {
        *error = "Active ASR provider is not a command provider.";
      }
      return false;
    }

    if (provider->command.empty()) {
      if (error) {
        *error = "Command ASR provider has empty command.";
      }
      return false;
    }

    provider_name_ = provider->name;
    command_.command = provider->command;
    command_.args = provider->args;
    command_.env = provider->env;
    command_.timeout_ms = provider->timeoutMs;

    initialized_ = true;
    return true;
  }

  Result Infer(const std::vector<int16_t> &pcm_data) override {
    Result result;
    if (!initialized_) {
      result.ok = false;
      result.error = "ASR provider is not initialized.";
      return result;
    }

    const auto *bytes =
        reinterpret_cast<const std::byte *>(pcm_data.data());
    auto command_result = vinput::process::RunCommandWithInput(
        command_, std::span<const std::byte>(bytes, pcm_data.size() * sizeof(int16_t)));

    if (command_result.launch_failed) {
      result.ok = false;
      result.error = FormatProviderError(std::move(command_result.stderr_text),
                                         "failed to start.");
      return result;
    }

    if (command_result.timed_out) {
      result.ok = false;
      result.error =
          FormatProviderError(std::move(command_result.stderr_text), "timed out.");
      return result;
    }

    if (command_result.exit_code != 0) {
      result.ok = false;
      result.error =
          FormatProviderError(std::move(command_result.stderr_text), "failed.");
      return result;
    }

    result.text = vinput::str::TrimAsciiWhitespace(command_result.stdout_text);
    if (result.text.empty()) {
      result.ok = false;
      result.error = FormatProviderError({}, "returned no text.");
    }
    return result;
  }

  void Shutdown() override { initialized_ = false; }

private:
  bool initialized_ = false;
  std::string provider_name_;
  vinput::process::CommandSpec command_;
};

}  // namespace

std::unique_ptr<Provider> CreateProvider(const CoreConfig &config,
                                         std::string *error) {
  const ::AsrProvider *provider = ResolveActiveAsrProvider(config);
  if (!provider) {
    if (error) {
      *error = "Active ASR provider not found.";
    }
    return nullptr;
  }

  if (provider->type == kLocalProviderType) {
    return std::make_unique<LocalProvider>();
  }
  if (provider->type == kCommandProviderType) {
    return std::make_unique<CommandProvider>();
  }

  if (error) {
    *error = "Unsupported ASR provider type: " + provider->type;
  }
  return nullptr;
}

}  // namespace vinput::asr
