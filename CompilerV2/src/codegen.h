#pragma once

#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast.h"
#include "diagnostic.h"
#include "sema.h"

namespace lunaris {

struct CodegenResult {
    bool ok = false;
    std::string assembly;
    std::vector<Diagnostic> diagnostics;
};

class CodeGenerator {
public:
    CodeGenerator(const Program& program, const SemanticResult& semantics, DiagnosticSink& diagnostics);

    [[nodiscard]] CodegenResult generate();

private:
    struct VariableSlot {
        TypeRef type;
        std::size_t offset = 0;
    };

    struct FunctionContext {
        std::unordered_map<std::string, VariableSlot> variables;
        std::size_t frame_size = 0;
    };

    struct ExpressionValue {
        TypeRef type;
        bool valid = false;
    };

    void emit_line(const std::string& line);
    void emit_function(const FunctionDecl& declaration);
    void emit_data(const DataDecl& declaration);
    void emit_startup_stub();
    void collect_function_layout(FunctionContext& context, const FunctionDecl& declaration);
    std::size_t align_up(std::size_t value, std::size_t alignment) const;
    std::size_t size_of(const TypeRef& type) const;
    std::size_t alignment_of(const TypeRef& type) const;
    bool is_pointer_like(const TypeRef& type) const;
    std::optional<ExpressionValue> infer_expression_type(const Expression& expression, const FunctionContext& context, const FunctionDecl& function);
    ExpressionValue compile_expression(const Expression& expression, const FunctionContext& context, const FunctionDecl& function);
    void compile_lvalue_store(const Expression& target, const FunctionContext& context, const FunctionDecl& function);
    void compile_statement(const Statement& statement, FunctionContext& context, const FunctionDecl& function);
    void compile_block(const std::vector<Statement>& body, FunctionContext& context, const FunctionDecl& function);
    void emit_store(std::size_t size);
    void emit_load(std::size_t size);
    std::string label_for_string(const std::string& value);
    std::string make_label(const std::string& prefix);
    std::string escape_string(const std::string& value) const;
    std::string operand_for_register(std::size_t index) const;
    std::optional<const FunctionDecl*> find_function(const std::string& name) const;
    std::optional<const DataDecl*> find_data(const std::string& name) const;

    const Program& program_;
    const SemanticResult& semantics_;
    DiagnosticSink& diagnostics_;
    std::ostringstream assembly_;
    std::unordered_map<std::string, std::size_t> string_literals_;
    std::vector<std::pair<std::string, std::string>> string_table_;
    std::size_t label_counter_ = 0;
};

} // namespace lunaris