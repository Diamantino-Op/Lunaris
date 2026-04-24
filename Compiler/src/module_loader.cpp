#include "module_loader.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <unordered_set>

#include "lexer.h"
#include "parser.h"

namespace lunaris {

namespace {

std::optional<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::filesystem::path resolve_module_path(const std::filesystem::path& source_path, const std::string& module) {
    std::filesystem::path module_path(module);
    if (module_path.extension().empty()) {
        module_path += ".lua";
    }
    if (module_path.is_relative()) {
        module_path = source_path.parent_path() / module_path;
    }
    return module_path.lexically_normal();
}

bool append_struct(const StructDecl& declaration, Program& program, DiagnosticSink& diagnostics) {
    for (const auto& existing : program.structs) {
        if (existing.name == declaration.name) {
            diagnostics.error(declaration.location, "duplicate struct declaration: " + declaration.name);
            return false;
        }
    }
    program.structs.push_back(declaration);
    return true;
}

bool append_function(const FunctionDecl& declaration, Program& program, DiagnosticSink& diagnostics) {
    for (const auto& existing : program.functions) {
        if (existing.name == declaration.name) {
            diagnostics.error(declaration.location, "duplicate function declaration: " + declaration.name);
            return false;
        }
    }

    FunctionDecl imported = declaration;
    imported.external_asm = true;
    if (imported.asm_symbol.empty()) {
        imported.asm_symbol = imported.name;
    }
    imported.body.clear();
    program.functions.push_back(std::move(imported));
    return true;
}

bool import_module(const std::filesystem::path& source_path,
                   Program& program,
                   DiagnosticSink& diagnostics,
                   std::unordered_set<std::string>& visited_paths) {
    const std::string normalized_path = source_path.lexically_normal().string();
    if (!visited_paths.insert(normalized_path).second) {
        return true;
    }

    auto source = read_file(source_path);
    if (!source.has_value()) {
        diagnostics.error(SourceLocation{1, 1}, "failed to read required module: " + normalized_path);
        return false;
    }

    DiagnosticSink parse_diagnostics;
    Lexer lexer(*source, parse_diagnostics);
    auto tokens = lexer.lex();
    Parser parser(std::move(tokens), parse_diagnostics);
    auto result = parser.parse();
    if (!result.ok) {
        for (const auto& diagnostic : result.diagnostics) {
            diagnostics.error(diagnostic.location, normalized_path + ": " + diagnostic.message);
        }
        return false;
    }

    bool ok = true;
    for (const auto& requirement : result.program.imports) {
        auto required_path = resolve_module_path(source_path, requirement.module);
        ok = import_module(required_path, program, diagnostics, visited_paths) && ok;
    }

    for (const auto& declaration : result.program.structs) {
        ok = append_struct(declaration, program, diagnostics) && ok;
    }

    for (const auto& declaration : result.program.functions) {
        ok = append_function(declaration, program, diagnostics) && ok;
    }

    return ok;
}

} // namespace

bool resolve_requirements(const std::string& source_path, Program& program, DiagnosticSink& diagnostics) {
    std::unordered_set<std::string> visited_paths;
    bool ok = true;
    for (const auto& requirement : program.imports) {
        auto required_path = resolve_module_path(std::filesystem::path(source_path), requirement.module);
        ok = import_module(required_path, program, diagnostics, visited_paths) && ok;
    }
    return ok;
}

} // namespace lunaris