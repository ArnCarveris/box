# Coding style

`python3 tools/lint.py` checks these rules for `.c`, `.h` and `.m` files in `include`, `src`,
`apps`, `test` and `bench`. `--fix` fixes signatures and blank lines.

## Files
- Start with a doc comment that has `@file <name>` (matching the file name) and `@brief`.
- At most 100 characters per line, no trailing whitespace, at most 2000 lines per file.

## Functions
- Parameters start on the line after the name, one per line, and the opening brace is on its own line:

```c
int sum(
    int a,
    int b)
{
    return a + b;
}
```

- At most 200 lines.
- No comments inside the body.
- One exit point: no `return` inside a block, use `goto error` instead.
- A blank line before `return` and after a closing `}` of a block.

## Doc comments
- Every non-static function has a doc comment with an `@param` for each parameter and an
  `@return` unless it returns `void`.
- Every type (`typedef struct`, `typedef enum`, `ECS_STRUCT`, `ECS_ENUM`) and every member has a
  doc comment.
- After the summary line, at most 2 paragraphs of at most 3 lines.

## Naming
- Components start with a capital (`Line`), private types are lower case and end in `_t` (`point_t`).

## Tests and benchmarks
- Test code only gets the function body checks, and the first line of a body may be a
  `// path/to/case` reference. Static functions in `bench` get every function check.
