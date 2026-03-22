#include "common/path_utils.h"
#include <cstdlib>
#include <sys/stat.h>

namespace vinput::path {

std::filesystem::path ExpandUserPath(std::string_view path) {
  if (path.empty() || path[0] != '~') {
    return std::filesystem::path(path);
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0')
    return {};
  return std::filesystem::path(home) /
         std::filesystem::path(
             path.substr(path.size() > 1 && path[1] == '/' ? 2 : 1));
}

std::filesystem::path DefaultModelBaseDir() {
  const char *xdg = std::getenv("XDG_DATA_HOME");
  if (xdg && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "vinput" / "models";
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0')
    return {};
  return std::filesystem::path(home) / ".local" / "share" /
         "vinput" / "models";
}

std::filesystem::path CoreConfigPath() {
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "vinput" / "config.json";
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0')
    return {};
  return std::filesystem::path(home) / ".config" / "vinput" /
         "config.json";
}

std::filesystem::path UserAsrProviderDir() {
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "vinput" / "asr-providers";
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0')
    return {};
  return std::filesystem::path(home) / ".config" / "vinput" / "asr-providers";
}

std::filesystem::path UserLlmAdaptorDir() {
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "vinput" / "llm-adaptors";
  }
  const char *home = std::getenv("HOME");
  if (!home || home[0] == '\0')
    return {};
  return std::filesystem::path(home) / ".config" / "vinput" / "llm-adaptors";
}

std::filesystem::path AdaptorRuntimeDir() {
  const char *xdg_runtime = std::getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime && xdg_runtime[0] != '\0') {
    return std::filesystem::path(xdg_runtime) / "vinput" / "adaptors";
  }

  const char *tmpdir = std::getenv("TMPDIR");
  std::filesystem::path base =
      (tmpdir && tmpdir[0] != '\0') ? std::filesystem::path(tmpdir)
                                    : std::filesystem::path("/tmp");
  return base / "vinput" / "adaptors";
}

bool isInsideFlatpak() {
  struct stat st;
  return stat("/.flatpak-info", &st) == 0;
}

} // namespace vinput::path
