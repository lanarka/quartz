# Quartz Assembler (qasm) — Reference

`qasm.py` is the assembler for **Quartz**, a small register-based virtual
machine. It turns a `.quartz` source file (plus anything it `%include`s)
into a `.bin` image that the Quartz VM loads and executes, or into a raw
headerless code blob with `--raw`.

This document describes the assembler's syntax, directives and command-line
usage, based on `qasm/qasm.py`, `qasm/quartz/isa.py`, `qasm/quartz/data.py`
and the examples in `examples/`.

---

## 1. Pipeline overview

```
source.quartz  --preprocess-->  merged lines  --parse--> events  --analyze--> encoded instrs  --encode--> bytes  --write--> .bin / raw
```

1. **preprocess()** — reads the main file and everything it transitively
   `%include`s, strips comments, and resolves the file-level directives
   `%include`, `%runtime`, `%entrypoint`, `%data`, `%define`.
2. **parse_source()** — parses each line independently with a small LALR
   grammar into `LabelDef` / `InstrDef` events. A syntax error on one line
   doesn't stop the rest of the file from being checked.
3. **analyze()** — resolves every instruction against the instruction set
   (`quartz/isa.py`), validates operand kinds/ranges, computes label
   addresses, and (optionally) runs the register type/"cast" checker.
4. **encode()** — serializes the resolved instructions to little-endian
   machine code.
5. **write_bin() / write_raw()** — wraps the code (plus optional `%data`
   segment) in the `.bin` header, or writes raw bytes.

---

## 2. Command-line usage

```
python qasm.py <source.quartz> <output.bin> [options]
```

| Option | Description |
|---|---|
| `--map[=path]` | Emit a `.map` file (address → label name). Without a value, defaults to `<output>.map`. Consumed by `qdisasm.py`. |
| `--cast-warnings` | Warn when a register is read back with a different type than the one it was last written with. |
| `--unused-const-warnings` | Warn about `%define` constants that are never referenced. |
| `--raw` | Write just the raw code bytes (no `.bin` header) — for embedding as a blob loaded at runtime (e.g. via `OP_VMX`). |

Example:

```
python qasm.py examples/hello.quartz hello.bin --map --cast-warnings
```

On success this prints something like:

```
OK: hello.bin (612 bytes, runtime version 255, entrypoint 0x000000)
```

Unlabeled/unused labels and constants are reported as warnings (not
errors) unless `--unused-const-warnings` promotes the latter to something
you actually see by default off.

There is also a companion disassembler:

```
python qdisasm.py <program.bin> [program.map]
```

It reconstructs readable Quartz assembly from a `.bin` image, using the
`.map` file (if found) to print label names instead of raw hex addresses at
jump/call targets.

---

## 3. Source line syntax

Each physical line is one of:

```
label:
mnemonic operand operand ...
label: mnemonic operand operand ...      // a label and an instruction may share a line
```

Blank lines and comments are ignored. Whitespace (spaces/tabs) between
tokens is not significant.

### Comments

```
// line comment, to end of line
/* block comment,
   can span multiple lines */
```

### Labels

```
main_loop:
    jmp main_loop
```

A label marks the current address (byte offset in the code segment). Label
names are `[A-Za-z_][A-Za-z0-9_]*`. Labels declared but never referenced
produce a warning ("Label 'x' is defined but never used").

### Operand kinds

| Syntax | Kind | Example |
|---|---|---|
| `#N` | register (0–15) | `#0`, `#15` |
| decimal integer | int | `42`, `-7` |
| `0x...` | hex integer | `0x2000` |
| `0b...` | binary integer | `0b1010` |
| `12.5` / `-3.0` | float literal | `12.5` |
| `'c'` | char literal | `'A'`, escapes `\n \r \t \0 \\ \'` |
| bare identifier | label / type name | `loop`, `i32` |
| `$NAME` / `$file.NAME` | `%define` constant reference | `$std.VIDEO_CMD_CLEAR` |

There are 16 general-purpose registers, `#0` through `#15`.

### Typed instructions

Some instructions carry an extra **type** operand right after the
mnemonic, selecting the integer/float width the instruction operates on:

```
mnemonic <type> operand operand ...
```

Valid type names: `i8 i16 i32 u8 u16 u32 i64 u64 f32 f64` (signed/unsigned
integers of 8/16/32/64 bits, plus 32- and 64-bit floats). This is encoded
as one extra type byte in the instruction. Instructions that only make
sense on integers (bitwise ops, shifts, `not`) reject float types with an
error. Untyped instructions (e.g. `mov`, jumps, `push`/`pop`) take no type
operand at all.

Example:

```
movi i32 #0 100      // #0 = 100, interpreted as i32
add  i32 #0 #13      // #0 = #0 + #13, as i32
cmp  u32 #0 #14       // compare #0, #14 as u32
```

---

## 4. Directives

Directives start with `%` and are handled entirely in the preprocessing
pass — they never appear in the encoded output directly.

### `%include "path"`

Textually inlines another `.quartz` file (path relative to the including
file), recursively. Circular includes are detected and reported as an
error.

```
%include "std.quartz"
```

Each included file gets a **file id** (its filename without extension,
e.g. `std.quartz` → `std`). Labels and `%define` constants defined inside
an included file are only visible under that qualification from *other*
files — see §5 below.

### `%runtime N`

Sets the target VM runtime version (an integer `0..255`) written into the
`.bin` header. May be specified once anywhere in the program (including in
an included file); a second occurrence is a warning and is ignored.
Defaults to `255` if omitted.

```
%runtime 5
```

### `%entrypoint <label | address>`

Sets where execution starts. Accepts either a label name or a raw address
(decimal or `0x...` hex, `0..0xFFFFFFFF`). Resolved once every label
address is known; an undefined label produces an error. May be specified
once; a duplicate is a warning. Defaults to address `0`.

```
%entrypoint main
```

### `%data "path/to/descriptor.json"`

Attaches a data segment described by a JSON file (processed by
`quartz/data.py`), embedded after the code in the `.bin` image and copied
into VM RAM at load. May be specified once.

```
%data "picture.data.json"
```

JSON shape:

```json
{
  "start": "0x2000",
  "blobs": [
    {"my_bytes":  {"from_bytes": [1, 2, 3, "0x0A"]}},
    {"my_file":   {"from_file": "sprite.bin"}},
    {"my_string": {"from_string": "hello\n"}}
  ]
}
```

- `start` — the RAM address (decimal or hex) where the data segment is
  loaded.
- `blobs` — an ordered list of named byte blobs, each one of:
  - `from_bytes`: a literal array of byte values (decimal or `"0x.."` hex
    strings).
  - `from_file`: raw bytes of an external file, read as-is.
  - `from_string`: a string encoded one byte per character (not UTF-8 —
    intended for ASCII text/font data).

Blobs are concatenated in declaration order; each blob's offset within the
segment is recorded and written to a `<output>.data-map` file
(`"<addr>:<name>"` per line) for debugging — the VM itself never reads
that file.

### `%define NAME value`

Declares a named constant, referenced later as `$NAME` (or, from another
file, `$file_id.NAME`). The value must be a literal — int, hex, binary,
float, or char — of exactly the same forms accepted for ordinary operands.

```
%define BUS_DEV_VIDEO   0
%define VIDEO_CMD_CLEAR 8
...
movi u32 #10 $BUS_DEV_VIDEO
movi u32 #11 $VIDEO_CMD_CLEAR
```

A `$NAME` reference is substituted with the literal it was defined with
and then validated exactly as if that literal had been written inline —
so `$NAME` can be used anywhere the corresponding literal kind is legal
(an `IMM8`/`IMM16`/`IMM32`/`IMMT`/float/char operand, but **not** in place
of a register or a label/address operand). Redefining the same name in the
same file is an error. Unused constants can optionally be warned about
with `--unused-const-warnings`.

---

## 5. Multi-file programs & name qualification

Each source file (main file or an `%include`d one) gets a short **file
id** — its filename without extension. Labels and `%define` constants
follow the same qualification rule:

- Referenced from **within the file that defines them**: use the bare
  name (`loop`, `$MY_CONST`).
- Referenced from **any other file**: must be qualified as
  `file_id.name` / `$file_id.NAME`.

This lets independently-written include files reuse label/constant names
without clashing. From `hello.quartz`, which includes `std.quartz`:

```
%include "std.quartz"
...
movi u32 #10 $std.BUS_DEV_MOUSE      // std.quartz's BUS_DEV_MOUSE constant
movi u32 #11 $std.MOUSE_PORT_MODE
```

A label reference is never automatically qualified for you across files —
only bare names typed *inside* their own defining file are implicitly
qualified with that file's id internally; a name that already contains a
`.` is left as-is.

---

## 6. Instruction set summary

The full, authoritative table is `quartz/isa.py:_BASE_INSTRUCTIONS` (kept
in sync with the VM's opcode numbering). Operand kinds: `REG` (`#N`),
`IMM8`/`IMM16`/`IMM32` (fixed-width immediate), `IMMT` (immediate whose
width follows the instruction's type byte), `FLOAT` (always 32-bit),
`ADDR` (32-bit code address, written as a label or a raw address).

| Mnemonic | Typed? | Operands | Notes |
|---|---|---|---|
| `nop` | – | – | |
| `halt` | – | – | |
| `rnd` | – | `REG IMM32` | random value, masked |
| `rndseed` | – | `REG` | |
| `wait` / `waiti` | – | `REG` / `IMM32` | |
| `time` / `tick` / `ramsize` | – | `REG` | |
| `vmx` | – | `REG REG REG` | run a raw code blob (see `--raw`) |
| `reset` | – | – | |
| `video` | – | `IMM8 IMM16 FLOAT IMM8` | |
| `videoinf` | – | `REG REG REG REG` | |
| `debug` | typed | `REG` | |
| `mov` | – | `REG REG` | untyped copy |
| `movi` | typed | `REG IMMT` | load immediate |
| `add sub mul div mod` | typed (float ok) | `REG REG` | |
| `and or xor shl shr` | typed (int only) | `REG REG` | |
| `addi subi` | typed | `REG IMMT` | |
| `inc dec` | typed | `REG` | |
| `not` | typed (int only) | `REG` | |
| `cmp` | typed | `REG REG` | |
| `cmpi` | typed | `REG IMMT` | |
| `jmp jz jnz jg jl jge jle` | – | `ADDR` | branch to label/address |
| `jmpr jzr jnzr jgr jlr jger jler` | – | `REG` | branch to address in register |
| `call` | – | `ADDR` | `callr` — `REG` |
| `ret` | – | – | |
| `push` / `pop` | – | `REG` | |
| `load` / `store` | typed | `REG ADDR` / `ADDR REG` | |
| `loadr` / `storer` | typed | `REG REG` | address held in a register |
| `memcpy` / `memset` | – | `REG REG REG` | |
| `bell` | – | – | |
| `busrst` | – | `REG` | |
| `busstat` | – | `REG REG` | |
| `busin` / `busout` | – | `REG REG REG` | device, port, value |
| `businb` / `busoutb` | – | `REG REG REG REG REG` | block (buffered) bus transfer |

The `and`/`or`/`xor`/`shl`/`shr`/`not` family rejects float types
(`f32`/`f64`) at assembly time.

---

## 7. Example: minimal program

```quartz
%runtime 255
%entrypoint main

main:
    movi i32 #0 0        // counter
loop:
    addi i32 #0 1
    cmpi i32 #0 10
    jl   loop
    halt
```

Assemble and inspect:

```
python qasm.py counter.quartz counter.bin --map
python qdisasm.py counter.bin
```

## 8. Example: using `%include` and `%define` (bus I/O)

`std.quartz` declares device/port constants; `hello.quartz` includes it
and drives the "bus" instructions (`busout`/`busin`/`businb`/`busoutb`)
using qualified references (`$std.BUS_DEV_VIDEO`, etc.) — see
`examples/hello.quartz` and `examples/std.quartz` for the full listing.

```quartz
%include "std.quartz"
...
movi u32 #10 $std.BUS_DEV_VIDEO
movi u32 #11 $std.VIDEO_CMD_CLEAR
busout #10 #11 #6
```

## 9. Example: `%data` segment

`examples/picture/picture.quartz` loads a `%data` descriptor
(`picture.data.json`) containing two picture blobs, then streams them to
the video device by address:

```quartz
%include "../std.quartz"
%data "sdk/examples/picture/picture.data.json"

movi u32 #4 0x000108d8   // address of the "Picture2" blob within the data segment
...
busoutb #10 #11 #4 #12 #13
```

See `examples/picture/picture.data.json` for the blob descriptor format
and §4 above for its JSON schema.

---

## 10. Errors and warnings

Errors are reported with file/line/column and a caret pointing at the
offending token; assembly aborts and writes no output if any errors are
found. Warnings (unused labels/constants, register type mismatches with
`--cast-warnings`, negative literals used for unsigned types, duplicate
directive definitions) are printed but don't stop assembly.

Common errors: `Unknown instruction`, `Unknown directive`, `Illegal
operand`, `Undefined label` / `Undefined constant`, `Instruction '...'
expects N operand(s), got M`, `Label '...' already defined`, `Circular
%include detected`.
