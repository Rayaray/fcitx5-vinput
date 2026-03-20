#include "core_config.h"

#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <system_error>

#include "common/file_utils.h"
#include "common/path_utils.h"

using json = nlohmann::json;

namespace {

struct ConfigCache {
  std::mutex mu;
  bool has_cache = false;
  std::filesystem::file_time_type mtime;
  std::uintmax_t size = 0;
  CoreConfig cached;
};

ConfigCache &GetConfigCache() {
  static ConfigCache cache;
  return cache;
}

bool GetConfigStat(const std::filesystem::path &path,
                   std::filesystem::file_time_type *mtime,
                   std::uintmax_t *size) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return false;
  }
  auto t = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return false;
  }
  auto s = std::filesystem::file_size(path, ec);
  if (ec) {
    return false;
  }
  if (mtime) *mtime = t;
  if (size) *size = s;
  return true;
}

bool IsSupportedAsrProviderType(std::string_view type) {
  return type == vinput::asr::kBuiltinProviderType ||
         type == vinput::asr::kCommandProviderType;
}

bool IsBuiltinAsrProvider(const AsrProvider &provider) {
  return provider.type == vinput::asr::kBuiltinProviderType;
}

AsrProvider DefaultBuiltinAsrProvider() {
  AsrProvider provider;
  provider.name = std::string(vinput::asr::kDefaultProviderName);
  provider.type = std::string(vinput::asr::kBuiltinProviderType);
  provider.model = std::string(vinput::asr::kDefaultBuiltinModel);
  provider.timeoutMs = vinput::asr::kDefaultProviderTimeoutMs;
  return provider;
}

}  // namespace

std::string GetCoreConfigPath() {
  const auto path = vinput::path::CoreConfigPath();
  std::filesystem::path resolved;
  if (vinput::file::ResolveSymlinkPath(path, &resolved, nullptr)) {
    return resolved.string();
  }
  return path.string();
}

// ---------------------------------------------------------------------------
// LlmProvider serialization
// ---------------------------------------------------------------------------

void to_json(json &j, const LlmProvider &p) {
  j = json{{"name", p.name},
           {"base_url", p.base_url},
           {"api_key", p.api_key}};
}

void from_json(const json &j, LlmProvider &p) {
  p.name = j.value("name", p.name);
  p.base_url = j.value("base_url", p.base_url);
  p.api_key = j.value("api_key", p.api_key);
}

// ---------------------------------------------------------------------------
// AsrProvider serialization
// ---------------------------------------------------------------------------

void to_json(json &j, const AsrProvider &p) {
  j = json{{"name", p.name},
           {"type", p.type},
           {"model", p.model},
           {"command", p.command},
           {"args", p.args},
           {"env", p.env},
           {"timeout_ms", p.timeoutMs}};
}

void from_json(const json &j, AsrProvider &p) {
  p.name = j.value("name", p.name);
  p.type = j.value("type", p.type);
  p.model = j.value("model", p.model);
  p.command = j.value("command", p.command);
  if (j.contains("args") && j.at("args").is_array()) {
    p.args = j.at("args").get<std::vector<std::string>>();
  }
  if (j.contains("env") && j.at("env").is_object()) {
    p.env = j.at("env").get<std::map<std::string, std::string>>();
  }
  p.timeoutMs = j.value("timeout_ms", p.timeoutMs);
}

// ---------------------------------------------------------------------------
// scene::Definition serialization (in vinput::scene namespace for ADL)
// ---------------------------------------------------------------------------

namespace vinput::scene {

void to_json(json &j, const Definition &d) {
  j = json{{"id", d.id},
           {"label", d.label},
           {"prompt", d.prompt},
           {"provider_id", d.provider_id},
           {"model", d.model},
           {"candidate_count", d.candidate_count},
           {"timeout_ms", d.timeout_ms},
           {"builtin", d.builtin}};
}

void from_json(const json &j, Definition &d) {
  d.id = j.value("id", std::string{});
  d.label = j.value("label", std::string{});
  d.prompt = j.value("prompt", std::string{});
  d.provider_id = j.value("provider_id", std::string{});
  d.model = j.value("model", std::string{});
  d.candidate_count =
      j.value("candidate_count", vinput::scene::kDefaultCandidateCount);
  d.timeout_ms = j.value("timeout_ms", vinput::scene::kDefaultTimeoutMs);
  d.builtin = j.value("builtin", false);
}

}  // namespace vinput::scene

// ---------------------------------------------------------------------------
// CoreConfig::Llm serialization
// ---------------------------------------------------------------------------

void to_json(json &j, const CoreConfig::Llm &p) {
  j = json{{"providers", p.providers}};
}

void from_json(const json &j, CoreConfig::Llm &p) {
  if (j.contains("providers")) {
    p.providers = j.at("providers").get<std::vector<LlmProvider>>();
  }
}

// ---------------------------------------------------------------------------
// CoreConfig::Asr serialization
// ---------------------------------------------------------------------------

void to_json(json &j, const CoreConfig::Asr::Vad &v) {
  j = json{{"enabled", v.enabled}};
}

void from_json(const json &j, CoreConfig::Asr::Vad &v) {
  v.enabled = j.value("enabled", v.enabled);
}

void to_json(json &j, const CoreConfig::Asr &a) {
  j = json{{"active_provider", a.activeProvider},
           {"normalize_audio", a.normalizeAudio},
           {"vad", a.vad},
           {"providers", a.providers}};
}

void from_json(const json &j, CoreConfig::Asr &a) {
  a.activeProvider = j.value("active_provider", a.activeProvider);
  a.normalizeAudio = j.value("normalize_audio", a.normalizeAudio);
  if (j.contains("vad")) {
    a.vad = j.at("vad").get<CoreConfig::Asr::Vad>();
  }
  if (j.contains("providers")) {
    a.providers = j.at("providers").get<std::vector<AsrProvider>>();
  }
}

// ---------------------------------------------------------------------------
// CoreConfig::Scenes serialization
// ---------------------------------------------------------------------------

void to_json(json &j, const CoreConfig::Scenes &s) {
  j = json::object();
  j["active_scene"] = s.activeScene;
  j["definitions"] = s.definitions;
}

void from_json(const json &j, CoreConfig::Scenes &s) {
  s.activeScene = j.value("active_scene", s.activeScene);
  if (j.contains("definitions")) {
    s.definitions =
        j.at("definitions").get<std::vector<vinput::scene::Definition>>();
  }
}

// ---------------------------------------------------------------------------
// CoreConfig serialization (top-level fields, no "core" wrapper)
// ---------------------------------------------------------------------------

void to_json(json &j, const CoreConfig &p) {
  j = json::object();
  j["capture_device"] = p.captureDevice;
  j["model_base_dir"] = p.modelBaseDir;
  j["registry_url"] = p.registryUrl;
  j["llm"] = p.llm;
  j["default_language"] = p.defaultLanguage;
  j["hotwords_file"] = p.hotwordsFile;
  j["scenes"] = p.scenes;
  j["asr"] = p.asr;
}

void from_json(const json &j, CoreConfig &p) {
  p.captureDevice = j.value("capture_device", p.captureDevice);
  p.modelBaseDir = j.value("model_base_dir", p.modelBaseDir);
  if (auto v = j.value("registry_url", std::string{}); !v.empty()) {
    p.registryUrl = std::move(v);
  }
  if (j.contains("llm")) {
    p.llm = j.at("llm").get<CoreConfig::Llm>();
  }
  p.defaultLanguage = j.value("default_language", p.defaultLanguage);
  p.hotwordsFile = j.value("hotwords_file", p.hotwordsFile);
  if (j.contains("scenes")) {
    p.scenes = j.at("scenes").get<CoreConfig::Scenes>();
  }
  if (j.contains("asr")) {
    p.asr = j.at("asr").get<CoreConfig::Asr>();
  }
}

namespace {

void EnsureBuiltInRawScene(CoreConfig *config) {
  if (!config) {
    return;
  }
  if (vinput::scene::Find(vinput::scene::Config{
                              .activeSceneId = config->scenes.activeScene,
                              .scenes = config->scenes.definitions},
                          vinput::scene::kRawSceneId)) {
    return;
  }

  vinput::scene::Definition raw;
  raw.id = std::string(vinput::scene::kRawSceneId);
  raw.candidate_count = vinput::scene::kMinCandidateCount;
  raw.builtin = true;
  config->scenes.definitions.insert(config->scenes.definitions.begin(),
                                    std::move(raw));
}

void EnsureBuiltInCommandScene(CoreConfig *config) {
  if (!config || FindCommandScene(*config)) {
    return;
  }

  vinput::scene::Definition cmd;
  cmd.id = std::string(vinput::scene::kCommandSceneId);
  cmd.prompt = vinput::scene::kBuiltinCommandScenePrompt;
  cmd.builtin = true;
  config->scenes.definitions.push_back(std::move(cmd));
}

CoreConfig LoadCoreConfigFromFile(const std::filesystem::path &path) {
  CoreConfig config;
  std::ifstream f(path);
  if (!f.is_open()) {
    return config;
  }

  try {
    json j;
    f >> j;
    config = j.get<CoreConfig>();
  } catch (const json::exception &e) {
    std::cerr << "Failed to parse vinput config: " << e.what() << std::endl;
  }
  return config;
}

}  // namespace

// ---------------------------------------------------------------------------
// LoadCoreConfig
// ---------------------------------------------------------------------------

CoreConfig LoadCoreConfig() {
  std::filesystem::path path = vinput::path::CoreConfigPath();

  auto &cache = GetConfigCache();
  std::lock_guard<std::mutex> lock(cache.mu);

  std::filesystem::file_time_type mtime;
  std::uintmax_t size = 0;
  const bool stat_ok = GetConfigStat(path, &mtime, &size);

  if (!stat_ok) {
    return CoreConfig{};
  }

  if (cache.has_cache && cache.mtime == mtime && cache.size == size) {
    return cache.cached;
  }

  CoreConfig config = LoadCoreConfigFromFile(path);
  EnsureBuiltInRawScene(&config);
  EnsureBuiltInCommandScene(&config);
  NormalizeCoreConfig(&config);

  cache.cached = config;
  cache.mtime = mtime;
  cache.size = size;
  cache.has_cache = true;
  return config;
}

// ---------------------------------------------------------------------------
// SaveCoreConfig
// ---------------------------------------------------------------------------

bool SaveCoreConfig(const CoreConfig &config) {
  std::filesystem::path path = vinput::path::CoreConfigPath();

  std::string err;
  if (!vinput::file::EnsureParentDirectory(path, &err)) {
    std::cerr << "Failed to create config directory: " << err << "\n";
    return false;
  }

  try {
    CoreConfig normalized = config;
    EnsureBuiltInRawScene(&normalized);
    EnsureBuiltInCommandScene(&normalized);
    NormalizeCoreConfig(&normalized);

    json j = normalized;
    std::string content = j.dump(4) + "\n";
    if (!vinput::file::AtomicWriteTextFile(path, content, &err)) {
      std::cerr << "Failed to write config: " << err << "\n";
      return false;
    }

    auto &cache = GetConfigCache();
    std::lock_guard<std::mutex> lock(cache.mu);
    std::filesystem::file_time_type mtime;
    std::uintmax_t size = 0;
    if (GetConfigStat(path, &mtime, &size)) {
      cache.cached = std::move(normalized);
      cache.mtime = mtime;
      cache.size = size;
      cache.has_cache = true;
    } else {
      cache.has_cache = false;
    }
    return true;
  } catch (const json::exception &e) {
    std::cerr << "Failed to serialize vinput config: " << e.what() << "\n";
    return false;
  }
}

// ---------------------------------------------------------------------------
// Helper functions
// ---------------------------------------------------------------------------

void NormalizeCoreConfig(CoreConfig *config) {
  if (!config) return;
  if (!config->modelBaseDir.empty()) {
    config->modelBaseDir =
        vinput::path::ExpandUserPath(config->modelBaseDir).string();
  }

  std::set<std::string> seen_provider_names;
  std::vector<AsrProvider> normalized_providers;
  normalized_providers.reserve(config->asr.providers.size());
  for (auto provider : config->asr.providers) {
    if (provider.name.empty()) {
      std::cerr << "Ignoring ASR provider with empty name\n";
      continue;
    }
    if (!IsSupportedAsrProviderType(provider.type)) {
      std::cerr << "Ignoring ASR provider '" << provider.name
                << "' with unsupported type '" << provider.type << "'\n";
      continue;
    }
    if (!seen_provider_names.insert(provider.name).second) {
      std::cerr << "Ignoring duplicate ASR provider '" << provider.name
                << "'\n";
      continue;
    }
    if (IsBuiltinAsrProvider(provider) && provider.model.empty()) {
      provider.model = std::string(vinput::asr::kDefaultBuiltinModel);
    }
    if (provider.type == vinput::asr::kCommandProviderType &&
        provider.command.empty()) {
      std::cerr << "Ignoring command ASR provider '" << provider.name
                << "' with empty command\n";
      continue;
    }
    for (auto it = provider.env.begin(); it != provider.env.end();) {
      if (it->first.empty()) {
        it = provider.env.erase(it);
      } else {
        ++it;
      }
    }
    if (provider.timeoutMs <= 0) {
      provider.timeoutMs = vinput::asr::kDefaultProviderTimeoutMs;
    }
    normalized_providers.push_back(std::move(provider));
  }
  config->asr.providers = std::move(normalized_providers);

  if (config->asr.providers.empty()) {
    config->asr.providers.push_back(DefaultBuiltinAsrProvider());
  }

  if (config->asr.activeProvider.empty() ||
      ResolveAsrProvider(*config, config->asr.activeProvider) == nullptr) {
    config->asr.activeProvider = config->asr.providers.front().name;
  }

  std::set<std::string> seen_scene_ids;
  std::vector<vinput::scene::Definition> normalized_scenes;
  normalized_scenes.reserve(config->scenes.definitions.size());
  for (auto scene : config->scenes.definitions) {
    vinput::scene::NormalizeDefinition(&scene);

    std::string error;
    if (!vinput::scene::ValidateDefinition(scene, &error)) {
      std::cerr << "Ignoring invalid scene '" << scene.id << "': " << error
                << "\n";
      continue;
    }
    if (!seen_scene_ids.insert(scene.id).second) {
      std::cerr << "Ignoring duplicate scene id '" << scene.id << "'\n";
      continue;
    }

    normalized_scenes.push_back(std::move(scene));
  }
  config->scenes.definitions = std::move(normalized_scenes);

  if (!vinput::scene::Find(vinput::scene::Config{
                               .activeSceneId = config->scenes.activeScene,
                               .scenes = config->scenes.definitions},
                           config->scenes.activeScene)) {
    for (const auto &scene : config->scenes.definitions) {
      if (!scene.builtin) {
        config->scenes.activeScene = scene.id;
        return;
      }
    }
    config->scenes.activeScene =
        config->scenes.definitions.empty() ? "" : config->scenes.definitions.front().id;
  }
}

const LlmProvider *ResolveLlmProvider(const CoreConfig &config,
                                      const std::string &provider_id) {
  if (provider_id.empty()) return nullptr;
  for (const auto &p : config.llm.providers) {
    if (p.name == provider_id) {
      return &p;
    }
  }
  return nullptr;
}

const AsrProvider *ResolveAsrProvider(const CoreConfig &config,
                                      const std::string &provider_id) {
  if (provider_id.empty()) return nullptr;
  for (const auto &p : config.asr.providers) {
    if (p.name == provider_id) {
      return &p;
    }
  }
  return nullptr;
}

const AsrProvider *ResolveActiveAsrProvider(const CoreConfig &config) {
  return ResolveAsrProvider(config, config.asr.activeProvider);
}

const AsrProvider *ResolveActiveBuiltinAsrProvider(const CoreConfig &config) {
  const AsrProvider *provider = ResolveActiveAsrProvider(config);
  if (!provider || !IsBuiltinAsrProvider(*provider)) {
    return nullptr;
  }
  return provider;
}

const AsrProvider *ResolvePreferredBuiltinAsrProvider(const CoreConfig &config) {
  if (const AsrProvider *provider = ResolveActiveBuiltinAsrProvider(config)) {
    return provider;
  }

  const AsrProvider *local_provider =
      ResolveAsrProvider(config, std::string(vinput::asr::kDefaultProviderName));
  if (local_provider && IsBuiltinAsrProvider(*local_provider)) {
    return local_provider;
  }

  for (const auto &provider : config.asr.providers) {
    if (IsBuiltinAsrProvider(provider)) {
      return &provider;
    }
  }

  return nullptr;
}

std::string ResolveActiveBuiltinModel(const CoreConfig &config) {
  const AsrProvider *provider = ResolveActiveBuiltinAsrProvider(config);
  if (!provider) {
    return {};
  }
  return provider->model;
}

std::string ResolvePreferredBuiltinModel(const CoreConfig &config) {
  const AsrProvider *provider = ResolvePreferredBuiltinAsrProvider(config);
  if (!provider) {
    return {};
  }
  return provider->model;
}

bool SetPreferredBuiltinModel(CoreConfig *config, const std::string &model,
                              std::string *error) {
  if (!config) {
    if (error) *error = "Config is null.";
    return false;
  }

  const AsrProvider *provider = ResolvePreferredBuiltinAsrProvider(*config);
  if (!provider) {
    if (error) {
      *error = "No builtin ASR provider configured.";
    }
    return false;
  }

  const std::string provider_name = provider->name;
  for (auto &candidate : config->asr.providers) {
    if (candidate.name != provider_name) {
      continue;
    }
    candidate.model = model.empty()
                          ? std::string(vinput::asr::kDefaultBuiltinModel)
                          : model;
    return true;
  }

  if (error) {
    *error = "Builtin ASR provider not found.";
  }
  return false;
}

const vinput::scene::Definition *FindCommandScene(const CoreConfig &config) {
  for (const auto &scene : config.scenes.definitions) {
    if (scene.id == vinput::scene::kCommandSceneId) {
      return &scene;
    }
  }
  return nullptr;
}

std::filesystem::path ResolveModelBaseDir(const CoreConfig &config) {
  if (!config.modelBaseDir.empty()) {
    return vinput::path::ExpandUserPath(config.modelBaseDir);
  }
  return vinput::path::DefaultModelBaseDir();
}
