---
applyTo: "**/*.js, **/*.ts, **/*.tsx"
---

### JavaScript/TypeScript Formatting Rules

**Braces:**
- Opening braces for functions, classes, and control blocks: new line
- Exception: Object literals and arrow functions with single expression: same line
- Inline empty constructors/methods: `{}` on same line
- Closing braces: always on own new line

**Imports:**
- Use ES6 `import`/`export` syntax
- Use relative paths (`./`, `../`) for local files
- Use package names for `node_modules` dependencies
- Order: external libraries first, internal modules second

**Indentation:**
- 4 spaces for: classes, functions, control blocks, objects, arrays

**Spacing:**
- No spaces around: `=`, `:`, unary `!`, arithmetic `+`/`-`
- Yes spaces around: comparison `!=`/`>=`/`===`, logical `&&`/`||`
- No space between keyword/function name and `(`
- Commas: space after, not before
- Semicolons: mandatory at end of statements, not before
- No spaces inside `()`/`[]`/`<>`, unless empty

**Blank Lines:**
- Between methods
- To separate logical blocks within methods

**Naming Conventions:**
- Classes/Interfaces/Types: `PascalCase`
- Functions/Methods: `camelCase`
- Class Members: `m_camelCase`
- Local Variables: `camelCase`
- Global/Static Constants: `UPPER_CASE_UNDERSCORED`

**Type Annotations (TypeScript):**
- Variable declaration: `variable: Type`
- Use `interface` for object definitions (extensible)
- Use `type` for unions/intersections
- Avoid `any`: strict typing preferred

**Comments:**
- Minimize usage
- Use `/** ... */` (JSDoc) for functions/classes to provide IntelliSense
- Use `//` for implementation details inside functions
- Don't use when code is obvious

**Error Handling:**
- Prefer `try/catch` blocks wrapping `async/await` logic
- Use specific Error classes

**Variable Declarations:**
- At top of scope/before control flow
- Use `const` by default
- Use `let` only if reassignment required
- Never use `var`
- Blank line after variable declaration group
- Control flow ends declaration group
- Allow inference for primitives
- Explicitly type function returns and complex objects

**Long Function Signatures:**
- Wrap parameters to new lines, indent 1 level
- `)` after last parameter

**Arrow Functions:**
- Prefer `() => {}` for callbacks and preserving `this` context
- Explicit return types recommended for exported functions

### File Naming

- Use `camelCase` for file names
- Extensions: `.ts` / `.tsx` / `.js`