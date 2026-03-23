#include "cli/command_adaptor.h"

#include <nlohmann/json.hpp>

#include "cli/dbus_client.h"
#include "common/adaptor_manager.h"
#include "common/i18n.h"
#include "common/string_utils.h"

namespace {

std::string RunningState(const vinput::adaptor::Info &info) {
  return vinput::adaptor::IsRunning(info) ? "running" : "stopped";
}

}  // namespace

int RunAdaptorList(Formatter &fmt, const CliContext &ctx) {
  std::string error;
  const auto adaptors = vinput::adaptor::Discover(&error);
  if (!error.empty()) {
    fmt.PrintError(error);
    return 1;
  }

  if (ctx.json_output) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &info : adaptors) {
      arr.push_back({
          {"id", info.id},
          {"name", info.name},
          {"source", vinput::adaptor::SourceToString(info.source)},
          {"description", info.description},
          {"author", info.author},
          {"version", info.version},
          {"path", info.path.string()},
          {"executable", info.executable},
          {"state", RunningState(info)},
      });
    }
    fmt.PrintJson(arr);
    return 0;
  }

  std::vector<std::string> headers = {_("ID"), _("SOURCE"), _("STATE"),
                                      _("DESCRIPTION")};
  std::vector<std::vector<std::string>> rows;
  for (const auto &info : adaptors) {
    rows.push_back({info.id,
                    vinput::adaptor::SourceToString(info.source),
                    RunningState(info),
                    info.description.empty() ? info.name : info.description});
  }
  fmt.PrintTable(headers, rows);
  return 0;
}

int RunAdaptorStart(const std::string &name, Formatter &fmt,
                    const CliContext &ctx) {
  (void)ctx;
  std::string error;
  vinput::cli::DbusClient dbus;
  if (!dbus.StartAdaptor(name, &error)) {
    fmt.PrintError(error);
    return 1;
  }
  fmt.PrintSuccess(vinput::str::FmtStr(_("Adaptor '%s' started."), name));
  return 0;
}

int RunAdaptorStop(const std::string &name, Formatter &fmt,
                   const CliContext &ctx) {
  (void)ctx;
  std::string error;
  vinput::cli::DbusClient dbus;
  if (!dbus.StopAdaptor(name, &error)) {
    fmt.PrintError(error);
    return 1;
  }
  fmt.PrintSuccess(vinput::str::FmtStr(_("Adaptor '%s' stopped."), name));
  return 0;
}
