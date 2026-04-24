#include "codegen.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <sstream>

namespace lunaris {

CodeGenerator::CodeGenerator(const Program& program, const SemanticResult& semantics, DiagnosticSink& diagnostics)
    : program_(program), semantics_(semantics), diagnostics_(diagnostics) {}

std::size_t CodeGenerator::align_up(std::size_t value, std::size_t alignment) const {
    if (alignment <= 1) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return value + (alignment - remainder);
}

std::size_t CodeGenerator::size_of(const TypeRef& type) const {
    if (type.kind == TypeKind::Pointer) {
        return 8;
    }

    if (type.name == "void") {
        return 0;
    }
    if (type.name == "bool" || type.name == "i8" || type.name == "u8") {
        return 1;
    }
    if (type.name == "i16" || type.name == "u16") {
        return 2;
    }
    if (type.name == "i32" || type.name == "u32") {
        return 4;
    }
    if (type.name == "i64" || type.name == "u64" || type.name == "usize" || type.name == "isize") {
        return 8;
    }

    auto layout = semantics_.struct_layouts.find(type.name);
    if (layout != semantics_.struct_layouts.end()) {
        return layout->second.size;
    }

    return 8;
}

std::size_t CodeGenerator::alignment_of(const TypeRef& type) const {
    if (type.kind == TypeKind::Pointer) {
        return 8;
    }

    if (type.name == "void") {
        return 1;
    }
    if (type.name == "bool" || type.name == "i8" || type.name == "u8") {
        return 1;
    }
    if (type.name == "i16" || type.name == "u16") {
        return 2;
    }
    if (type.name == "i32" || type.name == "u32") {
        return 4;
    }
    if (type.name == "i64" || type.name == "u64" || type.name == "usize" || type.name == "isize") {
        return 8;
    }

    auto layout = semantics_.struct_layouts.find(type.name);
    if (layout != semantics_.struct_layouts.end()) {
        return layout->second.alignment;
    }

    return 8;
}

bool CodeGenerator::is_pointer_like(const TypeRef& type) const {
    return type.kind == TypeKind::Pointer;
}

std::string CodeGenerator::escape_string(const std::string& value) const {
    std::ostringstream output;
    for (char ch : value) {
        switch (ch) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            output << ch;
            break;
        }
    }
    return output.str();
}

std::string CodeGenerator::label_for_string(const std::string& value) {
    auto existing = string_literals_.find(value);
    if (existing != string_literals_.end()) {
        return ".LC" + std::to_string(existing->second);
    }

    std::size_t index = string_literals_.size();
    string_literals_[value] = index;
    string_table_.emplace_back(".LC" + std::to_string(index), value);
    return ".LC" + std::to_string(index);
}

std::optional<const FunctionDecl*> CodeGenerator::find_function(const std::string& name) const {
    for (const auto& function : program_.functions) {
        if (function.name == name) {
            return &function;
        }
    }
    return std::nullopt;
}

std::optional<const DataDecl*> CodeGenerator::find_data(const std::string& name) const {
    for (const auto& declaration : program_.data) {
        if (declaration.name == name) {
            return &declaration;
        }
    }
    return std::nullopt;
}

void CodeGenerator::emit_line(const std::string& line) {
    assembly_ << line << '\n';
}

void CodeGenerator::emit_store(std::size_t size) {
    switch (size) {
    case 1: emit_line("    mov BYTE PTR [rax], dl"); break;
    case 2: emit_line("    mov WORD PTR [rax], dx"); break;
    case 4: emit_line("    mov DWORD PTR [rax], edx"); break;
    default: emit_line("    mov QWORD PTR [rax], rdx"); break;
    }
}

void CodeGenerator::emit_load(std::size_t size) {
    switch (size) {
    case 1: emit_line("    movzx eax, BYTE PTR [rax]"); break;
    case 2: emit_line("    movzx eax, WORD PTR [rax]"); break;
    case 4: emit_line("    mov eax, DWORD PTR [rax]"); break;
    default: emit_line("    mov rax, QWORD PTR [rax]"); break;
    }
}

void CodeGenerator::emit_data(const DataDecl& declaration) {
    const std::string section_name = declaration.section_name.value_or(".limine_requests");
    emit_line(".section " + section_name + ",\"aw\",@progbits");
    emit_line(declaration.name + ":");
    for (const auto& value : declaration.values) {
        emit_line("    .quad " + value);
    }
}

std::string CodeGenerator::operand_for_register(std::size_t index) const {
    static const char* registers[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    return registers[index];
}

void CodeGenerator::collect_function_layout(FunctionContext& context, const FunctionDecl& declaration) {
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

std::optional<CodeGenerator::ExpressionValue> CodeGenerator::infer_expression_type(const Expression& expression, const FunctionContext& context, const FunctionDecl& function) {
    switch (expression.kind) {
    case ExpressionKind::Identifier: {
        auto variable = context.variables.find(expression.text);
        if (variable != context.variables.end()) {
            return ExpressionValue{variable->second.type, true};
        }
        auto data = find_data(expression.text);
        if (data.has_value()) {
            return ExpressionValue{TypeRef::pointer((*data)->type), true};
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
        return std::nullopt;
    case ExpressionKind::Binary:
        if (expression.op == "==" || expression.op == "!=" || expression.op == "~=" || expression.op == "<" || expression.op == "<=" || expression.op == ">" || expression.op == ">=") {
            return ExpressionValue{TypeRef::named("i64"), true};
        }
        if (expression.left) {
            return infer_expression_type(*expression.left, context, function);
        }
        return std::nullopt;
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
            if (object.has_value() && object->type.kind == TypeKind::Named) {
                auto layout = semantics_.struct_layouts.find(object->type.name);
                if (layout != semantics_.struct_layouts.end()) {
                    for (const auto& field : layout->second.fields) {
                        if (field.name == expression.text) {
                            for (const auto& declaration : program_.structs) {
                                if (declaration.name == object->type.name) {
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
            if (object.has_value() && object->type.kind == TypeKind::Pointer && object->type.element && object->type.element->kind == TypeKind::Named) {
                auto layout = semantics_.struct_layouts.find(object->type.element->name);
                if (layout != semantics_.struct_layouts.end()) {
                    for (const auto& declaration : program_.structs) {
                        if (declaration.name == object->type.element->name) {
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

CodeGenerator::ExpressionValue CodeGenerator::compile_expression(const Expression& expression, const FunctionContext& context, const FunctionDecl& function) {
    switch (expression.kind) {
    case ExpressionKind::Identifier: {
        auto variable = context.variables.find(expression.text);
        if (variable == context.variables.end()) {
            auto data = find_data(expression.text);
            if (!data.has_value()) {
                diagnostics_.error(expression.location, "unknown identifier: " + expression.text);
                return ExpressionValue{TypeRef::named("i64"), false};
            }
            emit_line("    lea rax, [rip + " + (*data)->name + "]");
            return ExpressionValue{TypeRef::pointer((*data)->type), true};
        }
        emit_line("    mov rax, QWORD PTR [rbp - " + std::to_string(variable->second.offset) + "]");
        return ExpressionValue{variable->second.type, true};
    }
    case ExpressionKind::Number:
        emit_line("    mov rax, " + expression.text);
        return ExpressionValue{TypeRef::named("i64"), true};
    case ExpressionKind::String: {
        const std::string label = label_for_string(expression.text);
        emit_line("    lea rax, [rip + " + label + "]");
        return ExpressionValue{TypeRef::pointer(TypeRef::named("u8")), true};
    }
    case ExpressionKind::Unary:
        if (expression.op == "&" && expression.right) {
            if (expression.right->kind == ExpressionKind::Identifier) {
                auto variable = context.variables.find(expression.right->text);
                if (variable == context.variables.end()) {
                    auto data = find_data(expression.right->text);
                    if (!data.has_value()) {
                        diagnostics_.error(expression.location, "unknown identifier: " + expression.right->text);
                        return ExpressionValue{TypeRef::named("i64"), false};
                    }
                    emit_line("    lea rax, [rip + " + (*data)->name + "]");
                    return ExpressionValue{TypeRef::pointer((*data)->type), true};
                }
                emit_line("    lea rax, [rbp - " + std::to_string(variable->second.offset) + "]");
                return ExpressionValue{TypeRef::pointer(variable->second.type), true};
            }
            if (expression.right->kind == ExpressionKind::Member || expression.right->kind == ExpressionKind::Index || (expression.right->kind == ExpressionKind::Unary && expression.right->op == "*")) {
                compile_expression(*expression.right, context, function);
                return ExpressionValue{TypeRef::pointer(TypeRef::named("u8")), true};
            }
        }
        if (expression.op == "*" && expression.right) {
            auto value = compile_expression(*expression.right, context, function);
            if (!value.valid) {
                return value;
            }
            if (value.type.kind == TypeKind::Pointer && value.type.element) {
                emit_load(size_of(*value.type.element));
                return ExpressionValue{*value.type.element, true};
            }
            emit_load(8);
            return ExpressionValue{TypeRef::named("i64"), true};
        }
        if (expression.op == "-" && expression.right) {
            auto value = compile_expression(*expression.right, context, function);
            emit_line("    neg rax");
            return value;
        }
        break;
    case ExpressionKind::Binary:
        if (expression.left && expression.right) {
            if (expression.op == "==" || expression.op == "!=" || expression.op == "~=" || expression.op == "<" || expression.op == "<=" || expression.op == ">" || expression.op == ">=") {
                compile_expression(*expression.left, context, function);
                emit_line("    push rax");
                compile_expression(*expression.right, context, function);
                emit_line("    mov rdx, rax");
                emit_line("    pop rax");
                emit_line("    cmp rax, rdx");
                if (expression.op == "==") {
                    emit_line("    sete al");
                } else if (expression.op == "!=" || expression.op == "~=") {
                    emit_line("    setne al");
                } else if (expression.op == "<") {
                    emit_line("    setl al");
                } else if (expression.op == "<=") {
                    emit_line("    setle al");
                } else if (expression.op == ">") {
                    emit_line("    setg al");
                } else if (expression.op == ">=") {
                    emit_line("    setge al");
                }
                emit_line("    movzx eax, al");
                return ExpressionValue{TypeRef::named("i64"), true};
            }

            auto left_type = compile_expression(*expression.left, context, function);
            emit_line("    push rax");
            auto right_type = compile_expression(*expression.right, context, function);
            if (expression.op == "/") {
                emit_line("    mov rcx, rax");
                emit_line("    pop rax");
                emit_line("    cqo");
                emit_line("    idiv rcx");
            } else {
                emit_line("    mov rdx, rax");
                emit_line("    pop rax");
                if (expression.op == "+") {
                emit_line("    add rax, rdx");
                } else if (expression.op == "-") {
                    emit_line("    sub rax, rdx");
                } else if (expression.op == "*") {
                    emit_line("    imul rax, rdx");
                }
            }
            return left_type.valid ? left_type : right_type;
        }
        break;
    case ExpressionKind::Call:
        if (expression.callee && expression.callee->kind == ExpressionKind::Identifier) {
            if (expression.arguments.size() > 6) {
                diagnostics_.error(expression.location, "calls currently support at most 6 arguments");
                return ExpressionValue{TypeRef::named("i64"), false};
            }

            for (const auto& argument : expression.arguments) {
                compile_expression(argument, context, function);
                emit_line("    push rax");
            }

            for (std::size_t index = 0; index < expression.arguments.size(); ++index) {
                const std::size_t offset = 8 * (expression.arguments.size() - 1 - index);
                emit_line("    mov " + operand_for_register(index) + ", QWORD PTR [rsp + " + std::to_string(offset) + "]");
            }

            emit_line("    call " + expression.callee->text);
            if (!expression.arguments.empty()) {
                emit_line("    add rsp, " + std::to_string(8 * expression.arguments.size()));
            }

            auto callee = find_function(expression.callee->text);
            if (callee.has_value() && (*callee)->return_type.has_value()) {
                return ExpressionValue{*(*callee)->return_type, true};
            }
            return ExpressionValue{TypeRef::named("i64"), true};
        }
        diagnostics_.error(expression.location, "only direct function calls are supported yet");
        return ExpressionValue{TypeRef::named("i64"), false};
    case ExpressionKind::Member: {
        auto object_type = infer_expression_type(*expression.object, context, function);
        if (!object_type.has_value()) {
            diagnostics_.error(expression.location, "cannot resolve member access type");
            return ExpressionValue{TypeRef::named("i64"), false};
        }

        std::string base_register = "rax";
        if (expression.object->kind == ExpressionKind::Identifier) {
            auto variable = context.variables.find(expression.object->text);
            if (variable == context.variables.end()) {
                diagnostics_.error(expression.location, "unknown identifier: " + expression.object->text);
                return ExpressionValue{TypeRef::named("i64"), false};
            }

            if (variable->second.type.kind == TypeKind::Pointer && variable->second.type.element) {
                emit_line("    mov rax, QWORD PTR [rbp - " + std::to_string(variable->second.offset) + "]");
            } else {
                emit_line("    lea rax, [rbp - " + std::to_string(variable->second.offset) + "]");
            }
        } else {
            compile_expression(*expression.object, context, function);
            if (object_type->type.kind == TypeKind::Pointer && object_type->type.element) {
                // rax already holds the pointed-to address.
            }
        }

        TypeRef struct_type;
        if (object_type->type.kind == TypeKind::Pointer && object_type->type.element) {
            struct_type = *object_type->type.element;
        } else {
            struct_type = object_type->type;
        }

        auto layout = semantics_.struct_layouts.find(struct_type.name);
        if (layout == semantics_.struct_layouts.end()) {
            diagnostics_.error(expression.location, "unknown struct type for member access: " + struct_type.name);
            return ExpressionValue{TypeRef::named("i64"), false};
        }

        std::size_t field_offset = 0;
        std::optional<TypeRef> field_type;
        for (const auto& field : program_.structs) {
            if (field.name != struct_type.name) {
                continue;
            }
            for (const auto& source_field : field.fields) {
                if (source_field.name == expression.text) {
                    field_type = source_field.type;
                    break;
                }
            }
        }
        for (const auto& field : layout->second.fields) {
            if (field.name == expression.text) {
                field_offset = field.offset;
                break;
            }
        }
        if (!field_type.has_value()) {
            diagnostics_.error(expression.location, "unknown field: " + expression.text);
            return ExpressionValue{TypeRef::named("i64"), false};
        }

        emit_line("    add rax, " + std::to_string(field_offset));
        emit_load(size_of(*field_type));
        return ExpressionValue{*field_type, true};
    }
    case ExpressionKind::Index: {
        auto base_type = infer_expression_type(*expression.object, context, function);
        if (!base_type.has_value() || base_type->type.kind != TypeKind::Pointer || !base_type->type.element) {
            diagnostics_.error(expression.location, "indexing requires a pointer value");
            return ExpressionValue{TypeRef::named("i64"), false};
        }

        compile_expression(*expression.object, context, function);
        emit_line("    push rax");
        if (expression.right) {
            compile_expression(*expression.right, context, function);
        } else {
            emit_line("    xor rax, rax");
        }
        emit_line("    mov rdx, " + std::to_string(size_of(*base_type->type.element)));
        emit_line("    imul rax, rdx");
        emit_line("    pop rdx");
        emit_line("    add rax, rdx");
        emit_load(size_of(*base_type->type.element));
        return ExpressionValue{*base_type->type.element, true};
    }
    }

    diagnostics_.error(expression.location, "unsupported expression form");
    return ExpressionValue{TypeRef::named("i64"), false};
}

void CodeGenerator::compile_lvalue_store(const Expression& target, const FunctionContext& context, const FunctionDecl& function) {
    if (target.kind == ExpressionKind::Identifier) {
        auto variable = context.variables.find(target.text);
        if (variable == context.variables.end()) {
            auto data = find_data(target.text);
            if (!data.has_value()) {
                diagnostics_.error(target.location, "unknown identifier: " + target.text);
                return;
            }
            emit_line("    lea rax, [rip + " + (*data)->name + "]");
            return;
        }
        emit_line("    lea rax, [rbp - " + std::to_string(variable->second.offset) + "]");
        return;
    }

    if (target.kind == ExpressionKind::Unary && target.op == "*" && target.right) {
        compile_expression(*target.right, context, function);
        return;
    }

    if (target.kind == ExpressionKind::Member) {
        auto object_type = infer_expression_type(*target.object, context, function);
        if (!object_type.has_value()) {
            diagnostics_.error(target.location, "cannot resolve member access type");
            return;
        }

        if (target.object->kind == ExpressionKind::Identifier) {
            auto variable = context.variables.find(target.object->text);
            if (variable == context.variables.end()) {
                diagnostics_.error(target.location, "unknown identifier: " + target.object->text);
                return;
            }

            if (variable->second.type.kind == TypeKind::Pointer && variable->second.type.element) {
                emit_line("    mov rax, QWORD PTR [rbp - " + std::to_string(variable->second.offset) + "]");
            } else {
                emit_line("    lea rax, [rbp - " + std::to_string(variable->second.offset) + "]");
            }
        } else {
            compile_expression(*target.object, context, function);
        }

        TypeRef struct_type;
        if (object_type->type.kind == TypeKind::Pointer && object_type->type.element) {
            struct_type = *object_type->type.element;
        } else {
            struct_type = object_type->type;
        }

        auto layout = semantics_.struct_layouts.find(struct_type.name);
        if (layout == semantics_.struct_layouts.end()) {
            diagnostics_.error(target.location, "unknown struct type for member access: " + struct_type.name);
            return;
        }

        std::size_t field_offset = 0;
        for (const auto& field : layout->second.fields) {
            if (field.name == target.text) {
                field_offset = field.offset;
                break;
            }
        }

        emit_line("    add rax, " + std::to_string(field_offset));
        return;
    }

    if (target.kind == ExpressionKind::Index) {
        auto base_type = infer_expression_type(*target.object, context, function);
        if (!base_type.has_value() || base_type->type.kind != TypeKind::Pointer || !base_type->type.element) {
            diagnostics_.error(target.location, "indexing requires a pointer value");
            return;
        }

        compile_expression(*target.object, context, function);
        emit_line("    push rax");
        if (target.right) {
            compile_expression(*target.right, context, function);
        } else {
            emit_line("    xor rax, rax");
        }
        emit_line("    mov rdx, " + std::to_string(size_of(*base_type->type.element)));
        emit_line("    imul rax, rdx");
        emit_line("    pop rdx");
        emit_line("    add rax, rdx");
        return;
    }

    diagnostics_.error(target.location, "assignment target is not writable");
}

void CodeGenerator::compile_statement(const Statement& statement, FunctionContext& context, const FunctionDecl& function) {
    switch (statement.kind) {
    case StatementKind::Local: {
        auto slot = context.variables.find(statement.name);
        if (slot == context.variables.end()) {
            diagnostics_.error(statement.location, "missing stack slot for local: " + statement.name);
            return;
        }
        if (statement.value) {
            auto value = compile_expression(*statement.value, context, function);
            emit_line("    mov QWORD PTR [rbp - " + std::to_string(slot->second.offset) + "], rax");
        }
        return;
    }
    case StatementKind::Return:
        if (statement.expression) {
            compile_expression(*statement.expression, context, function);
        } else {
            emit_line("    xor rax, rax");
        }
        emit_line("    mov rsp, rbp");
        emit_line("    pop rbp");
        emit_line("    ret");
        return;
    case StatementKind::Assignment:
        if (statement.target && statement.value) {
            auto target_type = infer_expression_type(*statement.target, context, function);
            auto value_type = compile_expression(*statement.value, context, function);
            emit_line("    push rax");
            compile_lvalue_store(*statement.target, context, function);
            emit_line("    pop rdx");
            if (target_type.has_value()) {
                emit_store(size_of(target_type->type));
            } else {
                emit_store(8);
            }
        }
        return;
    case StatementKind::Expression:
        if (statement.expression) {
            compile_expression(*statement.expression, context, function);
        }
        return;
    case StatementKind::If: {
        const std::string else_label = make_label(".Lif_false_");
        const std::string end_label = make_label(".Lif_end_");
        if (statement.condition) {
            compile_expression(*statement.condition, context, function);
            emit_line("    cmp rax, 0");
            emit_line("    je " + else_label);
        }
        compile_block(statement.body, context, function);
        emit_line("    jmp " + end_label);
        emit_line(else_label + ":");
        emit_line(end_label + ":");
        return;
    }
    case StatementKind::While: {
        const std::string start_label = make_label(".Lwhile_start_");
        const std::string end_label = make_label(".Lwhile_end_");
        emit_line(start_label + ":");
        if (statement.condition) {
            compile_expression(*statement.condition, context, function);
            emit_line("    cmp rax, 0");
            emit_line("    je " + end_label);
        }
        compile_block(statement.body, context, function);
        emit_line("    jmp " + start_label);
        emit_line(end_label + ":");
        return;
    }
    }
}

void CodeGenerator::compile_block(const std::vector<Statement>& body, FunctionContext& context, const FunctionDecl& function) {
    for (const auto& statement : body) {
        compile_statement(statement, context, function);
        if (statement.kind == StatementKind::Return) {
            break;
        }
    }
}

std::string CodeGenerator::make_label(const std::string& prefix) {
    return prefix + std::to_string(label_counter_++);
}

void CodeGenerator::emit_startup_stub() {
    const bool has_main = find_function("main").has_value();
    emit_line(".globl _start");
    emit_line("_start:");
    emit_line("    xor rbp, rbp");
    emit_line("    and rsp, -16");
    if (has_main) {
        emit_line("    call main");
    }
    emit_line(".hang:");
    emit_line("    cli");
    emit_line("    hlt");
    emit_line("    jmp .hang");
}

void CodeGenerator::emit_function(const FunctionDecl& declaration) {
    if (declaration.external_asm) {
        if (!declaration.asm_symbol.empty()) {
            emit_line(".extern " + declaration.asm_symbol);
        }
        return;
    }

    FunctionContext context;
    collect_function_layout(context, declaration);

    emit_line(".globl " + declaration.name);
    emit_line(declaration.name + ":");
    emit_line("    push rbp");
    emit_line("    mov rbp, rsp");
    if (context.frame_size > 0) {
        emit_line("    sub rsp, " + std::to_string(context.frame_size));
    }

    static const char* registers[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    std::size_t parameter_index = 0;
    for (const auto& parameter : declaration.parameters) {
        auto slot = context.variables.find(parameter.name);
        if (slot == context.variables.end()) {
            continue;
        }

        if (parameter_index < 6) {
            emit_line("    mov QWORD PTR [rbp - " + std::to_string(slot->second.offset) + "], " + registers[parameter_index]);
        } else {
            const std::size_t stack_offset = 16 + 8 * (parameter_index - 6);
            emit_line("    mov rax, QWORD PTR [rbp + " + std::to_string(stack_offset) + "]");
            emit_line("    mov QWORD PTR [rbp - " + std::to_string(slot->second.offset) + "], rax");
        }
        ++parameter_index;
    }

    bool emitted_explicit_return = false;
    for (const auto& statement : declaration.body) {
        compile_statement(statement, context, declaration);
        if (statement.kind == StatementKind::Return) {
            emitted_explicit_return = true;
            break;
        }
    }

    if (!emitted_explicit_return) {
        emit_line("    xor rax, rax");
        emit_line("    mov rsp, rbp");
        emit_line("    pop rbp");
        emit_line("    ret");
    }
}

CodegenResult CodeGenerator::generate() {
    emit_line(".intel_syntax noprefix");
    emit_line(".section .text");

    emit_startup_stub();
    emit_line("");

    if (!program_.data.empty()) {
        for (const auto& data : program_.data) {
            emit_data(data);
            emit_line("");
        }
    }

    for (const auto& function : program_.functions) {
        emit_function(function);
        emit_line("");
    }

    if (!string_table_.empty()) {
        emit_line(".section .rodata");
        for (const auto& [label, value] : string_table_) {
            emit_line(label + ":");
            emit_line("    .asciz \"" + escape_string(value) + "\"");
        }
    }

    CodegenResult result;
    result.ok = !diagnostics_.has_errors();
    result.assembly = assembly_.str();
    result.diagnostics = diagnostics_.diagnostics();
    return result;
}

} // namespace lunaris