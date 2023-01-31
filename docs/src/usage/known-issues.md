# Known Issues

`godot-luau-script`, godot-cpp, Godot's GDExtension script language interface, and Godot 4 itself are all unstable.
As such, you may experience significant issues when using all of these things together.

## Upstream issue: Godot segfaults on exit

Tracking [godot-cpp#889](https://github.com/godotengine/godot-cpp/issues/889).

## Upstream issue: Godot segfaults on exit (2)

Tracking [godot#66475](https://github.com/godotengine/godot/issues/66475) and [godot#67155](https://github.com/godotengine/godot/pull/67155).

## Upstream issue: Godot crashes when loading a project for the first time

This likely occurs because the `ScriptLanguage` is used before initialization when the extension is loaded for the first time
(or, that it is used during asset reimport somehow).

# Type checking for `Array` and `Dictionary` `__index` is broken

Direction of how to solve this is currently undecided.