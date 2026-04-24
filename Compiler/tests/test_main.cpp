#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <iterator>

int main();

#define EXPECT_TRUE(condition) do { if (!(condition)) { std::cerr << __FILE__ << ':' << __LINE__ << ": expectation failed: " #condition "\n"; return EXIT_FAILURE; } } while (false)
#define EXPECT_EQ(left, right) do { if (!((left) == (right))) { std::cerr << __FILE__ << ':' << __LINE__ << ": expectation failed: " #left " == " #right "\n"; return EXIT_FAILURE; } } while (false)

#include "diagnostic.h"
#include "codegen.h"
#include "lexer.h"
#include "module_loader.h"
#include "parser.h"
#include "machine_backend.h"
#include "sema.h"

static int test_lexer_decodes_string_escapes() {
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer("function demo() return \"line1\\nline2\\t\\x1b\" end", diagnostics);
    auto tokens = lexer.lex();
    EXPECT_TRUE(!tokens.empty());
    auto string_token = std::find_if(tokens.begin(), tokens.end(), [](const lunaris::Token& token) { return token.kind == lunaris::TokenKind::String; });
    EXPECT_TRUE(string_token != tokens.end());
    EXPECT_EQ(string_token->lexeme, std::string("line1\nline2\t\x1b", 13));
    return diagnostics.has_errors() ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int test_lexer_keywords_and_symbols() {
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer("packed struct Point x: ptr(i64); y: i64; end data payload: u64 section \".limine_requests\" = 1, 2 require \"terminal\" asm function memcpy(dst: ptr(u8), src: ptr(u8), len: usize): ptr(u8) = memcpy; function demo(a: i64) if a ~= 0 then return a + 1 end end", diagnostics);
    auto tokens = lexer.lex();
    EXPECT_TRUE(!tokens.empty());
    EXPECT_EQ(tokens[0].kind, lunaris::TokenKind::KeywordPacked);
    EXPECT_EQ(tokens[1].kind, lunaris::TokenKind::KeywordStruct);
    EXPECT_EQ(tokens[2].kind, lunaris::TokenKind::Identifier);
    EXPECT_TRUE(std::any_of(tokens.begin(), tokens.end(), [](const lunaris::Token& token) { return token.kind == lunaris::TokenKind::KeywordSection; }));
    EXPECT_TRUE(std::any_of(tokens.begin(), tokens.end(), [](const lunaris::Token& token) { return token.kind == lunaris::TokenKind::KeywordRequire; }));
    EXPECT_TRUE(std::any_of(tokens.begin(), tokens.end(), [](const lunaris::Token& token) { return token.kind == lunaris::TokenKind::TildeEqual; }));
    return diagnostics.has_errors() ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int test_parser_accepts_require_directive() {
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer(
        "require \"terminal\"\n"
        "function demo() end",
        diagnostics);
    auto tokens = lexer.lex();
    lunaris::Parser parser(std::move(tokens), diagnostics);
    auto result = parser.parse();
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.program.imports.size(), 1u);
    EXPECT_EQ(result.program.imports[0].module, "terminal");
    return EXIT_SUCCESS;
}

static int test_parser_accepts_basic_function_and_struct() {
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer(
        "packed struct Point\n"
        "    x: ptr(i64);\n"
        "    y: i64;\n"
        "end\n"
        "data payload: u64 section \".limine_requests\" = 1, 2\n"
        "asm function memcpy(dst: ptr(u8), src: ptr(u8), len: usize): ptr(u8) = memcpy;\n"
        "function demo(a: i64)\n"
        "    local value: i64 = a + 1\n"
        "    return memcpy(value, value, value)\n"
        "end",
        diagnostics);
    auto tokens = lexer.lex();
    lunaris::Parser parser(std::move(tokens), diagnostics);
    auto result = parser.parse();
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.program.structs.size(), 1u);
    EXPECT_EQ(result.program.data.size(), 1u);
    EXPECT_TRUE(result.program.structs[0].packed);
    EXPECT_TRUE(result.program.data[0].section_name.has_value());
    EXPECT_EQ(result.program.data[0].section_name.value(), ".limine_requests");
    EXPECT_EQ(result.program.functions.size(), 2u);
    EXPECT_TRUE(result.program.functions[0].external_asm);
    EXPECT_EQ(result.program.functions[1].body.size(), 2u);
    EXPECT_TRUE(result.program.structs[0].fields[0].type.kind == lunaris::TypeKind::Pointer);
    EXPECT_TRUE(result.program.functions[0].parameters[0].type.kind == lunaris::TypeKind::Pointer);
    EXPECT_TRUE(result.program.functions[0].return_type.has_value());
    EXPECT_TRUE(result.program.functions[0].return_type->kind == lunaris::TypeKind::Pointer);
    return EXIT_SUCCESS;
}

static int test_semantic_analysis_computes_packed_struct_layout() {
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer(
        "packed struct Point\n"
        "    x: ptr(i64);\n"
        "    y: i64;\n"
        "end\n"
        "function demo(a: i64) return a end",
        diagnostics);
    auto tokens = lexer.lex();
    lunaris::Parser parser(std::move(tokens), diagnostics);
    auto parse_result = parser.parse();
    EXPECT_TRUE(parse_result.ok);

    lunaris::DiagnosticSink semantic_diagnostics;
    lunaris::SemanticAnalyzer analyzer(parse_result.program, semantic_diagnostics);
    auto semantic_result = analyzer.analyze();
    EXPECT_TRUE(semantic_result.ok);
    EXPECT_EQ(semantic_result.struct_layouts.at("Point").size, 16u);
    EXPECT_EQ(semantic_result.struct_layouts.at("Point").alignment, 1u);
    EXPECT_EQ(semantic_result.struct_layouts.at("Point").fields[0].offset, 0u);
    EXPECT_EQ(semantic_result.struct_layouts.at("Point").fields[1].offset, 8u);
    return EXIT_SUCCESS;
}

static int test_semantic_analysis_accepts_struct_named_data() {
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer(
        "packed struct limine_base_revision\n"
        "    magic0: u64;\n"
        "    magic1: u64;\n"
        "    revision: u64;\n"
        "end\n"
        "data limine_base_revision: limine_base_revision = 0xf9562b2d5c95a6c8, 0x6a7b384944536bdc, 6\n"
        "function demo() end",
        diagnostics);
    auto tokens = lexer.lex();
    lunaris::Parser parser(std::move(tokens), diagnostics);
    auto parse_result = parser.parse();
    EXPECT_TRUE(parse_result.ok);

    lunaris::DiagnosticSink semantic_diagnostics;
    lunaris::SemanticAnalyzer analyzer(parse_result.program, semantic_diagnostics);
    auto semantic_result = analyzer.analyze();
    EXPECT_TRUE(semantic_result.ok);
    EXPECT_EQ(semantic_result.data_layouts.at("limine_base_revision").size, 24u);
    return EXIT_SUCCESS;
}

static int test_codegen_emits_elf_ready_assembly() {
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer(
        "packed struct Point\n"
        "    x: ptr(i64);\n"
        "    y: i64;\n"
        "end\n"
        "asm function memcpy(dst: ptr(u8), src: ptr(u8), len: usize): ptr(u8) = memcpy;\n"
        "function main(argc: i64)\n"
        "    local value: i64 = 42\n"
        "    return memcpy(value, value, value)\n"
        "end",
        diagnostics);
    auto tokens = lexer.lex();
    lunaris::Parser parser(std::move(tokens), diagnostics);
    auto parse_result = parser.parse();
    EXPECT_TRUE(parse_result.ok);

    lunaris::DiagnosticSink semantic_diagnostics;
    lunaris::SemanticAnalyzer analyzer(parse_result.program, semantic_diagnostics);
    auto semantic_result = analyzer.analyze();
    EXPECT_TRUE(semantic_result.ok);

    lunaris::DiagnosticSink codegen_diagnostics;
    lunaris::CodeGenerator generator(parse_result.program, semantic_result, codegen_diagnostics);
    auto codegen_result = generator.generate();
    EXPECT_TRUE(codegen_result.ok);
    EXPECT_TRUE(codegen_result.assembly.find(".globl _start") != std::string::npos);
    EXPECT_TRUE(codegen_result.assembly.find("call main") != std::string::npos);
    EXPECT_TRUE(codegen_result.assembly.find(".extern memcpy") != std::string::npos);
    return EXIT_SUCCESS;
}

static int test_codegen_loads_scalar_global_values() {
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer(
        "data counter: u64 = 42\n"
        "function main()\n"
        "    return counter\n"
        "end",
        diagnostics);
    auto tokens = lexer.lex();
    lunaris::Parser parser(std::move(tokens), diagnostics);
    auto parse_result = parser.parse();
    EXPECT_TRUE(parse_result.ok);

    lunaris::DiagnosticSink semantic_diagnostics;
    lunaris::SemanticAnalyzer analyzer(parse_result.program, semantic_diagnostics);
    auto semantic_result = analyzer.analyze();
    EXPECT_TRUE(semantic_result.ok);

    lunaris::DiagnosticSink codegen_diagnostics;
    lunaris::CodeGenerator generator(parse_result.program, semantic_result, codegen_diagnostics);
    auto codegen_result = generator.generate();
    EXPECT_TRUE(codegen_result.ok);
    EXPECT_TRUE(codegen_result.assembly.find("lea rax, [rip + counter]") != std::string::npos);
    EXPECT_TRUE(codegen_result.assembly.find("mov rax, QWORD PTR [rax]") != std::string::npos);
    return EXIT_SUCCESS;
}

static int test_machine_backend_emits_string_literals() {
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer(
        "function main(): ptr(u8)\n"
        "    return \"hello\\n\"\n"
        "end",
        diagnostics);
    auto tokens = lexer.lex();
    lunaris::Parser parser(std::move(tokens), diagnostics);
    auto parse_result = parser.parse();
    EXPECT_TRUE(parse_result.ok);

    lunaris::DiagnosticSink semantic_diagnostics;
    lunaris::SemanticAnalyzer analyzer(parse_result.program, semantic_diagnostics);
    auto semantic_result = analyzer.analyze();
    EXPECT_TRUE(semantic_result.ok);

    lunaris::DiagnosticSink backend_diagnostics;
    lunaris::MachineBackend backend(parse_result.program, semantic_result, backend_diagnostics);
    auto object_result = backend.generate();
    EXPECT_TRUE(object_result.ok);
    const std::string expected = std::string("hello\n", 6);
    EXPECT_TRUE(std::search(object_result.bytes.begin(), object_result.bytes.end(), expected.begin(), expected.end()) != object_result.bytes.end());
    return EXIT_SUCCESS;
}

static int test_require_resolves_module_functions() {
    namespace fs = std::filesystem;
    auto read_text = [](const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };

    const fs::path temp_dir = fs::temp_directory_path() / "lunaris_require_test";
    fs::create_directories(temp_dir);

    const fs::path font_path = temp_dir / "terminal_font.lua";
    const fs::path terminal_path = temp_dir / "terminal.lua";
    const fs::path main_path = temp_dir / "main.lua";

    {
        std::ofstream file(font_path);
        file << "function terminal_font_word(index: u64) return 42 end\n";
    }
    {
        std::ofstream file(terminal_path);
        file << "require \"terminal_font\"\n";
        file << "function terminal_init(framebuffer: ptr(u32), width: u64, height: u64, pitch: u64)\n";
        file << "    return terminal_font_word(0)\n";
        file << "end\n";
        file << "function print(text: ptr(u8)) end\n";
        file << "function printf(format: ptr(u8), arg0: u64, arg1: u64, arg2: u64, arg3: u64, arg4: u64) end\n";
    }
    {
        std::ofstream file(main_path);
        file << "require \"terminal\"\n";
        file << "function main()\n";
        file << "    terminal_init(0, 0, 0, 0)\n";
        file << "    print(\"hello\")\n";
        file << "    printf(\"value %u\", 1, 0, 0, 0, 0)\n";
        file << "end\n";
    }

    auto source = read_text(main_path);
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer(source, diagnostics);
    auto tokens = lexer.lex();
    lunaris::Parser parser(std::move(tokens), diagnostics);
    auto parse_result = parser.parse();
    EXPECT_TRUE(parse_result.ok);
    EXPECT_TRUE(lunaris::resolve_requirements(main_path.string(), parse_result.program, diagnostics));

    EXPECT_TRUE(std::any_of(parse_result.program.functions.begin(), parse_result.program.functions.end(), [](const lunaris::FunctionDecl& function) {
        return function.name == "terminal_init" && function.external_asm;
    }));
    EXPECT_TRUE(std::any_of(parse_result.program.functions.begin(), parse_result.program.functions.end(), [](const lunaris::FunctionDecl& function) {
        return function.name == "terminal_font_word" && function.external_asm;
    }));

    lunaris::DiagnosticSink semantic_diagnostics;
    lunaris::SemanticAnalyzer analyzer(parse_result.program, semantic_diagnostics);
    auto semantic_result = analyzer.analyze();
    EXPECT_TRUE(semantic_result.ok);
    return EXIT_SUCCESS;
}

int main() {
    if (int result = test_lexer_decodes_string_escapes(); result != EXIT_SUCCESS) {
        return result;
    }
    if (int result = test_lexer_keywords_and_symbols(); result != EXIT_SUCCESS) {
        return result;
    }
    if (int result = test_parser_accepts_require_directive(); result != EXIT_SUCCESS) {
        return result;
    }
    if (int result = test_parser_accepts_basic_function_and_struct(); result != EXIT_SUCCESS) {
        return result;
    }
    if (int result = test_semantic_analysis_accepts_struct_named_data(); result != EXIT_SUCCESS) {
        return result;
    }
    if (int result = test_semantic_analysis_computes_packed_struct_layout(); result != EXIT_SUCCESS) {
        return result;
    }
    if (int result = test_codegen_emits_elf_ready_assembly(); result != EXIT_SUCCESS) {
        return result;
    }
    if (int result = test_codegen_loads_scalar_global_values(); result != EXIT_SUCCESS) {
        return result;
    }
    if (int result = test_machine_backend_emits_string_literals(); result != EXIT_SUCCESS) {
        return result;
    }
    if (int result = test_require_resolves_module_functions(); result != EXIT_SUCCESS) {
        return result;
    }
    std::cout << "all tests passed\n";
    return EXIT_SUCCESS;
}
