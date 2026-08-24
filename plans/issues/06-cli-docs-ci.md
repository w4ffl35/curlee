# Title: CLI/docs/CI integration for the freestanding + `phys.mem` path (`curlee build` help, wiki, CI job)

## Problem statement / motivation

Issues 1–5 deliver the freestanding target (Phys type, verifier semantics, codegen, runtime, extern +
boot stub + link). This final issue makes the feature discoverable, documented, and continuously
verified:

1. `curlee --help`/usage text must document the new `build` subcommand and `phys.mem` capability.
2. The wiki is the single source of truth per repo policy
   ([`.github/copilot-instructions.md`](.github/copilot-instructions.md:47)) — syntax, verification
   scope, and runtime pages must be updated in the same work item as the code (each prior issue carries
   its wiki updates; this issue covers the cross-cutting README/CLI/CI surface).
3. CI must actually exercise the freestanding path so it does not rot: build curlee → codegen →
   `gcc -ffreestanding -fno-builtin -nostdlib -c` → link → (qemu if available).

## Design

### CLI

- `curlee --help` gains (mirroring [`cli_impl.ipp`](src/cli/cli_impl.ipp:88)):
  ```
  curlee build [--target freestanding-c] [--link] [-o out.elf|out.c] <entry.curlee>
  curlee run --cap phys.mem <file.curlee>   # (phys.mem is freestanding-only; run rejects Phys programs)
  ```
- `curlee build --help` lists the freestanding subset (no hosted builtins) and the verification gate.
- Capability reference: `phys.mem` documented alongside the existing capability handling in
  [`cli_impl.ipp`](src/cli/cli_impl.ipp:522) (v1 surface list — decide whether `phys.mem` joins the
  v1-forbidden list; recommendation: it is **not** v1-forbidden since it is a real freestanding
  capability, but this must be an explicit decision in this issue).

### README / docs

- README architecture diagram ([`README.md`](README.md:72)) gains the freestanding path:
  `Program → Verifier → codegen → freestanding C → crt0/linker → kernel.elf`, with a "no proof, no
  build" note.
- A new short doc (or wiki page) "Freestanding targets" with the end-to-end hello-kernel walkthrough:
  `curlee build --link` → qemu.

### Wiki (in the `wiki/` repo — separate git repo, gitignored in main per
[`.github/copilot-instructions.md`](.github/copilot-instructions.md:50))

- `Language-Syntax`: `Phys<T>`, `phys<U>(literal)`, `.read()/.write()`, `extern fn` (already per issue
  1/5 — this issue ensures cross-links and the constant-address rule are consistent).
- `Verification-Scope`: trusted deref + opaque MMIO reads + extern-assumed contracts (issue 2/5).
- `Running-Programs`: `curlee build` flags, `phys.mem`, VM restriction, qemu boot steps.
- `Stability-and-Supported-Fragment`: freestanding fragment table + runtime surface (mem/halt/putc).

### CI

New `freestanding` job in [`ci.yml`](.github/workflows/ci.yml):

1. Configure/build curlee (linux-debug preset).
2. Run `curlee build --target freestanding-c` on a fixture; compile result with
   `gcc -ffreestanding -fno-builtin -nostdlib -c`.
3. Link the hello-kernel fixture with `--link`; `objdump -f` entry check.
4. If `qemu-system-x86_64` is present: boot `kernel.elf` and assert the deterministic exit (debug-exit
   port write in the smoke path). If absent: skip with a visible message (fail only on codegen/link
   errors, not on missing qemu).

## MVP scope

In-scope:

- CLI help/usage text, README architecture update, wiki page updates (in `wiki/` repo), CI
  `freestanding` job, capability surface decision for `phys.mem`.

Out-of-scope (explicit):

- Any new language/runtime features (this issue is integration only).
- Windows/macOS CI support (freestanding path is Linux/x86-64; the rest of the matrix stays as-is).

## Acceptance criteria

- [ ] `curlee --help` and `curlee build --help` document the subcommand, flags, freestanding subset,
      and verification gate.
- [ ] README architecture diagram shows the freestanding pipeline.
- [ ] Wiki pages updated in the `wiki/` repo and committed (Language-Syntax, Verification-Scope,
      Running-Programs, Stability-and-Supported-Fragment) with the Phys/extern/runtime/build content
      consistent across pages.
- [ ] `phys.mem` capability surface decision recorded (v1 list) and documented in
      `wiki/Running-Programs`.
- [ ] CI `freestanding` job is green: codegen + `-ffreestanding -fno-builtin -nostdlib -c` +
      link + entry-point check; qemu smoke runs when available and skips visibly otherwise.

## Verification plan

- Manual: `curlee --help`, `curlee build --help`, README render.
- CI: run the new `freestanding` job on a PR from `develop`; confirm the codegen/link/entry checks pass
  and qemu is exercised on the CI runner (or skipped cleanly).
- No behavior change to existing tests; run the full suite once:
  `ctest --preset linux-debug --output-on-failure`.

## Relevant wiki page(s)

- All four pages listed above — this issue is the cross-cutting consistency pass and is the canonical
  place to verify the wiki matches the shipped code (per
  [`.github/copilot-instructions.md`](.github/copilot-instructions.md:54): "update the relevant wiki
  page(s) in the same work item").

## Dependency note

Depends on issues 1–5 being merged (this issue integrates and documents the shipped surface).
