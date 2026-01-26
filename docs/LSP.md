# Curlee LSP (MVP)

Curlee ships a minimal Language Server Protocol (LSP) server for diagnostics, go-to-definition, and hover types.

## Build

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug --target curlee_lsp
```

## Run

The server speaks LSP over stdio:

```bash
./build/linux-debug/curlee_lsp
```

Configure your editor to launch the executable above as an LSP server.

## Manual test recipe

1. Start the server in a terminal:
   ```bash
   ./build/linux-debug/curlee_lsp
   ```
2. Send an `initialize` request over stdin:
   ```
   Content-Length: 65

   {"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
   ```
3. Open a Curlee document:
   ```
   Content-Length: 142

   {"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/test.curlee","text":"fn main() { return 1 + 2; }"}}}
   ```
4. Request hover types:
   ```
   Content-Length: 136

   {"jsonrpc":"2.0","id":2,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/test.curlee"},"position":{"line":0,"character":20}}}
   ```

You should receive JSON-RPC responses with capability info, diagnostics, and hover content.
