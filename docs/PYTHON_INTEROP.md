# Python Interop (Future)

This document outlines the design for Python interoperability in Curlee, explicitly gated behind an `unsafe` boundary and capability checks.

## Unsafe boundary

- All Python calls must be wrapped in an explicit `unsafe` block.
- Unsafe blocks are required for any FFI escape hatch.
- The compiler/VM must refuse to execute Python interop without the required capability.

## Proposed `python_ffi` module boundary

A dedicated module exposes the interop API:

- `python_ffi.call(module, function, args...) -> String`

The module is only available inside `unsafe` and requires the `python:ffi` capability.

## Calling convention (MVP)

- Arguments are marshaled as strings or integers (no object graph sharing in MVP).
- Return values are strings or integers.
- Errors are surfaced as Curlee diagnostics with span information.

## Embedding approach

**Preferred (MVP):** subprocess execution

- Launch `python` as a subprocess.
- Serialize args via stdin/stdout.
- Avoids ABI coupling with libpython.

**Alternative (future):** embed libpython

- Requires stable ABI handling and version pinning.

## Curlee shield pattern

Curlee validates inputs and contracts, then delegates work to Python:

```
unsafe {
  requires cap python:ffi;
  let result = python_ffi.call("math", "sqrt", "4");
  return parse_int(result);
}
```

## Capability requirements

- `python:ffi` is required to execute any Python interop call.
- Without `python:ffi`, the runtime must refuse to execute.

## Open questions

- How should Python errors map to Curlee diagnostics?
- Should subprocess execution be sandboxed or capability-scoped?
- How are large payloads streamed safely?
- What is the minimal type conversion set for MVP?
