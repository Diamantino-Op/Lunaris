#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "codegen.h"
#include "diagnostic.h"
#include "machine_backend.h"
#include "module_loader.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"

namespace {

std::optional<std::string> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

enum class BackendKind {
    Machine,
    Legacy,
};

void print_diagnostics(const std::vector<lunaris::Diagnostic>& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        std::cerr << diagnostic.location.line << ':' << diagnostic.location.column << ": "
                  << diagnostic.message << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    BackendKind backend = BackendKind::Machine;
    std::string input_path;

    for (int index = 1; index < argc; ++index) {
        std::string argument = argv[index];
        if (argument == "--old-codegen" || argument == "--legacy-codegen") {
            backend = BackendKind::Legacy;
            continue;
        }
        if (argument == "--machine-code" || argument == "--machine-backend") {
            backend = BackendKind::Machine;
            continue;
        }
        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "unknown flag: " << argument << '\n';
            std::cerr << "usage: lunaris_compiler_v2 [--old-codegen] <input.lua>\n";
            return 1;
        }
        if (input_path.empty()) {
            input_path = std::move(argument);
            continue;
        }
        std::cerr << "usage: lunaris_compiler_v2 [--old-codegen] <input.lua>\n";
        return 1;
    }

    if (input_path.empty()) {
        std::cerr << "usage: lunaris_compiler_v2 [--old-codegen] <input.lua>\n";
        return 1;
    }

    auto source = read_file(input_path);
    if (!source.has_value()) {
        std::cerr << "failed to read input file\n";
        return 1;
    }

    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer(*source, diagnostics);
    auto tokens = lexer.lex();
    lunaris::Parser parser(std::move(tokens), diagnostics);
    auto result = parser.parse();

    if (!result.ok) {
        print_diagnostics(result.diagnostics);
        return 1;
    }

    if (!lunaris::resolve_requirements(input_path, result.program, diagnostics)) {
        print_diagnostics(diagnostics.diagnostics());
        return 1;
    }

    lunaris::DiagnosticSink semantic_diagnostics;
    lunaris::SemanticAnalyzer analyzer(result.program, semantic_diagnostics);
    auto semantic_result = analyzer.analyze();

    if (!semantic_result.ok) {
        print_diagnostics(semantic_result.diagnostics);
        return 1;
    }

    if (backend == BackendKind::Legacy) {
        lunaris::DiagnosticSink codegen_diagnostics;
        lunaris::CodeGenerator generator(result.program, semantic_result, codegen_diagnostics);
        auto codegen_result = generator.generate();

        if (!codegen_result.ok) {
            print_diagnostics(codegen_result.diagnostics);
            return 1;
        }

        std::cout << codegen_result.assembly;
        return std::cout ? 0 : 1;
    }

    lunaris::DiagnosticSink codegen_diagnostics;
    lunaris::MachineBackend generator(result.program, semantic_result, codegen_diagnostics);
    auto codegen_result = generator.generate();

    if (!codegen_result.ok) {
        print_diagnostics(codegen_result.diagnostics);
        return 1;
    }

    std::cout.write(reinterpret_cast<const char*>(codegen_result.bytes.data()), static_cast<std::streamsize>(codegen_result.bytes.size()));
    return std::cout ? 0 : 1;
}
