#include "machine_backend.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>

namespace lunaris {

namespace {

constexpr std::uint16_t ELF_ET_REL = 1;
constexpr std::uint16_t ELF_EM_X86_64 = 62;
constexpr std::uint8_t ELF_OSABI_SYSV = 0;
constexpr std::uint8_t ELFCLASS64 = 2;
constexpr std::uint8_t ELFDATA2LSB = 1;

constexpr std::uint32_t SHT_NULL = 0;
constexpr std::uint32_t SHT_PROGBITS = 1;
constexpr std::uint32_t SHT_SYMTAB = 2;
constexpr std::uint32_t SHT_STRTAB = 3;
constexpr std::uint32_t SHT_RELA = 4;

constexpr std::uint64_t SHF_ALLOC = 0x2;
constexpr std::uint64_t SHF_WRITE = 0x1;
constexpr std::uint64_t SHF_EXECINSTR = 0x4;

constexpr std::uint8_t STB_LOCAL = 0;
constexpr std::uint8_t STB_GLOBAL = 1;
constexpr std::uint8_t STT_NOTYPE = 0;
constexpr std::uint8_t STT_OBJECT = 1;
constexpr std::uint8_t STT_FUNC = 2;

constexpr std::uint32_t R_X86_64_PLT32 = 4;
constexpr std::uint32_t R_X86_64_64 = 1;

std::uint64_t elf64_r_info(std::uint32_t symbol_index, std::uint32_t type) {
    return (static_cast<std::uint64_t>(symbol_index) << 32) | type;
}

void append_u8(std::vector<std::uint8_t>& bytes, std::uint8_t value) {
    bytes.push_back(value);
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int index = 0; index < 4; ++index) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8)) & 0xff));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (int index = 0; index < 8; ++index) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (index * 8)) & 0xff));
    }
}

void append_padding(std::vector<std::uint8_t>& bytes, std::size_t alignment) {
    if (alignment == 0) {
        return;
    }
    while (bytes.size() % alignment != 0) {
        bytes.push_back(0);
    }
}

void patch_i32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::int32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xff);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
}

std::uint8_t rex_byte(bool w, std::uint8_t reg, std::uint8_t index, std::uint8_t rm) {
    std::uint8_t rex = 0x40;
    if (w) rex |= 0x08;
    if (reg & 0x08) rex |= 0x04;
    if (index & 0x08) rex |= 0x02;
    if (rm & 0x08) rex |= 0x01;
    return rex;
}

std::uint8_t low3(std::uint8_t value) {
    return static_cast<std::uint8_t>(value & 0x07);
}

} // namespace

MachineBackend::FunctionBuilder::FunctionBuilder() = default;

std::size_t MachineBackend::FunctionBuilder::offset() const {
    return bytes_.size();
}

void MachineBackend::FunctionBuilder::emit_u8(std::uint8_t value) { bytes_.push_back(value); }
void MachineBackend::FunctionBuilder::emit_u16(std::uint16_t value) { append_u16(bytes_, value); }
void MachineBackend::FunctionBuilder::emit_u32(std::uint32_t value) { append_u32(bytes_, value); }
void MachineBackend::FunctionBuilder::emit_u64(std::uint64_t value) { append_u64(bytes_, value); }
void MachineBackend::FunctionBuilder::emit_bytes(const std::vector<std::uint8_t>& values) { bytes_.insert(bytes_.end(), values.begin(), values.end()); }
void MachineBackend::FunctionBuilder::emit_bytes(std::initializer_list<std::uint8_t> values) { bytes_.insert(bytes_.end(), values.begin(), values.end()); }

void MachineBackend::FunctionBuilder::mark_label(const std::string& label) {
    labels_[label] = bytes_.size();
}

void MachineBackend::FunctionBuilder::emit_jump_short_placeholder(const std::string& label, std::uint8_t opcode) {
    (void)label;
    fixups_.push_back(BranchFixup{bytes_.size(), label, false, 0, opcode});
    bytes_.push_back(0);
}

void MachineBackend::FunctionBuilder::emit_jump_near_placeholder(const std::string& label, std::uint8_t opcode_prefix, std::uint8_t opcode) {
    if (opcode_prefix == 0xE9) {
        bytes_.push_back(0xE9);
        fixups_.push_back(BranchFixup{bytes_.size(), label, true, opcode_prefix, opcode});
        append_u32(bytes_, 0);
        return;
    }

    bytes_.push_back(opcode_prefix);
    bytes_.push_back(opcode);
    fixups_.push_back(BranchFixup{bytes_.size(), label, true, opcode_prefix, opcode});
    append_u32(bytes_, 0);
}

void MachineBackend::FunctionBuilder::emit_call_placeholder(const std::string& symbol) {
    bytes_.push_back(0xe8);
    relocations_.push_back(RelocationEntry{bytes_.size(), symbol, R_X86_64_PLT32, -4});
    append_u32(bytes_, 0);
}

void MachineBackend::FunctionBuilder::emit_movabs_placeholder(const std::string& symbol) {
    bytes_.push_back(0x48);
    bytes_.push_back(0xB8);
    relocations_.push_back(RelocationEntry{bytes_.size(), symbol, R_X86_64_64, 0});
    append_u64(bytes_, 0);
}

void MachineBackend::FunctionBuilder::patch_labels() {
    for (const auto& fixup : fixups_) {
        auto label = labels_.find(fixup.label);
        if (label == labels_.end()) {
            continue;
        }
        if (fixup.is_near) {
            const std::int32_t displacement = static_cast<std::int32_t>(label->second) - static_cast<std::int32_t>(fixup.patch_offset + 4);
            patch_i32(bytes_, fixup.patch_offset, displacement);
        } else {
            const std::int8_t displacement = static_cast<std::int8_t>(static_cast<std::int32_t>(label->second) - static_cast<std::int32_t>(fixup.patch_offset + 1));
            bytes_[fixup.patch_offset] = static_cast<std::uint8_t>(displacement);
        }
    }
}

const std::vector<std::uint8_t>& MachineBackend::FunctionBuilder::bytes() const { return bytes_; }
const std::vector<MachineBackend::RelocationEntry>& MachineBackend::FunctionBuilder::relocations() const { return relocations_; }

MachineBackend::MachineBackend(const Program& program, const SemanticResult& semantics, DiagnosticSink& diagnostics)
    : program_(program), semantics_(semantics), diagnostics_(diagnostics) {
    data_.reserve(program_.data.size());
    for (const auto& declaration : program_.data) {
        DataEntry entry;
        entry.location = declaration.location;
        entry.name = declaration.name;
        entry.type = declaration.type;
        entry.section_name = declaration.section_name.value_or(".limine_requests");
        entry.values = declaration.values;
        auto resolved = semantics_.data_layouts.find(declaration.name);
        if (resolved != semantics_.data_layouts.end()) {
            entry.size = resolved->second.size;
        } else {
            entry.size = declaration.values.size() * 8;
        }
        data_.push_back(std::move(entry));
    }

    symbols_.reserve(program_.data.size() + program_.functions.size());
    for (const auto& data : program_.data) {
        SymbolEntry symbol;
        symbol.name = data.name;
        symbol.defined = true;
        auto resolved = semantics_.data_layouts.find(data.name);
        symbol.size = resolved != semantics_.data_layouts.end() ? resolved->second.size : data.values.size() * 8;
        symbol.section_index = 0;
        symbol.is_function = false;
        symbols_.push_back(symbol);
    }
    for (const auto& function : program_.functions) {
        SymbolEntry symbol;
        symbol.name = function.name;
        symbol.defined = !function.external_asm;
        symbol.is_function = true;
        symbols_.push_back(symbol);
    }
    for (std::size_t index = 0; index < symbols_.size(); ++index) {
        symbol_indices_[symbols_[index].name] = index;
    }
}

std::size_t MachineBackend::align_up(std::size_t value, std::size_t alignment) const {
    if (alignment <= 1) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

std::size_t MachineBackend::size_of(const TypeRef& type) const {
    if (type.kind == TypeKind::Pointer) {
        return 8;
    }
    if (type.name == "void") return 0;
    if (type.name == "bool" || type.name == "i8" || type.name == "u8") return 1;
    if (type.name == "i16" || type.name == "u16") return 2;
    if (type.name == "i32" || type.name == "u32") return 4;
    if (type.name == "i64" || type.name == "u64" || type.name == "usize" || type.name == "isize") return 8;
    auto layout = semantics_.struct_layouts.find(type.name);
    if (layout != semantics_.struct_layouts.end()) {
        return layout->second.size;
    }
    return 8;
}

std::size_t MachineBackend::alignment_of(const TypeRef& type) const {
    if (type.kind == TypeKind::Pointer) {
        return 8;
    }
    if (type.name == "void") return 1;
    if (type.name == "bool" || type.name == "i8" || type.name == "u8") return 1;
    if (type.name == "i16" || type.name == "u16") return 2;
    if (type.name == "i32" || type.name == "u32") return 4;
    if (type.name == "i64" || type.name == "u64" || type.name == "usize" || type.name == "isize") return 8;
    auto layout = semantics_.struct_layouts.find(type.name);
    if (layout != semantics_.struct_layouts.end()) {
        return layout->second.alignment;
    }
    return 8;
}

std::optional<const FunctionDecl*> MachineBackend::find_function(const std::string& name) const {
    for (const auto& function : program_.functions) {
        if (function.name == name) {
            return &function;
        }
    }
    return std::nullopt;
}

std::optional<const MachineBackend::DataEntry*> MachineBackend::find_data(const std::string& name) const {
    for (const auto& data : data_) {
        if (data.name == name) {
            return &data;
        }
    }
    return std::nullopt;
}

bool MachineBackend::data_is_aggregate(const DataEntry& entry) const {
    if (entry.type.kind != TypeKind::Named) {
        return false;
    }
    return semantics_.struct_layouts.find(entry.type.name) != semantics_.struct_layouts.end();
}

std::size_t MachineBackend::symbol_index(const std::string& name) const {
    auto found = symbol_indices_.find(name);
    if (found == symbol_indices_.end()) {
        return 0;
    }
    return found->second + 1;
}

std::string MachineBackend::make_label(const std::string& prefix) {
    return prefix + std::to_string(label_counter_++);
}

std::string MachineBackend::register_string_literal(const SourceLocation& location, const std::string& value) {
    StringLiteralEntry entry;
    entry.location = location;
    entry.symbol = make_label(".Lstr_");
    entry.value = value;
    string_literals_.push_back(entry);

    SymbolEntry symbol;
    symbol.name = entry.symbol;
    symbol.defined = true;
    symbol.size = entry.value.size() + 1;
    symbol.section_index = 0;
    symbol.is_function = false;
    symbols_.push_back(symbol);
    symbol_indices_[entry.symbol] = symbols_.size() - 1;

    return entry.symbol;
}

std::string MachineBackend::escape_string(const std::string& value) const {
    std::string output;
    output.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default: output.push_back(ch); break;
        }
    }
    return output;
}

void MachineBackend::emit_rex(FunctionBuilder& builder, bool w, std::uint8_t reg, std::uint8_t index, std::uint8_t rm) const {
    const std::uint8_t rex = rex_byte(w, reg, index, rm);
    if (rex != 0x40) {
        builder.emit_u8(rex);
    }
}

void MachineBackend::emit_modrm(FunctionBuilder& builder, std::uint8_t mod, std::uint8_t reg, std::uint8_t rm) const {
    builder.emit_u8(static_cast<std::uint8_t>((mod << 6) | (low3(reg) << 3) | low3(rm)));
}

void MachineBackend::emit_reg_reg(FunctionBuilder& builder, std::uint8_t opcode, Register dst, Register src, bool wide) const {
    emit_rex(builder, wide, static_cast<std::uint8_t>(src), 0, static_cast<std::uint8_t>(dst));
    builder.emit_u8(opcode);
    emit_modrm(builder, 0b11, static_cast<std::uint8_t>(src), static_cast<std::uint8_t>(dst));
}

void MachineBackend::emit_mov_reg_reg(FunctionBuilder& builder, Register dst, Register src, bool wide) const {
    emit_reg_reg(builder, 0x89, dst, src, wide);
}

void MachineBackend::emit_reg_imm64(FunctionBuilder& builder, Register reg, std::uint64_t value) const {
    emit_rex(builder, true, 0, 0, static_cast<std::uint8_t>(reg));
    builder.emit_u8(static_cast<std::uint8_t>(0xB8 + low3(static_cast<std::uint8_t>(reg))));
    builder.emit_u64(value);
}

void MachineBackend::emit_mov_reg_from_stack(FunctionBuilder& builder, Register reg, std::int32_t displacement) const {
    emit_rex(builder, true, static_cast<std::uint8_t>(reg), 0, static_cast<std::uint8_t>(Register::RBP));
    builder.emit_u8(0x8B);
    emit_modrm(builder, 0b10, static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(Register::RBP));
    builder.emit_u32(static_cast<std::uint32_t>(displacement));
}

void MachineBackend::emit_mov_stack_from_reg(FunctionBuilder& builder, std::int32_t displacement, Register reg) const {
    emit_rex(builder, true, static_cast<std::uint8_t>(reg), 0, static_cast<std::uint8_t>(Register::RBP));
    builder.emit_u8(0x89);
    emit_modrm(builder, 0b10, static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(Register::RBP));
    builder.emit_u32(static_cast<std::uint32_t>(displacement));
}

void MachineBackend::emit_lea_stack(FunctionBuilder& builder, Register reg, std::int32_t displacement) const {
    emit_rex(builder, true, static_cast<std::uint8_t>(reg), 0, static_cast<std::uint8_t>(Register::RBP));
    builder.emit_u8(0x8D);
    emit_modrm(builder, 0b10, static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(Register::RBP));
    builder.emit_u32(static_cast<std::uint32_t>(displacement));
}

void MachineBackend::emit_mov_reg_from_mem(FunctionBuilder& builder, Register reg, Register base, std::int32_t displacement, std::size_t size) const {
    const bool wide = size == 8;
    if (size == 1) {
        emit_rex(builder, false, static_cast<std::uint8_t>(reg), 0, static_cast<std::uint8_t>(base));
        builder.emit_u8(0x0F);
        builder.emit_u8(0xB6);
    } else if (size == 2) {
        emit_rex(builder, false, static_cast<std::uint8_t>(reg), 0, static_cast<std::uint8_t>(base));
        builder.emit_u8(0x0F);
        builder.emit_u8(0xB7);
    } else {
        emit_rex(builder, wide, static_cast<std::uint8_t>(reg), 0, static_cast<std::uint8_t>(base));
        builder.emit_u8(0x8B);
    }

    if (base == Register::RAX && displacement == 0) {
        emit_modrm(builder, 0b00, static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(base));
    } else if (base == Register::RBP || displacement != 0) {
        emit_modrm(builder, 0b10, static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(base));
        builder.emit_u32(static_cast<std::uint32_t>(displacement));
    } else {
        emit_modrm(builder, 0b00, static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(base));
    }
}

void MachineBackend::emit_movabs_symbol(FunctionBuilder& builder, Register reg, const std::string& symbol) const {
    if (reg != Register::RAX) {
        diagnostics_.error({}, "internal error: emit_movabs_symbol currently only supports rax");
        return;
    }
    builder.emit_movabs_placeholder(symbol);
}

void MachineBackend::emit_mov_mem_from_reg(FunctionBuilder& builder, Register base, std::int32_t displacement, Register reg, std::size_t size) const {
    if (size == 1) {
        emit_rex(builder, false, static_cast<std::uint8_t>(reg), 0, static_cast<std::uint8_t>(base));
        builder.emit_u8(0x88);
    } else {
        emit_rex(builder, size == 8, static_cast<std::uint8_t>(reg), 0, static_cast<std::uint8_t>(base));
        builder.emit_u8(0x89);
    }

    if (base == Register::RAX && displacement == 0) {
        emit_modrm(builder, 0b00, static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(base));
    } else {
        emit_modrm(builder, 0b10, static_cast<std::uint8_t>(reg), static_cast<std::uint8_t>(base));
        builder.emit_u32(static_cast<std::uint32_t>(displacement));
    }
}

void MachineBackend::emit_test_reg_reg(FunctionBuilder& builder, Register lhs, Register rhs) const {
    emit_rex(builder, true, static_cast<std::uint8_t>(rhs), 0, static_cast<std::uint8_t>(lhs));
    builder.emit_u8(0x85);
    emit_modrm(builder, 0b11, static_cast<std::uint8_t>(rhs), static_cast<std::uint8_t>(lhs));
}

void MachineBackend::emit_cmp_reg_reg(FunctionBuilder& builder, Register lhs, Register rhs) const {
    emit_rex(builder, true, static_cast<std::uint8_t>(rhs), 0, static_cast<std::uint8_t>(lhs));
    builder.emit_u8(0x39);
    emit_modrm(builder, 0b11, static_cast<std::uint8_t>(rhs), static_cast<std::uint8_t>(lhs));
}

void MachineBackend::emit_cmp_reg_imm8(FunctionBuilder& builder, Register reg, std::int8_t value) const {
    emit_rex(builder, true, 0, 0, static_cast<std::uint8_t>(reg));
    builder.emit_u8(0x83);
    emit_modrm(builder, 0b11, 7, static_cast<std::uint8_t>(reg));
    builder.emit_u8(static_cast<std::uint8_t>(value));
}

void MachineBackend::emit_xor_reg_reg(FunctionBuilder& builder, Register lhs, Register rhs) const {
    emit_rex(builder, true, static_cast<std::uint8_t>(rhs), 0, static_cast<std::uint8_t>(lhs));
    builder.emit_u8(0x31);
    emit_modrm(builder, 0b11, static_cast<std::uint8_t>(rhs), static_cast<std::uint8_t>(lhs));
}

void MachineBackend::emit_add_reg_reg(FunctionBuilder& builder, Register lhs, Register rhs) const {
    emit_rex(builder, true, static_cast<std::uint8_t>(rhs), 0, static_cast<std::uint8_t>(lhs));
    builder.emit_u8(0x01);
    emit_modrm(builder, 0b11, static_cast<std::uint8_t>(rhs), static_cast<std::uint8_t>(lhs));
}

void MachineBackend::emit_sub_reg_reg(FunctionBuilder& builder, Register lhs, Register rhs) const {
    emit_rex(builder, true, static_cast<std::uint8_t>(rhs), 0, static_cast<std::uint8_t>(lhs));
    builder.emit_u8(0x29);
    emit_modrm(builder, 0b11, static_cast<std::uint8_t>(rhs), static_cast<std::uint8_t>(lhs));
}

void MachineBackend::emit_imul_reg_reg(FunctionBuilder& builder, Register lhs, Register rhs) const {
    emit_rex(builder, true, static_cast<std::uint8_t>(lhs), 0, static_cast<std::uint8_t>(rhs));
    builder.emit_u8(0x0F);
    builder.emit_u8(0xAF);
    emit_modrm(builder, 0b11, static_cast<std::uint8_t>(lhs), static_cast<std::uint8_t>(rhs));
}

void MachineBackend::emit_neg_reg(FunctionBuilder& builder, Register reg) const {
    emit_rex(builder, true, 0, 0, static_cast<std::uint8_t>(reg));
    builder.emit_u8(0xF7);
    emit_modrm(builder, 0b11, 3, static_cast<std::uint8_t>(reg));
}

void MachineBackend::emit_idiv_reg(FunctionBuilder& builder, Register reg) const {
    emit_rex(builder, true, 0, 0, static_cast<std::uint8_t>(reg));
    builder.emit_u8(0xF7);
    emit_modrm(builder, 0b11, 7, static_cast<std::uint8_t>(reg));
}

void MachineBackend::emit_cqo(FunctionBuilder& builder) const {
    builder.emit_bytes({0x48, 0x99});
}

void MachineBackend::emit_push_reg(FunctionBuilder& builder, Register reg) const {
    const std::uint8_t base = static_cast<std::uint8_t>(reg);
    if (base >= 8) {
        builder.emit_u8(static_cast<std::uint8_t>(0x41));
    }
    builder.emit_u8(static_cast<std::uint8_t>(0x50 + low3(base)));
}

void MachineBackend::emit_pop_reg(FunctionBuilder& builder, Register reg) const {
    const std::uint8_t base = static_cast<std::uint8_t>(reg);
    if (base >= 8) {
        builder.emit_u8(static_cast<std::uint8_t>(0x41));
    }
    builder.emit_u8(static_cast<std::uint8_t>(0x58 + low3(base)));
}

void MachineBackend::emit_ret(FunctionBuilder& builder) const {
    builder.emit_u8(0xC3);
}

void MachineBackend::emit_call_symbol(FunctionBuilder& builder, const std::string& symbol) const {
    builder.emit_call_placeholder(symbol);
}

void MachineBackend::emit_jump(FunctionBuilder& builder, const std::string& label, std::uint8_t opcode_prefix, std::uint8_t opcode) const {
    builder.emit_jump_near_placeholder(label, opcode_prefix, opcode);
}

void MachineBackend::emit_compare_setcc(FunctionBuilder& builder, const std::string& op) const {
    builder.emit_u8(0x0F);
    if (op == "==") builder.emit_u8(0x94);
    else if (op == "!=" || op == "~=") builder.emit_u8(0x95);
    else if (op == "<") builder.emit_u8(0x9C);
    else if (op == "<=") builder.emit_u8(0x9E);
    else if (op == ">") builder.emit_u8(0x9F);
    else if (op == ">=") builder.emit_u8(0x9D);
    else builder.emit_u8(0x94);
    emit_modrm(builder, 0b11, 0, 0);
}

void MachineBackend::emit_prologue(FunctionBuilder& builder, const FunctionContext& context) {
    builder.emit_u8(0x55);
    builder.emit_bytes({0x48, 0x89, 0xE5});
    if (context.frame_size > 0) {
        builder.emit_bytes({0x48, 0x81, 0xEC});
        builder.emit_u32(static_cast<std::uint32_t>(context.frame_size));
    }
}

void MachineBackend::emit_epilogue(FunctionBuilder& builder) {
    builder.emit_bytes({0x48, 0x89, 0xEC});
    builder.emit_u8(0x5D);
    emit_ret(builder);
}

void MachineBackend::collect_function_layout(FunctionContext& context, const FunctionDecl& declaration) {
    std::size_t offset = 0;

    auto allocate = [&](const std::string& name, const TypeRef& type) {
        const std::size_t alignment = std::min<std::size_t>(8, alignment_of(type));
        offset = align_up(offset, alignment);
        offset += std::max<std::size_t>(1, size_of(type));
        context.variables[name] = VariableSlot{type, offset};
    };

    std::function<void(const std::vector<Statement>&)> scan_body = [&](const std::vector<Statement>& body) {
        for (const auto& statement : body) {
            if (statement.kind == StatementKind::Local) {
                TypeRef type = statement.type.has_value() ? *statement.type : TypeRef::named("i64");
                if (statement.value && !statement.type.has_value()) {
                    auto inferred = infer_expression_type(*statement.value, context, declaration);
                    if (inferred.has_value()) {
                        type = inferred->type;
                    }
                }
                if (context.variables.find(statement.name) == context.variables.end()) {
                    allocate(statement.name, type);
                }
            }
            if (!statement.body.empty()) {
                scan_body(statement.body);
            }
        }
    };

    for (const auto& parameter : declaration.parameters) {
        allocate(parameter.name, parameter.type);
    }

    scan_body(declaration.body);
    context.frame_size = align_up(offset, 16);
}

std::optional<MachineBackend::ExpressionValue> MachineBackend::infer_expression_type(const Expression& expression, const FunctionContext& context, const FunctionDecl& function) const {
    switch (expression.kind) {
    case ExpressionKind::Identifier: {
        if (expression.text == "true" || expression.text == "false") {
            return ExpressionValue{TypeRef::named("bool"), true};
        }
        if (expression.text == "nil") {
            return ExpressionValue{TypeRef::pointer(TypeRef::named("void")), true};
        }
        auto variable = context.variables.find(expression.text);
        if (variable != context.variables.end()) {
            return ExpressionValue{variable->second.type, true};
        }
        auto data = find_data(expression.text);
        if (data.has_value()) {
            if (data_is_aggregate(**data)) {
                return ExpressionValue{TypeRef::pointer((*data)->type), true};
            }
            return ExpressionValue{(*data)->type, true};
        }
        auto callee = find_function(expression.text);
        if (callee.has_value()) {
            if ((*callee)->return_type.has_value()) {
                return ExpressionValue{*(*callee)->return_type, true};
            }
            return ExpressionValue{TypeRef::named("void"), true};
        }
        return std::nullopt;
    }
    case ExpressionKind::Number:
        return ExpressionValue{TypeRef::named("i64"), true};
    case ExpressionKind::String:
        return ExpressionValue{TypeRef::pointer(TypeRef::named("u8")), true};
    case ExpressionKind::Unary:
        if (expression.op == "&" && expression.right) {
            auto value = infer_expression_type(*expression.right, context, function);
            if (value.has_value()) {
                return ExpressionValue{TypeRef::pointer(value->type), true};
            }
        }
        if (expression.op == "*" && expression.right) {
            auto value = infer_expression_type(*expression.right, context, function);
            if (value.has_value() && value->type.kind == TypeKind::Pointer && value->type.element) {
                return ExpressionValue{*value->type.element, true};
            }
        }
        if (expression.right) {
            return infer_expression_type(*expression.right, context, function);
        }
        return std::nullopt;
    case ExpressionKind::Binary:
        if (expression.op == "==" || expression.op == "!=" || expression.op == "~=" || expression.op == "<" || expression.op == "<=" || expression.op == ">" || expression.op == ">=") {
            return ExpressionValue{TypeRef::named("bool"), true};
        }
        if (expression.left) {
            return infer_expression_type(*expression.left, context, function);
        }
        return ExpressionValue{TypeRef::named("i64"), true};
    case ExpressionKind::Call:
        if (expression.callee && expression.callee->kind == ExpressionKind::Identifier) {
            auto callee = find_function(expression.callee->text);
            if (callee.has_value()) {
                if ((*callee)->return_type.has_value()) {
                    return ExpressionValue{*(*callee)->return_type, true};
                }
                return ExpressionValue{TypeRef::named("void"), true};
            }
        }
        return ExpressionValue{TypeRef::named("i64"), true};
    case ExpressionKind::Member:
        if (expression.object) {
            auto object = infer_expression_type(*expression.object, context, function);
            if (object.has_value()) {
                const TypeRef* struct_type = nullptr;
                if (object->type.kind == TypeKind::Pointer && object->type.element) {
                    struct_type = object->type.element.get();
                } else {
                    struct_type = &object->type;
                }
                if (struct_type->kind == TypeKind::Named) {
                    auto layout = semantics_.struct_layouts.find(struct_type->name);
                    if (layout != semantics_.struct_layouts.end()) {
                        for (const auto& field : layout->second.fields) {
                            if (field.name == expression.text) {
                                for (const auto& declaration : program_.structs) {
                                    if (declaration.name == struct_type->name) {
                                        for (const auto& source_field : declaration.fields) {
                                            if (source_field.name == expression.text) {
                                                return ExpressionValue{source_field.type, true};
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        return std::nullopt;
    case ExpressionKind::Index:
        if (expression.object) {
            auto object = infer_expression_type(*expression.object, context, function);
            if (object.has_value() && object->type.kind == TypeKind::Pointer && object->type.element) {
                return ExpressionValue{*object->type.element, true};
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

bool MachineBackend::compile_lvalue_address(FunctionBuilder& builder, const Expression& target, const FunctionContext& context, const FunctionDecl& function) {
    if (target.kind == ExpressionKind::Identifier) {
        auto variable = context.variables.find(target.text);
        if (variable != context.variables.end()) {
            emit_lea_stack(builder, Register::RAX, -static_cast<std::int32_t>(variable->second.offset));
            return true;
        }
        auto data = find_data(target.text);
        if (data.has_value()) {
            emit_movabs_symbol(builder, Register::RAX, target.text);
            return true;
        }
        if (variable == context.variables.end()) {
            diagnostics_.error(target.location, "unknown identifier: " + target.text);
            return false;
        }
    }

    if (target.kind == ExpressionKind::Unary && target.op == "*" && target.right) {
        auto value = compile_expression(builder, *target.right, context, function);
        (void)value;
        return true;
    }

    if (target.kind == ExpressionKind::Member) {
        auto object = infer_expression_type(*target.object, context, function);
        if (!object.has_value()) {
            diagnostics_.error(target.location, "cannot resolve member access type");
            return false;
        }

        if (target.object->kind == ExpressionKind::Identifier) {
            auto variable = context.variables.find(target.object->text);
            if (variable != context.variables.end()) {
                if (variable->second.type.kind == TypeKind::Pointer && variable->second.type.element) {
                    emit_mov_reg_from_stack(builder, Register::RAX, -static_cast<std::int32_t>(variable->second.offset));
                } else {
                    emit_lea_stack(builder, Register::RAX, -static_cast<std::int32_t>(variable->second.offset));
                }
            } else {
                auto data = find_data(target.object->text);
                if (!data.has_value()) {
                    diagnostics_.error(target.location, "unknown identifier: " + target.object->text);
                    return false;
                }
                emit_movabs_symbol(builder, Register::RAX, target.object->text);
            }
        } else {
            compile_expression(builder, *target.object, context, function);
        }

        TypeRef struct_type = object->type.kind == TypeKind::Pointer && object->type.element ? *object->type.element : object->type;
        auto layout = semantics_.struct_layouts.find(struct_type.name);
        if (layout == semantics_.struct_layouts.end()) {
            diagnostics_.error(target.location, "unknown struct type for member access: " + struct_type.name);
            return false;
        }

        std::size_t field_offset = 0;
        for (const auto& field : layout->second.fields) {
            if (field.name == target.text) {
                field_offset = field.offset;
                break;
            }
        }
        emit_reg_imm64(builder, Register::RDX, field_offset);
        emit_add_reg_reg(builder, Register::RAX, Register::RDX);
        return true;
    }

    if (target.kind == ExpressionKind::Index) {
        auto base_type = infer_expression_type(*target.object, context, function);
        if (!base_type.has_value() || base_type->type.kind != TypeKind::Pointer || !base_type->type.element) {
            diagnostics_.error(target.location, "indexing requires a pointer value");
            return false;
        }

        compile_expression(builder, *target.object, context, function);
        emit_push_reg(builder, Register::RAX);
        if (target.right) {
            compile_expression(builder, *target.right, context, function);
        } else {
            emit_xor_reg_reg(builder, Register::RAX, Register::RAX);
        }
        emit_reg_imm64(builder, Register::RDX, size_of(*base_type->type.element));
        emit_imul_reg_reg(builder, Register::RAX, Register::RDX);
        emit_pop_reg(builder, Register::RDX);
        emit_add_reg_reg(builder, Register::RAX, Register::RDX);
        return true;
    }

    diagnostics_.error(target.location, "assignment target is not writable");
    return false;
}

void MachineBackend::emit_store(FunctionBuilder& builder, std::size_t size) {
    if (size == 1) {
        builder.emit_u8(0x88);
        emit_modrm(builder, 0b00, static_cast<std::uint8_t>(Register::RAX), static_cast<std::uint8_t>(Register::RDX));
        return;
    }
    if (size == 2) {
        builder.emit_u8(0x66);
        builder.emit_u8(0x89);
        emit_modrm(builder, 0b00, static_cast<std::uint8_t>(Register::RAX), static_cast<std::uint8_t>(Register::RDX));
        return;
    }
    if (size == 4) {
        builder.emit_u8(0x89);
        emit_modrm(builder, 0b00, static_cast<std::uint8_t>(Register::RAX), static_cast<std::uint8_t>(Register::RDX));
        return;
    } else {
        emit_rex(builder, true, static_cast<std::uint8_t>(Register::RAX), 0, static_cast<std::uint8_t>(Register::RDX));
        builder.emit_u8(0x89);
        emit_modrm(builder, 0b00, static_cast<std::uint8_t>(Register::RAX), static_cast<std::uint8_t>(Register::RDX));
        return;
    }
}

void MachineBackend::emit_load(FunctionBuilder& builder, std::size_t size) {
    if (size == 1) {
        builder.emit_bytes({0x0F, 0xB6});
    } else if (size == 2) {
        builder.emit_bytes({0x0F, 0xB7});
    } else if (size == 4) {
        builder.emit_u8(0x8B);
    } else {
        builder.emit_bytes({0x48, 0x8B});
    }
    emit_modrm(builder, 0b00, static_cast<std::uint8_t>(Register::RAX), static_cast<std::uint8_t>(Register::RAX));
}

MachineBackend::ExpressionValue MachineBackend::compile_expression(FunctionBuilder& builder, const Expression& expression, const FunctionContext& context, const FunctionDecl& function) {
    switch (expression.kind) {
    case ExpressionKind::Identifier: {
        if (expression.text == "true") {
            emit_reg_imm64(builder, Register::RAX, 1);
            return ExpressionValue{TypeRef::named("bool"), true};
        }
        if (expression.text == "false" || expression.text == "nil") {
            emit_xor_reg_reg(builder, Register::RAX, Register::RAX);
            if (expression.text == "nil") {
                return ExpressionValue{TypeRef::pointer(TypeRef::named("void")), true};
            }
            return ExpressionValue{TypeRef::named("bool"), true};
        }
        auto variable = context.variables.find(expression.text);
        if (variable != context.variables.end()) {
            emit_mov_reg_from_stack(builder, Register::RAX, -static_cast<std::int32_t>(variable->second.offset));
            return ExpressionValue{variable->second.type, true};
        }
        auto data = find_data(expression.text);
        if (data.has_value()) {
            emit_movabs_symbol(builder, Register::RAX, expression.text);
            if (data_is_aggregate(**data)) {
                return ExpressionValue{TypeRef::pointer((*data)->type), true};
            }
            emit_mov_reg_from_mem(builder, Register::RAX, Register::RAX, 0, size_of((*data)->type));
            return ExpressionValue{(*data)->type, true};
        }
        diagnostics_.error(expression.location, "unknown identifier: " + expression.text);
        emit_xor_reg_reg(builder, Register::RAX, Register::RAX);
        return ExpressionValue{TypeRef::named("i64"), false};
    }
    case ExpressionKind::Number: {
        std::uint64_t value = 0;
        try {
            value = static_cast<std::uint64_t>(std::stoll(expression.text, nullptr, 0));
        } catch (...) {
            diagnostics_.error(expression.location, "invalid numeric literal");
        }
        emit_reg_imm64(builder, Register::RAX, value);
        return ExpressionValue{TypeRef::named("i64"), true};
    }
    case ExpressionKind::String:
        emit_movabs_symbol(builder, Register::RAX, register_string_literal(expression.location, expression.text));
        return ExpressionValue{TypeRef::pointer(TypeRef::named("u8")), true};
    case ExpressionKind::Unary:
        if (expression.op == "&" && expression.right) {
            if (!compile_lvalue_address(builder, *expression.right, context, function)) {
                return ExpressionValue{TypeRef::named("i64"), false};
            }
            auto inferred = infer_expression_type(*expression.right, context, function);
            if (inferred.has_value()) {
                return ExpressionValue{TypeRef::pointer(inferred->type), true};
            }
            return ExpressionValue{TypeRef::pointer(TypeRef::named("i64")), true};
        }
        if (expression.op == "*" && expression.right) {
            auto value = compile_expression(builder, *expression.right, context, function);
            if (!value.valid) {
                return value;
            }
            if (value.type.kind == TypeKind::Pointer && value.type.element) {
                emit_mov_reg_from_mem(builder, Register::RAX, Register::RAX, 0, size_of(*value.type.element));
                return ExpressionValue{*value.type.element, true};
            }
            emit_mov_reg_from_mem(builder, Register::RAX, Register::RAX, 0, 8);
            return ExpressionValue{TypeRef::named("i64"), true};
        }
        if (expression.op == "-" && expression.right) {
            auto value = compile_expression(builder, *expression.right, context, function);
            if (value.valid) {
                emit_neg_reg(builder, Register::RAX);
            }
            return value;
        }
        break;
    case ExpressionKind::Binary:
        if (expression.left && expression.right) {
            if (expression.op == "==" || expression.op == "!=" || expression.op == "~=" || expression.op == "<" || expression.op == "<=" || expression.op == ">" || expression.op == ">=") {
                compile_expression(builder, *expression.left, context, function);
                emit_push_reg(builder, Register::RAX);
                compile_expression(builder, *expression.right, context, function);
                emit_mov_reg_reg(builder, Register::RDX, Register::RAX, true);
                emit_pop_reg(builder, Register::RAX);
                emit_cmp_reg_reg(builder, Register::RAX, Register::RDX);
                if (expression.op == "==") builder.emit_bytes({0x0F, 0x94, 0xC0});
                else if (expression.op == "!=" || expression.op == "~=") builder.emit_bytes({0x0F, 0x95, 0xC0});
                else if (expression.op == "<") builder.emit_bytes({0x0F, 0x9C, 0xC0});
                else if (expression.op == "<=") builder.emit_bytes({0x0F, 0x9E, 0xC0});
                else if (expression.op == ">") builder.emit_bytes({0x0F, 0x9F, 0xC0});
                else if (expression.op == ">=") builder.emit_bytes({0x0F, 0x9D, 0xC0});
                builder.emit_bytes({0x0F, 0xB6, 0xC0});
                return ExpressionValue{TypeRef::named("bool"), true};
            }

            auto left = compile_expression(builder, *expression.left, context, function);
            emit_push_reg(builder, Register::RAX);
            auto right = compile_expression(builder, *expression.right, context, function);
            if (expression.op == "/") {
                emit_mov_reg_reg(builder, Register::RCX, Register::RAX, true);
                emit_pop_reg(builder, Register::RAX);
                emit_cqo(builder);
                emit_idiv_reg(builder, Register::RCX);
            } else {
                emit_mov_reg_reg(builder, Register::RDX, Register::RAX, true);
                emit_pop_reg(builder, Register::RAX);

                if (expression.op == "+") {
                    emit_add_reg_reg(builder, Register::RAX, Register::RDX);
                } else if (expression.op == "-") {
                    emit_sub_reg_reg(builder, Register::RAX, Register::RDX);
                } else if (expression.op == "*") {
                    emit_imul_reg_reg(builder, Register::RAX, Register::RDX);
                }
            }

            return left.valid ? left : right;
        }
        break;
    case ExpressionKind::Call:
        if (expression.callee && expression.callee->kind == ExpressionKind::Identifier) {
            if (expression.arguments.size() > 6) {
                diagnostics_.error(expression.location, "calls currently support at most 6 arguments");
                return ExpressionValue{TypeRef::named("i64"), false};
            }

            for (const auto& argument : expression.arguments) {
                compile_expression(builder, argument, context, function);
                emit_push_reg(builder, Register::RAX);
            }

            static const Register argument_registers[] = {Register::RDI, Register::RSI, Register::RDX, Register::RCX, Register::R8, Register::R9};
            for (std::size_t index = expression.arguments.size(); index > 0; --index) {
                emit_pop_reg(builder, argument_registers[index - 1]);
            }

            emit_call_symbol(builder, expression.callee->text);

            auto callee = find_function(expression.callee->text);
            if (callee.has_value() && (*callee)->return_type.has_value()) {
                return ExpressionValue{*(*callee)->return_type, true};
            }
            return ExpressionValue{TypeRef::named("i64"), true};
        }
        diagnostics_.error(expression.location, "only direct function calls are supported yet");
        emit_xor_reg_reg(builder, Register::RAX, Register::RAX);
        return ExpressionValue{TypeRef::named("i64"), false};
    case ExpressionKind::Member: {
        auto object_type = infer_expression_type(*expression.object, context, function);
        if (!object_type.has_value()) {
            diagnostics_.error(expression.location, "cannot resolve member access type");
            emit_xor_reg_reg(builder, Register::RAX, Register::RAX);
            return ExpressionValue{TypeRef::named("i64"), false};
        }

        if (expression.object->kind == ExpressionKind::Identifier) {
            auto variable = context.variables.find(expression.object->text);
            if (variable != context.variables.end()) {
                if (variable->second.type.kind == TypeKind::Pointer && variable->second.type.element) {
                    emit_mov_reg_from_stack(builder, Register::RAX, -static_cast<std::int32_t>(variable->second.offset));
                } else {
                    emit_lea_stack(builder, Register::RAX, -static_cast<std::int32_t>(variable->second.offset));
                }
            } else {
                auto data = find_data(expression.object->text);
                if (!data.has_value()) {
                    diagnostics_.error(expression.location, "unknown identifier: " + expression.object->text);
                    emit_xor_reg_reg(builder, Register::RAX, Register::RAX);
                    return ExpressionValue{TypeRef::named("i64"), false};
                }
                emit_movabs_symbol(builder, Register::RAX, expression.object->text);
            }
        } else {
            compile_expression(builder, *expression.object, context, function);
        }

        TypeRef struct_type = object_type->type.kind == TypeKind::Pointer && object_type->type.element ? *object_type->type.element : object_type->type;
        auto layout = semantics_.struct_layouts.find(struct_type.name);
        if (layout == semantics_.struct_layouts.end()) {
            diagnostics_.error(expression.location, "unknown struct type for member access: " + struct_type.name);
            emit_xor_reg_reg(builder, Register::RAX, Register::RAX);
            return ExpressionValue{TypeRef::named("i64"), false};
        }

        std::size_t field_offset = 0;
        std::optional<TypeRef> field_type;
        for (const auto& field : layout->second.fields) {
            if (field.name == expression.text) {
                field_offset = field.offset;
                break;
            }
        }
        for (const auto& declaration : program_.structs) {
            if (declaration.name == struct_type.name) {
                for (const auto& field : declaration.fields) {
                    if (field.name == expression.text) {
                        field_type = field.type;
                        break;
                    }
                }
            }
        }

        emit_reg_imm64(builder, Register::RDX, field_offset);
        emit_add_reg_reg(builder, Register::RAX, Register::RDX);
        emit_mov_reg_from_mem(builder, Register::RAX, Register::RAX, 0, field_type.has_value() ? size_of(*field_type) : 8);
        return ExpressionValue{field_type.value_or(TypeRef::named("i64")), true};
    }
    case ExpressionKind::Index: {
        auto base_type = infer_expression_type(*expression.object, context, function);
        if (!base_type.has_value() || base_type->type.kind != TypeKind::Pointer || !base_type->type.element) {
            diagnostics_.error(expression.location, "indexing requires a pointer value");
            emit_xor_reg_reg(builder, Register::RAX, Register::RAX);
            return ExpressionValue{TypeRef::named("i64"), false};
        }

        compile_expression(builder, *expression.object, context, function);
        emit_push_reg(builder, Register::RAX);
        if (expression.right) {
            compile_expression(builder, *expression.right, context, function);
        } else {
            emit_xor_reg_reg(builder, Register::RAX, Register::RAX);
        }
        emit_reg_imm64(builder, Register::RDX, size_of(*base_type->type.element));
        emit_imul_reg_reg(builder, Register::RAX, Register::RDX);
        emit_pop_reg(builder, Register::RDX);
        emit_add_reg_reg(builder, Register::RAX, Register::RDX);
        emit_mov_reg_from_mem(builder, Register::RAX, Register::RAX, 0, size_of(*base_type->type.element));
        return ExpressionValue{*base_type->type.element, true};
    }
    }

    diagnostics_.error(expression.location, "unsupported expression form");
    emit_xor_reg_reg(builder, Register::RAX, Register::RAX);
    return ExpressionValue{TypeRef::named("i64"), false};
}

bool MachineBackend::compile_statement(FunctionBuilder& builder, const Statement& statement, FunctionContext& context, const FunctionDecl& function) {
    switch (statement.kind) {
    case StatementKind::Local: {
        auto slot = context.variables.find(statement.name);
        if (slot == context.variables.end()) {
            diagnostics_.error(statement.location, "missing stack slot for local: " + statement.name);
            return false;
        }
        if (statement.value) {
            auto value = compile_expression(builder, *statement.value, context, function);
            (void)value;
            emit_mov_stack_from_reg(builder, -static_cast<std::int32_t>(slot->second.offset), Register::RAX);
        }
        return false;
    }
    case StatementKind::Return: {
        if (statement.expression) {
            compile_expression(builder, *statement.expression, context, function);
        } else {
            emit_xor_reg_reg(builder, Register::RAX, Register::RAX);
        }
        emit_epilogue(builder);
        return true;
    }
    case StatementKind::Assignment: {
        if (statement.target && statement.value) {
            if (!compile_lvalue_address(builder, *statement.target, context, function)) {
                return false;
            }
            emit_push_reg(builder, Register::RAX);
            compile_expression(builder, *statement.value, context, function);
            emit_pop_reg(builder, Register::RDX);

            auto target_type = infer_expression_type(*statement.target, context, function);
            emit_store(builder, target_type.has_value() ? size_of(target_type->type) : 8);
        }
        return false;
    }
    case StatementKind::Expression:
        if (statement.expression) {
            compile_expression(builder, *statement.expression, context, function);
        }
        return false;
    case StatementKind::If: {
        const std::string false_label = make_label(".Lif_false_");
        const std::string end_label = make_label(".Lif_end_");
        if (statement.condition) {
            compile_expression(builder, *statement.condition, context, function);
            emit_cmp_reg_imm8(builder, Register::RAX, 0);
            builder.emit_jump_near_placeholder(false_label, 0x0F, 0x84);
        }
        compile_block(builder, statement.body, context, function);
        builder.emit_jump_near_placeholder(end_label, 0xE9, 0x00);
        builder.mark_label(false_label);
        builder.mark_label(end_label);
        return false;
    }
    case StatementKind::While: {
        const std::string start_label = make_label(".Lwhile_start_");
        const std::string end_label = make_label(".Lwhile_end_");
        builder.mark_label(start_label);
        if (statement.condition) {
            compile_expression(builder, *statement.condition, context, function);
            emit_cmp_reg_imm8(builder, Register::RAX, 0);
            builder.emit_jump_near_placeholder(end_label, 0x0F, 0x84);
        }
        compile_block(builder, statement.body, context, function);
        builder.emit_jump_near_placeholder(start_label, 0xE9, 0x00);
        builder.mark_label(end_label);
        return false;
    }
    }

    return false;
}

void MachineBackend::compile_block(FunctionBuilder& builder, const std::vector<Statement>& body, FunctionContext& context, const FunctionDecl& function) {
    for (const auto& statement : body) {
        if (compile_statement(builder, statement, context, function)) {
            return;
        }
    }
}

void MachineBackend::emit_return(FunctionBuilder& builder, const FunctionContext& context, const FunctionDecl& function, const Statement* statement) {
    (void)context;
    (void)function;
    (void)statement;
    emit_epilogue(builder);
}

void MachineBackend::emit_function(FunctionBuilder& builder, const FunctionDecl& declaration) {
    if (declaration.external_asm) {
        return;
    }

    FunctionContext context;
    collect_function_layout(context, declaration);

    emit_prologue(builder, context);

    static const Register argument_registers[] = {Register::RDI, Register::RSI, Register::RDX, Register::RCX, Register::R8, Register::R9};
    for (std::size_t index = 0; index < declaration.parameters.size(); ++index) {
        auto slot = context.variables.find(declaration.parameters[index].name);
        if (slot == context.variables.end()) {
            continue;
        }

        if (index < 6) {
            emit_mov_stack_from_reg(builder, -static_cast<std::int32_t>(slot->second.offset), argument_registers[index]);
        } else {
            emit_mov_reg_from_stack(builder, Register::RAX, 16 + static_cast<std::int32_t>(8 * (index - 6)));
            emit_mov_stack_from_reg(builder, -static_cast<std::int32_t>(slot->second.offset), Register::RAX);
        }
    }

    bool saw_explicit_return = false;
    for (const auto& statement : declaration.body) {
        if (compile_statement(builder, statement, context, declaration)) {
            saw_explicit_return = true;
            break;
        }
    }

    if (!saw_explicit_return) {
        if (!declaration.return_type.has_value() || declaration.return_type->name == "void") {
            emit_epilogue(builder);
        } else {
            emit_xor_reg_reg(builder, Register::RAX, Register::RAX);
            emit_epilogue(builder);
        }
    }
}

ObjectFileResult MachineBackend::build_object_file(const std::vector<std::uint8_t>& text,
                                                   const std::vector<SectionEntry>& data_sections,
                                                   const std::vector<RelocationEntry>& relocations,
                                                   const std::vector<SymbolEntry>& symbols) const {
    std::vector<std::uint8_t> strtab(1, 0);
    std::vector<std::uint32_t> symbol_name_offsets(symbols.size() + 1, 0);
    for (std::size_t index = 0; index < symbols.size(); ++index) {
        symbol_name_offsets[index + 1] = static_cast<std::uint32_t>(strtab.size());
        strtab.insert(strtab.end(), symbols[index].name.begin(), symbols[index].name.end());
        strtab.push_back(0);
    }

    std::vector<std::uint8_t> shstrtab(1, 0);
    auto append_sh_name = [&](const std::string& name) -> std::uint32_t {
        const std::uint32_t offset = static_cast<std::uint32_t>(shstrtab.size());
        shstrtab.insert(shstrtab.end(), name.begin(), name.end());
        shstrtab.push_back(0);
        return offset;
    };

    const std::uint32_t sh_name_text = append_sh_name(".text");
    std::vector<std::uint32_t> sh_name_data_sections;
    sh_name_data_sections.reserve(data_sections.size());
    for (const auto& section : data_sections) {
        sh_name_data_sections.push_back(append_sh_name(section.name));
    }
    const std::uint32_t sh_name_rela_text = append_sh_name(".rela.text");
    const std::uint32_t sh_name_symtab = append_sh_name(".symtab");
    const std::uint32_t sh_name_strtab = append_sh_name(".strtab");
    const std::uint32_t sh_name_shstrtab = append_sh_name(".shstrtab");

    std::vector<std::uint8_t> symtab;
    symtab.reserve((symbols.size() + 1) * 24);
    append_u32(symtab, 0);
    symtab.push_back(0);
    symtab.push_back(0);
    append_u16(symtab, 0);
    append_u64(symtab, 0);
    append_u64(symtab, 0);

    for (const auto& symbol : symbols) {
        append_u32(symtab, symbol_name_offsets[symbol_index(symbol.name)]);
        symtab.push_back(static_cast<std::uint8_t>((STB_GLOBAL << 4) | (symbol.is_function ? STT_FUNC : STT_OBJECT)));
        symtab.push_back(0);
        append_u16(symtab, static_cast<std::uint16_t>(symbol.defined ? symbol.section_index : 0));
        append_u64(symtab, symbol.value);
        append_u64(symtab, symbol.size);
    }

    std::vector<std::uint8_t> rela_text;
    rela_text.reserve(relocations.size() * 24);
    for (const auto& relocation : relocations) {
        append_u64(rela_text, relocation.offset);
        append_u64(rela_text, elf64_r_info(static_cast<std::uint32_t>(symbol_index(relocation.symbol)), relocation.type));
        append_u64(rela_text, static_cast<std::uint64_t>(relocation.addend));
    }

    auto align_offset = [](std::size_t value, std::size_t alignment) {
        if (alignment <= 1) return value;
        const std::size_t remainder = value % alignment;
        return remainder == 0 ? value : value + (alignment - remainder);
    };

    const std::size_t elf_header_size = 64;
    std::size_t offset = elf_header_size;

    offset = align_offset(offset, 16);
    const std::size_t text_offset = offset;
    offset += text.size();

    std::vector<std::size_t> data_offsets;
    data_offsets.reserve(data_sections.size());
    for (const auto& section : data_sections) {
        offset = align_offset(offset, 8);
        data_offsets.push_back(offset);
        offset += section.bytes.size();
    }

    offset = align_offset(offset, 8);
    const std::size_t rela_offset = offset;
    offset += rela_text.size();

    offset = align_offset(offset, 8);
    const std::size_t symtab_offset = offset;
    offset += symtab.size();

    offset = align_offset(offset, 1);
    const std::size_t strtab_offset = offset;
    offset += strtab.size();

    offset = align_offset(offset, 1);
    const std::size_t shstrtab_offset = offset;
    offset += shstrtab.size();

    offset = align_offset(offset, 8);
    const std::size_t shoff = offset;
    const std::size_t shnum = 6 + data_sections.size();

    std::vector<std::uint8_t> bytes;
    bytes.reserve(shoff + shnum * 64);

    // ELF header
    bytes.insert(bytes.end(), {0x7F, 'E', 'L', 'F', ELFCLASS64, ELFDATA2LSB, 1, ELF_OSABI_SYSV, 0});
    while (bytes.size() < 16) {
        bytes.push_back(0);
    }
    append_u16(bytes, ELF_ET_REL);
    append_u16(bytes, ELF_EM_X86_64);
    append_u32(bytes, 1);
    append_u64(bytes, 0);
    append_u64(bytes, 0);
    append_u64(bytes, shoff);
    append_u32(bytes, 0);
    append_u16(bytes, 64);
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u16(bytes, 64);
    append_u16(bytes, static_cast<std::uint16_t>(shnum));
    append_u16(bytes, static_cast<std::uint16_t>(shnum - 1));

    append_padding(bytes, 16);
    bytes.insert(bytes.end(), text.begin(), text.end());
    for (const auto& section : data_sections) {
        append_padding(bytes, 8);
        bytes.insert(bytes.end(), section.bytes.begin(), section.bytes.end());
    }
    append_padding(bytes, 8);
    bytes.insert(bytes.end(), rela_text.begin(), rela_text.end());
    append_padding(bytes, 8);
    bytes.insert(bytes.end(), symtab.begin(), symtab.end());
    append_padding(bytes, 1);
    bytes.insert(bytes.end(), strtab.begin(), strtab.end());
    append_padding(bytes, 1);
    bytes.insert(bytes.end(), shstrtab.begin(), shstrtab.end());
    append_padding(bytes, 8);

    auto append_shdr = [&](std::uint32_t name, std::uint32_t type, std::uint64_t flags, std::uint64_t addr, std::uint64_t section_offset, std::uint64_t size, std::uint32_t link, std::uint32_t info, std::uint64_t addralign, std::uint64_t entsize) {
        append_u32(bytes, name);
        append_u32(bytes, type);
        append_u64(bytes, flags);
        append_u64(bytes, addr);
        append_u64(bytes, section_offset);
        append_u64(bytes, size);
        append_u32(bytes, link);
        append_u32(bytes, info);
        append_u64(bytes, addralign);
        append_u64(bytes, entsize);
    };

    // Null section
    append_shdr(0, SHT_NULL, 0, 0, 0, 0, 0, 0, 0, 0);
    append_shdr(sh_name_text, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0, text_offset, text.size(), 0, 0, 16, 0);
    for (std::size_t index = 0; index < data_sections.size(); ++index) {
        append_shdr(sh_name_data_sections[index], SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0, data_offsets[index], data_sections[index].bytes.size(), 0, 0, 8, 0);
    }
    const std::uint32_t symtab_section_index = static_cast<std::uint32_t>(3 + data_sections.size());
    const std::uint32_t strtab_section_index = symtab_section_index + 1;
    append_shdr(sh_name_rela_text, SHT_RELA, 0, 0, rela_offset, rela_text.size(), symtab_section_index, 1, 8, 24);
    append_shdr(sh_name_symtab, SHT_SYMTAB, 0, 0, symtab_offset, symtab.size(), strtab_section_index, 1, 8, 24);
    append_shdr(sh_name_strtab, SHT_STRTAB, 0, 0, strtab_offset, strtab.size(), 0, 0, 1, 0);
    append_shdr(sh_name_shstrtab, SHT_STRTAB, 0, 0, shstrtab_offset, shstrtab.size(), 0, 0, 1, 0);

    return ObjectFileResult{true, std::move(bytes), {}};
}

ObjectFileResult MachineBackend::generate() {
    std::vector<std::uint8_t> text;
    std::vector<SectionEntry> data_sections;
    std::unordered_map<std::string, std::size_t> section_indices;
    std::vector<RelocationEntry> relocations;

    auto ensure_section = [&](const std::string& name) -> SectionEntry& {
        auto found = section_indices.find(name);
        if (found == section_indices.end()) {
            SectionEntry section;
            section.name = name;
            section.index = 2 + data_sections.size();
            data_sections.push_back(std::move(section));
            found = section_indices.emplace(name, data_sections.size() - 1).first;
        }
        return data_sections[found->second];
    };

    for (const auto& data_entry : data_) {
        auto& symbol = symbols_[symbol_index(data_entry.name) - 1];
        symbol.defined = true;
        auto& section = ensure_section(data_entry.section_name);
        symbol.section_index = section.index;
        symbol.value = section.bytes.size();
        symbol.size = data_entry.size;

        append_padding(section.bytes, 8);
        symbol.value = section.bytes.size();
        for (const auto& value : data_entry.values) {
            try {
                append_u64(section.bytes, static_cast<std::uint64_t>(std::stoull(value, nullptr, 0)));
            } catch (...) {
                diagnostics_.error(data_entry.location, "invalid data initializer: " + value);
                append_u64(section.bytes, 0);
            }
        }
    }

    for (const auto& function : program_.functions) {
        if (function.external_asm) {
            continue;
        }

        FunctionBuilder builder;
        FunctionContext context;
        collect_function_layout(context, function);
        emit_function(builder, function);
        builder.patch_labels();

        symbols_[symbol_index(function.name) - 1].defined = true;
        symbols_[symbol_index(function.name) - 1].value = text.size();
        symbols_[symbol_index(function.name) - 1].size = builder.bytes().size();
        symbols_[symbol_index(function.name) - 1].section_index = 1;

        for (const auto& relocation : builder.relocations()) {
            RelocationEntry adjusted = relocation;
            adjusted.offset += text.size();
            relocations.push_back(std::move(adjusted));
        }
        text.insert(text.end(), builder.bytes().begin(), builder.bytes().end());
    }

    for (const auto& string_literal : string_literals_) {
        auto& symbol = symbols_[symbol_index(string_literal.symbol) - 1];
        symbol.defined = true;
        auto& section = ensure_section(".rodata");
        symbol.section_index = section.index;
        symbol.value = section.bytes.size();
        symbol.size = string_literal.value.size() + 1;
        section.bytes.insert(section.bytes.end(), string_literal.value.begin(), string_literal.value.end());
        section.bytes.push_back(0);
    }

    ObjectFileResult result = build_object_file(text, data_sections, relocations, symbols_);
    result.ok = result.ok && !diagnostics_.has_errors();
    result.diagnostics = diagnostics_.diagnostics();
    return result;
}

} // namespace lunaris