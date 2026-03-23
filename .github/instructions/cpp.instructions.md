---
applyTo: "**/*.cpp, **/*.h, **/*.inl"
---

### C/C++ Formatting Rules

**Braces:**
- Opening braces for namespaces, functions, and control blocks: new line
- Opening braces for struct/class definitions in headers: same line
- Inline empty constructors in headers: `{}` on same line
- Closing braces: always on own new line

**Includes:**
- Use `""` for local files
- Use `<>` for library includes

**Indentation:**
- 4 spaces for: types, functions, control blocks, constructor initializer lists
- non for: namespaces

**Spacing:**
- No spaces around: `=`, `::`, `:`, unary `!`, dereference `*`, arithmetic `+`/`-`, between keyword/function and `()`
- Yes spaces around: comparison `!=`/`>=`, logical `&&`/`||`
- Commas: space after, not before
- Semicolons: not before
- No space between keyword/function name and `(`
- No spaces inside `()`/`<>`, unless empty

**Constructor Initializers:**
- `:` after constructor `()`

**Blank Lines:**
- Between methods
- To separate logical blocks within methods

**Naming Conventions:**
- Types: `PascalCase`
- Functions/Methods: `camelCase`
- Class Members: `m_camelCase`
- Struct/Local Variables: `camelCase`
- Macros: `UPPER_CASE_UNDERSCORED`

**Pointers/References:**
- `type *var` / `type &var`

**Preprocessor:**
- `#ifdef`/`#endif` unindented
- Content inside indented
- `#endif//COMMENT` allowed

**Comments:**
- Minimize usage
- Use `//` for single-line comments
- After statement is acceptable
- Don't use when code is obvious

**Error Handling:**
- Avoid try/catch if error codes are usable

**Auto Keyword:**
- Minimize use

**Variable Declarations:**
- At top of scope/before control flow
- Blank line after variable declaration group
- Control flow ends declaration group

**Long Function Signatures:**
- Wrap parameters to new lines, indent 1 level
- `)` after last parameter

**Header Guards:**
- Format: `_PROJECT_FILENAME_EXT_`
- Do not use `#pragma once`

**Namespaces:**
- Avoid using directives, prefer explicit qualification
- Allow namespace aliases for brevity

### File Naming

- Use `camelCase` for file names
- Extensions: `.h` / `.cpp` / `.inl`
