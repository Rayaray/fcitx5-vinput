#include "common/config_path.h"

#include <sstream>

#include "common/core_config.h"
#include "common/file_utils.h"
#include "common/path_utils.h"

namespace vinput::config {

static const std::string kExtraPrefix = "extra.";

// Parse a dotpath like "extra.active_model" → vector ["active_model"]
// Returns false if prefix is not "extra."
static bool ParseExtraDotpath(const std::string &dotpath,
                              std::vector<std::string> *keys,
                              std::string *error) {
  if (dotpath.substr(0, kExtraPrefix.size()) != kExtraPrefix) {
    if (error)
      *error = "Only 'extra.*' dotpaths are supported. Got: " + dotpath;
    return false;
  }
  std::string rest = dotpath.substr(kExtraPrefix.size());
  if (rest.empty()) {
    if (error)
      *error = "Empty path after 'extra.'";
    return false;
  }
  keys->clear();
  std::istringstream ss(rest);
  std::string token;
  while (std::getline(ss, token, '.')) {
    if (token.empty()) {
      if (error)
        *error = "Empty segment in dotpath: " + dotpath;
      return false;
    }
    keys->push_back(token);
  }
  return true;
}

static bool PathEquals(const std::vector<std::string> &keys,
                       std::initializer_list<std::string_view> expected) {
  if (keys.size() != expected.size()) {
    return false;
  }

  auto it = keys.begin();
  for (std::string_view value : expected) {
    if (*it != value) {
      return false;
    }
    ++it;
  }
  return true;
}

static bool ParseBoolValue(const std::string &value, bool *out,
                           std::string *error) {
  if (value == "true") {
    *out = true;
    return true;
  }
  if (value == "false") {
    *out = false;
    return true;
  }
  if (error) {
    *error = "Expected boolean value 'true' or 'false'.";
  }
  return false;
}

static bool SetTypedConfigValue(CoreConfig *config,
                                const std::vector<std::string> &keys,
                                const std::string &value,
                                std::string *error) {
  if (PathEquals(keys, {"capture_device"})) {
    config->captureDevice = value;
    return true;
  }
  if (PathEquals(keys, {"model_base_dir"})) {
    config->modelBaseDir = value;
    return true;
  }
  if (PathEquals(keys, {"default_language"})) {
    config->defaultLanguage = value;
    return true;
  }
  if (PathEquals(keys, {"hotwords_file"})) {
    config->hotwordsFile = value;
    return true;
  }
  if (PathEquals(keys, {"scenes", "active_scene"})) {
    config->scenes.activeScene = value;
    return true;
  }
  if (PathEquals(keys, {"asr", "normalize_audio"})) {
    return ParseBoolValue(value, &config->asr.normalizeAudio, error);
  }
  if (PathEquals(keys, {"asr", "active_provider"})) {
    config->asr.activeProvider = value;
    return true;
  }
  if (PathEquals(keys, {"asr", "vad", "enabled"})) {
    return ParseBoolValue(value, &config->asr.vad.enabled, error);
  }

  if (error) {
    *error =
        "Unsupported config path for 'config set'. Use a dedicated command "
        "or edit the extra config file directly.";
  }
  return false;
}

static bool GetTypedConfigValue(const CoreConfig &config,
                                const std::vector<std::string> &keys,
                                std::string *value, std::string *error) {
  if (PathEquals(keys, {"capture_device"})) {
    *value = config.captureDevice;
    return true;
  }
  if (PathEquals(keys, {"model_base_dir"})) {
    *value = config.modelBaseDir;
    return true;
  }
  if (PathEquals(keys, {"default_language"})) {
    *value = config.defaultLanguage;
    return true;
  }
  if (PathEquals(keys, {"hotwords_file"})) {
    *value = config.hotwordsFile;
    return true;
  }
  if (PathEquals(keys, {"scenes", "active_scene"})) {
    *value = config.scenes.activeScene;
    return true;
  }
  if (PathEquals(keys, {"asr", "normalize_audio"})) {
    *value = config.asr.normalizeAudio ? "true" : "false";
    return true;
  }
  if (PathEquals(keys, {"asr", "active_provider"})) {
    *value = config.asr.activeProvider;
    return true;
  }
  if (PathEquals(keys, {"asr", "vad", "enabled"})) {
    *value = config.asr.vad.enabled ? "true" : "false";
    return true;
  }

  if (error) {
    *error =
        "Unsupported config path for 'config get'. Use a dedicated command "
        "or edit the extra config file directly.";
  }
  return false;
}

bool GetConfigValue(const std::string &dotpath, std::string *value,
                    std::string *error) {
  std::vector<std::string> keys;
  if (!ParseExtraDotpath(dotpath, &keys, error))
    return false;

  CoreConfig config = LoadCoreConfig();
  NormalizeCoreConfig(&config);
  return GetTypedConfigValue(config, keys, value, error);
}

bool SetConfigValue(const std::string &dotpath, const std::string &value,
                    std::string *error) {
  std::vector<std::string> keys;
  if (!ParseExtraDotpath(dotpath, &keys, error))
    return false;

  CoreConfig config = LoadCoreConfig();
  NormalizeCoreConfig(&config);

  if (!SetTypedConfigValue(&config, keys, value, error)) {
    return false;
  }
  if (!SaveCoreConfig(config)) {
    if (error) {
      *error = "Failed to save config.";
    }
    return false;
  }
  return true;
}

std::filesystem::path GetEditTarget(const std::string &target) {
  std::filesystem::path path;
  if (target == "extra") {
    path = vinput::path::CoreConfigPath();
  } else {
    // "fcitx" → $XDG_CONFIG_HOME/fcitx5/conf/vinput.conf
    //           or ~/.config/fcitx5/conf/vinput.conf
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
      path = std::filesystem::path(xdg) / "fcitx5" / "conf" / "vinput.conf";
    } else {
      const char *home = std::getenv("HOME");
      if (!home || home[0] == '\0')
        return {};
      path = std::filesystem::path(home) / ".config" / "fcitx5" / "conf" /
             "vinput.conf";
    }
  }

  std::filesystem::path resolved;
  if (vinput::file::ResolveSymlinkPath(path, &resolved, nullptr)) {
    return resolved;
  }
  return path;
}

} // namespace vinput::config
