#include "sema.h"

#include <algorithm>

namespace lunaris {

SemanticAnalyzer::SemanticAnalyzer(const Program& program, DiagnosticSink& diagnostics)
    : program_(program), diagnostics_(diagnostics) {}

std::size_t SemanticAnalyzer::align_up(std::size_t value, std::size_t alignment) const {
    if (alignment <= 1) {
        return value;
    }
    const std::size_t remainder = value % alignment;
    if (remainder == 0) {
        return value;
    }
    return value + (alignment - remainder);
}

void SemanticAnalyzer::collect_structs() {
    for (const auto& declaration : program_.structs) {
        if (struct_declarations_.contains(declaration.name)) {
            diagnostics_.error(declaration.location, "duplicate struct declaration: " + declaration.name);
            continue;
        }
        struct_declarations_[declaration.name] = &declaration;
    }
}

void SemanticAnalyzer::collect_function_signatures() {
    std::unordered_map<std::string, const FunctionDecl*> function_names;
    for (const auto& declaration : program_.functions) {
        if (function_names.contains(declaration.name)) {
            diagnostics_.error(declaration.location, "duplicate function declaration: " + declaration.name);
            continue;
        }
        function_names[declaration.name] = &declaration;
    }
}

std::optional<SemanticAnalyzer::ResolvedType> SemanticAnalyzer::resolve_type(const TypeRef& type) {
    if (type.kind == TypeKind::Pointer) {
        return ResolvedType{8, 8, true};
    }

    if (type.name == "void") {
        return ResolvedType{0, 1, true};
    }
    if (type.name == "bool" || type.name == "i8" || type.name == "u8") {
        return ResolvedType{1, 1, true};
    }
    if (type.name == "i16" || type.name == "u16") {
        return ResolvedType{2, 2, true};
    }
    if (type.name == "i32" || type.name == "u32") {
        return ResolvedType{4, 4, true};
    }
    if (type.name == "i64" || type.name == "u64" || type.name == "usize" || type.name == "isize") {
        return ResolvedType{8, 8, true};
    }

    auto layout = struct_layouts_.find(type.name);
    if (layout != struct_layouts_.end()) {
        return ResolvedType{layout->second.size, layout->second.alignment, true};
    }

    return std::nullopt;
}

bool SemanticAnalyzer::compute_struct_layout(const StructDecl& declaration, StructLayout& layout) {
    layout.name = declaration.name;
    layout.packed = declaration.packed;
    layout.fields.clear();

    std::size_t offset = 0;
    std::size_t alignment = declaration.packed ? 1 : 1;

    for (const auto& field : declaration.fields) {
        auto resolved = resolve_type(field.type);
        if (!resolved.has_value()) {
            return false;
        }

        std::size_t field_alignment = declaration.packed ? 1 : resolved->alignment;
        offset = declaration.packed ? offset : align_up(offset, field_alignment);

        layout.fields.push_back(FieldLayout{field.name, offset, resolved->size, field_alignment});
        offset += resolved->size;
        alignment = std::max(alignment, field_alignment);
    }

    layout.alignment = declaration.packed ? 1 : alignment;
    layout.size = declaration.packed ? offset : align_up(offset, layout.alignment);
    return true;
}

SemanticResult SemanticAnalyzer::analyze() {
    collect_structs();
    collect_function_signatures();

    bool progress = true;
    while (progress) {
        progress = false;
        for (const auto& declaration : program_.structs) {
            if (struct_layouts_.contains(declaration.name)) {
                continue;
            }

            StructLayout layout;
            if (!compute_struct_layout(declaration, layout)) {
                continue;
            }

            struct_layouts_[declaration.name] = std::move(layout);
            progress = true;
        }
    }

    for (const auto& declaration : program_.structs) {
        if (!struct_layouts_.contains(declaration.name)) {
            diagnostics_.error(declaration.location, "unable to resolve layout for struct: " + declaration.name);
        }
    }

    for (const auto& declaration : program_.functions) {
        for (const auto& parameter : declaration.parameters) {
            if (!resolve_type(parameter.type).has_value()) {
                diagnostics_.error(parameter.location, "unknown parameter type: " + parameter.type.name);
            }
        }
        if (declaration.return_type.has_value() && !resolve_type(*declaration.return_type).has_value()) {
            diagnostics_.error(declaration.location, "unknown return type: " + declaration.return_type->name);
        }
    }

    SemanticResult result;
    result.ok = !diagnostics_.has_errors();
    result.struct_layouts = std::move(struct_layouts_);
    result.diagnostics = diagnostics_.diagnostics();
    return result;
}

} // namespace lunaris