#include <CLI/CLI.hpp>
#include <iostream>
#include <string>
#include <unistd.h>

#include "cli/cli_context.h"
#include "cli/command_config.h"
#include "cli/command_daemon.h"
#include "cli/command_device.h"
#include "cli/command_hotword.h"
#include "cli/command_init.h"
#include "cli/command_adaptor.h"
#include "cli/command_asr.h"
#include "cli/command_llm.h"
#include "cli/command_model.h"
#include "cli/command_recording.h"
#include "cli/command_scene.h"
#include "cli/command_status.h"
#include "cli/formatter.h"
#include "common/core_config.h"
#include "common/i18n.h"
#include "common/postprocess_scene.h"

namespace {

AsrProvider LoadDefaultAsrProviderTemplate() {
  CoreConfig config;
  std::string error;
  if (!LoadBundledDefaultCoreConfig(&config, &error)) {
    return {};
  }

  if (!config.asr.providers.empty()) {
    return config.asr.providers.front();
  }

  return {};
}

}  // namespace

int main(int argc, char *argv[]) {
  vinput::i18n::Init();

  CLI::App app{_("vinput - Voice input model and daemon manager")};
  app.require_subcommand(0, 1);
  app.set_help_flag("-h,--help", _("Print this help message and exit"));

  bool json_output = false;
  app.add_flag("-j,--json", json_output, _("Output in JSON format"));

  // ---- model subcommand ----
  auto *model_cmd = app.add_subcommand("model", _("Manage ASR models"));
  model_cmd->alias("m");
  model_cmd->require_subcommand(1);

  bool model_list_remote = false;
  auto *model_list = model_cmd->add_subcommand(
      "list", _("List installed (and optionally remote) models"));
  model_list->alias("ls");
  model_list->add_flag("-r,--remote", model_list_remote,
                       _("Include remote models from registry"));

  std::string model_add_name;
  auto *model_add =
      model_cmd->add_subcommand("add", _("Download and install a model"));
  model_add->add_option("name", model_add_name, _("Model name"))->required();

  std::string model_use_name;
  auto *model_use = model_cmd->add_subcommand("use", _("Set active model"));
  model_use->add_option("name", model_use_name, _("Model name"))->required();

  std::string model_remove_name;
  bool model_remove_force = false;
  auto *model_remove =
      model_cmd->add_subcommand("remove", _("Remove an installed model"));
  model_remove->alias("rm");
  model_remove->add_option("name", model_remove_name, _("Model name"))->required();
  model_remove->add_flag("-f,--force", model_remove_force, _("Skip confirmation"));

  std::string model_info_name;
  auto *model_info = model_cmd->add_subcommand("info", _("Show model details"));
  model_info->add_option("name", model_info_name, _("Model name"))->required();

  // ---- scene subcommand ----
  auto *scene_cmd = app.add_subcommand("scene", _("Manage recognition scenes"));
  scene_cmd->alias("s");
  scene_cmd->require_subcommand(1);

  auto *scene_list = scene_cmd->add_subcommand("list", _("List all scenes"));
  scene_list->alias("ls");

  auto *scene_add = scene_cmd->add_subcommand("add", _("Add a new scene"));
  std::string scene_add_id;
  std::string scene_add_label;
  std::string scene_add_prompt;
  std::string scene_add_provider;
  std::string scene_add_model;
  int scene_add_candidates = vinput::scene::kDefaultCandidateCount;
  int scene_add_timeout_ms = vinput::scene::kDefaultTimeoutMs;
  scene_add->add_option("--id", scene_add_id, _("Scene ID"))->required();
  scene_add->add_option("-l,--label", scene_add_label, _("Display label"));
  scene_add->add_option("-t,--prompt", scene_add_prompt, _("LLM prompt"));
  scene_add->add_option("-p,--provider", scene_add_provider, _("LLM provider name"));
  scene_add->add_option("-m,--model", scene_add_model, _("LLM model name"));
  scene_add->add_option("-c,--candidates", scene_add_candidates, _("Candidate count"))
      ->default_val(vinput::scene::kDefaultCandidateCount);
  scene_add
      ->add_option("--timeout", scene_add_timeout_ms,
                   _("Request timeout in milliseconds"))
      ->default_val(vinput::scene::kDefaultTimeoutMs);

  std::string scene_use_id;
  auto *scene_use = scene_cmd->add_subcommand("use", _("Set active scene"));
  scene_use->add_option("id", scene_use_id, _("Scene ID"))->required();

  std::string scene_remove_id;
  bool scene_remove_force = false;
  auto *scene_remove = scene_cmd->add_subcommand("remove", _("Remove a scene"));
  scene_remove->alias("rm");
  scene_remove->add_option("id", scene_remove_id, _("Scene ID"))->required();
  scene_remove->add_flag("-f,--force", scene_remove_force,
                         _("Force removal of built-in scenes"));

  // ---- llm subcommand ----
  auto *llm_cmd = app.add_subcommand("llm", _("Manage LLM providers"));
  llm_cmd->alias("l");
  llm_cmd->require_subcommand(1);

  auto *llm_list =
      llm_cmd->add_subcommand("list", _("List configured LLM providers"));
  llm_list->alias("ls");

  std::string llm_add_name;
  std::string llm_add_base_url;
  std::string llm_add_api_key;
  auto *llm_add = llm_cmd->add_subcommand("add", _("Add an LLM provider"));
  llm_add->add_option("name", llm_add_name, _("Provider name"))->required();
  llm_add->add_option("-u,--base-url", llm_add_base_url, _("Base URL"))->required();
  llm_add->add_option("-k,--api-key", llm_add_api_key, _("API key"));

  std::string llm_remove_name;
  bool llm_remove_force = false;
  auto *llm_remove =
      llm_cmd->add_subcommand("remove", _("Remove an LLM provider"));
  llm_remove->alias("rm");
  llm_remove->add_option("name", llm_remove_name, _("Provider name"))->required();
  llm_remove->add_flag("-f,--force", llm_remove_force, _("Skip confirmation"));

  // ---- asr subcommand ----
  auto *asr_cmd = app.add_subcommand("asr", _("Manage ASR providers"));
  asr_cmd->require_subcommand(1);

  auto *asr_list =
      asr_cmd->add_subcommand("list", _("List configured ASR providers"));
  asr_list->alias("ls");

  const AsrProvider asr_add_default = LoadDefaultAsrProviderTemplate();
  std::string asr_add_name;
  std::string asr_add_type = asr_add_default.type.empty()
                                 ? std::string(vinput::asr::kLocalProviderType)
                                 : asr_add_default.type;
  std::string asr_add_model;
  std::string asr_add_command;
  std::vector<std::string> asr_add_args;
  std::vector<std::string> asr_add_env;
  int asr_add_timeout_ms = asr_add_default.timeoutMs;
  auto *asr_add = asr_cmd->add_subcommand("add", _("Add an ASR provider"));
  asr_add->add_option("name", asr_add_name, _("Provider name"))->required();
  asr_add->add_option("--type", asr_add_type, _("Provider type: local or command"))
      ->default_val(asr_add_type);
  asr_add->add_option("-m,--model", asr_add_model, _("Local model name"));
  asr_add->add_option("-c,--command", asr_add_command,
                      _("Command or executable path for command providers"));
  asr_add->add_option("--arg", asr_add_args, _("Command argument"))
      ->expected(0, -1);
  asr_add->add_option("--env", asr_add_env, _("Environment entry as KEY=VALUE"))
      ->expected(0, -1);
  asr_add
      ->add_option("--timeout", asr_add_timeout_ms,
                   _("Provider timeout in milliseconds"))
      ->default_val(asr_add_timeout_ms);

  std::string asr_remove_name;
  bool asr_remove_force = false;
  auto *asr_remove =
      asr_cmd->add_subcommand("remove", _("Remove an ASR provider"));
  asr_remove->alias("rm");
  asr_remove->add_option("name", asr_remove_name, _("Provider name"))->required();
  asr_remove->add_flag("-f,--force", asr_remove_force, _("Skip active-provider protection"));

  std::string asr_use_name;
  auto *asr_use = asr_cmd->add_subcommand("use", _("Set active ASR provider"));
  asr_use->add_option("name", asr_use_name, _("Provider name"))->required();

  std::string asr_edit_name;
  auto *asr_edit =
      asr_cmd->add_subcommand("edit", _("Open an external ASR provider script"));
  asr_edit->add_option("name", asr_edit_name, _("Provider name"))->required();

  // ---- adaptor subcommand ----
  auto *adaptor_cmd =
      app.add_subcommand("adaptor", _("Manage built-in and user LLM adaptors"));
  adaptor_cmd->require_subcommand(1);

  auto *adaptor_list =
      adaptor_cmd->add_subcommand("list", _("List available LLM adaptors"));
  adaptor_list->alias("ls");

  std::string adaptor_start_name;
  auto *adaptor_start =
      adaptor_cmd->add_subcommand("start", _("Start an LLM adaptor"));
  adaptor_start->add_option("name", adaptor_start_name,
                            _("Adaptor ID"))->required();

  std::string adaptor_stop_name;
  auto *adaptor_stop =
      adaptor_cmd->add_subcommand("stop", _("Stop an LLM adaptor"));
  adaptor_stop->add_option("name", adaptor_stop_name,
                           _("Adaptor ID"))->required();

  // ---- config subcommand ----
  auto *config_cmd =
      app.add_subcommand("config", _("Read or write configuration values"));
  config_cmd->alias("cfg");
  config_cmd->require_subcommand(1);

  std::string config_get_path;
  auto *config_get =
      config_cmd->add_subcommand("get", _("Get a config value by dotpath"));
  config_get
      ->add_option("path", config_get_path,
                   _("Config dotpath (e.g. fcitx.triggerKey)"))
      ->required();

  std::string config_set_path;
  std::string config_set_value;
  bool config_set_stdin = false;
  auto *config_set =
      config_cmd->add_subcommand("set", _("Set a config value by dotpath"));
  config_set->add_option("path", config_set_path, _("Config dotpath"))->required();
  config_set->add_option("value", config_set_value, _("New value"));
  config_set->add_flag("-i,--stdin", config_set_stdin, _("Read value from stdin"));

  std::string config_edit_target;
  auto *config_edit =
      config_cmd->add_subcommand("edit", _("Open config file in editor"));
  config_edit
      ->add_option("target", config_edit_target,
                   _("Config target: fcitx or extra"))
      ->required();

  // ---- daemon subcommand ----
  auto *daemon_cmd = app.add_subcommand("daemon", _("Control the vinput daemon"));
  daemon_cmd->alias("d");
  daemon_cmd->require_subcommand(1);

  auto *daemon_start = daemon_cmd->add_subcommand("start", _("Start the daemon"));
  auto *daemon_stop = daemon_cmd->add_subcommand("stop", _("Stop the daemon"));
  auto *daemon_restart =
      daemon_cmd->add_subcommand("restart", _("Restart the daemon"));

  bool daemon_logs_follow = false;
  int daemon_logs_lines = 20;
  auto *daemon_logs = daemon_cmd->add_subcommand("logs", _("Show daemon logs"));
  daemon_logs->add_flag("-f,--follow", daemon_logs_follow, _("Follow log output"));
  daemon_logs->add_option("-n", daemon_logs_lines, _("Number of lines to show"))
      ->default_val(20);

  // ---- init subcommand ----
  auto *init_cmd =
      app.add_subcommand("init", _("Initialize default config and directories"));
  bool init_force = false;
  init_cmd->add_flag("-f,--force", init_force, _("Overwrite existing config"));

  // ---- hotword subcommand ----
  auto *hotword_cmd = app.add_subcommand("hotword", _("Manage hotwords file"));
  hotword_cmd->alias("hw");
  hotword_cmd->require_subcommand(1);

  auto *hotword_get =
      hotword_cmd->add_subcommand("get", _("Show configured hotwords file path"));

  std::string hotword_set_path;
  auto *hotword_set = hotword_cmd->add_subcommand(
      "set", _("Set hotwords file path"));
  hotword_set->add_option("path", hotword_set_path, _("Path to hotwords file"))
      ->required();

  auto *hotword_clear =
      hotword_cmd->add_subcommand("clear", _("Clear hotwords file path"));
  auto *hotword_edit =
      hotword_cmd->add_subcommand("edit", _("Edit hotwords file in editor"));

  // ---- device subcommand ----
  auto *device_cmd = app.add_subcommand("device", _("Manage capture devices"));
  device_cmd->alias("dev");
  device_cmd->require_subcommand(1);

  auto *device_list =
      device_cmd->add_subcommand("list", _("List available audio input devices"));
  device_list->alias("ls");

  std::string device_use_name;
  auto *device_use =
      device_cmd->add_subcommand("use", _("Set active capture device"));
  device_use->add_option("name", device_use_name, _("Device name or 'default'"))
      ->required();

  // ---- recording subcommand ----
  auto *recording_cmd =
      app.add_subcommand("recording", _("Control voice recording"));
  recording_cmd->alias("rec");
  recording_cmd->require_subcommand(1);

  auto *recording_start =
      recording_cmd->add_subcommand("start", _("Start recording"));
  auto *recording_stop =
      recording_cmd->add_subcommand("stop", _("Stop recording and recognize"));
  std::string recording_stop_scene;
  recording_stop->add_option("-s,--scene", recording_stop_scene,
                             _("Scene ID (default: active scene)"));
  auto *recording_toggle =
      recording_cmd->add_subcommand("toggle", _("Toggle recording start/stop"));
  std::string recording_toggle_scene;
  recording_toggle->add_option("-s,--scene", recording_toggle_scene,
                               _("Scene ID (default: active scene)"));

  // ---- status subcommand ----
  auto *status_cmd = app.add_subcommand("status", _("Show overall vinput status"));
  status_cmd->alias("st");

  // ---- Parse ----
  CLI11_PARSE(app, argc, argv);

  if (argc == 1) {
    std::cout << app.help();
    return 0;
  }

  // Build context after parsing
  CliContext ctx;
  ctx.json_output = json_output;
  ctx.is_tty = (isatty(STDOUT_FILENO) == 1);

  auto fmt = CreateFormatter(ctx);

  // ---- Dispatch ----

  // model
  if (model_list->parsed()) {
    return RunModelList(model_list_remote, *fmt, ctx);
  } else if (model_add->parsed()) {
    return RunModelAdd(model_add_name, *fmt, ctx);
  } else if (model_use->parsed()) {
    return RunModelUse(model_use_name, *fmt, ctx);
  } else if (model_remove->parsed()) {
    return RunModelRemove(model_remove_name, model_remove_force, *fmt, ctx);
  } else if (model_info->parsed()) {
    return RunModelInfo(model_info_name, *fmt, ctx);
  }

  // scene
  else if (scene_list->parsed()) {
    return RunSceneList(*fmt, ctx);
  } else if (scene_add->parsed()) {
    return RunSceneAdd(scene_add_id, scene_add_label, scene_add_prompt,
                       scene_add_provider, scene_add_model,
                       scene_add_candidates, scene_add_timeout_ms, *fmt, ctx);
  } else if (scene_use->parsed()) {
    return RunSceneUse(scene_use_id, *fmt, ctx);
  } else if (scene_remove->parsed()) {
    return RunSceneRemove(scene_remove_id, scene_remove_force, *fmt, ctx);
  }

  // llm
  else if (llm_list->parsed()) {
    return RunLlmList(*fmt, ctx);
  } else if (llm_add->parsed()) {
    return RunLlmAdd(llm_add_name, llm_add_base_url,
                     llm_add_api_key, *fmt, ctx);
  } else if (llm_remove->parsed()) {
    return RunLlmRemove(llm_remove_name, llm_remove_force, *fmt, ctx);
  }

  // asr
  else if (asr_list->parsed()) {
    return RunAsrList(*fmt, ctx);
  } else if (asr_add->parsed()) {
    return RunAsrAdd(asr_add_name, asr_add_type, asr_add_model,
                     asr_add_command, asr_add_args, asr_add_env,
                     asr_add_timeout_ms, *fmt, ctx);
  } else if (asr_remove->parsed()) {
    return RunAsrRemove(asr_remove_name, asr_remove_force, *fmt, ctx);
  } else if (asr_use->parsed()) {
    return RunAsrUse(asr_use_name, *fmt, ctx);
  } else if (asr_edit->parsed()) {
    return RunAsrEdit(asr_edit_name, *fmt, ctx);
  }

  // adaptor
  else if (adaptor_list->parsed()) {
    return RunAdaptorList(*fmt, ctx);
  } else if (adaptor_start->parsed()) {
    return RunAdaptorStart(adaptor_start_name, *fmt, ctx);
  } else if (adaptor_stop->parsed()) {
    return RunAdaptorStop(adaptor_stop_name, *fmt, ctx);
  }

  // hotword
  else if (hotword_get->parsed()) {
    return RunHotwordGet(*fmt, ctx);
  } else if (hotword_set->parsed()) {
    return RunHotwordSet(hotword_set_path, *fmt, ctx);
  } else if (hotword_clear->parsed()) {
    return RunHotwordClear(*fmt, ctx);
  } else if (hotword_edit->parsed()) {
    return RunHotwordEdit(*fmt, ctx);
  }

  // device
  else if (device_list->parsed()) {
    return RunDeviceList(*fmt, ctx);
  } else if (device_use->parsed()) {
    return RunDeviceUse(device_use_name, *fmt, ctx);
  }

  // config
  else if (config_get->parsed()) {
    return RunConfigGet(config_get_path, *fmt, ctx);
  } else if (config_set->parsed()) {
    return RunConfigSet(config_set_path, config_set_value, config_set_stdin,
                        *fmt, ctx);
  } else if (config_edit->parsed()) {
    return RunConfigEdit(config_edit_target, *fmt, ctx);
  }

  // daemon
  else if (daemon_start->parsed()) {
    return RunDaemonStart(*fmt, ctx);
  } else if (daemon_stop->parsed()) {
    return RunDaemonStop(*fmt, ctx);
  } else if (daemon_restart->parsed()) {
    return RunDaemonRestart(*fmt, ctx);
  } else if (daemon_logs->parsed()) {
    return RunDaemonLogs(daemon_logs_follow, daemon_logs_lines, *fmt, ctx);
  }

  // status
  else if (status_cmd->parsed()) {
    return RunStatus(*fmt, ctx);
  }

  // recording
  else if (recording_start->parsed()) {
    return RunRecordingStart(*fmt, ctx);
  } else if (recording_stop->parsed()) {
    return RunRecordingStop(recording_stop_scene, *fmt, ctx);
  } else if (recording_toggle->parsed()) {
    return RunRecordingToggle(recording_toggle_scene, *fmt, ctx);
  }

  // init
  else if (init_cmd->parsed()) {
    return RunInit(init_force, *fmt, ctx);
  }

  return 0;
}
