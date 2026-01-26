# Tensor-first IR (Future)

This document captures a future milestone for a tensor-first intermediate representation (IR) and optional accelerator backends.

## Goals

- Represent tensor operations as first-class IR nodes.
- Enable optional lowering to NPU/GPU backends.
- Preserve verification-first semantics (no proof, no run).

## Open questions

- What tensor shapes and dtypes are in the MVP scope?
- How are effects and capabilities modeled for accelerator execution?
- How does the IR preserve contracts and refinement info?
- What is the minimal backend interface for deterministic execution?
- How do we enforce resource bounds (fuel) on accelerator runs?
