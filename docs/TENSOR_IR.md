# Tensor IR (Design + Scope Boundaries)

This document describes a future milestone for a tensor-first intermediate representation (IR) in Curlee, along with determinism constraints and explicit out-of-scope items.

Curlee is verification-first: code should not run unless declared contracts can be discharged. Tensor execution must preserve that bar.

## Goals

- Represent tensor operations as first-class IR nodes.
- Enable an optional lowering path to accelerator backends (NPU/GPU) when deterministic and resource-bounded.
- Preserve verification-first semantics (no proof, no run).
- Keep the initial fragment small and decidable.

## Scope boundaries (explicit)

In-scope for the first Tensor IR milestone:

- A small, explicitly-typed, explicitly-shaped tensor IR.
- Deterministic semantics (same input -> same output) for all supported ops.
- A stable text printer suitable for golden tests.
- A minimal lowering path from Curlee’s front-end into Tensor IR (even if behind a flag).

Out-of-scope initially (must fail with a clear diagnostic if encountered):

- Automatic shape inference beyond trivial consistency checks.
- Data-dependent shapes / dynamic rank.
- Non-deterministic parallel scheduling.
- Approximate math kernels (e.g., “fast math”, non-deterministic reductions).
- Any backend execution that cannot be proven deterministic and resource-bounded.

## IR overview

Tensor IR is a pure, effect-free representation where each op has:

- An op name + fixed semantics.
- Typed operands and a typed result.
- An explicit output shape.
- Explicit preconditions (e.g., shape compatibility).

The IR should be serializable/printable in a stable format.

## Type and shape model

Minimal model (MVP):

- DType: `i32`, `i64`, `f32` (optional at first; `i32`-only is acceptable for MVP).
- Shape: `rank` plus a vector of dimensions.
  - Dimensions are compile-time known integers in the first milestone.
  - “Unknown” dimensions and symbolic constraints are out-of-scope.

Shape inference policy:

- Out-of-scope to infer shapes from dataflow.
- In-scope to check that declared shapes are consistent with op rules (e.g., matmul inner dims match).

## Op set sketch

Start small and decidable. A plausible first op set:

- `const`: create constant tensor (shape + dtype + literal payload)
- `zeros`: allocate zero tensor (shape + dtype)
- `add`, `sub`, `mul`: elementwise (same shape; broadcasting out-of-scope)
- `matmul`: 2D matrix multiply (`[m,k] x [k,n] -> [m,n]`)
- `reshape`: view/reshape with same element count
- `transpose`: permute axes (explicit permutation)
- `slice`: take a rectangular slice (static indices)
- `reduce_sum`: reduce along an axis (defined evaluation order; see determinism)

Every op must declare:

- Shape rule (precondition + result shape)
- DType rule
- Fuel cost model (see resource bounds)

## Determinism constraints

Curlee’s execution must be deterministic; tensor execution inherits this requirement.

Rules:

- The IR semantics define a single evaluation order (no implicit parallelism).
- Reductions (like `reduce_sum`) must define a strict fold order.
- Floating-point behavior must be pinned:
  - Either restrict MVP to integer-only tensors, OR
  - Specify IEEE-754 `f32` semantics on the CPU reference backend and require all backends to match bit-for-bit.

Backends are only permitted if they can demonstrate conformance to the deterministic semantics.

## Lowering points

Potential lowering pipeline:

1. Curlee AST/type-checker identifies tensor-typed values and emits Tensor IR instead of (or alongside) scalar bytecode.
2. Tensor IR is printed for debugging/golden tests.
3. Tensor IR is lowered to:
   - CPU reference executor (interpreter) for correctness and determinism.
   - Optional backend interface for accelerator execution (future).

If any proof obligation for tensor execution cannot be discharged, the compiler must refuse to run.

## Minimal CPU reference execution plan

Implement a CPU reference executor that:

- Interprets Tensor IR ops in program order.
- Uses a deterministic memory layout (e.g., row-major) and a stable allocator.
- Implements ops in straightforward scalar loops (correctness > speed).
- Accounts resource usage with fuel:
  - Charge per op + per element (e.g., `matmul` charges `m*k*n`).
  - Hard-stop once fuel is exhausted, with an error that points to the op span.

This executor is the “source of truth” for backend conformance.

## Test strategy

- Golden tests for the Tensor IR printer (stable text output).
- Unit tests for shape-checking rules per op (good diagnostics on mismatch).
- Determinism tests: same program executed twice yields identical output bytes.
- Backend conformance tests (future): run a fixed corpus through CPU reference vs backend, byte-for-byte compare.
