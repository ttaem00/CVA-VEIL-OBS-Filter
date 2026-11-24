# VEIL OBS Filter — Agent & Multi-LLM Operating Guide

This public repository (`ttaem00/CVA-VEIL-OBS-Filter`) contains **VEIL Filter**, a Windows x64 native OBS Studio plugin (verified against OBS Studio 33.0.1) for face/skin/makeup refinement and portrait/chroma background matting.

## 1. Core Invariants, Licensing & Safety

- Preserve component-level licensing and notices ([LICENSE](LICENSE), [CREDITS.md](CREDITS.md), [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md), `LICENSES/`): GPL-2.0-or-later for face beauty integration, GPL-3.0-or-later for portrait matting integration, and separate Microsoft DirectML redistribution terms.
- Keep brand icon provenance ([assets/brand/README.md](assets/brand/README.md), `assets/brand/source-manifest.json`), user preset compatibility, comparison-mode return controls, and installer rollback ([installer/Installer.cs](installer/Installer.cs)) intact.
- Never commit build outputs (`work/`, `dist/`), personal OBS profiles/scenes, machine-specific user paths, or credentials.

## 2. Build & Validation Commands

Run from PowerShell 7 (`pwsh.exe`) with pinned toolchain dependencies (`docs/BUILD.md`):

```powershell
pwsh.exe -NoProfile -ExecutionPolicy Bypass -File ./build.ps1 -Configuration Release
```

- `build.ps1` configures Ninja + `toolchain-llvm-mingw.cmake`, builds the native plugin, runs `ctest --output-on-failure`, and stages files to `dist/obs-trueitem-beauty` without modifying a live OBS installation.
- Report CTest/build results, installer self-tests, and live OBS studio verification as separate evidence scopes.

## 3. Multi-LLM Coexistence (`Codex` · `Claude Code` · `Antigravity`)

- `AGENTS.md` is the single canonical guide for all coding agents; do not add duplicate root instruction files.
- Never reset or clean uncommitted files in an existing checkout. Use an isolated issue branch/worktree (`codex/issue-<N>-<slug>`) and **merge-commit PRs only** (no direct push to `main`, no squash/rebase, no `--no-verify`), backed by independent `/adversarial-review`.
