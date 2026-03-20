#include "common/file_utils.h"

#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace {

constexpr int kMaxSymlinkDepth = 32;

std::filesystem::path ResolveRelativeSymlinkTarget(
    const std::filesystem::path& symlink_path,
    const std::filesystem::path& link_target) {
  if (link_target.is_absolute()) {
    return link_target;
  }

  const auto parent = symlink_path.parent_path();
  if (parent.empty()) {
    return link_target.lexically_normal();
  }

  std::error_code ec;
  const auto canonical_parent = std::filesystem::canonical(parent, ec);
  if (!ec) {
    return (canonical_parent / link_target).lexically_normal();
  }
  return (parent / link_target).lexically_normal();
}

}  // namespace

namespace vinput::file {

bool ResolveSymlinkPath(const std::filesystem::path& path,
                        std::filesystem::path* resolved,
                        std::string* error) {
  if (!resolved) {
    if (error) {
      *error = "ResolveSymlinkPath requires a destination path.";
    }
    return false;
  }

  std::filesystem::path current = path;
  for (int depth = 0; depth < kMaxSymlinkDepth; ++depth) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(current, ec);
    if (ec) {
      if (error) {
        *error = "Failed to inspect path " + current.string() + ": " +
                 ec.message();
      }
      return false;
    }
    if (status.type() != std::filesystem::file_type::symlink) {
      *resolved = current;
      return true;
    }

    const auto link_target = std::filesystem::read_symlink(current, ec);
    if (ec) {
      if (error) {
        *error = "Failed to read symlink " + current.string() + ": " +
                 ec.message();
      }
      return false;
    }

    current = ResolveRelativeSymlinkTarget(current, link_target);
  }

  if (error) {
    *error = "Too many symlink levels while resolving " + path.string();
  }
  return false;
}

bool EnsureParentDirectory(const std::filesystem::path& path, std::string* error) {
  auto parent = path.parent_path();
  if (parent.empty() || std::filesystem::exists(parent)) {
    return true;
  }
  std::error_code ec;
  std::filesystem::create_directories(parent, ec);
  if (ec) {
    if (error) *error = "Failed to create directory " + parent.string() + ": " + ec.message();
    return false;
  }
  return true;
}

bool AtomicWriteTextFile(const std::filesystem::path& target, std::string_view content, std::string* error) {
  std::filesystem::path resolved_target;
  if (!ResolveSymlinkPath(target, &resolved_target, error)) {
    return false;
  }

  if (!EnsureParentDirectory(resolved_target, error)) {
    return false;
  }

  auto tmp_path = resolved_target.parent_path() /
                  (resolved_target.filename().string() + ".tmp.XXXXXX");
  std::string tmp_str = tmp_path.string();

  int fd = mkstemp(tmp_str.data());
  if (fd < 0) {
    if (error) *error = std::string("mkstemp failed: ") + std::strerror(errno);
    return false;
  }

  // Write content
  const char* data = content.data();
  size_t remaining = content.size();
  while (remaining > 0) {
    ssize_t written = write(fd, data, remaining);
    if (written < 0) {
      if (errno == EINTR) continue;
      if (error) *error = std::string("write failed: ") + std::strerror(errno);
      close(fd);
      unlink(tmp_str.c_str());
      return false;
    }
    data += written;
    remaining -= static_cast<size_t>(written);
  }

  if (fsync(fd) != 0) {
    if (error) *error = std::string("fsync failed: ") + std::strerror(errno);
    close(fd);
    unlink(tmp_str.c_str());
    return false;
  }

  close(fd);

  std::error_code ec;
  std::filesystem::rename(tmp_str, resolved_target, ec);
  if (ec) {
    if (error) *error = "rename failed: " + ec.message();
    unlink(tmp_str.c_str());
    return false;
  }

  return true;
}

}  // namespace vinput::file
