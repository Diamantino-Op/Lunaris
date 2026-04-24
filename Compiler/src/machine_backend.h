#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "ast.h"
#include "diagnostic.h"
#include "sema.h"

namespace lunaris {

struct ObjectFileResult {
    bool ok = false;
    std::vector<std::uint8_t> bytes;
    std::vector<Diagnostic> diagnostics;
};

class MachineBackend {
public:
    MachineBackend(const Program& program, const SemanticResult& semantics, DiagnosticSink& diagnostics);

    [[nodiscard]] ObjectFileResult generate();

private:
    enum class Register : std::uint8_t {
        RAX = 0,
        RCX = 1,
        RDX = 2,
        RBX = 3,
        RSP = 4,
        RBP = 5,
        RSI = 6,
        RDI = 7,
        R8 = 8,
        R9 = 9,
    };

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

    struct RelocationEntry {
        std::size_t offset = 0;
        std::string symbol;
        std::uint32_t type = 0;
        std::int64_t addend = 0;
    };

    struct DataEntry {
        SourceLocation location;
        std::string name;
        TypeRef type;
        std::string section_name;
        std::vector<std::string> values;
        std::size_t size = 0;
    };

    struct StringLiteralEntry {
        SourceLocation location;
        std::string symbol;
        std::string value;
    };

    struct SectionEntry {
        std::string name;
        std::vector<std::uint8_t> bytes;
        std::size_t index = 0;
    };

    struct SymbolEntry {
        std::string name;
        bool defined = false;
        std::size_t value = 0;
        std::size_t size = 0;
        std::size_t section_index = 0;
        bool is_function = true;
    };

    class FunctionBuilder {
    public:
        FunctionBuilder();

        std::size_t offset() const;
        void emit_u8(std::uint8_t value);
        void emit_u16(std::uint16_t value);
        void emit_u32(std::uint32_t value);
        void emit_u64(std::uint64_t value);
        void emit_bytes(const std::vector<std::uint8_t>& values);
        void emit_bytes(std::initializer_list<std::uint8_t> values);
        void mark_label(const std::string& label);
        void emit_jump_short_placeholder(const std::string& label, std::uint8_t opcode);
        void emit_jump_near_placeholder(const std::string& label, std::uint8_t opcode_prefix, std::uint8_t opcode);
        void emit_call_placeholder(const std::string& symbol);
        void emit_movabs_placeholder(const std::string& symbol);
        void patch_labels();

        const std::vector<std::uint8_t>& bytes() const;
        const std::vector<RelocationEntry>& relocations() const;

    private:
        struct BranchFixup {
            std::size_t patch_offset = 0;
            std::string label;
            bool is_near = true;
            std::uint8_t opcode_prefix = 0;
            std::uint8_t opcode = 0;
        };

        std::vector<std::uint8_t> bytes_;
        std::unordered_map<std::string, std::size_t> labels_;
        std::vector<BranchFixup> fixups_;
        std::vector<RelocationEntry> relocations_;
    };

    void emit_prologue(FunctionBuilder& builder, const FunctionContext& context);
    void emit_epilogue(FunctionBuilder& builder);
    void emit_return(FunctionBuilder& builder, const FunctionContext& context, const FunctionDecl& function, const Statement* statement);
    void emit_function(FunctionBuilder& builder, const FunctionDecl& declaration);
    void collect_function_layout(FunctionContext& context, const FunctionDecl& declaration);
    std::size_t align_up(std::size_t value, std::size_t alignment) const;
    std::size_t size_of(const TypeRef& type) const;
    std::size_t alignment_of(const TypeRef& type) const;
    std::optional<ExpressionValue> infer_expression_type(const Expression& expression, const FunctionContext& context, const FunctionDecl& function) const;
    ExpressionValue compile_expression(FunctionBuilder& builder, const Expression& expression, const FunctionContext& context, const FunctionDecl& function);
    bool compile_lvalue_address(FunctionBuilder& builder, const Expression& target, const FunctionContext& context, const FunctionDecl& function);
    bool compile_statement(FunctionBuilder& builder, const Statement& statement, FunctionContext& context, const FunctionDecl& function);
    void compile_block(FunctionBuilder& builder, const std::vector<Statement>& body, FunctionContext& context, const FunctionDecl& function);
    void emit_store(FunctionBuilder& builder, std::size_t size);
    void emit_load(FunctionBuilder& builder, std::size_t size);
    std::string make_label(const std::string& prefix);
    std::string escape_string(const std::string& value) const;
    std::string register_string_literal(const SourceLocation& location, const std::string& value);
    std::optional<const FunctionDecl*> find_function(const std::string& name) const;
    std::size_t symbol_index(const std::string& name) const;
    bool data_is_aggregate(const DataEntry& entry) const;
    ObjectFileResult build_object_file(const std::vector<std::uint8_t>& text,
                                       const std::vector<SectionEntry>& data_sections,
                                       const std::vector<RelocationEntry>& relocations,
                                       const std::vector<SymbolEntry>& symbols) const;

    void emit_rex(FunctionBuilder& builder, bool w, std::uint8_t reg, std::uint8_t index, std::uint8_t rm) const;
    void emit_modrm(FunctionBuilder& builder, std::uint8_t mod, std::uint8_t reg, std::uint8_t rm) const;
    void emit_reg_reg(FunctionBuilder& builder, std::uint8_t opcode, Register dst, Register src, bool wide = true) const;
    void emit_mov_reg_reg(FunctionBuilder& builder, Register dst, Register src, bool wide = true) const;
    void emit_reg_imm64(FunctionBuilder& builder, Register reg, std::uint64_t value) const;
    void emit_mov_reg_from_stack(FunctionBuilder& builder, Register reg, std::int32_t displacement) const;
    void emit_mov_stack_from_reg(FunctionBuilder& builder, std::int32_t displacement, Register reg) const;
    void emit_lea_stack(FunctionBuilder& builder, Register reg, std::int32_t displacement) const;
    void emit_mov_reg_from_mem(FunctionBuilder& builder, Register reg, Register base, std::int32_t displacement, std::size_t size) const;
    void emit_mov_mem_from_reg(FunctionBuilder& builder, Register base, std::int32_t displacement, Register reg, std::size_t size) const;
    void emit_movabs_symbol(FunctionBuilder& builder, Register reg, const std::string& symbol) const;
    void emit_test_reg_reg(FunctionBuilder& builder, Register lhs, Register rhs) const;
    void emit_cmp_reg_reg(FunctionBuilder& builder, Register lhs, Register rhs) const;
    void emit_cmp_reg_imm8(FunctionBuilder& builder, Register reg, std::int8_t value) const;
    void emit_xor_reg_reg(FunctionBuilder& builder, Register lhs, Register rhs) const;
    void emit_add_reg_reg(FunctionBuilder& builder, Register lhs, Register rhs) const;
    void emit_sub_reg_reg(FunctionBuilder& builder, Register lhs, Register rhs) const;
    void emit_imul_reg_reg(FunctionBuilder& builder, Register lhs, Register rhs) const;
    void emit_neg_reg(FunctionBuilder& builder, Register reg) const;
    void emit_idiv_reg(FunctionBuilder& builder, Register reg) const;
    void emit_cqo(FunctionBuilder& builder) const;
    void emit_push_reg(FunctionBuilder& builder, Register reg) const;
    void emit_pop_reg(FunctionBuilder& builder, Register reg) const;
    void emit_ret(FunctionBuilder& builder) const;
    void emit_call_symbol(FunctionBuilder& builder, const std::string& symbol) const;
    void emit_jump(FunctionBuilder& builder, const std::string& label, std::uint8_t opcode_prefix, std::uint8_t opcode) const;
    void emit_compare_setcc(FunctionBuilder& builder, const std::string& op) const;
    std::string operand_for_register(std::size_t index) const;
    std::optional<const DataEntry*> find_data(const std::string& name) const;

    const Program& program_;
    const SemanticResult& semantics_;
    DiagnosticSink& diagnostics_;
    std::vector<DataEntry> data_;
    std::vector<StringLiteralEntry> string_literals_;
    std::vector<SymbolEntry> symbols_;
    std::unordered_map<std::string, std::size_t> symbol_indices_;
    std::size_t label_counter_ = 0;
};

} // namespace lunaris