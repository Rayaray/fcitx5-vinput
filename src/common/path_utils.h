#pragma once
#include <filesystem>
#include <string_view>

namespace vinput::path {
std::filesystem::path ExpandUserPath(std::string_view path);
std::filesystem::path DefaultModelBaseDir();
std::filesystem::path CoreConfigPath();
std::filesystem::path UserExtensionDir();
std::filesystem::path ExtensionRuntimeDir();
bool isInsideFlatpak();
std::filesystem::path UserAsrProviderDir();
std::filesystem::path UserLlmAdaptorDir();
std::filesystem::path AdaptorRuntimeDir();
} // namespace vinput::path
