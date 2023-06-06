#include "luau_analysis.h"

#include <Luau/Ast.h>
#include <Luau/Lexer.h>
#include <Luau/Location.h>
#include <Luau/ParseResult.h>
#include <gdextension_interface.h>
#include <string.h>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/variant/char_string.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "luau_lib.h"
#include "utils.h"

using namespace godot;

/* BASE ANALYSIS */

static Luau::AstLocal *find_return_local(Luau::AstArray<Luau::AstStat *> body) {
    for (Luau::AstStat *stat : body) {
        if (Luau::AstStatBlock *block = stat->as<Luau::AstStatBlock>()) {
            // Can return from inside a block, for some reason.
            return find_return_local(block->body);
        } else if (Luau::AstStatReturn *ret = stat->as<Luau::AstStatReturn>()) {
            if (ret->list.size == 0)
                return nullptr;

            Luau::AstExpr *expr = *ret->list.begin();
            if (Luau::AstExprLocal *local = expr->as<Luau::AstExprLocal>()) {
                return local->local;
            }
        }
    }

    return nullptr;
}

struct LocalDefinitionFinder : public Luau::AstVisitor {
    Luau::AstLocal *local;
    Luau::AstExpr *result = nullptr;

    bool visit(Luau::AstStatLocal *node) override {
        Luau::AstLocal *const *vars = node->vars.begin();
        Luau::AstExpr *const *values = node->values.begin();

        for (int i = 0; i < node->vars.size && i < node->values.size; i++) {
            if (vars[i] == local) {
                result = values[i];
            }
        }

        return false;
    }

    LocalDefinitionFinder(Luau::AstLocal *local) :
            local(local) {}
};

struct TypesMethodsFinder : public Luau::AstVisitor {
    Luau::AstLocal *impl;

    HashMap<StringName, Luau::AstStatFunction *> methods;

    bool visit(Luau::AstStatFunction *func) override {
        if (Luau::AstExprIndexName *index = func->name->as<Luau::AstExprIndexName>()) {
            if (Luau::AstExprLocal *local = index->expr->as<Luau::AstExprLocal>()) {
                if (local->local == impl) {
                    methods.insert(index->index.value, func);
                }
            }
        }

        return false;
    }

    TypesMethodsFinder(Luau::AstLocal *impl) :
            impl(impl) {}
};

// Scans the script AST for key components. As this functionality is non-essential (for scripts running),
// it will for simplicity be quite picky about how classes are defined:
// - The returned definition and impl table (if any) must be defined as locals.
// - The impl table (if any) must be passed into `RegisterImpl` as a local.
// - The returned value must be the same local variable as the one that defined the class.
// - All methods which chain on classes (namely, `RegisterImpl`) must be called in the same expression that defines the class definition.
// Basically, make everything "idiomatic" (if such a thing exists) and don't do anything weird, then this should work.
bool luascript_analyze(const char *src, const Luau::ParseResult &parse_result, LuauScriptAnalysisResult &result) {
    // Step 1: Extract comments
    LocalVector<const char *> line_offsets;
    line_offsets.push_back(src);
    {
        const char *ptr = src;

        while (*ptr) {
            if (*ptr == '\n') {
                line_offsets.push_back(ptr + 1);
            }

            ptr++;
        }
    }

    for (const Luau::Comment &comment : parse_result.commentLocations) {
        if (comment.type == Luau::Lexeme::BrokenComment) {
            continue;
        }

        const Luau::Location &loc = comment.location;
        const char *start_line_ptr = line_offsets[loc.begin.line];

        LuauComment parsed_comment;

        if (comment.type == Luau::Lexeme::BlockComment) {
            parsed_comment.type = LuauComment::BLOCK;
        } else {
            // Search from start of line. If there is nothing other than whitespace before the comment, count it as "exclusive".
            const char *ptr = start_line_ptr;

            while (*ptr) {
                if (*ptr == '-' && *(ptr + 1) == '-') {
                    parsed_comment.type = LuauComment::SINGLE_LINE_EXCL;
                    break;
                } else if (*ptr != '\t' && *ptr != ' ' && *ptr != '\v' && *ptr != '\f') {
                    parsed_comment.type = LuauComment::SINGLE_LINE;
                    break;
                }

                ptr++;
            }
        }

        parsed_comment.location = loc;

        const char *start_ptr = start_line_ptr + loc.begin.column;
        const char *end_ptr = line_offsets[loc.end.line] + loc.end.column; // not inclusive
        parsed_comment.contents = String::utf8(start_ptr, end_ptr - start_ptr);

        result.comments.push_back(parsed_comment);
    }

    // Step 2: Scan root return value for definition expression.
    result.definition = find_return_local(parse_result.root->body);
    if (!result.definition)
        return false;

    LocalDefinitionFinder def_local_def_finder(result.definition);
    parse_result.root->visit(&def_local_def_finder);
    if (!def_local_def_finder.result)
        return false;

    // Step 3: Find the implementation table, if any.
    Luau::AstExprCall *chained_call = def_local_def_finder.result->as<Luau::AstExprCall>();

    while (chained_call) {
        Luau::AstExpr *func = chained_call->func;

        if (Luau::AstExprIndexName *index = func->as<Luau::AstExprIndexName>()) {
            if (index->op == ':' && index->index == "RegisterImpl" && chained_call->args.size >= 1) {
                if (Luau::AstExprLocal *found_local = (*chained_call->args.begin())->as<Luau::AstExprLocal>()) {
                    result.impl = found_local->local;
                    break;
                }
            }

            chained_call = index->expr->as<Luau::AstExprCall>();
        } else {
            break;
        }
    }

    if (!result.impl)
        return false;

    // Step 4: Find defined methods and types.
    TypesMethodsFinder types_methods_finder(result.impl);
    parse_result.root->visit(&types_methods_finder);

    result.methods = types_methods_finder.methods;

    return true;
}

/* AST FUNCTIONS */

static bool get_type(const char *type_name, GDProperty &prop) {
    static HashMap<String, GDExtensionVariantType> variant_types;
    static bool did_init = false;

    if (!did_init) {
        // Special cases
        variant_types.insert("nil", GDEXTENSION_VARIANT_TYPE_NIL);
        variant_types.insert("boolean", GDEXTENSION_VARIANT_TYPE_BOOL);
        variant_types.insert("integer", GDEXTENSION_VARIANT_TYPE_INT);
        variant_types.insert("number", GDEXTENSION_VARIANT_TYPE_FLOAT);
        variant_types.insert("string", GDEXTENSION_VARIANT_TYPE_STRING);

        for (int i = GDEXTENSION_VARIANT_TYPE_VECTOR2; i < GDEXTENSION_VARIANT_TYPE_VARIANT_MAX; i++) {
            variant_types.insert(Variant::get_type_name(Variant::Type(i)), GDExtensionVariantType(i));
        }

        did_init = true;
    }

    // Special case
    if (strcmp(type_name, "Variant") == 0) {
        prop.type = GDEXTENSION_VARIANT_TYPE_NIL;
        prop.usage = PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT;

        return true;
    }

    HashMap<String, GDExtensionVariantType>::ConstIterator E = variant_types.find(type_name);

    if (E) {
        // Variant type
        prop.type = E->value;
    } else if (Utils::class_exists(type_name)) {
        prop.type = GDEXTENSION_VARIANT_TYPE_OBJECT;

        if (Utils::is_parent_class(type_name, "Resource")) {
            // Resource
            prop.hint = PROPERTY_HINT_RESOURCE_TYPE;
            prop.hint_string = type_name;
        } else {
            // Object
            prop.class_name = type_name;
        }
    } else {
        return false;
    }

    return true;
}

static bool get_prop(Luau::AstTypeReference *type, GDProperty &prop) {
    const char *type_name = type->name.value;

    if (!type->hasParameterList) {
        return get_type(type_name, prop);
    }

    if (strcmp(type_name, "TypedArray") == 0) {
        // TypedArray
        Luau::AstType *param = type->parameters.begin()->type;

        if (param) {
            if (Luau::AstTypeReference *param_ref = param->as<Luau::AstTypeReference>()) {
                GDProperty type_info;
                if (!get_type(param_ref->name.value, type_info))
                    return false;

                prop.type = GDEXTENSION_VARIANT_TYPE_ARRAY;
                prop.hint = PROPERTY_HINT_ARRAY_TYPE;

                if (type_info.type == GDEXTENSION_VARIANT_TYPE_OBJECT) {
                    if (type_info.hint == PROPERTY_HINT_RESOURCE_TYPE) {
                        prop.hint_string = Utils::resource_type_hint(type_info.hint_string);
                    } else {
                        prop.hint_string = type_info.class_name;
                    }
                } else {
                    prop.hint_string = Variant::get_type_name(Variant::Type(type_info.type));
                }

                return true;
            }
        }
    }

    return false;
}

static Luau::AstTypeReference *get_type_reference(Luau::AstType *type, bool *was_conditional = nullptr) {
    if (Luau::AstTypeReference *ref = type->as<Luau::AstTypeReference>()) {
        return ref;
    }

    // Union with nil -> T?
    if (Luau::AstTypeUnion *uni = type->as<Luau::AstTypeUnion>()) {
        if (uni->types.size != 2)
            return nullptr;

        bool nil_found = false;
        Luau::AstTypeReference *first_non_nil_ref = nullptr;

        for (Luau::AstType *uni_type : uni->types) {
            if (Luau::AstTypeReference *ref = uni_type->as<Luau::AstTypeReference>()) {
                if (ref->name == "nil")
                    nil_found = true;
                else if (!first_non_nil_ref)
                    first_non_nil_ref = ref;

                if (nil_found && first_non_nil_ref) {
                    if (was_conditional)
                        *was_conditional = true;

                    return first_non_nil_ref;
                }
            } else {
                return nullptr;
            }
        }
    }

    return nullptr;
}

bool luascript_ast_method(const LuauScriptAnalysisResult &analysis, const StringName &method, GDMethod &ret) {
    HashMap<StringName, Luau::AstStatFunction *>::ConstIterator E = analysis.methods.find(method);

    if (!E)
        return false;

    Luau::AstStatFunction *stat_func = E->value;
    Luau::AstExprFunction *func = stat_func->func;

    ret.name = method;

    if (func->returnAnnotation.has_value()) {
        const Luau::AstArray<Luau::AstType *> &types = func->returnAnnotation.value().types;
        if (types.size > 1)
            return false;

        bool ret_conditional = false;
        Luau::AstTypeReference *ref = get_type_reference(*types.begin(), &ret_conditional);
        if (!ref)
            return false;

        GDProperty return_val;

        if (ret_conditional) {
            // Assume Variant if method can return nil
            return_val.type = GDEXTENSION_VARIANT_TYPE_NIL;
            return_val.usage = PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT;
        } else if (!get_prop(ref, return_val)) {
            return false;
        }

        ret.return_val = return_val;
    }

    if (func->vararg)
        ret.flags = ret.flags | METHOD_FLAG_VARARG;

    int arg_offset = func->self ? 0 : 1;

    int i = 0;
    ret.arguments.resize(func->self ? func->args.size : func->args.size - 1);

    GDProperty *arg_props = ret.arguments.ptrw();
    for (Luau::AstLocal *arg : func->args) {
        if (i < arg_offset) {
            i++;
            continue;
        }

        GDProperty arg_prop;
        arg_prop.name = arg->name.value;

        if (!arg->annotation)
            return false;

        Luau::AstTypeReference *arg_type = get_type_reference(arg->annotation);
        if (!arg_type || !get_prop(arg_type, arg_prop))
            return false;

        arg_props[i - arg_offset] = arg_prop;
        i++;
    }

    return true;
}
