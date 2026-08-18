# Security Policy

Curlee is a verification-first language whose whole point is making unsafe
programs *not run*. We take security reports seriously — especially anything
that defeats the verification gate, the capability model, the fuel-bounded VM,
the bundle format, or the Python sandbox.

## Supported versions

Curlee is pre-1.0 and in "production-readiness stabilization". Security fixes
land on the current `master` and are backported only when a release explicitly
supports it. Assume only the latest tagged release is supported.

## Reporting a vulnerability

**Do not open a public issue for a security vulnerability.**

Use the repository's private vulnerability reporting path:

- GitHub: **Security → Report a vulnerability** on this repository
  (https://github.com/w4ffl35/curlee/security/advisories)

Please include:

1. The affected component (lexer / parser / resolver / type checker /
   verifier / emitter / VM / bundle / CLI / LSP / Python interop).
2. A minimal reproduction (source file or bundle bytes) if possible.
3. Impact: what an attacker can do (e.g., verification bypass, capability
   escape, sandbox escape, fuel exhaustion, memory unsafety).
4. Environment: OS, compiler, Z3 source (system vs vendored), release/branch.

You will receive a response within 5 business days. We ask that you do not
publicly disclose the issue until a fix is released and announced.

## Scope

In scope:

- Compiler verification bypasses (proving false obligations or skipping checks).
- Capability/crossing enforcement failures.
- Sandbox escape or escape from the deterministic VM.
- Memory safety issues in the toolchain.
- Bundle parser robustness (bundles are untrusted input by design).
- Supply-chain issues in the build (e.g., unverified vendored dependencies).

Out of scope (by design, documented):

- The `python_ffi` stub (not yet implemented).
- The bundle manifest hash is integrity-only, not cryptographic authentication.
- The vendored Z3 path builds Z3 from a pinned tag; verify your clone's git
  history if you consume prebuilt artifacts.
