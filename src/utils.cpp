#include "utils.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

using namespace godot;

// TODO: the real ClassDB is not available in godot-cpp yet. this is what we get.
Object *Utils::class_db = nullptr;

Object *Utils::get_class_db() {
    if (class_db == nullptr)
        class_db = Engine::get_singleton()->get_singleton("ClassDB");

    return class_db;
}

bool Utils::class_exists(const StringName &class_name) {
    return get_class_db()->call("class_exists", class_name);
}

bool Utils::is_parent_class(const StringName &class_name, const StringName &inherits) {
    return get_class_db()->call("is_parent_class", class_name, inherits);
}

StringName Utils::get_parent_class(const StringName &class_name) {
    return get_class_db()->call("get_parent_class", class_name);
}

String Utils::to_pascal_case(const String &input) {
    String out = input.to_pascal_case();

    // to_pascal_case strips leading/trailing underscores. leading is most common and this handles that
    for (int i = 0; i < input.length() && input[i] == '_'; i++)
        out = "_" + out;

    return out;
}

String Utils::resource_type_hint(const String &type) {
    // see core/object/object.h
    Array hint_values;
    hint_values.resize(3);
    hint_values[0] = Variant::OBJECT;
    hint_values[1] = PROPERTY_HINT_RESOURCE_TYPE;
    hint_values[2] = type;

    return String("{0}/{1}:{2}").format(hint_values);
}
