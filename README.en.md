# ImageDiscropper

[![CI](https://github.com/GoldenCreeperJ/ImageDiscropper/actions/workflows/ci.yml/badge.svg)](https://github.com/GoldenCreeperJ/ImageDiscropper/actions/workflows/ci.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)](.github/workflows/ci.yml)

**Image region extraction, reverse erasing & grid splitting tool** | [中文](README.md)

> In one sentence: conventional cropping only answers "which part to keep"; this tool answers "**which lines to cut along, which blocks to keep, and how to reassemble them**".

ImageDiscropper is a **fully local** image cutting tool. Every feature reduces to a single **Grid-Selection-Emit** pipeline:
full-span cut lines induce a regular grid over the image, a selection set plus a polarity decide what is kept/removed,
and the result is exported by separating, collapsing, or rearranging.

---
## 🤖 AI-led development disclosure

This project is primarily AI-led (~99%), and the documentation is AI-generated as well. The functional spec is
[`SPEC.md`](SPEC.md); please read [`CONTRIBUTING.md`](CONTRIBUTING.md) before contributing.

## ✨ Features

- **Three tiers, one engine** (NFR-0: tiers are just parameter presets; L1 ⊂ L2 ⊂ L3):

  | Tier   | Mode                | Positioning               | Description                                                                                                                                              |
  |--------|---------------------|---------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------|
  | **L1** | Standard extraction | Compatibility baseline    | Plain rectangle / horizontal band / vertical band extraction; polarity is always `keep`                                                                  |
  | **L2** | ⭐ Reverse erasing   | Differentiating core      | **Erase by lines**: pull out the middle seam and join both sides — zero fabrication, lossless; supports cross / horizontal / vertical / multi-rect union |
  | **L3** | ⭐ Grid splitting    | Complete expression layer | Parameterized grid + free cell picking + explicit ordering + rearrange composition (jigsaw shuffling, atlas slicing)                                     |

- **Axiom**: cut lines are **full-span straight lines across the whole image**, not segments — the fundamental premise that separates this tool from conventional cropping.
- **Three export styles**: separate (one file per kept block) / collapse (join remaining blocks after removing whole rows/columns) / rearrange (fill blocks into a new canvas by an explicit sequence).
- **Pre-processing layer**: rotate, flip, resize, grayscale, invert, channel split; independent vector **annotation layer** (optional burn-in at export).
- **Local processing, lossless first** (NFR-1/2): images never leave your machine; cutting is pure pixel relocation — only JPEG output is lossy.
- **GUI + CLI frontends**: Qt 6 desktop app (live preview, draggable cut lines, undo/redo) plus the `idc` command line (scriptable, config-driven).

```text
Source → ① cut-line set → ② induced grid → ③ selection set → ④ polarity (Keep/Remove) → ⑤ emit (Collapse/Rearrange)
```

## 📁 Repository layout

```text
ImageDiscropper/
├── ImageDiscropperCpp/          # C++ implementation (current mainline, v1.0.0)
│   ├── ImageDiscropperCore/     #   Core: Grid-Selection-Emit engine + basic image processing (static library)
│   ├── ImageDiscropperCli/      #   CLI: thin command-line shell (executable idc)
│   └── ImageDiscropperGui/      #   GUI: Qt 6 desktop frontend (executable idc_gui)
└── ImageDiscropperRust/         # Rust implementation (planned, not started yet)
```

```text
GUI ──────┐
          ├── Core
CLI ──────┘
```

## 🚀 Quick start

### Prerequisites

- CMake ≥ 3.28 + a C++17 compiler (MSVC / MinGW / Clang)
- [vcpkg](https://vcpkg.io) (manifest mode installs everything automatically — no manual `vcpkg install`): point the `VCPKG_ROOT` environment variable at your vcpkg directory
- The dependency manifest lives in [`ImageDiscropperCpp/vcpkg.json`](ImageDiscropperCpp/vcpkg.json) (`stb` / `libwebp` / `nlohmann-json` / `qtbase`)

### Build (Windows / Linux / macOS)

CMake Presets are the recommended path (the first configure installs dependencies from the manifest; `qtbase` takes a while):

```bash
cd ImageDiscropperCpp
cmake --preset windows         # Windows: Ninja + MSVC (Debug; windows-release = static Qt, single file)
cmake --build build/windows
ctest --test-dir build/windows --output-on-failure   # Core unit_tests + CLI cli_tests

# Linux / macOS: single-config Ninja presets (linux-debug / linux-release / macos-debug / macos-release)
cmake --preset linux-debug
cmake --build build/linux-debug
ctest --test-dir build/linux-debug --output-on-failure
```

Or configure manually (manifest mode activates automatically, no preinstalled packages needed):

```bash
cmake -S ImageDiscropperCpp -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build
```

Binaries land in the build directory's `bin/`: `idc(.exe)`, `idc_gui(.exe)`, `demo(.exe)`.
(CLion users can open `ImageDiscropperCpp/` directly as the CMake source directory; CLion picks up the presets.)

Every push is verified by GitHub Actions ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)): **three-platform Release builds (GUI included, identical to the shipped artifacts) plus the full test suite** (minute-level with a warm dependency cache); Debug-configuration GUI verification on all three platforms runs manually ([`.github/workflows/dbg-verify.yml`](.github/workflows/dbg-verify.yml)).

To skip building the GUI app: `cmake --preset windows -DIDC_BUILD_GUI=OFF` (qtbase is still installed).

### Release

Tagging `v*` publishes automatically ([`.github/workflows/cd.yml`](.github/workflows/cd.yml)): three-platform Release builds (**static Qt, single-file executables**) →
uploads **6 executables** (3 platforms × GUI/CLI) plus a `SHA256SUMS` checksum file → creates a GitHub Release.

```bash
git tag v1.0.0 && git push origin v1.0.0
```

The CD can also be triggered manually from the Actions tab — just enter a tag name (created automatically at the current HEAD if missing; no push needed).

> Binaries are unsigned for now (SmartScreen may warn on Windows); Linux builds require system X11 libraries (present on any normal desktop).

### CLI quick tour

```bash
idc extract --input photo.jpg --rect 100,100,300,250 --output out.png
idc erase   --input photo.jpg --rect 100,100,300,250 --merge collapse --output out.png
idc erase   --input photo.jpg --rect 100,100,300,250 --output-dir ./out/ --format png
idc grid    --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 \
            --compose --canvas 2x2 --output result.png
idc config  --load my-config.json --input photo.jpg --output result.png
```

See `idc <command> --help` for all options. GUI operations are documented in [`ImageDiscropperGui/README.md`](ImageDiscropperCpp/ImageDiscropperGui/README.md) (Chinese).

## 📚 Documentation index

Documentation is organized by level; **each level has a clearly delimited scope and levels do not repeat each other**:

| Level                         | Documents                                                        | Scope                                                                                                                                                    |
|-------------------------------|------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------|
| ① Repo front page             | [`README.md`](README.md) / this file                             | What the project is, features, quick start, doc navigation, license — **an index only**, no deep technical detail                                        |
| ② Contributing                | [`CONTRIBUTING.md`](CONTRIBUTING.md)                             | Build & test workflow, code/doc conventions, commit & PR etiquette                                                                                       |
| ③ C++ implementation overview | [`ImageDiscropperCpp/README.md`](ImageDiscropperCpp/README.md)   | Composition of the C++ codebase, mapping to spec concepts, build & run, implementation status                                                            |
| ④ Functional spec             | [`SPEC.md`](SPEC.md)                                             | **The single behavioral baseline**: modes / export / boundary cases (E-1~E-8) / NFRs / config schema; contributing conventions live in `CONTRIBUTING.md` |
| ⑤ Module docs                 | `ImageDiscropper{Core,Cli,Gui}/README.md`                        | Each module's responsibility, API mapping to the spec, build instructions; the GUI README also covers user operations and design notes                   |
| ⑥ Directory docs              | `README.md` inside every `include/`, `src/`, … subdirectory      | That directory's responsibility boundary, file-splitting rationale, file list (repo-wide directory doc convention)                                       |
| ⑦ Placeholder implementation  | [`ImageDiscropperRust/README.md`](ImageDiscropperRust/README.md) | Planning status of the Rust implementation                                                                                                               |

## 📊 Project status

| Phase   | Scope                                                                                           | Status                                                                                       |
|---------|-------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------|
| **MVP** | Unified engine + L2 reverse erasing + L1 extraction + separate/collapse export                  | ✅ Done                                                                                       |
| **v2**  | L3 grid splitting (ordering + rearrange composition) + config presets, multi-rect union erasing | ✅ Done                                                                                       |
| **v3**  | Basic image processing (FR-1), annotation layer                                                 | ⏳ Core support layer ready; GUI frontend done; full CLI/GUI ↔ engine integration in progress |

## 📄 License

The project as a whole is licensed under [GPL-3.0](LICENSE), covering every language's Core implementation (C++ and the future Rust one, etc.) as well as the CLI and GUI.
Community standards: [Code of Conduct](CODE_OF_CONDUCT.md); vulnerability reporting: [Security Policy](SECURITY.md).
