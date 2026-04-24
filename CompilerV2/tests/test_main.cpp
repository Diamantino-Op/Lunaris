#include <cstdlib>
#include <algorithm>
#include <iostream>

int main();

#define EXPECT_TRUE(condition) do { if (!(condition)) { std::cerr << __FILE__ << ':' << __LINE__ << ": expectation failed: " #condition "\n"; return EXIT_FAILURE; } } while (false)
#define EXPECT_EQ(left, right) do { if (!((left) == (right))) { std::cerr << __FILE__ << ':' << __LINE__ << ": expectation failed: " #left " == " #right "\n"; return EXIT_FAILURE; } } while (false)

#include "diagnostic.h"
#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"

static int test_lexer_keywords_and_symbols() {
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer("packed struct Point { x: i64*; y: i64; } data payload: u64 section \".limine_requests\" = 1, 2 asm function memcpy(dst: u8*, src: u8*, len: usize): u8* = memcpy; function demo(a: i64) if a ~= 0 then return a + 1 end end", diagnostics);
    auto tokens = lexer.lex();
    EXPECT_TRUE(!tokens.empty());
    EXPECT_EQ(tokens[0].kind, lunaris::TokenKind::KeywordPacked);
    EXPECT_EQ(tokens[1].kind, lunaris::TokenKind::KeywordStruct);
    EXPECT_EQ(tokens[2].kind, lunaris::TokenKind::Identifier);
    EXPECT_TRUE(std::any_of(tokens.begin(), tokens.end(), [](const lunaris::Token& token) { return token.kind == lunaris::TokenKind::KeywordSection; }));
    EXPECT_TRUE(std::any_of(tokens.begin(), tokens.end(), [](const lunaris::Token& token) { return token.kind == lunaris::TokenKind::TildeEqual; }));
    return diagnostics.has_errors() ? EXIT_FAILURE : EXIT_SUCCESS;
}

static int test_parser_accepts_basic_function_and_struct() {
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer(
        "packed struct Point { x: i64*; y: i64; }\n"
        "data payload: u64 section \".limine_requests\" = 1, 2\n"
        "asm function memcpy(dst: u8*, src: u8*, len: usize): u8* = memcpy;\n"
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
    return EXIT_SUCCESS;
}

static int test_semantic_analysis_computes_packed_struct_layout() {
    lunaris::DiagnosticSink diagnostics;
    lunaris::Lexer lexer(
        "packed struct Point { x: i64*; y: i64; }\n"
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
        "packed struct limine_base_revision { magic0: u64; magic1: u64; revision: u64; }\n"
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
        "packed struct Point { x: i64*; y: i64; }\n"
        "asm function memcpy(dst: u8*, src: u8*, len: usize): u8* = memcpy;\n"
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

int main() {
    if (int result = test_lexer_keywords_and_symbols(); result != EXIT_SUCCESS) {
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
    std::cout << "all tests passed\n";
    return EXIT_SUCCESS;
}
