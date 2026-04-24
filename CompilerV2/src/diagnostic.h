#pragma once

#include <string>
#include <vector>

#include "token.h"

namespace lunaris {

struct Diagnostic {
    SourceLocation location;
    std::string message;
};

class DiagnosticSink {
public:
    void error(SourceLocation location, std::string message);
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const;
    [[nodiscard]] bool has_errors() const;

private:
    std::vector<Diagnostic> diagnostics_;
};

} // namespace lunaris
