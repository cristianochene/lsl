# lsl (LS + Lua)

`lsl` is a lightweight, high-performance command-line directory listing tool written in C and fully customizable via Lua. It serves as a modern, scriptable alternative to traditional `ls`, allowing you to define formatting, layout, file icons, and color rules entirely through Lua configuration files.

---

## Features

- **Lua-Powered Customization:** Control display formats, column widths, colors, and layout logic using Lua.
- **High Performance:** Runs up to **1.8x faster than standard `ls -la`** with minimal overhead.
- **Smart Metadata Handling:** Detects file types, executable binaries, symbolic links, file permissions, modified timestamps, and human-readable file sizes.
- **Flexible Sorting:** Sort by name, file size, or extension in ascending or descending order, with directories prioritized at the top.
- **Custom Visuals:** Easily map file extensions to custom icons, emojis, and ANSI colors.
- **Multiple Layouts:** Supports both grid and detailed list view modes.

---

## Benchmarks

| Command | Execution Time (Mean ± σ) | Relative Speed |
| :--- | :--- | :--- |
| **`lsl '.'`** | **687.3 µs ± 69.4 µs** | **1.00x (Fastest)** |
| **`ls -la '.'`** | 1.5 ms ± 0.1 ms | 2.12x slower |
| **`lsd -la '.'`** | 3.0 ms ± 0.2 ms | 4.41x slower |
| **`eza -la '.'`** | 3.0 ms ± 0.2 ms | 4.42x slower |

`lsl` runs **~2.1x faster than standard GNU `ls`** and **~4.4x faster than modern Rust-based alternatives (`eza`, `lsd`)**.

## Installation

### Prerequisites

Ensure you have GCC and the Lua development libraries installed:

```bash
# Ubuntu / Debian
sudo apt install build-essential liblua5.3-dev

# Arch Linux
sudo pacman -S base-devel lua

# Fedora
sudo dnf install gcc lua-devel
