#include "diagnostic.h"

namespace lunaris {

void DiagnosticSink::error(SourceLocation location, std::string message) {
    diagnostics_.push_back(Diagnostic{location, std::move(message)});
}

const std::vector<Diagnostic>& DiagnosticSink::diagnostics() const {
    return diagnostics_;
}

bool DiagnosticSink::has_errors() const {
    return !diagnostics_.empty();
}

} // namespace lunaris
