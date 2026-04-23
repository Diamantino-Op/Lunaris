#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "diagnostic.h"
#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"

namespace {

std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: lunaris_compiler <input.lua>\n";
        return 1;
    }

    std::string source = read_file(argv[1]);
    if (source.empty()) {
        std::cerr << "failed to read input file\n";
        return 1;
    }

    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer(source, diagnostics);
    auto tokens = lexer.lex();
    lunaris::Parser parser(std::move(tokens), diagnostics);
    auto result = parser.parse();

    if (!result.ok) {
        for (const auto& diagnostic : result.diagnostics) {
            std::cerr << diagnostic.location.line << ':' << diagnostic.location.column << ": "
                      << diagnostic.message << '\n';
        }
        return 1;
    }

    lunaris::DiagnosticSink semantic_diagnostics;
    lunaris::SemanticAnalyzer analyzer(result.program, semantic_diagnostics);
    auto semantic_result = analyzer.analyze();

    if (!semantic_result.ok) {
        for (const auto& diagnostic : semantic_result.diagnostics) {
            std::cerr << diagnostic.location.line << ':' << diagnostic.location.column << ": "
                      << diagnostic.message << '\n';
        }
        return 1;
    }

    lunaris::DiagnosticSink codegen_diagnostics;
    lunaris::CodeGenerator generator(result.program, semantic_result, codegen_diagnostics);
    auto codegen_result = generator.generate();

    if (!codegen_result.ok) {
        for (const auto& diagnostic : codegen_result.diagnostics) {
            std::cerr << diagnostic.location.line << ':' << diagnostic.location.column << ": "
                      << diagnostic.message << '\n';
        }
        return 1;
    }

    std::cout << codegen_result.assembly;
    return 0;
}
