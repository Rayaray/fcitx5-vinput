#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "common/postprocess_scene.h"

namespace vinput::asr {

inline constexpr char kLocalProviderType[] = "local";
inline constexpr char kCommandProviderType[] = "command";

}  // namespace vinput::asr

struct LlmProvider {
  std::string name;
  std::string base_url;
  std::string api_key;
};

struct LlmAdaptor {
  std::string id;
  std::string command;
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
};

struct AsrProvider {
  std::string name;
  std::string type;
  bool builtin{false};
  std::string model;
  std::string command;
  std::vector<std::string> args;
  std::map<std::string, std::string> env;
  int timeoutMs = 0;
};

struct RegistrySource {
  std::string name;
  std::string url;
};

struct CoreConfig {
  int version = 0;
  std::string captureDevice;
  std::string modelBaseDir;
  struct Registry {
    std::vector<RegistrySource> sources;
  } registry;

  std::string defaultLanguage;

  std::string hotwordsFile;

  struct Llm {
    std::vector<LlmProvider> providers;
    std::vector<LlmAdaptor> adaptors;
  } llm;

  struct Asr {
    std::string activeProvider;
    bool normalizeAudio{true};
    struct Vad {
      bool enabled{true};
    } vad;
    std::vector<AsrProvider> providers;
  } asr;

  struct Scenes {
    std::string activeScene;
    std::vector<vinput::scene::Definition> definitions;
  } scenes;
};

// API Functions
CoreConfig LoadCoreConfig();
bool LoadBundledDefaultCoreConfig(CoreConfig *config, std::string *error);
bool SaveCoreConfig(const CoreConfig &config);
std::string GetCoreConfigPath();

void NormalizeCoreConfig(CoreConfig *config);
const LlmProvider *ResolveLlmProvider(const CoreConfig &config,
                                      const std::string &provider_id);
const LlmAdaptor *ResolveLlmAdaptor(const CoreConfig &config,
                                    const std::string &adaptor_id);
const AsrProvider *ResolveAsrProvider(const CoreConfig &config,
                                      const std::string &provider_id);
const AsrProvider *ResolveActiveAsrProvider(const CoreConfig &config);
const AsrProvider *ResolveActiveLocalAsrProvider(const CoreConfig &config);
const AsrProvider *ResolvePreferredLocalAsrProvider(const CoreConfig &config);
std::string ResolveActiveLocalModel(const CoreConfig &config);
std::string ResolvePreferredLocalModel(const CoreConfig &config);
std::vector<std::string> ResolveRegistryUrls(const CoreConfig &config);
bool SetPreferredLocalModel(CoreConfig *config, const std::string &model,
                            std::string *error);
const vinput::scene::Definition *FindCommandScene(const CoreConfig &config);
std::filesystem::path ResolveModelBaseDir(const CoreConfig &config);
