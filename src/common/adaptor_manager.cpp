#include "common/adaptor_manager.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <fstream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "common/core_config.h"
#include "common/path_utils.h"
#include "common/process_utils.h"

namespace vinput::adaptor {

namespace fs = std::filesystem;

namespace {

struct SearchRoot {
  fs::path path;
  Source source = Source::kBuiltin;
};

fs::path BuiltinAdaptorDir() {
  const fs::path source = VINPUT_LLM_ADAPTOR_SOURCE_DIR;
  std::error_code ec;
  if (fs::exists(source, ec) && !ec) {
    return source;
  }
  return fs::path(VINPUT_LLM_ADAPTOR_INSTALL_DIR);
}

std::vector<SearchRoot> BuildSearchRoots() {
  return {
      {vinput::path::UserLlmAdaptorDir(), Source::kUser},
      {BuiltinAdaptorDir(), Source::kBuiltin},
  };
}

bool IsExecutableFile(const fs::path &path) {
  std::error_code ec;
  const auto status = fs::status(path, ec);
  if (ec || status.type() != fs::file_type::regular) {
    return false;
  }
  const auto perms = status.permissions();
  using perms_t = fs::perms;
  return (perms & perms_t::owner_exec) != perms_t::none ||
         (perms & perms_t::group_exec) != perms_t::none ||
         (perms & perms_t::others_exec) != perms_t::none;
}

void ParseMetadataBlock(const fs::path &path, Info *info) {
  if (!info) {
    return;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    return;
  }

  bool in_block = false;
  std::string line;
  const std::regex field_re(R"(^\s*#\s*@(\w+)\s+(.+?)\s*$)");

  while (std::getline(file, line)) {
    if (!in_block) {
      if (line == "# ==vinput-adaptor==") {
        in_block = true;
      }
      continue;
    }

    if (line == "# ==/vinput-adaptor==") {
      break;
    }

    std::smatch match;
    if (!std::regex_match(line, match, field_re)) {
      continue;
    }

    const std::string key = match[1].str();
    const std::string value = match[2].str();
    if (key == "name") {
      info->name = value;
    } else if (key == "description") {
      info->description = value;
    } else if (key == "author") {
      info->author = value;
    } else if (key == "version") {
      info->version = value;
    } else if (key == "env") {
      info->env_entries.push_back(value);
    }
  }
}

pid_t ReadPid(const Info &info) {
  std::ifstream file(vinput::path::AdaptorRuntimeDir() / (info.id + ".pid"));
  pid_t pid = -1;
  file >> pid;
  return pid;
}

bool ProcessExists(pid_t pid) {
  if (pid <= 0) {
    return false;
  }
  return kill(pid, 0) == 0 || errno == EPERM;
}

vinput::process::CommandSpec DefaultCommandSpecForPath(const fs::path &path) {
  vinput::process::CommandSpec spec;
  if (path.extension() == ".py") {
    spec.command = "python3";
    spec.args = {path.string()};
    return spec;
  }
  spec.command = path.string();
  return spec;
}

}  // namespace

vinput::process::CommandSpec BuildCommandSpec(const Info &info,
                                              const CoreConfig &config) {
  vinput::process::CommandSpec spec = DefaultCommandSpecForPath(info.path);

  const auto *configured = ResolveLlmAdaptor(config, info.id);
  if (!configured) {
    return spec;
  }

  if (!configured->command.empty()) {
    spec.command = configured->command;
  }
  spec.args = configured->args;
  spec.env = configured->env;
  return spec;
}

std::filesystem::path PidPath(std::string_view adaptor_id) {
  return vinput::path::AdaptorRuntimeDir() /
         (std::string(adaptor_id) + ".pid");
}

bool WritePidFile(std::string_view adaptor_id, pid_t pid, std::string *error) {
  std::error_code ec;
  const fs::path runtime_dir = vinput::path::AdaptorRuntimeDir();
  fs::create_directories(runtime_dir, ec);
  if (ec) {
    if (error) {
      *error = "failed to create runtime directory: " + ec.message();
    }
    return false;
  }

  std::ofstream pid_file(PidPath(adaptor_id), std::ios::out | std::ios::trunc);
  if (!pid_file.is_open()) {
    if (error) {
      *error = "failed to write pid file: " + std::string(adaptor_id);
    }
    return false;
  }
  pid_file << pid;
  if (!pid_file.good()) {
    if (error) {
      *error = "failed to persist pid file: " + std::string(adaptor_id);
    }
    return false;
  }
  if (error) {
    error->clear();
  }
  return true;
}

void RemovePidFile(std::string_view adaptor_id) {
  std::error_code ec;
  fs::remove(PidPath(adaptor_id), ec);
}

std::string SourceToString(Source source) {
  switch (source) {
  case Source::kBuiltin:
    return "builtin";
  case Source::kUser:
    return "user";
  default:
    return "unknown";
  }
}

std::vector<Info> Discover(std::string *error) {
  std::vector<Info> results;
  std::set<std::string> seen_ids;

  for (const auto &root : BuildSearchRoots()) {
    std::error_code ec;
    if (!fs::exists(root.path, ec) || ec || !fs::is_directory(root.path, ec)) {
      continue;
    }

    for (const auto &entry : fs::directory_iterator(root.path, ec)) {
      if (ec) {
        if (error) {
          *error = "failed to scan adaptor directory: " + root.path.string();
        }
        return {};
      }
      if (!entry.is_regular_file()) {
        continue;
      }

      Info info;
      info.id = entry.path().stem().string();
      info.name = info.id;
      info.source = root.source;
      info.path = entry.path();
      {
        const auto spec = DefaultCommandSpecForPath(info.path);
        info.default_command = spec.command;
        info.default_args = spec.args;
      }
      info.executable = IsExecutableFile(info.path);
      if (!info.executable) {
        continue;
      }
      ParseMetadataBlock(info.path, &info);

      if (!seen_ids.insert(info.id).second) {
        continue;
      }
      results.push_back(std::move(info));
    }
  }

  if (error) {
    error->clear();
  }
  std::sort(results.begin(), results.end(),
            [](const Info &a, const Info &b) { return a.id < b.id; });
  return results;
}

std::optional<Info> FindById(std::string_view id, std::string *error) {
  const auto adaptors = Discover(error);
  if (error && !error->empty()) {
    return std::nullopt;
  }

  for (const auto &info : adaptors) {
    if (info.id == id) {
      return info;
    }
  }

  if (error) {
    *error = "adaptor not found: " + std::string(id);
  }
  return std::nullopt;
}

bool IsRunning(const Info &info) {
  const pid_t pid = ReadPid(info);
  return ProcessExists(pid);
}

bool Start(const Info &info, const CoreConfig &config, std::string *error) {
  if (!info.executable) {
    if (error) {
      *error = "adaptor is not executable: " + info.path.string();
    }
    return false;
  }
  if (IsRunning(info)) {
    if (error) {
      *error = "adaptor is already running: " + info.id;
    }
    return false;
  }

  std::error_code ec;
  const fs::path runtime_dir = vinput::path::AdaptorRuntimeDir();
  fs::create_directories(runtime_dir, ec);
  if (ec) {
    if (error) {
      *error = "failed to create runtime directory: " + ec.message();
    }
    return false;
  }

  const auto spec = BuildCommandSpec(info, config);
  pid_t pid = -1;
  std::string spawn_error;
  if (!vinput::process::SpawnDetached(spec, info.path.parent_path(), &pid,
                                      &spawn_error)) {
    if (error) {
      *error = spawn_error;
    }
    return false;
  }

  usleep(250000);
  int status = 0;
  const pid_t wait_result = waitpid(pid, &status, WNOHANG);
  if (wait_result == pid) {
    if (error) {
      *error = "adaptor exited immediately";
    }
    return false;
  }

  return WritePidFile(info.id, pid, error);
}

bool Stop(const Info &info, std::string *error) {
  const pid_t pid = ReadPid(info);
  if (!ProcessExists(pid)) {
    RemovePidFile(info.id);
    if (error) {
      *error = "adaptor is not running: " + info.id;
    }
    return false;
  }

  kill(pid, SIGTERM);
  for (int i = 0; i < 20; ++i) {
    if (!ProcessExists(pid)) {
      RemovePidFile(info.id);
      if (error) {
        error->clear();
      }
      return true;
    }
    usleep(100000);
  }

  kill(pid, SIGKILL);
  RemovePidFile(info.id);
  if (error) {
    error->clear();
  }
  return true;
}

}  // namespace vinput::adaptor
