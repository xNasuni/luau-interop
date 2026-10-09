export async function lifetimeTests({LuauState, M, check, equal}) {
    const execute = (vm, source, ...args) => vm.loadstring(source, '@lifetime.luau', true)(...args);
    const owned = new Set();
    const create = async () => { const vm = await LuauState.createAsync(); owned.add(vm); return vm; };
    const close = vm => { vm.destroy(); owned.delete(vm); };
    const rejects = async fn => { let rejected = false; try { await fn(); } catch { rejected = true; } equal(rejected, true); };
    try {
        await check('recreate-100', async () => {
            for (let i = 0; i < 100; ++i) {
                const vm = await create();
                try { equal(await execute(vm, 'return 42'), [42]); } finally { close(vm); }
            }
        });
        await check('two-live-vms', async () => {
            const b = await create(); b.env.set('marker', 'B', true);
            const [table, fn] = await execute(b, 'return {answer=42}, function(f) return marker, f(42) end');
            for (let i = 0; i < 100; ++i) {
                const a = await create();
                equal(await execute(a, 'return 42'), [42]); close(a);
                equal(table.get('answer'), 42);
                equal(await fn(x => x + 1), ['B', 43]);
            }
            close(b);
        });
        await check('closed-wrapper', async () => {
            const a = await create(); const [fn] = await execute(a, 'return function() return 11 end'); close(a);
            const c = await create(); await rejects(() => fn()); equal(await execute(c, 'return 42'), [42]); close(c);
        });
        await check('coroutine-owner', async () => {
            const b = await create();
            const [table, fn] = await execute(b, `local co=coroutine.create(function() return {answer=42}, function() return 43 end end)
                local ok,t,f=coroutine.resume(co) assert(ok) return t,f`);
            const a = await create(); close(a);
            equal(table.get('answer'), 42); equal(await fn(), [43]); close(b);
        });
        await check('release-reuse', async () => {
            const vm = await create();
            const [get] = await execute(vm, 'local t={answer=42} return function() return t end');
            const [original] = await get(); const clone = original[M.LUA_VALUE].persistentRef();
            clone[M.LUA_VALUE].release(); equal((await get())[0] === original, true);
            original[M.LUA_VALUE].release(); await rejects(() => original.get('answer'));
            const [fresh] = await get(); equal(fresh === original, false); equal(fresh.get('answer'), 42);
            for (let i=0;i<100;i++) { const clone=fresh[M.LUA_VALUE].persistentRef(); equal(clone.get('answer'),42); clone[M.LUA_VALUE].release(); }
            close(vm);
            await rejects(() => fresh[M.LUA_VALUE].persistentRef());
            await rejects(() => fresh[M.LUA_VALUE].release());
        });
        await check('error-recovery', async () => {
            for (const source of ['local =', 'error("sentinel")']) {
                const vm = await create(); await rejects(() => execute(vm, source)); close(vm);
                const next = await create(); equal(await execute(next, 'return 42'), [42]); close(next);
            }
        });
        await check('callback-gc', async () => {
            for (let i=0;i<30;i++) {
                const vm=await create();
                const callback=x=>x+1;
                const [roundtrip]=await execute(vm,'return function(f) return f end');
                equal((await roundtrip(callback))[0]===callback,true);
                for (let j=0;j<30;j++) await execute(vm,'local t={} for i=1,1000 do t[i]={i} end return true');
                equal((await roundtrip(callback))[0]===callback,true); close(vm);
            }
        });
    } finally {
        for (const vm of owned) close(vm);
        equal(M.states.filter(Boolean).length, 0);
    }
}
