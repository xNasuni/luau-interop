# Web interop regressions

Build `Luau.Web.JSPI` and `Luau.Web.Asyncify` using Emscripten and the repository's
CMake configuration. Then, with Node 24:

```sh
cd tests/web
npm ci --ignore-scripts
npx playwright install chromium
npm test -- --backend JSPI --build-dir ../../build-web
npm test -- --backend Asyncify --build-dir ../../build-web
```

`--build-dir` is relative to the working directory. The harness copies the unchanged
`luau-web@1.5.0` front entrypoint and the selected freshly built backend to an ignored
temporary directory; it does not patch an installed dependency. The npm package
retains its own license. Only the selected backend is available, preventing silent
fallback. `--report results.json` optionally saves the result and backend hash.

JSPI executes in a real Chromium module worker. Asyncify executes in a fresh browser
window because the current build excludes the worker environment. The test deletes
the JSPI feature properties in that isolated context to select the front API's
existing fallback. This is backend selection for testing, not a production patch.
Both environments have a bounded execution deadline and are closed after each run.

The cases exercise incoming UTF-8 byte lengths, embedded NUL in values and keys,
object reads/iteration, callback returns, caught JavaScript errors, and raw global
assignment. Point the same runner at an affected build to verify it exits nonzero.
The raw-global call includes the candidate ABI's extra length arguments; an older
build ignores them and demonstrates the truncation. The public front API is unchanged.

The lifetime suite runs in one loaded runtime (not a fresh worker per VM):

```sh
npm test -- --backend JSPI --build-dir ../../build-web --suite lifetime
npm test -- --backend Asyncify --build-dir ../../build-web --suite lifetime
```

It covers 100 recreation cycles, a second live VM during repeated close/recreate,
closed wrappers, coroutine-owned exports, clone/release/reference reuse, error
recovery and callback identity under allocation/GC pressure. All owned states are
closed. Those counts are not a claim of zero heap leaks. Same-state calls are
serialized; destroying an actively executing VM remains unsupported.
