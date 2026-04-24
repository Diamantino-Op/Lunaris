#pragma once

#include <string>

#include "ast.h"
#include "diagnostic.h"

namespace lunaris {

bool resolve_requirements(const std::string& source_path, Program& program, DiagnosticSink& diagnostics);

} // namespace lunaris