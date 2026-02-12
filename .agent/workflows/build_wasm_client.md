---
description: Build the Perun WASM Client
---

1. Navigate to the web client directory
```bash
cd perun-web-client
```

2. Build the WASM client for the web target
// turbo
```bash
wasm-pack build --target web
```

3. Verify the output
// turbo
```bash
ls -F pkg/
```
