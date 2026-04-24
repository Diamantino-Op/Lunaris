# Lunaris Lua Dialect

Lunaris uses a Lua-based source language for kernel code and low-level helpers. The syntax stays close to Lua, but it adds a few compiler-facing forms for data declarations, packed structs, and assembly bindings.

## Overview

- Top-level `data` declarations map to object-file data, optionally into a named ELF section.
- `struct` and `packed struct` definitions use `end`-terminated blocks.
- `asm function` binds a Lua declaration to an assembly symbol.
- Standard Lua flow control such as `if`, `while`, `return`, and local variables is supported.
- The inequality operator uses Lua syntax: `~=`.

## Declarations

### Data

```lua
data payload: u64 section ".limine_requests" = 1, 2
```

Use `data` for top-level objects that should be emitted into the object file. Add `section "..."` when the data must live in a specific ELF section.

### Structs

```lua
packed struct limine_base_revision
	magic0: u64
	magic1: u64
	revision: u64
end
```

Structs are written as block forms that end with `end`, just like functions. Use `packed struct` when you need the compiler to omit padding between fields.

### Assembly bindings

```lua
asm function hlt(): void = hlt;
```

This declares that the Lua function name is implemented by the assembly symbol on the right-hand side.

## Functions and Control Flow

```lua
function kernel_main()
	local base_revision: ptr(limine_base_revision) = limine_base_revision

	if base_revision.revision ~= 0 then
		hcf_base_revision()
	end

	while 1 do
		return
	end
end
```

Functions use `end` to close the body. `if` uses `then`, `while` uses `do`, and `local` declarations may include type annotations and initializers.

## Example

The kernel entry point in [LunarisOS/kernel/src/main.lua](../LunarisOS/kernel/src/main.lua) shows the current style used by the project, including Limine request data, packed structs, and assembly bindings.

## Notes

- Pointer types use `ptr(...)`, such as `ptr(u8)` or `ptr(limine_framebuffer)`.
- The language accepts `end` for normal function blocks as well as struct blocks.
- Assembly labels are resolved by the compiler and can be navigated from the VS Code extension.