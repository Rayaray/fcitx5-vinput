#pragma once

#include "cli/cli_context.h"
#include "cli/formatter.h"

#include <string>

int RunAdaptorList(Formatter &fmt, const CliContext &ctx);
int RunAdaptorStart(const std::string &name, Formatter &fmt,
                    const CliContext &ctx);
int RunAdaptorStop(const std::string &name, Formatter &fmt,
                   const CliContext &ctx);
