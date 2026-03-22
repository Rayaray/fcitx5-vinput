#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct CoreConfig;

namespace vinput::adaptor {

enum class Source {
  kBuiltin,
  kUser,
};

struct Info {
  std::string id;
  std::string name;
  std::string description;
  std::string author;
  std::string version;
  std::vector<std::string> env_entries;
  Source source = Source::kBuiltin;
  std::filesystem::path path;
  std::string default_command;
  std::vector<std::string> default_args;
  bool executable = false;
};

std::string SourceToString(Source source);

std::vector<Info> Discover(std::string *error);
std::optional<Info> FindById(std::string_view id, std::string *error);

bool IsRunning(const Info &info);
bool Start(const Info &info, const CoreConfig &config, std::string *error);
bool Stop(const Info &info, std::string *error);

}  // namespace vinput::adaptor
