#!/usr/bin/env python3
"""
Amalgamate sqlite-vec into a single distributable .c file.

Reads the dev sqlite-vec.c and inlines any #include "sqlite-vec-*.c" files,
stripping LSP-support blocks and per-file include guards.

Usage:
    python3 scripts/amalgamate.py sqlite-vec.c > dist/sqlite-vec.c
"""

import re
import sys
import os


def strip_lsp_block(content):
    """Remove the LSP-support pattern:
        #ifndef SQLITE_VEC_H
        #include "sqlite-vec.c" // ...
        #endif
    """
    pattern = re.compile(
        r'^\s*#ifndef\s+SQLITE_VEC_H\s*\n'
        r'\s*#include\s+"sqlite-vec\.c"[^\n]*\n'
        r'\s*#endif[^\n]*\n',
        re.MULTILINE,
    )
    return pattern.sub('', content)


def strip_include_guard(content, guard_macro):
    """Remove the include guard pair:
        #ifndef GUARD_MACRO
        #define GUARD_MACRO
        ...content...
        (trailing #endif removed)
    """
    # Strip the #ifndef / #define pair at the top
    header_pattern = re.compile(
        r'^\s*#ifndef\s+' + re.escape(guard_macro) + r'\s*\n'
        r'\s*#define\s+' + re.escape(guard_macro) + r'\s*\n',
        re.MULTILINE,
    )
    content = header_pattern.sub('', content, count=1)

    # Strip the trailing #endif (last one in file that closes the guard)
    # Find the last #endif and remove it
    lines = content.rstrip('\n').split('\n')
    for i in range(len(lines) - 1, -1, -1):
        if re.match(r'^\s*#endif', lines[i]):
            lines.pop(i)
            break

    return '\n'.join(lines) + '\n'


def detect_include_guard(content):
    """Detect an include guard macro like SQLITE_VEC_IVF_C."""
    m = re.match(
        r'\s*(?:/\*[\s\S]*?\*/\s*)?'  # optional block comment
        r'#ifndef\s+(SQLITE_VEC_\w+_C)\s*\n'
        r'#define\s+\1',
        content,
    )
    return m.group(1) if m else None


def inline_include(match, base_dir):
    """Replace an #include "sqlite-vec-*.c" with the file's contents."""
    filename = match.group(1)
    filepath = os.path.join(base_dir, filename)

    if not os.path.exists(filepath):
        print(f"Warning: {filepath} not found, leaving #include in place", file=sys.stderr)
        return match.group(0)

    with open(filepath, 'r') as f:
        content = f.read()

    # Strip LSP-support block
    content = strip_lsp_block(content)

    # Strip include guard if present
    guard = detect_include_guard(content)
    if guard:
        content = strip_include_guard(content, guard)

    separator = '/' * 78
    header = f'\n{separator}\n// Begin inlined: {filename}\n{separator}\n\n'
    footer = f'\n{separator}\n// End inlined: {filename}\n{separator}\n'

    return header + content.strip('\n') + footer


def amalgamate(input_path):
    base_dir = os.path.dirname(os.path.abspath(input_path))

    with open(input_path, 'r') as f:
        content = f.read()

    # Replace #include "sqlite-vec-*.c" with inlined contents
    include_pattern = re.compile(r'^#include\s+"(sqlite-vec-[^"]+\.c)"\s*$', re.MULTILINE)
    content = include_pattern.sub(lambda m: inline_include(m, base_dir), content)

    return content


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <input-file>", file=sys.stderr)
        sys.exit(1)

    result = amalgamate(sys.argv[1])
    sys.stdout.write(result)


if __name__ == '__main__':
    main()
