#include "vinput_config.h"

#include "common/i18n.h"
#include "common/file_utils.h"

#include <fcitx-config/iniparser.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/standardpath.h>

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

namespace {

fcitx::ListConstrain<fcitx::KeyConstrain> TriggerKeyListConstrain() {
  return fcitx::KeyListConstrain(fcitx::KeyConstrainFlags{
      fcitx::KeyConstrainFlag::AllowModifierOnly,
      fcitx::KeyConstrainFlag::AllowModifierLess,
  });
}

fcitx::ListConstrain<fcitx::KeyConstrain> SceneMenuKeyListConstrain() {
  return fcitx::KeyListConstrain(fcitx::KeyConstrainFlags{
      fcitx::KeyConstrainFlag::AllowModifierOnly,
      fcitx::KeyConstrainFlag::AllowModifierLess,
  });
}

std::string UserPkgConfigPath(std::string_view relative_path) {
  return (std::filesystem::path(fcitx::StandardPath::global().userDirectory(
              fcitx::StandardPath::Type::PkgConfig)) /
          std::string(relative_path))
      .string();
}

std::string TriggerKeyLabel() { return _("Trigger Key"); }

std::string TriggerKeyTooltip() {
  const std::string path = UserPkgConfigPath(kVinputConfigPath);
  char buf[1024];
  std::snprintf(
      buf, sizeof(buf),
      _("Press and hold this key to record. Release it to start recognition. "
        "Supports regular keys, modifier keys, and modified key combinations. "
        "You can configure multiple trigger keys. The config is stored at %s."),
      path.c_str());
  return buf;
}

std::string CommandKeysLabel() { return _("Command Keys"); }

std::string CommandKeysTooltip() {
  return _("Press and hold this key to record a voice command to operate on "
           "the selected text. If there is no active selection, vinput falls "
           "back to the current primary-selection clipboard text. Default is "
           "Right Control.");
}

std::string SceneMenuKeyLabel() { return _("Postprocess Menu Keys"); }

std::string SceneMenuKeyTooltip() {
  return _("Configure one or more keys to open the postprocess menu. The "
           "default is Right Alt + Control.");
}

std::string PagePrevKeysLabel() { return _("Previous Page Keys"); }

std::string PagePrevKeysTooltip() {
  return _("Keys for paging to the previous page in the postprocess menu and "
           "the postprocess candidate menu. Defaults to Page Up and keypad "
           "Page Up.");
}

std::string PageNextKeysLabel() { return _("Next Page Keys"); }

std::string PageNextKeysTooltip() {
  return _("Keys for paging to the next page in the postprocess menu and the "
           "postprocess candidate menu. Defaults to Page Down and keypad Page "
           "Down.");
}

} // namespace

VinputConfig::VinputConfig(const VinputSettings &settings)
    : triggerKeys(this, "TriggerKey", TriggerKeyLabel(), settings.triggerKeys,
                 TriggerKeyListConstrain(), {},
                 fcitx::ToolTipAnnotation(TriggerKeyTooltip())),
      commandKeys(this, "CommandKeys", CommandKeysLabel(), settings.commandKeys,
                  TriggerKeyListConstrain(), {},
                  fcitx::ToolTipAnnotation(CommandKeysTooltip())),
      sceneMenuKeys(this, "SceneMenuKey", SceneMenuKeyLabel(),
                   settings.sceneMenuKeys, SceneMenuKeyListConstrain(), {},
                   fcitx::ToolTipAnnotation(SceneMenuKeyTooltip())),
      pagePrevKeys(this, "PagePrevKeys", PagePrevKeysLabel(),
                   settings.pagePrevKeys, TriggerKeyListConstrain(), {},
                   fcitx::ToolTipAnnotation(PagePrevKeysTooltip())),
      pageNextKeys(this, "PageNextKeys", PageNextKeysLabel(),
                   settings.pageNextKeys, TriggerKeyListConstrain(), {},
                   fcitx::ToolTipAnnotation(PageNextKeysTooltip())),
      modelManager(this, "ModelManager", _("Open Vinput Settings"),
                   "vinput-gui") {}

VinputSettings VinputConfig::settings() const {
  VinputSettings settings;
  settings.triggerKeys = triggerKeys.value();
  settings.commandKeys = commandKeys.value();
  settings.sceneMenuKeys = sceneMenuKeys.value();
  settings.pagePrevKeys = pagePrevKeys.value();
  settings.pageNextKeys = pageNextKeys.value();
  return settings;
}

VinputSettings LoadVinputSettings() {
  VinputConfig config(VinputSettings{});
  fcitx::readAsIni(config, fcitx::StandardPath::Type::PkgConfig,
                   kVinputConfigPath);
  return config.settings();
}

bool SaveVinputSettings(const VinputSettings &settings) {
  VinputConfig config(settings);
  fcitx::RawConfig raw;
  config.save(raw);

  std::ostringstream out;
  if (!fcitx::writeAsIni(raw, out)) {
    std::cerr << "Failed to serialize vinput addon config.\n";
    return false;
  }

  std::string error;
  if (!vinput::file::AtomicWriteTextFile(UserPkgConfigPath(kVinputConfigPath),
                                         out.str(), &error)) {
    std::cerr << "Failed to save vinput addon config: " << error << "\n";
    return false;
  }
  return true;
}

std::unique_ptr<VinputConfig>
BuildVinputConfig(const VinputSettings &settings) {
  return std::make_unique<VinputConfig>(settings);
}
