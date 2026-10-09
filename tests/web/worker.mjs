export async function runTests(data) {
    const results = [];
    const check = async (id, test) => {
        try { await test(); results.push({id, pass: true}); }
        catch (error) { results.push({id, pass: false, error: String(error)}); }
    };
    const equal = (actual, expected) => {
        if (JSON.stringify(actual) !== JSON.stringify(expected))
            throw Error(`expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
    };
    let vm;
    try {
        if (data.backend === 'Asyncify') {
            // Select the unchanged front API's fallback in this dedicated worker.
            delete WebAssembly.Suspending;
            delete WebAssembly.promising;
        }
        equal('Suspending' in WebAssembly && 'promising' in WebAssembly, data.backend === 'JSPI');
        const {LuauState, InternalLuauWasmModule: M} = await import(`${data.runtime}/index.js`);
        if (data.suite === 'lifetime') {
            const {lifetimeTests} = await import('./lifetime.mjs');
            await lifetimeTests({LuauState, M, check, equal});
            return results;
        }
        vm = await LuauState.createAsync();
        M.options.set('LUA_IMPLICIT_ARRAYS_TO_JS_ARRAYS', false);
        const [probe] = await vm.loadstring(`return {
            echo = function(x) return x end,
            inspect = function(x) return #x, string.byte(x, 1, #x) end,
            field = function(o, k) return o[k] end,
            iterate = function(o) local t = {} for k,v in o do t[k] = v end return t end,
            callback = function(f) return f() end,
            caught = function(f) local ok, e = pcall(f) return ok, e end,
            table = function() return {} end,
            match = function(a,b) return a == b end,
        }`, '@web-regression.luau', true)();
        const call = (name, ...args) => probe.get(name)(...args);
        const corpus = ['', '\0', '\0a', 'a\0', 'a\0b', 'a\0\0b', 'é漢\0👩🏽‍💻', '\\0', '\\u0000'];
        for (const [i, input] of corpus.entries()) {
            await check(`value-${i}`, async () => {
                equal(await call('echo', input), [input]);
                const bytes = [...new TextEncoder().encode(input)];
                equal(await call('inspect', input), [bytes.length, ...bytes]);
                const [table] = await call('table'); table.set('value', input);
                equal(table.get('value'), input);
                equal(await call('field', {value: input}, 'value'), [input]);
                equal(await call('callback', () => input), [input]);
            });
        }
        await check('distinct-table-keys', async () => {
            const [table] = await call('table');
            const keys = ['a', 'a\0b', '\0', 'é漢\0👩🏽‍💻', 'a\0b\0', ''];
            keys.forEach((key, i) => table.set(key, `value-${i}`));
            equal(table.keys().sort(), [...keys].sort());
            keys.forEach((key, i) => equal(table.get(key), `value-${i}`));
        });
        await check('object-keys-and-iteration', async () => {
            equal(await call('field', {'a\0b': 'v\0tail'}, 'a\0b'), ['v\0tail']);
            const [table] = await call('iterate', new Map([['a\0b', 'v\0tail']]));
            equal(table.keys(), ['a\0b']); equal(table.get('a\0b'), 'v\0tail');
        });
        await check('raw-global', async () => {
            const key = 'global\0key', value = 'v\0tail', bytes = s => new TextEncoder().encode(s).length;
            M.ccall('pushGlobalToLua', 'void', ['number','string','string','string','number','number'],
                [vm.state, key, 'string', value, bytes(key), bytes(value)]);
            equal(vm.env.global.get(key), value);
            equal(vm.env.global.keys().filter(k => k.startsWith('global')), [key]);
        });
        await check('opaque-identifiers', async () => {
            equal(await call('match', 'scope\0new', 'scope\0old'), [false]);
            equal(await call('match', 'scope\0new', 'scope\0new'), [true]);
        });
        await check('js-error-ingress', async () => {
            const [ok, error] = await call('caught', () => { throw Error('before\0after'); });
            equal(ok, false);
            if (!error.includes('before\0after')) throw Error(`lost error bytes: ${JSON.stringify(error)}`);
        });
    } catch (error) { results.push({id: 'setup', pass: false, error: String(error)}); }
    finally { if (vm && !vm.destroyed) vm.destroy(); }
    return results;
}
if (typeof WorkerGlobalScope !== 'undefined') {
    self.onmessage = async ({data}) => postMessage(await runTests(data));
}

