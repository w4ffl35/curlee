Golden diagnostics fixtures.

- Each `*.curlee` contains exactly one marked span: `[[...]]`.
- The corresponding `*.golden` file is the expected output of the diagnostic renderer.
- Tests strip the `[[` and `]]` markers before rendering.
