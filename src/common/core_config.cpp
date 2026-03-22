#include "core_config.h"

#include "config.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <system_error>

#include "common/file_utils.h"
#include "common/path_utils.h"

using json = nlohmann::json;

void from_json(const json &j, CoreConfig &p);

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
  return type == vinput::asr::kLocalProviderType ||
         type == vinput::asr::kCommandProviderType;
}

bool IsLocalAsrProvider(const AsrProvider &provider) {
  return provider.type == vinput::asr::kLocalProviderType;
}

std::filesystem::path BuiltinAsrProviderDir() {
  const std::filesystem::path source_dir = VINPUT_ASR_PROVIDER_SOURCE_DIR;
  std::error_code ec;
  if (std::filesystem::exists(source_dir, ec) && !ec) {
    return source_dir;
  }
  return std::filesystem::path(VINPUT_ASR_PROVIDER_INSTALL_DIR);
}

void NormalizeCommandProviderSpec(AsrProvider *provider) {
  if (!provider || provider->type != vinput::asr::kCommandProviderType) {
    return;
  }
}

std::filesystem::path BundledDefaultCoreConfigPath() {
  const std::filesystem::path source = VINPUT_DEFAULT_CORE_CONFIG_SOURCE_PATH;
  std::error_code ec;
  if (std::filesystem::exists(source, ec) && !ec) {
    return source;
  }
  return std::filesystem::path(VINPUT_DEFAULT_CORE_CONFIG_INSTALL_PATH);
}

bool ReplaceAll(std::string *text, const std::string &from,
                const std::string &to) {
  if (!text || from.empty()) {
    return false;
  }
  bool replaced = false;
  std::size_t pos = 0;
  while ((pos = text->find(from, pos)) != std::string::npos) {
    text->replace(pos, from.size(), to);
    pos += to.size();
    replaced = true;
  }
  return replaced;
}

bool LoadBundledDefaultConfigText(std::string *content, std::string *error) {
  if (!content) {
    if (error) {
      *error = "Content is null.";
    }
    return false;
  }

  const auto path = BundledDefaultCoreConfigPath();
  std::ifstream file(path);
  if (!file.is_open()) {
    if (error) {
      *error = "Failed to open bundled default config: " + path.string();
    }
    return false;
  }

  *content = std::string((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  ReplaceAll(content, "${VINPUT_ASR_PROVIDER_DIR}", BuiltinAsrProviderDir().string());
  if (error) {
    error->clear();
  }
  return true;
}

void MaterializeBuiltinSceneLabels(CoreConfig *config) {
  if (!config) {
    return;
  }
  for (auto &scene : config->scenes.definitions) {
    if (!vinput::scene::IsBuiltinSceneId(scene.id)) {
      continue;
    }
    if (scene.label.empty() || vinput::scene::IsBuiltinSceneLabelKey(scene.label)) {
      scene.label = vinput::scene::DisplayLabel(scene);
    }
  }
}

bool LoadBundledDefaultConfigImpl(CoreConfig *config, std::string *content,
                                  std::string *error) {
  std::string bundled_text;
  if (!LoadBundledDefaultConfigText(&bundled_text, error)) {
    return false;
  }

  try {
    json bundled_json = json::parse(bundled_text);
    if (config) {
      from_json(bundled_json, *config);
    }
    if (content) {
      *content = bundled_json.dump(4) + "\n";
    }
    if (error) {
      error->clear();
    }
    return true;
  } catch (const std::exception &e) {
    if (error) {
      *error = "Failed to parse bundled default config: " +
               std::string(e.what());
    }
    return false;
  }
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

void to_json(json &j, const LlmAdaptor &p) {
  j = json{{"id", p.id},
           {"command", p.command},
           {"args", p.args},
           {"env", p.env}};
}

void from_json(const json &j, LlmAdaptor &p) {
  p.id = j.value("id", p.id);
  p.command = j.value("command", p.command);
  if (j.contains("args") && j.at("args").is_array()) {
    p.args = j.at("args").get<std::vector<std::string>>();
  }
  if (j.contains("env") && j.at("env").is_object()) {
    p.env = j.at("env").get<std::map<std::string, std::string>>();
  }
}

void to_json(json &j, const AsrProvider &p) {
  j = json{{"name", p.name},
           {"type", p.type},
           {"builtin", p.builtin},
           {"model", p.model},
           {"command", p.command},
           {"args", p.args},
           {"env", p.env},
           {"timeout_ms", p.timeoutMs}};
}

void from_json(const json &j, AsrProvider &p) {
  p.name = j.value("name", p.name);
  p.type = j.value("type", p.type);
  p.builtin = j.value("builtin", p.builtin);
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
// RegistrySource serialization
// ---------------------------------------------------------------------------

void to_json(json &j, const RegistrySource &s) {
  j = json{{"name", s.name}, {"url", s.url}};
}

void from_json(const json &j, RegistrySource &s) {
  s.name = j.value("name", s.name);
  s.url = j.value("url", s.url);
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
  j = json{{"providers", p.providers}, {"adaptors", p.adaptors}};
}

void from_json(const json &j, CoreConfig::Llm &p) {
  if (j.contains("providers")) {
    p.providers = j.at("providers").get<std::vector<LlmProvider>>();
  }
  if (j.contains("adaptors")) {
    p.adaptors = j.at("adaptors").get<std::vector<LlmAdaptor>>();
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
// CoreConfig::Registry serialization
// ---------------------------------------------------------------------------

void to_json(json &j, const CoreConfig::Registry &r) {
  j = json{{"sources", r.sources}};
}

void from_json(const json &j, CoreConfig::Registry &r) {
  if (j.contains("sources")) {
    r.sources = j.at("sources").get<std::vector<RegistrySource>>();
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
  j["version"] = p.version;
  j["capture_device"] = p.captureDevice;
  j["model_base_dir"] = p.modelBaseDir;
  j["registry"] = p.registry;
  j["llm"] = p.llm;
  j["default_language"] = p.defaultLanguage;
  j["hotwords_file"] = p.hotwordsFile;
  j["scenes"] = p.scenes;
  j["asr"] = p.asr;
}

void from_json(const json &j, CoreConfig &p) {
  p.version = j.value("version", p.version);
  p.captureDevice = j.value("capture_device", p.captureDevice);
  p.modelBaseDir = j.value("model_base_dir", p.modelBaseDir);
  if (j.contains("registry")) {
    p.registry = j.at("registry").get<CoreConfig::Registry>();
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

CoreConfig LoadCoreConfigFromFile(const std::filesystem::path &path) {
  CoreConfig config;
  std::ifstream f(path);
  if (!f.is_open()) {
    return config;
  }

  try {
    json j;
    f >> j;
    from_json(j, config);
  } catch (const json::exception &e) {
    std::cerr << "Failed to parse vinput config: " << e.what() << std::endl;
  }
  return config;
}

bool LoadBundledDefaultCoreConfig(CoreConfig *config, std::string *error) {
  return LoadBundledDefaultConfigImpl(config, nullptr, error);
}

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

  if (stat_ok && cache.has_cache && cache.mtime == mtime && cache.size == size) {
    return cache.cached;
  }

  std::string bundled_error;
  std::string bundled_content;
  CoreConfig bundled_config;
  const bool has_bundled_default =
      LoadBundledDefaultConfigImpl(&bundled_config, &bundled_content,
                                   &bundled_error);
  if (!has_bundled_default && !bundled_error.empty()) {
    std::cerr << bundled_error << "\n";
  }

  CoreConfig config;
  bool should_write_bundled = false;
  bool cacheable = stat_ok;
  if (!stat_ok) {
    if (!has_bundled_default) {
      return CoreConfig{};
    }
    config = bundled_config;
    should_write_bundled = true;
  } else {
    config = LoadCoreConfigFromFile(path);
    if (has_bundled_default && config.version < bundled_config.version) {
      config = bundled_config;
      should_write_bundled = true;
    }
  }

  if (should_write_bundled) {
    std::string write_error;
    MaterializeBuiltinSceneLabels(&config);
    json bundled_json = config;
    bundled_content = bundled_json.dump(4) + "\n";
    if (!vinput::file::AtomicWriteTextFile(path, bundled_content, &write_error)) {
      std::cerr << "Failed to write bundled vinput config: " << write_error
                << "\n";
      cacheable = false;
    } else {
      cacheable = GetConfigStat(path, &mtime, &size);
    }
  }

  NormalizeCoreConfig(&config);

  if (cacheable) {
    cache.cached = config;
    cache.mtime = mtime;
    cache.size = size;
    cache.has_cache = true;
  } else {
    cache.has_cache = false;
  }
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
    NormalizeCoreConfig(&normalized);
    MaterializeBuiltinSceneLabels(&normalized);

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

  std::set<std::string> seen_registry_names;
  std::set<std::string> seen_registry_urls;
  std::vector<RegistrySource> normalized_sources;
  normalized_sources.reserve(config->registry.sources.size());
  for (auto source : config->registry.sources) {
    if (source.url.empty()) {
      std::cerr << "Ignoring registry source with empty URL\n";
      continue;
    }
    if (source.name.empty()) {
      source.name = "source-" + std::to_string(normalized_sources.size() + 1);
    }
    if (!seen_registry_names.insert(source.name).second) {
      std::cerr << "Ignoring duplicate registry source '" << source.name
                << "'\n";
      continue;
    }
    if (!seen_registry_urls.insert(source.url).second) {
      std::cerr << "Ignoring duplicate registry URL '" << source.url << "'\n";
      continue;
    }
    normalized_sources.push_back(std::move(source));
  }

  config->registry.sources = std::move(normalized_sources);

  std::set<std::string> seen_adaptor_ids;
  std::vector<LlmAdaptor> normalized_adaptors;
  normalized_adaptors.reserve(config->llm.adaptors.size());
  for (auto adaptor : config->llm.adaptors) {
    if (adaptor.id.empty()) {
      std::cerr << "Ignoring LLM adaptor config with empty id\n";
      continue;
    }
    if (!seen_adaptor_ids.insert(adaptor.id).second) {
      std::cerr << "Ignoring duplicate LLM adaptor config '" << adaptor.id
                << "'\n";
      continue;
    }
    for (auto it = adaptor.env.begin(); it != adaptor.env.end();) {
      if (it->first.empty()) {
        it = adaptor.env.erase(it);
      } else {
        ++it;
      }
    }
    normalized_adaptors.push_back(std::move(adaptor));
  }
  config->llm.adaptors = std::move(normalized_adaptors);

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
    NormalizeCommandProviderSpec(&provider);
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
      std::cerr << "Ignoring ASR provider '" << provider.name
                << "' with invalid timeout\n";
      continue;
    }
    normalized_providers.push_back(std::move(provider));
  }
  config->asr.providers = std::move(normalized_providers);

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

const LlmAdaptor *ResolveLlmAdaptor(const CoreConfig &config,
                                    const std::string &adaptor_id) {
  if (adaptor_id.empty()) {
    return nullptr;
  }
  for (const auto &adaptor : config.llm.adaptors) {
    if (adaptor.id == adaptor_id) {
      return &adaptor;
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

const AsrProvider *ResolveActiveLocalAsrProvider(const CoreConfig &config) {
  const AsrProvider *provider = ResolveActiveAsrProvider(config);
  if (!provider || !IsLocalAsrProvider(*provider)) {
    return nullptr;
  }
  return provider;
}

const AsrProvider *ResolvePreferredLocalAsrProvider(const CoreConfig &config) {
  if (const AsrProvider *provider = ResolveActiveLocalAsrProvider(config)) {
    return provider;
  }

  for (const auto &provider : config.asr.providers) {
    if (IsLocalAsrProvider(provider)) {
      return &provider;
    }
  }

  return nullptr;
}

std::string ResolveActiveLocalModel(const CoreConfig &config) {
  const AsrProvider *provider = ResolveActiveLocalAsrProvider(config);
  if (!provider) {
    return {};
  }
  return provider->model;
}

std::string ResolvePreferredLocalModel(const CoreConfig &config) {
  const AsrProvider *provider = ResolvePreferredLocalAsrProvider(config);
  if (!provider) {
    return {};
  }
  return provider->model;
}

std::vector<std::string> ResolveRegistryUrls(const CoreConfig &config) {
  std::vector<std::string> urls;
  urls.reserve(config.registry.sources.size());
  std::set<std::string> seen;
  for (const auto &source : config.registry.sources) {
    if (source.url.empty()) {
      continue;
    }
    if (!seen.insert(source.url).second) {
      continue;
    }
    urls.push_back(source.url);
  }
  return urls;
}

bool SetPreferredLocalModel(CoreConfig *config, const std::string &model,
                            std::string *error) {
  if (!config) {
    if (error) *error = "Config is null.";
    return false;
  }

  const AsrProvider *provider = ResolvePreferredLocalAsrProvider(*config);
  if (!provider) {
    if (error) {
      *error = "No local ASR provider configured.";
    }
    return false;
  }

  const std::string provider_name = provider->name;
  for (auto &candidate : config->asr.providers) {
    if (candidate.name != provider_name) {
      continue;
    }
    candidate.model = model;
    return true;
  }

  if (error) {
    *error = "Local ASR provider not found.";
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
