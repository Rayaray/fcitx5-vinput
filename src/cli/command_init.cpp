#include "cli/command_init.h"

#include <filesystem>

#include "common/core_config.h"
#include "common/file_utils.h"
#include "common/i18n.h"
#include "common/path_utils.h"
#include "common/string_utils.h"

namespace fs = std::filesystem;

int RunInit(bool force, Formatter& fmt, const CliContext& ctx) {
    (void)ctx;
    bool any_created = false;

    // 1. Create config.json with defaults
    auto config_path = vinput::path::CoreConfigPath();
    if (fs::exists(config_path) && !force) {
        fmt.PrintInfo(
            vinput::str::FmtStr(_("Config already exists: %s"), config_path.string()));
    } else {
        std::string load_error;
        CoreConfig config;
        if (!LoadBundledDefaultCoreConfig(&config, &load_error)) {
            fmt.PrintError(load_error);
            return 1;
        }
        if (SaveCoreConfig(config)) {
            fmt.PrintSuccess(
                vinput::str::FmtStr(_("Created config: %s"), config_path.string()));
            any_created = true;
        } else {
            fmt.PrintError(_("Failed to create config"));
            return 1;
        }
    }

    // 2. Create model base directory
    auto model_dir = vinput::path::DefaultModelBaseDir();
    if (fs::exists(model_dir)) {
        fmt.PrintInfo(
            vinput::str::FmtStr(_("Model dir already exists: %s"), model_dir.string()));
    } else {
        std::error_code ec;
        fs::create_directories(model_dir, ec);
        if (ec) {
            fmt.PrintError(
                vinput::str::FmtStr(_("Failed to create model dir: %s"), ec.message()));
            return 1;
        }
        fmt.PrintSuccess(
            vinput::str::FmtStr(_("Created model dir: %s"), model_dir.string()));
        any_created = true;
    }

    if (!any_created) {
        fmt.PrintInfo(_("Everything already initialized"));
    }

    return 0;
}
