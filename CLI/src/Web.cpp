// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "lstate.h"
#include "lobject.h"
#include "ludata.h"

#include "Luau/Common.h"
#include "Luau/Frontend.h"
#include "Luau/BuiltinDefinitions.h"

#include <string>
#include <memory>

#include <string.h>

#include <unordered_map>
#include <sstream>
#include <iomanip>

typedef struct
{
    const char* ref;
} jsref_ud;

void fprint(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "\x1b[1;38;5;13m[luau-web] \x1b[38;5;15m[info] \x1b[22m");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\x1b[0m\n");
    va_end(args);
}

void fprinterr(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "\x1b[1;38;5;13m[luau-web] \x1b[38;5;1m[error] \x1b[22m");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\x1b[0m\n");
    va_end(args);
}

void fprintwarn(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "\x1b[1;38;5;13m[luau-web] \x1b[38;5;11m[warn] \x1b[22m");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\x1b[0m\n");
    va_end(args);
}

static std::unordered_map<lua_State*, int> emEnvMap;
static std::unordered_map<lua_State*, int> emGlobalsMap;
static std::unordered_map<lua_State*, int> emFakeGlobalsMap;
static std::unordered_map<const void*, int> refCache;

int getPersistentRef(lua_State* L, int index)
{
    const void* ptr = lua_topointer(L, index);
    auto it = refCache.find(ptr);
    if (it != refCache.end())
        return it->second;

    int ref = lua_ref(L, index);
    refCache[ptr] = ref;
    return ref;
}

void setEnvId(lua_State* L, int envId)
{
    emEnvMap[L] = envId;
}

int getEnvId(lua_State* L)
{
    auto it = emEnvMap.find(L);
    if (it != emEnvMap.end())
        return it->second;

    lua_State* M = lua_mainthread(L);
    it = emEnvMap.find(M);
    return it != emEnvMap.end() ? it->second : -1;
}

static int saveGlobalsRefToMap(lua_State* L, std::unordered_map<lua_State*, int>& map)
{
    auto it = map.find(L);
    if (it != map.end())
        return it->second;

    lua_pushvalue(L, LUA_GLOBALSINDEX);
    int ref = lua_ref(L, -1);
    lua_pop(L, 1);
    map[L] = ref;
    return ref;
}

int saveOriginalGlobalsRef(lua_State* L)
{
    return saveGlobalsRefToMap(L, emGlobalsMap);
}
int saveSandboxedGlobalsRef(lua_State* L)
{
    return saveGlobalsRefToMap(L, emFakeGlobalsMap);
}

// clang-format off
EM_JS(void, setEnvFromJS, (int L_ptr, int envId, int globalsRef, int fakeGlobalsRef), {
    if (envId == 0)
    {
        return;
    }

    if (!Module.states[envId]) {
        throw new RuntimeError("no state for env id " + envId);
    }

    const extraKeys = {
        "global": Module.LuaValue(L_ptr, envId, "ltable", fakeGlobalsRef),
        "istable": function(value) {
            if (!Module.safeIn(Module.LUA_VALUE, value)) {
                return false
            }

            const data = value[Module.LUA_VALUE];
            if (data.released) {
                throw new Module.GlueError("illegal state: istable on released lua value");
            }
            if (data.stateIdx != envId) {
                throw new Module.GlueError("illegal state: istable on lua value from different env");
            }

            return data.type == "ltable";
        },
        "isfunction": function(value) {
            if (!Module.safeIn(Module.LUA_VALUE, value)) {
                return false
            }

            const data = value[Module.LUA_VALUE];
            if (data.released) {
                throw new Module.GlueError("illegal state: isfunction on released lua value");
            }
            if (data.stateIdx != envId) {
                throw new Module.GlueError("illegal state: isfunction on lua value from different env");
            }

            return data.type == "lfunction";
        },
        "isreadonly": function(value) {
            if (!Module.safeIn(Module.LUA_VALUE, value)) {
                throw new Module.GlueError("illegal state: isreadonly on a non-lua value")
            }

            const data = value[Module.LUA_VALUE];
            if (data.released) {
                throw new Module.GlueError("illegal state: isreadonly on released lua value");
            }
            if (data.stateIdx != envId) {
                throw new Module.GlueError("illegal state: isreadonly on lua value from different env");
            }
            if (data.type != "ltable") {
                throw new Module.GlueError("illegal state: isreadonly on non-table lua value");
            }

            return Module.ccall('isreadonly', 'boolean', [ 'number', 'number' ], [ L_ptr, data.ref ]);
        },
        "setreadonly": function(value, readonly) {
            if (!Module.safeIn(Module.LUA_VALUE, value)) {
                throw new Module.GlueError("illegal state: setreadonly on non-lua value");
            }

            const data = value[Module.LUA_VALUE];
            if (data.released) {
                throw new Module.GlueError("illegal state: setreadonly on released lua value");
            }
            if (data.type != "ltable") {
                throw new Module.GlueError("illegal state: setreadonly on non-table lua value");
            }
            if (data.stateIdx != envId) {
                throw new Module.GlueError("illegal state: setreadonly on lua value from different env");
            }

            Module.ccall('setreadonly', 'void', [ 'number', 'number', 'boolean' ], [ L_ptr, data.ref, readonly ]);
        },
        "getrawmetatable": function(value) {
            if (!Module.safeIn(Module.LUA_VALUE, value)) {
                throw new Module.GlueError("illegal state: isreadonly on a non-lua value")
            }

            const data = value[Module.LUA_VALUE];
            if (data.released) {
                throw new Module.GlueError("illegal state: isreadonly on released lua value");
            }
            if (data.stateIdx != envId) {
                throw new Module.GlueError("illegal state: isreadonly on lua value from different env");
            }
            if (data.type != "ltable") {
                throw new Module.GlueError("illegal state: isreadonly on non-table lua value");
            }
        }
    };

    Module.states[envId].env = Module.LuaValue(
        L_ptr, envId, "ltable", globalsRef, extraKeys
    );
});

EM_JS(void, ensureInterop, (), {
    class FatalJSError extends Error {
        constructor(message) {
            super(message);
            this.name = "FatalJSError";
        }
    };

    class LuaError extends Error {
        constructor(message) {
            super(message);
            this.name = "LuaError";
            this.stack = this.stack
                .split("\n")
                .filter(line => !line.includes("wasm://wasm"))
                .join("\n");
        }
    };

    class GlueError extends Error {
        constructor(message) {
            super(message);
            this.name = "GlueError";
            this.stack = this.stack
                .split("\n")
                .filter(line => !line.includes("wasm://wasm"))
                .join("\n");
        }
    };

    class RuntimeError extends Error {
        constructor(message) {
            super(message);
            this.name = "RuntimeError";
            this.stack = this.stack
                .split("\n")
                .filter(line => !line.includes("wasm://wasm"))
                .filter(line => !line.includes("base64Decode"))
                .join("\n");
        }
    };

    Module.FatalJSError = FatalJSError;
    Module.LuaError = LuaError;
    Module.GlueError = GlueError;
    Module.securityTransmitList = Module.securityTransmitList || {};

    const AsyncFunction = async function () {}.constructor;
    const GeneratorFunction = function* () {}.constructor;
    const AsyncGeneratorFunction = async function* () {}.constructor;

    //code exec
    Module.securityTransmitList.set(AsyncGeneratorFunction, true);
    Module.securityTransmitList.set(GeneratorFunction, true);
    Module.securityTransmitList.set(AsyncFunction, true);
    Module.securityTransmitList.set(Function, true);
    Module.securityTransmitList.set(eval, true);

    //nodejs
    if (typeof require !== "undefined") {
        Module.securityTransmitList.set(require, true);
    }
    if (typeof process !== "undefined") {
        Module.securityTransmitList.set(process, true);
    }

    //globals
    if (typeof global !== "undefined") {
        Module.securityTransmitList.set(global, true);
    }
    if (typeof globalThis !== "undefined") {
        Module.securityTransmitList.set(globalThis, true);
    }

    Module.fprint = function(...args) {
        console.error("\x1b[1;38;5;13m[luau-web] \x1b[38;5;15m[info]\x1b[22m", ...args, "\x1b[0m");
    };
    
    Module.fprintwarn = function(...args) {
        console.error("\x1b[1;38;5;13m[luau-web] \x1b[38;5;11m[warn]\x1b[22m", ...args, "\x1b[0m");
    };

    Module.fprinterr = function(...args) {
        console.error("\x1b[1;38;5;13m[luau-web] \x1b[38;5;1m[error]\x1b[22m", ...args, "\x1b[0m");
    };

    Module.LuaValue = function(state, stateIdx, type, ref, extraProps)
    {
        if (Module.states[stateIdx].luaValueCache.has(ref))
        {
            return Module.states[stateIdx].luaValueCache.get(ref);
        }

        const obj = {
            [Module.LUA_VALUE]: {
                ref,
                stateIdx,
                type,
                state,
                persistentRef() {
                    return Module.LuaValue(this.state, this.stateIdx, this.type, Module.ccall('luaCloneref', 'int', [ 'number', 'number' ], [ this.state, this.ref ]));
                },
                release() {
                    if (this.released)
                    {
                        return;
                    }
                    Module.ccall('luaUnref', 'void', [ 'number', 'number' ], [ this.state, this.ref ]);
                    this.released = true;
                }
            },
            toString() {
                return "[LuaReference " + type + " " + ref + "]";
            }
        };

        var luaValue = obj;

        if (type == "lfunction") {
            luaValue = new Proxy(function(){}, {
                apply(target, thisArg, args) {
                    return Module.callLuaFunction(stateIdx, obj, args);
                },
                get(target, prop, receiver) {
                    if (prop in obj) {
                        return obj[prop];
                    }
                    return undefined;
                },
                set(target, prop, value, receiver) {
                    obj[prop] = value;
                    return true;
                },
                has(target, prop) {
                    return prop in obj;
                },
                ownKeys(target) {
                    return Reflect.ownKeys(obj);
                },
                getOwnPropertyDescriptor(target, prop) {
                    return Object.getOwnPropertyDescriptor(obj, prop);
                }
            });
        };

        if (type == "ltable") {
            obj['get'] = function(key) {
                return Module.indexLuaTable(stateIdx, obj, key);
            };

            obj['set'] = function(key, value, bypassReadonly = false) {
                return Module.newIndexLuaTable(stateIdx, obj, key, value, bypassReadonly);
            };

            obj['keys'] = function() {
                return Module.keysLuaTable(stateIdx, obj);
            };

            if (extraProps) {
                 // for .env.global (running scope) and other specials
                for (const [key, value] of Object.entries(extraProps)) {
                    obj[key] = value;
                }
            }

            obj[Symbol.iterator] = function* () {
                const keys = Module.keysLuaTable(stateIdx, obj);
                for (const key of keys) {
                    yield [key, Module.indexLuaTable(stateIdx, obj, key)];
                }
            };
            
            luaValue = new Proxy({}, {
                get(target, prop, receiver) {
                    if (prop === Symbol.iterator || prop in obj) {
                        return obj[prop];
                    }
                    if (prop in obj) {
                        return obj[prop];
                    }
                    return Module.indexLuaTable(stateIdx, obj, prop);
                },
                set(target, prop, value, receiver) {
                    return Module.newIndexLuaTable(stateIdx, obj, prop, value, false);
                },
                has(target, prop) {
                    if (prop in obj) {
                        return true;
                    }
                    
                    return Module.indexLuaTable(stateIdx, obj, prop) != null;
                },
                ownKeys(target) {
                    return Module.keysLuaTable(stateIdx, obj);
                },
                getOwnPropertyDescriptor(target, prop) {
                    // const keys = Module.keysLuaTable(obj);
                    // if (keys.includes(prop) || prop in obj) {
                    //     return {
                    //         enumerable: true,
                    //         configurable: true,
                    //         writable: true,
                    //         value: this.get(target, prop)
                    //     }
                    // }

                    return Object.getOwnPropertyDescriptor(obj, prop);
                }
            });
        };

        Module.states[stateIdx].luaValueCache.set(ref, luaValue);

        return luaValue;
    };

    Module.callLuaFunction = function(stateIdx, luaFunction, args) {
        if (!Module.states[stateIdx]) {
            throw new RuntimeError("no state for env id " + stateIdx);
        }
        
        const luaFunctionData = luaFunction[Module.LUA_VALUE];
        
        if (luaFunctionData.released) {
            throw new GlueError("attempt to call released function");
            return;
        }

        const trimmed = args.slice(0, args.findLastIndex(x => x != undefined) + 1);
        const argDataKey = Module.states[stateIdx].nextTXKey++;

        Module.states[stateIdx].transactionData[argDataKey] = trimmed;

        const status = Module.ccall("luaPcall", 'number', [ 'number', 'number', 'number' ], [ luaFunctionData.state, luaFunctionData.ref, argDataKey ]);

        const multretData = Module.states[stateIdx].transactionData[argDataKey];
        delete Module.states[stateIdx].transactionData[argDataKey];

        const argData = multretData.map(v => Module.luauToJsValue(stateIdx, luaFunctionData.state, v));

        if (status != 0) {
            throw new LuaError(argData[0] ? argData[0] : "No output from Luau");
        }

        return argData;
    };

    Module.indexLuaTable = function(stateIdx, luaTable, key) {
        const luaTableData = luaTable[Module.LUA_VALUE];

        if (luaTableData.released) {
            throw new GlueError("attempt to index released table");
            return;
        }

        const [type, value] = Module.jsToLuauValue(stateIdx, null, key);

        const transactionIdx = Module.ccall("luaIndex", "number", [ "number", "number", "string", "string" ], [ luaTableData.state, luaTableData.ref, type, value ]);

        const transactionData = Module.states[stateIdx].transactionData[transactionIdx];
        delete Module.states[stateIdx].transactionData[transactionIdx];

        const luauValue = Module.luauToJsValue(stateIdx, luaTableData.state, transactionData);

        return luauValue
    };

    Module.newIndexLuaTable = function(stateIdx, luaTable, key, value, bypassReadonly = false) {
        if (!Module.states[stateIdx]) {
            throw new RuntimeError("no state for env id " + stateIdx);
        }
    
        const luaTableData = luaTable[Module.LUA_VALUE];

        if (luaTableData.released) {
            throw new GlueError("attempt to newindex released table");
            return;
        }

        const [KT, KV] = Module.jsToLuauValue(stateIdx, null, key);
        const [VT, VV] = Module.jsToLuauValue(stateIdx, null, value);

        const modified = Module.ccall("luaNewIndex", "number", [ "number", "number", "string", "string", "string", "string", "boolean" ], [ luaTableData.state, luaTableData.ref, KT, KV, VT, VV, bypassReadonly ]);

        return modified == 1;
    };

    Module.keysLuaTable = function(stateIdx, luaTable) {
        const luaTableData = luaTable[Module.LUA_VALUE];

        if (luaTableData.released) {
            throw new GlueError("attempt to get keys from released table");
            return;
        }

        const argIdx = Module.states[stateIdx].nextTXKey++;

        Module.ccall("luaKeys", "number", [ "number", "number", "number" ], [ luaTableData.state, luaTableData.ref, argIdx ]);

        const transactionData = Module.states[stateIdx].transactionData[argIdx];
        delete Module.states[stateIdx].transactionData[argIdx];

        const luauValue = transactionData.map(v => Module.luauToJsValue(stateIdx, luaTableData.state, v));

        return luauValue;
    };

    Module.getPersistentRef = function(stateIdx, jsValue, parent, key) {
    if (!Module.states[stateIdx]) {
        throw new RuntimeError("no state for env id " + stateIdx);
    }

        if (Module.securityTransmitList.has(jsValue)) {
            Module.fprintwarn("illegal state: can't get persistent ref, js value '%s' is blocked", key ? String(key) : "unknown");
            return 0;
        }

        if (Module.states[stateIdx].jsValueReverse.has(jsValue) && Module.states[stateIdx].jsValueReverse.get(jsValue)?.[Module.JS_VALUE]?.parent == parent) {
            return Module.states[stateIdx].jsValueReverse.get(jsValue);
        }

        const ref = Module.states[stateIdx].nextJSRef--;
        const obj = {
            [Module.JS_VALUE]: {
                ref,
                value: jsValue,
                parent: Module.states[stateIdx].jsValueReverse.has(parent) ? Module.states[stateIdx].jsValueCache.get(Module.states[stateIdx].jsValueReverse.get(parent)) : parent,
                key,
                released: false,
                release() {
                    if (this.released) return;
                    Module.states[stateIdx].jsValueCache.delete(ref);
                    Module.states[stateIdx].jsValueReverse.delete(jsValue);
                    this.released = true;
                }
            },
            toString() {
                return "[JsReference " + String(typeof(jsValue)) + " " + String(ref) + "]";
            }
        };

        Module.states[stateIdx].jsValueCache.set(ref, obj);
        Module.states[stateIdx].jsValueReverse.set(jsValue, ref);

        return ref;
    };

    Module.luauToJsValue = function(stateIdx, L_ptr, v)
    {
        if (!Module.states[stateIdx]) {
            throw new RuntimeError("no state for env id " + stateIdx);
        }

        if (typeof v == "undefined" || typeof v.type == "undefined" || typeof v.value == "undefined") {
            return null;
        }

        switch (v.type)
        {
        //--> value types
        case "string":
            return String(v.value).replaceAll("\\u0000", "\0");
        case "number":
            return (typeof v.value == "number" ? v.value : Number(v.value));
        case "boolean":
            return (typeof v.value == "boolean" ? v.value == true : v.value == "true");
        case "nil":
        case "undefined":
            return null;
        //--> reference types
        case "jsymbol":
        case "jobject":
        case "jfunction":
            if (typeof v.value == "number" && Module.states[stateIdx].jsValueCache.has(v.value)) {
                const jsValue = Module.states[stateIdx].jsValueCache.get(v.value);
                if (jsValue && Module.safeIn(Module.JS_VALUE, jsValue)) {
                    return jsValue[Module.JS_VALUE].value;
                };
            };
            Module.fprintwarn(`illegal state: cannot transmit ${v.type} invalid ${v.value}`);
            return null;
        case "table":
        case "function":
        case "userdata":
        case "thread":
        case "buffer":
        {
            const ref = parseInt(v.value, 10);
            return Module.LuaValue(L_ptr, stateIdx, "l" + v.type, ref);
        }
        default:
            Module.fprintwarn(`illegal l2j conversion: unsupported type '${v.type}', defaulted to null: ${v.value}`);
            return null;
        }
    };

    Module.safeIn = function(inValue, value) {
        try {
            return inValue in value;
        } catch (e) {
        }
        return false;
    };

    Module.jsToLuauValue = function(stateIdx, parent, key) {
        let type = "unknown";
        let value = null;
        
        if (parent != null) {
            if (parent instanceof Map) {
                value = parent.get(key) ?? null;
            } else {
                value = parent[key] ?? null;
            }
        } else {
            value = key;
        }

        if (Module.securityTransmitList.has(value)) {
            Module.fprintwarn(`illegal j2l conversion: js value '${key ? String(key) : "unknown"}' is blocked`);
            return ["nil", "nil"];
        }
        
        if (typeof value == "number")
        {
            type = "number";
            value = String(value);
        }
        else if (typeof value == "string") {
            type = "string";
            value = String(value);
        }
        else if (typeof value == "boolean")
        {
            type = "boolean";
            value = String(value);
        }
        else if (typeof value == "symbol") {
            type = "jsymbol";
            value = String(Module.getPersistentRef(stateIdx, value, parent, key));
        }
        else if (typeof value == "function" && !(Module.safeIn(Module.LUA_VALUE, value)) && !(Module.safeIn(Module.JS_VALUE, value)))
        {
            type = "jfunction";
            value = String(Module.getPersistentRef(stateIdx, value, parent, key));
        }
        else if (typeof value == "object" || (Module.safeIn(Module.LUA_VALUE, value) || Module.safeIn(Module.JS_VALUE, value)) || value instanceof Map)
        {
            if (Module.safeIn(Module.LUA_VALUE, value)) {
                const data = value[Module.LUA_VALUE];
                if (!data.released) {
                    type = data.type;
                    value = String(data.ref);
                } else {
                    Module.fprintwarn("illegal operation: will not pass released reference");
                    type = "nil";
                    value = "nil";
                }
            } else if (Module.safeIn(Module.JS_VALUE, value)) {
                const data = value[Module.JS_VALUE];
                type = "jobject";
                value = String(data.ref);
            } else {
                type = "jobject";
                value = String(Module.getPersistentRef(stateIdx, value, parent, key));
            }
        }
        else if (!value || typeof value == "undefined") {
            type = "nil";
            value = "nil";
        } else {
            Module.fprintwarn(`illegal j2l conversion: unsupported type '${typeof value}', defaulted to nil: ${String(value)}`);
            type = "nil";
            value = "nil";
        }

        return [type, value];
    };
});

EM_JS(int, getJSProperty, (int L_ptr, int envId, const char* jsRefIdStr, const char* keyCStr), {
    if (!Module.states[envId]) {
        throw new RuntimeError("no state for env id " + envId);
    }

    const jsRefId = JSON.parse(UTF8ToString(jsRefIdStr));
    const key = JSON.parse(UTF8ToString(keyCStr));

    const data = Module.states[envId].jsValueCache.get(jsRefId);

    if (data) {
        if (!data[Module.JS_VALUE]) {
            Module.fprintwarn("illegal state: js callback on non js data");  
            return 0;
        };

        if (data[Module.JS_VALUE].released) {
            Module.fprintwarn("illegal state: lua read on released js data"); 
            return 0;
        };

        const keyData = Module.luauToJsValue(envId, L_ptr, key);

        const [type, value] = Module.jsToLuauValue(envId, data[Module.JS_VALUE].value, keyData);

        Module.ccall('pushValueToLuaWrapper', 'void', [ 'number', 'string', 'string', 'string' ], [ L_ptr, type, value, `${keyData}` ]);
        return 1;
    }

    return 0;
});

EM_JS(int, setJSProperty, (int L_ptr, int envId, const char* jsRefIdStr, const char* keyCStr, const char* valueCStr), {
    if (!Module.states[envId]) {
        throw new RuntimeError("no state for env id " + envId);
    }

    const jsRefId = JSON.parse(UTF8ToString(jsRefIdStr));
    const key = JSON.parse(UTF8ToString(keyCStr));
    const value = JSON.parse(UTF8ToString(valueCStr));

    const data = Module.states[envId].jsValueCache.get(jsRefId);

    if (data) {
        function luaError(s) {
            Module.ccall('pushValueToLuaWrapper', 'void', [ 'number', 'string', 'string', 'string' ], [ L_ptr, 'string', s, `<jserror>` ]);
            return -1;
        }

        if (!data[Module.JS_VALUE]) {
            Module.fprintwarn("illegal state: js callback on non js data");  
            return 0;
        };

        if (data[Module.JS_VALUE].released) {
            Module.fprintwarn("illegal state: lua write on released js data"); 
            return 0;
        };

        if (typeof data[Module.JS_VALUE].value !== "object" || !(data[Module.JS_VALUE].value instanceof Map)) {
            return luaError("attempt to modify a readonly table");
        };

        if (!data[Module.JS_VALUE].value.has(Module.JS_MUTABLE)) {
            Module.fprintwarn("illegal state: lua write on non-mutable js data");
            return luaError("attempt to modify a readonly table");
        };

        try {
            const keyData = Module.luauToJsValue(envId, L_ptr, key);
            const valueData = Module.luauToJsValue(envId, L_ptr, value);

            data[Module.JS_VALUE].value.set(keyData, valueData);
            return 0;
        } catch (e) {
            const errorStr = (e && e.toString) ? e.toString() : String(e);
            return luaError(errorStr);
        }
    }

    return 0;
});

EM_JS(char*, prepareJSKeyList, (int envId, const char* jsRefIdStr), {
    if (!Module.states[envId]) {
        throw new RuntimeError("no state for env id " + envId);
    }

    const jsRefId = JSON.parse(UTF8ToString(jsRefIdStr));
    const data = Module.states[envId].jsValueCache.get(jsRefId);

    if (data && data[Module.JS_VALUE]) {
        let keys = null;
        if (data[Module.JS_VALUE].value instanceof Map) {
            keys = Array.from(data[Module.JS_VALUE].value.keys()).filter(k =>
                k !== Module.LUA_VALUE && k !== Module.JS_VALUE && k !== Module.JS_MUTABLE
            );
        } else if (typeof data[Module.JS_VALUE].value === 'object') {
            keys = Object.keys(data[Module.JS_VALUE].value);
        }

        if (keys) {
            const keysId = String(jsRefId) + "_keys_" + crypto.randomUUID();
            
            Module.states[envId].jsValueCache.set(keysId, {
                [Module.JS_VALUE]: {
                    value: keys,
                    released: false
                }
            });
            
            const returnStr = JSON.stringify(keysId);
            const lengthBytes = lengthBytesUTF8(returnStr) + 1;
            const stringOnWasmHeap = _malloc(lengthBytes);
            stringToUTF8(returnStr, stringOnWasmHeap, lengthBytes);
            return stringOnWasmHeap;
        }
    }

    return 0;
});

EM_JS(void, releaseJSKeyList, (int envId, const char* keysRefIdStr), {
    if (!Module.states[envId]) {
        throw new RuntimeError("no state for env id " + envId);
    }

    const keysRefId = UTF8ToString(keysRefIdStr);
    Module.states[envId].jsValueCache.delete(keysRefId);
});

EM_JS(int, getJSIteratorNext, (int L_ptr, int envId, const char* jsRefIdStr, const char* keysRefIdStr, int index), {
    if (!Module.states[envId]) {
        throw new RuntimeError("no state for env id " + envId);
    }

    const jsRefId = JSON.parse(UTF8ToString(jsRefIdStr));
    const keysRefId = JSON.parse(UTF8ToString(keysRefIdStr));
    
    const objData = Module.states[envId].jsValueCache.get(jsRefId);
    const keysData = Module.states[envId].jsValueCache.get(keysRefId);

    if (objData && keysData) {
        const keys = keysData[Module.JS_VALUE].value;
        if (index >= keys.length) {
            Module.states[envId].jsValueCache.delete(keysRefId);
            return 0; 
        }

        const currentKey = keys[index];

        Module.ccall('pushValueToLuaWrapper', 'void', ['number', 'string', 'string', 'string'], [L_ptr, 'string', String(currentKey), "jsiter__key"]);

        const [type, value] = Module.jsToLuauValue(envId, objData[Module.JS_VALUE].value, currentKey);
        
        const valueStr = String(value);

        Module.ccall('pushValueToLuaWrapper', 'void', ['number', 'string', 'string', 'string'], [L_ptr, type, valueStr, "jsiter__value"]);

        return 1;
    }

    return 0;
});

// clang-format on

bool isReferenceType(int kind)
{
    return kind == LUA_TVECTOR || kind == LUA_TTABLE || kind == LUA_TFUNCTION || kind == LUA_TUSERDATA || kind == LUA_TTHREAD || kind == LUA_TBUFFER;
}

bool isValueType(int kind)
{
    return kind == LUA_TNIL || kind == LUA_TBOOLEAN || kind == LUA_TNUMBER || kind == LUA_TSTRING;
}

const char* luauTypeName(int kind)
{
    switch (kind)
    {
    case LUA_TNIL:
        return "nil";
    case LUA_TBOOLEAN:
        return "boolean";
    case LUA_TNUMBER:
        return "number";
    case LUA_TSTRING:
        return "string";
    case LUA_TTABLE:
        return "table";
    case LUA_TFUNCTION:
        return "function";
    case LUA_TTHREAD:
        return "thread";
    case LUA_TUSERDATA:
        return "userdata";
    case LUA_TLIGHTUSERDATA:
        return "lightuserdata";
    case LUA_TVECTOR:
        return "vector";
    case LUA_TBUFFER:
        return "buffer";
    default:
        return "unknown";
    }
}

std::string serializeLuaValue(lua_State* L, int index, int* refOut)
{
    int valueType = lua_type(L, index);

    switch (valueType)
    {
    case LUA_TNUMBER:
    {
        char buf[64];
        lua_Number num = lua_tonumber(L, index);
        snprintf(buf, sizeof(buf), "%.17g", num);
        return std::string("{\"type\":\"number\",\"value\":") + buf + "}";
    }
    case LUA_TSTRING:
    {
        size_t len;
        const char* str = lua_tolstring(L, index, &len);
        std::string escapedStr;
        for (size_t i = 0; i < len; i++)
        {
            switch (str[i])
            {
            case '"':
                escapedStr += "\\\"";
                break;
            case '\\':
                escapedStr += "\\\\";
                break;
            case '\b':
                escapedStr += "\\b";
                break;
            case '\f':
                escapedStr += "\\f";
                break;
            case '\n':
                escapedStr += "\\n";
                break;
            case '\r':
                escapedStr += "\\r";
                break;
            case '\t':
                escapedStr += "\\t";
                break;
            case '\0':
                escapedStr += "\\u0000";
                break;
            default:
                if ((unsigned char)str[i] < 32)
                {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)str[i]);
                    escapedStr += buf;
                }
                else
                {
                    escapedStr += str[i];
                }
                break;
            }
        }
        return std::string("{\"type\":\"string\",\"value\":\"") + escapedStr + "\"}";
    }
    case LUA_TBOOLEAN:
        return std::string("{\"type\":\"boolean\",\"value\":") + (lua_toboolean(L, index) ? "true}" : "false}");
    case LUA_TNIL:
        return "{\"type\":\"nil\",\"value\":null}";
    case LUA_TUSERDATA:
    {
        int tag = lua_userdatatag(L, index);
        if (tag != UTAG_PROXY && (tag == UTAG_JSFUNC || tag == UTAG_JSOBJECT))
        {
            const char* detectedType = "nil";
            jsref_ud* ud = (jsref_ud*)lua_touserdata(L, index);
            if (!ud)
            {
                fprinterr("illegal state: invalid userdata for proxy_index");
                break;
            }

            if (tag == UTAG_JSFUNC)
            {
                detectedType = "jfunction";
            }
            if (tag == UTAG_JSOBJECT)
            {
                detectedType = "jobject";
            }

            std::string result = std::string("{\"type\":\"") + detectedType + std::string("\",\"value\":") + ud->ref + "}";

            return result;
        }
        [[fallthrough]];
    }
    case LUA_TTABLE:
    case LUA_TFUNCTION:
    case LUA_TTHREAD:
    case LUA_TBUFFER:
    {
        const char* typeName = luauTypeName(valueType);

        int ref = getPersistentRef(L, index);
        *refOut = ref;

        char buf[64];
        snprintf(buf, sizeof(buf), "%d", ref);

        return std::string("{\"type\":\"") + typeName + "\",\"value\":\"" + buf + "\"}";
    }
    default:
        fprintwarn("illegal serialization: unsupported value type '%s' [%d]", luauTypeName(valueType), valueType);
        return "{\"type\":\"unknown\",\"value\":null}";
    }
    return "{\"type\":\"unknown\",\"value\":null}";
}

// clang-format off
EM_JS(int, pushTransactionString, (int envId, const char* str), {
    const transactionKey = Module.states[envId].nextTXKey++;
    Module.states[envId].transactionData[transactionKey] = UTF8ToString(str);

    return transactionKey;
});
// clang-format on

extern "C" int getLuaValue(lua_State* L, int index)
{
    int envId = getEnvId(L);
    if (envId == -1)
    {
        fprinterr("illegal state: no environment id found for lua state");
        return -1;
    }

    int ref = LUA_NOREF;
    std::string value = serializeLuaValue(L, index, &ref);

    lua_pop(L, 1);

    return pushTransactionString(envId, value.c_str());
}

int proxy_index(lua_State* L)
{
    jsref_ud* ud = (jsref_ud*)lua_touserdata(L, 1);
    if (!ud || !ud->ref)
    {
        fprinterr("illegal state: invalid userdata for proxy_index");
        lua_pushnil(L);
        return 1;
    }

    const char* jsRefIdStr = ud->ref;
    int keyType = lua_type(L, -1);

    if (isValueType(keyType) || isReferenceType(keyType))
    {
        int ref = LUA_REFNIL;
        std::string luauKeyJson = serializeLuaValue(L, -1, &ref);

        int envId = getEnvId(L);
        if (envId == -1)
        {
            fprinterr("illegal state: no environment id found for lua state");
            lua_pop(L, 1);
            return 0;
        }

        lua_pop(L, 1);
        return getJSProperty((int)L, envId, jsRefIdStr, luauKeyJson.c_str());
    }
    else
    {
        fprintwarn("illegal type: unsupported key type '%s' for object '%s'", luauTypeName(keyType), jsRefIdStr);
        lua_pop(L, 1);
        return 0;
    }
}

int proxy_newindex(lua_State* L)
{
    jsref_ud* ud = (jsref_ud*)lua_touserdata(L, 1);
    if (!ud || !ud->ref)
    {
        fprinterr("illegal state: invalid userdata for proxy_index");
        lua_pushnil(L);
        return 1;
    }

    const char* jsRefIdStr = ud->ref;

    int keyType = lua_type(L, -2);
    int valueType = lua_type(L, -1);

    if ((isValueType(keyType) || isReferenceType(keyType)) && (isValueType(valueType) || isReferenceType(valueType)))
    {
        int kref = LUA_REFNIL;
        std::string luauKeyJson = serializeLuaValue(L, -2, &kref);
        int vref = LUA_REFNIL;
        std::string luauValueJson = serializeLuaValue(L, -1, &vref);

        int envId = getEnvId(L);
        if (envId == -1)
        {
            fprinterr("illegal state: no environment id found for lua state");
            return 0;
        }

        int result = setJSProperty((int)L, envId, jsRefIdStr, luauKeyJson.c_str(), luauValueJson.c_str());
        if (result == -1)
        {
            if (!lua_isstring(L, -1))
            {
                lua_pop(L, 1);
                lua_pushstring(L, "No output from JS");
            }

            lua_error(L);
            return 0;
        }

        lua_pop(L, 2);
        return result;
    }
    else
    {
        if (!isValueType(keyType) && !isReferenceType(keyType))
        {
            fprintwarn("illegal type: unsupported key type '%s' for object '%s'", luauTypeName(keyType), jsRefIdStr);
        }
        if (!isValueType(valueType) && !isReferenceType(valueType))
        {
            fprintwarn("illegal type: unsupported value type '%s' for object '%s'", luauTypeName(valueType), jsRefIdStr);
        }

        lua_pop(L, 2);
        return 0;
    }
}

int proxy_iter_next(lua_State* L)
{
    int envId = getEnvId(L);
    if (envId == -1)
    {
        fprinterr("illegal state: no environment id found for lua state");
        return 0;
    }

    const char* jsRefIdStr = lua_tostring(L, lua_upvalueindex(1));
    const char* keysRefIdStr = lua_tostring(L, lua_upvalueindex(2));
    int index = lua_tointeger(L, lua_upvalueindex(3));

    int found = getJSIteratorNext((int)L, envId, jsRefIdStr, keysRefIdStr, index);

    if (found)
    {
        lua_pushinteger(L, index + 1);
        lua_replace(L, lua_upvalueindex(3));

        return 2;
    }

    return 0;
}

int proxy_iter(lua_State* L)
{
    int envId = getEnvId(L);
    if (envId == -1)
    {
        fprinterr("illegal state: no environment id found for lua state");
        lua_pushnil(L);
        return 1;
    }

    jsref_ud* ud = (jsref_ud*)lua_touserdata(L, 1);
    if (!ud || !ud->ref)
    {
        lua_pushnil(L);
        return 1;
    }

    char* keysRefRaw = prepareJSKeyList(envId, ud->ref);
    if (!keysRefRaw)
    {
        lua_pushnil(L);
        return 1;
    }

    lua_pushstring(L, ud->ref);
    lua_pushstring(L, keysRefRaw);
    lua_pushinteger(L, 0);

    releaseJSKeyList(envId, keysRefRaw);
    free(keysRefRaw);

    lua_pushcclosure(L, proxy_iter_next, "proxy_iter_next", 3);

    return 1;
}

// clang-format off
EM_JS(int, callJSFunction, (int L_ptr, int envId, const char* jsRefIdJson, const char* argsJson), {
    if (!Module.states[envId]) {
        throw new RuntimeError("no state for env id " + envId);
    }

    const pathStr = UTF8ToString(jsRefIdJson);
    const argsStr = UTF8ToString(argsJson);

    const rawArgs = JSON.parse(argsStr);
    const actualArgs = rawArgs.slice(1);

    const args = actualArgs.map(arg=>Module.luauToJsValue(envId, L_ptr, arg));

    const key = JSON.parse(pathStr);
    var trimmed = [];

    function luaError(s) {
        Module.ccall('pushValueToLuaWrapper', 'void', [ 'number', 'string', 'string', 'string' ], [ L_ptr, 'string', s, `<jserror>` ]);
        return -1;
    }

    if (Module.states[envId].jsValueCache.has(key)) {
        const data = Module.states[envId].jsValueCache.get(key)[Module.JS_VALUE];
        const returnData = [null];

        if (data && data.value) {
            try {
                const func = data.value;
                const ctx = data.parent?.[Module.JS_VALUE]?.value ?? null;
        
                try {
                    returnData[0] = func.apply(ctx, args);
                } catch (e) {
                    // todo(xNasuni): find better method of detecting constructors
                    if (e.toString().includes("constructor") &&
                        e.toString().includes("new")) {
                        returnData[0] = Reflect.construct(func, args);
                    } else {
                        throw e;
                    }
                }
            } catch (e) {
                if (e instanceof Module.FatalJSError) {
                    throw e;
                } else {
                    const errorStr = (e && e.toString) ? e.toString() : String(e);
                    return luaError(errorStr);
                }
            }

            // possibly re-enable in the future for long term applications
            // args.forEach(arg => arg?.[Module.LUA_VALUE]?.release?.());

            trimmed = Array.isArray(returnData[0]) ? [...returnData[0]] : [returnData[0]];
        } else {
            Module.fprintwarn("illegal state: no js val found for path", pathStr);
            return luaError('not tied to valid jval');
        }
    } else {
        Module.fprintwarn("illegal state: no js function found for path", pathStr);
        return luaError('not tied to valid ref');
    }

    const returnDataKey = Module.states[envId].nextTXKey++;
    Module.states[envId].transactionData[returnDataKey] = trimmed;

    return returnDataKey;
});

EM_JS(int, retrieveRetc, (int returnDataKey), {
    const returnData = Module.transactionData[returnDataKey];
    const count = Array.isArray(returnData) ? returnData.length : 0;
    return count;
});

EM_JS(int, pushRetData, (int L_ptr, int envId, int returnDataKey), {
    if (!Module.states[envId]) {
        throw new RuntimeError("no state for env id " + envId);
    }

    const returnData = Module.states[envId].transactionData[returnDataKey];
    delete Module.states[envId].transactionData[returnDataKey];

    if (!returnData || !Array.isArray(returnData) || returnData.length <= 0) {
        return 0;
    }

    returnData.forEach((data) => {
        const [type, value] = Module.jsToLuauValue(envId, null, data);
        Module.ccall('pushValueToLuaWrapper', 'void', [ 'number', 'string', 'string', 'string' ], [ L_ptr, type, value, `${value}` ]);
    });

    return returnData.length;
});

// clang-format on

int proxy_call(lua_State* L)
{
    jsref_ud* ud = (jsref_ud*)lua_touserdata(L, 1);
    if (!ud || !ud->ref)
    {
        fprinterr("illegal state: invalid userdata for proxy_index");
        lua_pushnil(L);
        return 1;
    }

    const char* jsRefIdStr = ud->ref;

    int envId = getEnvId(L);

    if (envId == -1)
    {
        fprinterr("illegal state: no environment id found for function call");
        return 0;
    }

    int argc = lua_gettop(L);

    std::string argsJson = "[";
    for (int i = 1; i <= argc; i++)
    {
        int ref = LUA_REFNIL;
        argsJson += serializeLuaValue(L, i, &ref);
        if (i < argc)
        {
            argsJson += ",";
        }
    }
    argsJson += "]";

    int returnDataKey = callJSFunction((int)L, envId, jsRefIdStr, argsJson.c_str());

    if (returnDataKey == -1)
    {
        if (!lua_isstring(L, -1))
        {
            lua_pop(L, 1);
            lua_pushstring(L, "No output from JS");
        }

        lua_error(L);
        return 0;
    }

    return pushRetData((int)L, envId, returnDataKey);
}

void pushValueToLua(lua_State* L, const char* type, const char* value, const char* key = nullptr)
{
    if (strcmp(type, "number") == 0)
    {
        lua_Number n = atof(value);
        lua_pushnumber(L, n);
    }
    else if (strcmp(type, "string") == 0)
    {
        lua_pushstring(L, value);
    }
    else if (strcmp(type, "boolean") == 0)
    {
        lua_pushboolean(L, strcmp(value, "true") == 0);
    }
    else if (strcmp(type, "nil") == 0)
    {
        lua_pushnil(L);
    }
    else if (strcmp(type, "ltable") == 0 || strcmp(type, "lfunction") == 0 || strcmp(type, "luserdata") == 0 || strcmp(type, "lthread") == 0 ||
             strcmp(type, "lbuffer") == 0)
    {
        int ref = atoi(value);
        lua_getref(L, ref);
    }
    else if (strcmp(type, "jobject") == 0 || strcmp(type, "jsymbol") == 0)
    {
        int ref = atoi(value);
        if (ref == 0)
        {
            fprintwarn("illegal push: js %s value '%s' is blocked", type, key ? key : "unknown");
            lua_pushnil(L);
            return;
        }

        jsref_ud* ud = (jsref_ud*)lua_newuserdatataggedwithmetatable(L, sizeof(jsref_ud), UTAG_JSOBJECT);
        ud->ref = strdup(value);
    }
    else if (strcmp(type, "jfunction") == 0)
    {
        int ref = atoi(value);
        if (ref == 0)
        {
            fprintwarn("illegal push: js function value '%s' is blocked", key ? key : "unknown");
            lua_pushnil(L);
            return;
        }

        jsref_ud* ud = (jsref_ud*)lua_newuserdatataggedwithmetatable(L, sizeof(jsref_ud), UTAG_JSFUNC);
        ud->ref = strdup(value);
    }
    else
    {
        fprintwarn(
            "illegal push: unsupported type '%s' for key '%s' with value '%s'",
            type ? type : "unknown",
            key ? key : "unknown",
            value ? value : "unknown"
        );
        lua_pushnil(L);
    }
}

extern "C" void pushGlobalToLua(lua_State* L, const char* key, const char* type, const char* value)
{
    if (!L || !key || !type || !value)
    {
        fprintwarn("illegal push: some arguments are null: L=%p, key=%p, type=%p, value=%p", (void*)L, (void*)key, (void*)type, (void*)value);
        return;
    }

    pushValueToLua(L, type, value, key);
    lua_setglobal(L, key);
}

extern "C" void pushValueToLuaWrapper(lua_State* L, const char* type, const char* value, const char* key)
{
    pushValueToLua(L, type, value, key);
}

// clang-format off
EM_JS(int, pushArgs, (int L_int, int envId, int argIdx), {
    if (!Module.states[envId]) {
        throw new RuntimeError("no state for env id " + envId);
    }

    const argData = Module.states[envId].transactionData[argIdx];
    const length = argData.length;
    delete Module.states[envId].transactionData[argIdx];

    argData.forEach((data) => {
        const [type, value] = Module.jsToLuauValue(envId, null, data);
        Module.ccall("pushValueToLuaWrapper", 'void', [ 'number', 'string', 'string', 'string' ], [ L_int, type, value, '<callarg>' ]);
    });
    
    return length;
});

EM_JS(void, setMultretData, (int L_int, int envId, const char* multretJson, int argIdx), {
    if (!Module.states[envId]) {
        throw new RuntimeError("no state for env id " + envId);
    }

    const multretData = JSON.parse(UTF8ToString(multretJson));
    Module.states[envId].transactionData[argIdx] = multretData;
})
// clang-format on

extern "C" int luaPcall(lua_State* L, int ref, int argIdx)
{
    int top = lua_gettop(L);

    int envId = getEnvId(L);
    if (envId == -1)
    {
        fprinterr("illegal state: no environment id found for lua state");
        return LUA_ERRRUN;
    }

    try
    {
        if (ref != LUA_NOREF)
        {
            lua_getref(L, ref);
        }

        int nargs = pushArgs((int)L, envId, argIdx);

        int status = lua_pcall(L, nargs, LUA_MULTRET, 0);

        std::string retJson = "[";

        if (status == LUA_OK)
        {
            int newTop = lua_gettop(L);
            int nresults = newTop - top;

            for (int i = 1; i <= nresults; i++)
            {
                int ref = LUA_NOREF;
                retJson += serializeLuaValue(L, top + i, &ref);

                if (i < nresults)
                {
                    retJson += ",";
                }
            }
        }
        else
        {
            int ref = LUA_NOREF;
            retJson += serializeLuaValue(L, -1, &ref);
        }

        retJson += "]";

        lua_settop(L, top);
        setMultretData((int)L, envId, retJson.c_str(), argIdx);

        return status;
    }
    catch (const std::exception& e)
    {
        lua_settop(L, top);
        std::string errorJson = "[{\"error\":\"";
        errorJson += e.what();
        errorJson += "\"}]";
        setMultretData((int)L, envId, errorJson.c_str(), argIdx);
        return LUA_ERRRUN;
    }
    catch (...)
    {
        lua_settop(L, top);
        std::string errorJson = "[{\"error\":\"unknown error\"}]";
        setMultretData((int)L, envId, errorJson.c_str(), argIdx);
        return LUA_ERRRUN;
    }
}

extern "C" int luaCloneref(lua_State* L, int ref)
{
    lua_getref(L, ref);
    int persistentRef = lua_ref(L, -1);
    return persistentRef;
}

extern "C" void luaUnref(lua_State* L, int ref)
{
    if (!L || ref <= 0)
    {
        return;
    }

    lua_getref(L, ref);
    const void* ptr = lua_topointer(L, -1);
    lua_pop(L, 1);

    if (ptr)
    {
        refCache.erase(ptr);
    }

    lua_unref(L, ref);
}

// clang-format off
EM_JS(int, sendValueToJS, (int envId, const char* valueJson), {
    const value = JSON.parse(UTF8ToString(valueJson));
    const key = Module.states[envId].nextTXKey++;

    Module.states[envId].transactionData[key] = value;

    return key;
});
// clang-format on

extern "C" int luaIndex(lua_State* L, int lref, const char* KT, const char* KV)
{
    int envId = getEnvId(L);
    if (envId == -1)
    {
        fprinterr("illegal state: no environment id found for lua state");
        return -1;
    }

    lua_getref(L, lref);
    pushValueToLua(L, KT, KV, "<indexarg>");

    lua_rawget(L, -2);

    int ref = LUA_NOREF;
    std::string valueJson = serializeLuaValue(L, -1, &ref);

    lua_pop(L, 2);
    return sendValueToJS(envId, valueJson.c_str());
}

extern "C" bool luaNewIndex(lua_State* L, int lref, const char* KT, const char* KV, const char* VT, const char* VV, bool bypassReadonly)
{
    lua_getref(L, lref);

    bool wasSetWritable = false;
    if (lua_getreadonly(L, -1) == 1)
    {
        if (bypassReadonly)
        {
            lua_setreadonly(L, -1, 0);
            wasSetWritable = true;
        }
        else
        {
            return false;
        }
    }

    pushValueToLua(L, KT, KV, "<indexarg>");
    pushValueToLua(L, VT, VV, "<valuearg>");

    lua_rawset(L, -3);

    if (wasSetWritable)
    {
        lua_setreadonly(L, -1, 1);
    }

    lua_pop(L, 1);
    return true;
}

extern "C" int luaKeys(lua_State* L, int lref, int argIdx)
{
    int envId = getEnvId(L);
    if (envId == -1)
    {
        fprinterr("illegal state: no environment id found for lua state");
        return -1;
    }

    int top = lua_gettop(L);

    try
    {
        lua_getref(L, lref);

        if (!lua_istable(L, -1))
        {
            lua_pop(L, 1);
            setMultretData((int)L, envId, "[]", argIdx);
            return 0;
        }

        std::string retJson = "[";
        bool first = true;

        lua_pushnil(L);
        while (lua_next(L, -2))
        {
            if (!first)
            {
                retJson += ",";
            }

            int ref = LUA_NOREF;
            retJson += serializeLuaValue(L, -2, &ref);

            lua_pop(L, 1);
            first = false;
        }

        retJson += "]";

        lua_settop(L, top);

        setMultretData((int)L, envId, retJson.c_str(), argIdx);

        return 0;
    }
    catch (const std::exception& e)
    {
        lua_settop(L, top);
        std::string errorJson = "[{\"error\":\"" + std::string(e.what()) + "\"}]";
        setMultretData((int)L, envId, errorJson.c_str(), argIdx);
        return 1;
    }
}

static void setupState(lua_State* L)
{
    try
    {
        luaL_openlibs(L);

        luaL_sandbox(L);

        lua_newtable(L);
        lua_pushcclosurek(L, proxy_index, "__index", 0, NULL);
        lua_setfield(L, -2, "__index");
        lua_pushcclosurek(L, proxy_call, "__call", 0, NULL);
        lua_setfield(L, -2, "__call");
        lua_pushstring(L, "The metatable is locked");
        lua_setfield(L, -2, "__metatable");
        lua_pushstring(L, "function");
        lua_setfield(L, -2, "__type");
        lua_setreadonly(L, -1, true);
        L->global->udatamt[UTAG_JSFUNC] = hvalue(L->top - 1);
        lua_pop(L, 1);

        lua_newtable(L);
        lua_pushcclosurek(L, proxy_index, "__index", 0, NULL);
        lua_setfield(L, -2, "__index");
        lua_pushcclosurek(L, proxy_newindex, "__newindex", 0, NULL);
        lua_setfield(L, -2, "__newindex");
        lua_pushcclosurek(L, proxy_iter, "__iter", 0, NULL);
        lua_setfield(L, -2, "__iter");
        lua_pushstring(L, "The metatable is locked");
        lua_setfield(L, -2, "__metatable");
        lua_pushnil(L);
        lua_setfield(L, -2, "__metatable");
        lua_pushstring(L, "table");
        lua_setfield(L, -2, "__type");
        lua_setreadonly(L, -1, true);
        L->global->udatamt[UTAG_JSOBJECT] = hvalue(L->top - 1);
        lua_pop(L, 1);
    }
    catch (const std::exception& e)
    {
        fprinterr("failed to setup interop: %s", e.what());
    }
}

extern "C" lua_State* makeLuaState(int envId)
{
    // setup flags
    for (Luau::FValue<bool>* flag = Luau::FValue<bool>::list; flag; flag = flag->next)
        if (strncmp(flag->name, "Luau", 4) == 0)
            flag->value = true;

    // create new state
    lua_State* L = luaL_newstate();

    // setup state
    setupState(L);

    // save globals
    int globalsRef = saveOriginalGlobalsRef(L);

    // sandbox thread and globals
    luaL_sandboxthread(L);

    // save fake globals
    int fakeGlobalsRef = saveSandboxedGlobalsRef(L);

    // check for env (only for web/emscripten)
    ensureInterop();

    if (envId != 0)
    {
        setEnvId(L, envId);
        setEnvFromJS((int)L, envId, globalsRef, fakeGlobalsRef);
    }

    return L;
}

// clang-format off
EM_JS(char*, acceptStringTransaction, (int envIdx, int transactionIdx), {
    const source = Module.states[envIdx].transactionData[transactionIdx] || "none";
    delete Module.states[envIdx].transactionData[transactionIdx];

    const length = lengthBytesUTF8(source) + 1;
    const ptr = _malloc(length);
    stringToUTF8(source, ptr, length);

    return ptr;
});
// clang-format on

extern "C" int luauLoad(lua_State* L, int sourceIdx, int chunkNameIdx)
{
    int envId = getEnvId(L);
    if (envId == -1)
    {
        fprinterr("illegal state: no environment id found for lua state");
        lua_pushstring(L, "failed to find state");
        return -1;
    }

    char* source = acceptStringTransaction(envId, sourceIdx);
    char* chunkName = acceptStringTransaction(envId, chunkNameIdx);

    if (!source || source == nullptr)
    {
        fprinterr("failed to accept source from transaction");
        lua_pushstring(L, "failed to accept source from transaction");
        return -1;
    }

    if (!chunkName || chunkName == nullptr)
    {
        fprinterr("failed to accept chunkName from transaction");
        lua_pushstring(L, "failed to accept chunkName from transaction");
        return -1;
    }

    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(source, strlen(source), nullptr, &bytecodeSize);
    free(source);

    int result = luau_load(L, chunkName, bytecode, bytecodeSize, 0);
    free(bytecode);

    return result;
}

extern "C" void luauClose(lua_State* L)
{
    lua_close(L);
}

extern "C" bool isreadonly(lua_State* L, int lref)
{
    lua_getref(L, lref);
    bool readonly = lua_getreadonly(L, -1) == 1;
    lua_pop(L, 1);
    return readonly;
}

extern "C" void setreadonly(lua_State* L, int lref, bool readonly)
{
    lua_getref(L, lref);
    lua_setreadonly(L, -1, readonly ? 1 : 0);
    lua_pop(L, 1);
}

extern "C" int getrawmetatable(lua_State* L, int lref)
{
    lua_getref(L, lref);
    if (!lua_getmetatable(L, -1))
    {
        lua_pop(L, 1);
        return LUA_NOREF;
    }
    int ref = lua_ref(L, -1);
    lua_pop(L, 2);
    return ref;
}
// extern "C" bool luaNewIndex(lua_State* L, int lref, const char* KT, const char* KV, const char* VT, const char* VV, bool bypassReadonly)
// {
//     lua_getref(L, lref);

//     bool wasSetWritable = false;
//     if (lua_getreadonly(L, -1) == 1)
//     {
//         if (bypassReadonly)
//         {
//             lua_setreadonly(L, -1, 0);
//             wasSetWritable = true;
//         }
//         else

#endif