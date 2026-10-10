## Instruction reference

The table below lists every VM opcode, grouped into the same sections (and
in the same order) as the `switch (op)` inside `vm_do_step()` in `vm.c`.

Argument notation used below:

- `REG` — one register index, encoded as a single byte (`0`–`15`)
- `TYPE` — one type tag byte (see the types table above)
- `IMM` — an immediate value, 4 bytes normally, 8 bytes if `TYPE` is
  `I64`/`U64`/`F64`
- `ADDR32` — an absolute code address, 4 bytes (used by jumps/calls) or a
  RAM address, 4 bytes (used by `load`/`store`)
- `u8`/`u16`/`u32`/`f32` (lowercase, immediate) — a fixed-width immediate
  that isn't a typed `IMM` (e.g. `OP_VIDEO`'s operands)
- `(reg)` next to an operand name — that operand is a register holding the
  actual value/address used by the instruction (register-indirect)
- `(none)` — the instruction takes no operands

### ESSENTIALS

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_NOP` | `nop` | `(none)` | Does nothing. |
| `OP_HALT` | `halt` | `(none)` | Stops the VM (window closes automatically). |
| `OP_RAND` | `rnd` | `REG IMM32` | `dst`, `max`. Writes a random value in `[0, max)` into `dst` (treats `max=0` as `1`). |
| `OP_RAND_SEED` | `rndseed` | `seed(reg)` | Seeds the pseudo-random number generator used by `rnd` with `reg[seed]` (low 32 bits, via `srand()`). Without an explicit seed, the VM uses whatever default seed the runtime started with. |
| `OP_WAIT` | `wait` | `REG` | Sleeps for `reg[R]` microseconds (hybrid sleep, not a CPU-burning busy loop; capped at 5 seconds). |
| `OP_WAITI` | `waiti` | `IMM32` | Sleeps for a fixed number of microseconds (same cap as `wait`). |
| `OP_TIME` | `time` | `dst(reg)` | Stores the current wall-clock time, in milliseconds since the Unix epoch, into `dst`. Resolution is milliseconds and depends on the system clock. |
| `OP_TICK` | `tick` | `dst(reg)` | Stores the number of frames rendered so far into `dst`. Incremented by 1 on every `vsync`; reset to 0 on a full VM reset (`OP_RESET` / `Ctrl+Alt+R`), but **not** by `OP_VMX`. |
| `OP_RAMSIZE` | `ramsize` | `dst(reg)` | Stores the total size of the VM's RAM, in bytes, into `dst`. |
| `OP_VMX` | `vmx` | `code_addr(reg) code_len(reg) entrypoint(reg)` | "Warm restart": replaces the running code with new code copied from RAM at `code_addr`/`code_len` (raw machine code, same format as a `.bin`'s code segment without the file header). RAM is **not** touched — it stays exactly as the previous run left it. Registers, flags, and both stacks are zeroed, and `pc` jumps to `entrypoint`. The program's original data segment is not reloaded. Faults if `code_len == 0`, the code range doesn't fit in RAM, or `entrypoint >= code_len`. |
| `OP_RESET` | `reset` | `(none)` | "Cold" restart, identical to `Ctrl+Alt+R`: zeroes registers/flags/stacks/RAM, reloads the program's data segment, resets `pc` to the original entry point, and clears both display layers. Unlike `OP_VMX`, RAM is **not** preserved. |
| `OP_VIDEO` | `video` | `video_mode(u8) fps(u16) scale(f32) cursor(u8)` (immediates) | Changes the active video mode, target FPS, window scale, and OS mouse cursor visibility at runtime (same knobs as `--videomode`/`--fps`/`--scale`/`--cursor`). `video_mode` must be `1`/`2`/`3`, otherwise the VM faults and none of the four values change. `fps` goes straight to `SetTargetFPS()` (`0` = uncapped); `scale` is clamped to `[RT_MIN_SCALE, RT_MAX_SCALE]`; `cursor` uses `0`=hidden / nonzero=visible. **Switching video modes reallocates the framebuffer and text layer — old content is not preserved.** |
| `OP_VIDEO_INFO` | `videoinf` | `dst_mode(reg) dst_fps(reg) dst_scale(reg) dst_cursor(reg)` | Reads back the current video mode, FPS, window scale, and cursor visibility (whatever `OP_VIDEO`, the startup CLI options, or `Ctrl+Alt+PgUp`/`PgDn`/`S` last set). `dst_scale` receives the **raw bit pattern** of the `f32` value (read it with a `TYPE_F32` operation, not as a plain integer). `dst_cursor` receives a plain `0`/`1`. |

### DEBUG

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_DEBUG` | `debug` | `TYPE REG` | Debug-prints a register's value (interpreted as `TYPE`) to stdout. |

### DATA MOVE

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_MOV` | `mov` | `REG REG` | `dst`, `src`. Raw 64-bit register copy. |
| `OP_MOVI` | `movi` | `TYPE REG IMM` | `dst`, immediate. Loads an immediate value (normalized to `TYPE`) into `dst`. |

### ARITHMETIC (REGISTER, REGISTER)

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_ADD` | `add` | `TYPE REG REG` | `dst`, `src`. `dst = dst + src` (as `TYPE`). |
| `OP_SUB` | `sub` | `TYPE REG REG` | `dst = dst - src`. |
| `OP_MUL` | `mul` | `TYPE REG REG` | `dst = dst * src`. |
| `OP_DIV` | `div` | `TYPE REG REG` | `dst = dst / src`. Faults on integer division by zero. |
| `OP_MOD` | `mod` | `TYPE REG REG` | `dst = dst % src` (or `fmod` for float types). Faults on integer division by zero. |
| `OP_AND` | `and` | `TYPE REG REG` | `dst = dst & src`. Integer types only. |
| `OP_OR` | `or` | `TYPE REG REG` | `dst = dst \| src`. Integer types only. |
| `OP_XOR` | `xor` | `TYPE REG REG` | `dst = dst ^ src`. Integer types only. |
| `OP_SHL` | `shl` | `TYPE REG REG` | `dst = dst << (src & 63)`. Integer types only. |
| `OP_SHR` | `shr` | `TYPE REG REG` | `dst = dst >> (src & 63)`. Logical shift for unsigned types, arithmetic for signed. |

### ARITHMETIC (REGISTER, IMMEDIATE)

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_ADDI` | `addi` | `TYPE REG IMM` | `dst = dst + imm`. |
| `OP_SUBI` | `subi` | `TYPE REG IMM` | `dst = dst - imm`. |
| `OP_INC` | `inc` | `TYPE REG` | `dst = dst + 1` (`1.0` for float types). |
| `OP_DEC` | `dec` | `TYPE REG` | `dst = dst - 1`. |
| `OP_NOT` | `not` | `TYPE REG` | `dst = ~dst` (bitwise). Integer types only. |

### COMPARATION

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_CMP` | `cmp` | `TYPE REG REG` | Compares `reg[a]` vs `reg[b]` as `TYPE`; sets the zero/negative flags used by conditional jumps. |
| `OP_CMPI` | `cmpi` | `TYPE REG IMM` | Compares `reg[a]` vs an immediate as `TYPE`. |

### JUMPS

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_JMP` | `jmp` | `ADDR32` | Unconditional jump. |
| `OP_JZ` | `jz` | `ADDR32` | Jump if equal (zero flag set). |
| `OP_JNZ` | `jnz` | `ADDR32` | Jump if not equal. |
| `OP_JG` | `jg` | `ADDR32` | Jump if greater. |
| `OP_JL` | `jl` | `ADDR32` | Jump if less. |
| `OP_JGE` | `jge` | `ADDR32` | Jump if greater or equal. |
| `OP_JLE` | `jle` | `ADDR32` | Jump if less or equal. |

### JUMPS (REGISTER)

Unlike `jmp`/`jz`/`jnz`/.../`call`, which take their target address as a
`u32` immediate embedded directly in the code stream, the instructions
below read their target address at runtime from a register. This is useful
for computed jumps, jump tables, or calling a function pointer stored in
RAM. None of these validate that the target address is within `code_len` —
behavior matches the immediate-address family: an invalid address simply
faults naturally on the next instruction fetch (out-of-bounds PC).

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_JMPR` | `jmpr` | `addr(reg)` | Unconditional jump: `pc = addr`. |
| `OP_JZR` | `jzr` | `addr(reg)` | Jump to `addr` if the zero flag is set (register-address equivalent of `jz`). |
| `OP_JNZR` | `jnzr` | `addr(reg)` | Jump to `addr` if the zero flag is **not** set (equivalent of `jnz`). |
| `OP_JGR` | `jgr` | `addr(reg)` | Jump to `addr` if greater-than (equivalent of `jg`). |
| `OP_JLR` | `jlr` | `addr(reg)` | Jump to `addr` if less-than (equivalent of `jl`). |
| `OP_JGER` | `jger` | `addr(reg)` | Jump to `addr` if greater-or-equal (equivalent of `jge`). |
| `OP_JLER` | `jler` | `addr(reg)` | Jump to `addr` if less-or-equal (equivalent of `jle`). |

### CALL FUNCTIONS

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_CALLR` | `callr` | `addr(reg)` | Register-addressed call: pushes the return address (the instruction right after `callr`) onto the call stack, then sets `pc = addr`. Faults with a call-stack overflow if the stack is already full. Returns normally via `ret`. |
| `OP_CALL` | `call` | `ADDR32` | Pushes the return address onto the call stack, then jumps to the immediate address. |
| `OP_RET` | `ret` | `(none)` | Pops an address off the call stack and jumps to it. |

### DATA STACK

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_PUSH` | `push` | `REG` | Pushes `reg[R]` onto the data stack. |
| `OP_POP` | `pop` | `REG` | Pops the data stack into `reg[R]`. |

### MEMORY ACCESS

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_LOAD` | `load` | `TYPE REG ADDR32` | `dst`, address. `reg[dst] = RAM[addr]`, read as `TYPE`. |
| `OP_STORE` | `store` | `TYPE ADDR32 REG` | address, `src`. `RAM[addr] = reg[src]`, written as `TYPE`. |
| `OP_LOADR` | `loadr` | `TYPE REG REG` | `dst`, `addr_reg`. Register-indirect load: `reg[dst] = RAM[reg[addr_reg]]`. |
| `OP_STORER` | `storer` | `TYPE REG REG` | `addr_reg`, `src`. Register-indirect store: `RAM[reg[addr_reg]] = reg[src]`. |

### MEMORY (BLOCK)

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_MEMCPY` | `memcpy` | `dst_addr(reg) src_addr(reg) len(reg)` | Copies `len` bytes from RAM `src_addr` to RAM `dst_addr`. Both `[dst_addr, dst_addr+len)` and `[src_addr, src_addr+len)` must lie entirely within RAM. Implemented with `memmove`, so overlapping ranges are safe. `len == 0` is a no-op. |
| `OP_MEMSET` | `memset` | `addr(reg) value(reg) len(reg)` | Fills `len` bytes at RAM `addr` with the low byte of `value`. `[addr, addr+len)` must lie entirely within RAM. `len == 0` is a no-op. |

### SOUND

| OP_CODE | Mnemonic | Arguments | Description |
|---|---|---|---|
| `OP_BEEP` | `beep` | `(none)` | Plays the fixed, built-in bell sound. |
