#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct RemoteModelEntry {
  std::string name;
  std::string display_name;
  std::string description;
  std::vector<std::string> urls; // download URLs (tried in order, fallback)
  std::string sha256;
  nlohmann::json vinput_model; // pre-built vinput-model.json content

  // Convenience accessors reading from vinput_model
  std::string model_type() const { return vinput_model.value("model_type", ""); }
  std::string language() const { return vinput_model.value("language", ""); }
  bool supports_hotwords() const { return vinput_model.value("supports_hotwords", false); }
  uint64_t size_bytes() const { return vinput_model.value("size_bytes", uint64_t{0}); }
};

struct InstallProgress {
  uint64_t downloaded_bytes = 0;
  uint64_t total_bytes = 0;
  double speed_bps = 0;
};

using ProgressCallback = std::function<void(const InstallProgress &)>;

class ModelRepository {
public:
  explicit ModelRepository(const std::string &base_dir);

  // Fetch remote model registry
  std::vector<RemoteModelEntry> FetchRegistry(const std::string &registry_url,
                                              std::string *error) const;
  std::vector<RemoteModelEntry> FetchRegistry(
      const std::vector<std::string> &registry_urls, std::string *error,
      std::string *resolved_registry_url = nullptr) const;

  // Download and install a model
  bool InstallModel(const std::string &registry_url,
                    const std::string &model_name, ProgressCallback progress_cb,
                    std::string *error) const;
  bool InstallModel(const std::vector<std::string> &registry_urls,
                    const std::string &model_name, ProgressCallback progress_cb,
                    std::string *error,
                    std::string *resolved_registry_url = nullptr) const;

private:
  bool DownloadFile(const std::string &url,
                    const std::filesystem::path &dest, ProgressCallback cb,
                    std::string *error) const;
  bool VerifySha256(const std::filesystem::path &file,
                    const std::string &expected, std::string *error) const;
  bool ExtractArchive(const std::filesystem::path &archive,
                      const std::filesystem::path &dest,
                      std::string *error) const;
  std::filesystem::path base_dir_;
};
