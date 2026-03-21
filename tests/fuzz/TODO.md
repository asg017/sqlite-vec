# Fuzz Testing TODO: Undefined Behavior Findings

UBSAN findings from fuzz targets. None are crash-level bugs, but all are
formally undefined behavior per the C standard.

## Class 1: Function pointer type mismatch (~20 sites)

`fvec_cleanup_noop` is defined as `void (f32 *)` but called through
`vector_cleanup` which is `void (*)(void *)`. Two cleanup typedefs exist
with incompatible signatures:

```c
typedef void (*vector_cleanup)(void *p);      // line 597
typedef void (*fvec_cleanup)(f32 *vector);     // line 695
```

Affected lines: 1031, 1049, 1050, 1160, 1200, 1201, 1241, 1242, 1282,
1283, 1324, 1325, 1356, 1424, 1524, 1525, 1582, 1583, 1699, 1749, 1798,
2520, 7236, 8501, and sqlite3.c:82930 (via sqlite3_result_blob destructor).

Low practical severity — calling conventions on all real platforms pass
`f32 *` and `void *` identically — but flags on every UBSAN run.

Fix: change `fvec_cleanup_noop` to take `void *`, or unify the typedefs.

## Class 2: Misaligned f32 reads (~10 sites)

`f32` (4-byte alignment required) read from potentially unaligned addresses.
Happens when a blob from SQLite's internal storage is cast to `f32 *` and
dereferenced. The blob pointer may not be 4-byte aligned.

Affected lines: 369, 446, 473-475, 1401, 1461, 1501, 1559, 1653, 1726,
1789, 1793.

Medium severity — silent on x86/ARM64 (hardware supports unaligned float
access) but UB on strict-alignment architectures.

Fix: use `memcpy` to load floats from potentially-unaligned memory, or
ensure blob pointers are aligned before use.

## Class 3: Float-to-integer overflow (1 site)

`vec_quantize_int8` at line 1461 — when `srcVector[i]` is a large float,
the expression `((srcVector[i] - (-1.0)) / step) - 128` overflows
`signed char` range. Assigning this to `i8 out[i]` is UB.

Low-medium severity — silent truncation in practice.

Fix: clamp the result before cast.
