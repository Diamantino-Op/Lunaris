#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast.h"
#include "diagnostic.h"

namespace lunaris {

struct FieldLayout {
    std::string name;
    std::size_t offset = 0;
    std::size_t size = 0;
    std::size_t alignment = 1;
};

struct StructLayout {
    std::string name;
    bool packed = false;
    std::size_t size = 0;
    std::size_t alignment = 1;
    std::vector<FieldLayout> fields;
};

struct SemanticResult {
    bool ok = false;
    std::unordered_map<std::string, StructLayout> struct_layouts;
    std::vector<Diagnostic> diagnostics;
};

class SemanticAnalyzer {
public:
    SemanticAnalyzer(const Program& program, DiagnosticSink& diagnostics);

    [[nodiscard]] SemanticResult analyze();

private:
    struct ResolvedType {
        std::size_t size = 0;
        std::size_t alignment = 1;
        bool valid = false;
    };

    std::optional<ResolvedType> resolve_type(const TypeRef& type);
    void collect_structs();
    void collect_function_signatures();
    bool compute_struct_layout(const StructDecl& declaration, StructLayout& layout);
    std::size_t align_up(std::size_t value, std::size_t alignment) const;

    const Program& program_;
    DiagnosticSink& diagnostics_;
    std::unordered_map<std::string, const StructDecl*> struct_declarations_;
    std::unordered_map<std::string, StructLayout> struct_layouts_;
};

} // namespace lunaris