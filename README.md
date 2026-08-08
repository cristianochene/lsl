# `lsl` — Next-Generation Directory Listing

`lsl` is a Linux directory-listing tool that keeps its performance-critical path in C while providing an optional Lua/LuaJIT presentation layer. The default mode **does not create a Lua VM**: it scans, sorts, and writes using native buffers only.

> This project is Linux-only because its scanner intentionally uses `SYS_getdents64` and `struct linux_dirent64`.

## C and Lua Architecture

```text
open(O_DIRECTORY) → getdents64 (256 KiB) → contiguous Entry[] → qsort
                           ↘ string arena                ↘ write(2) buffer
                                                        ↘ Lua (opt-in)
```

### C Engine

* **Batched system calls:** `getdents64` fills 256 KiB buffers and provides `d_type`. `fstatat(..., AT_SYMLINK_NOFOLLOW)` is called only for `-l`, `--stats`, `DT_UNKNOWN`, or when a Lua formatter requests metadata.
* **Predictable memory use:** names and paths live in 64 KiB arena blocks, while `Entry` descriptors live in a geometrically growing contiguous array. There is no allocation per directory entry.
* **Fast path:** no Lua symbol or state participates unless `--lua` or `--config` is specified. Output accumulates in a 256 KiB buffer and is flushed with `write(2)`.
* **Non-recursive tree traversal:** `--tree` uses an explicit directory stack, so process stack usage does not grow with directory depth.
* **Native sorting:** C performs natural-name sorting with `qsort` and `strverscmp`. `--no-sort` skips that work and preserves kernel order.

### Zero-Copy Lua Bridge

Lua support is strictly opt-in. Each callback receives a full userdata containing only a pointer to an `Entry` in the C array. The `lsl.entry.__index` metatable reads fields directly instead of constructing and copying a Lua table for every file.

Cheap fields (`name`, `path`, `extension`, type, and depth) never issue a metadata call. Reading `size`, `mtime`, `mode`, or `is_executable` triggers lazy metadata loading once for that entry.

A configuration file may provide either or both callbacks:

```lua
function filter_entry(file) return true end -- optional
function format_entry(file) return file.name end -- optional
```

Available fields are `name`, `path`, `extension`, `is_dir`, `is_symlink`, `depth`, `size`, `mtime`, `mode`, and `is_executable`. Strings reference arena-backed storage during the callback. The result of `format_entry` is copied directly into the output buffer before the Lua value is removed from the stack.

## Implemented Features

* Nerd Fonts icons and ANSI colors in the native renderer, with replaceable extension and theme mappings in `config.lua`;
* long listing mode (`-l`) with lazy metadata;
* iterative tree traversal (`--tree`), hidden entries (`-a`), and directory-first sorting (`--dirs-first`);
* Lua filters and formatting through `--lua` or `--config FILE`;
* a statistics footer (`--stats`) containing file, directory, link, and byte totals;
* reverse sorting (`--reverse`) and sorting bypass (`--no-sort`);
* built-in command-line help through `-h` or `--help`.

## Building

### Dependencies

```bash
# Debian/Ubuntu — choose an available Lua version or LuaJIT
sudo apt install build-essential pkg-config liblua5.5-dev
# sudo apt install build-essential pkg-config liblua5.4-dev
# sudo apt install build-essential pkg-config libluajit-5.1-dev
```

The Makefile searches for `luajit`, `lua5.5`, `lua5.4`, `lua5.3`, and `lua`, in that order, through `pkg-config`. If no Lua development package is available, it deliberately builds a C-only fast-path binary; that binary reports a clear error when Lua options are requested.

```bash
make clean && make
sudo make install
```

The default optimized profile is equivalent to:

```bash
gcc -std=c11 -O3 -march=native -flto -Wall -Wextra -Wpedantic \
  $(pkg-config --cflags lua5.5) -DLSL_WITH_LUA main.c -o lsl \
  -flto $(pkg-config --libs lua5.5)
```

To use Clang or produce a binary without host-specific instructions:

```bash
make clean && make CC=clang
make clean && make CFLAGS='-O3 -flto -std=c11 -Wall -Wextra -Wpedantic'
```

## Usage

```bash
lsl                       # C fast path in the current directory
lsl -a --dirs-first /tmp
lsl -l --stats ~/src
lsl --tree --no-sort .
lsl --lua                 # load ~/.config/lsl/config.lua
lsl --config ./config.lua # load an explicit theme/filter
```

The repository includes a default [`config.lua`](config.lua). Install it as the user configuration with:

```bash
mkdir -p ~/.config/lsl
cp config.lua ~/.config/lsl/config.lua
lsl --lua
```

Run `lsl --help` for the concise command-line reference.

## Reproducible Benchmarks

Absolute timing claims are not published without identifying the hardware, filesystem, cache state, and dataset. The following procedure creates an identical 100,000-entry workload, warms the page cache, and measures the fast and detailed paths separately:

```bash
mkdir -p /tmp/lsl-bench
seq 1 100000 | xargs -P8 -I{} touch /tmp/lsl-bench/file-{}.txt
lsl --no-sort /tmp/lsl-bench >/dev/null # warm-up
hyperfine --warmup 5 \
  'lsl --no-sort /tmp/lsl-bench >/dev/null' \
  'ls -U /tmp/lsl-bench >/dev/null' \
  'eza -U /tmp/lsl-bench >/dev/null'

hyperfine --warmup 3 \
  'lsl -l /tmp/lsl-bench >/dev/null' \
  'ls -l /tmp/lsl-bench >/dev/null' \
  'eza -l /tmp/lsl-bench >/dev/null'
```

Always compare equivalent modes: `--no-sort` against `ls -U` and `eza -U`, and `-l` against other long-listing modes. The architecture minimizes system calls and allocations, but no tool can guarantee a universal win because the kernel, libc, filesystem, cache, terminal, and selected options all affect results.

## Limitations and Security

* Lua configuration executes unsandboxed user code. Load trusted files only.
* Colors and icons require a compatible terminal and Nerd Font. This version preserves ANSI sequences when output is redirected.
* Byte totals use logical file size (`st_size`), not allocated blocks, and do not follow symbolic links.
