// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "lua.h"
#include "lualib.h"
#include "luacode.h"

#include "Luau/Common.h"

#include <string>
#include <memory>

#include <string.h>

#ifdef __EMSCRIPTEN__
#include <unordered_map>
#include <sstream>
#include <iomanip>

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

EM_JS(void, setEnvFromJS, (int envId, int L_ptr), {
    if (envId == 0)
    {
        return;
    }

    let env = Module.environments[envId];

    for (let key in env)
    {
        let [type, value] = Module.jsToLuauValue(env[key]);

        Module.ccall('pushGlobalToLua', 'void', [ 'number', 'string', 'string', 'string' ], [ L_ptr, key, type, value ]);
    }
});

EM_JS(void, ensureInterop, (), {
    Module.LUA_VALUE = Module.LUA_VALUE || Symbol("LuaValue");
    Module.JS_VALUE = Module.JS_VALUE || Symbol("JsValue");

    Module.luaValueCache = Module.luaValueCache || new Map();
    Module.jsValueCache = Module.jsValueCache || new Map();
    Module.jsValueReverse = Module.jsValueReverse || new Map();

    Module.transactionData = Module.transactionData || [];
    Module.environments = Module.environments || [];

    Module.nextJSRef = Module.nextJSRef || 1;

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

    Module.fprint = function(...args) {
        console.error("\x1b[1;38;5;13m[luau-web] \x1b[38;5;15m[info]\x1b[22m", ...args, "\x1b[0m");
    };
    
    Module.fprintwarn = function(...args) {
        console.error("\x1b[1;38;5;13m[luau-web] \x1b[38;5;11m[warn]\x1b[22m", ...args, "\x1b[0m");
    };

    Module.fprinterr = function(...args) {
        console.error("\x1b[1;38;5;13m[luau-web] \x1b[38;5;1m[error]\x1b[22m", ...args, "\x1b[0m");
    };

    Module.LuaValue = function(state, type, ref)
    {
        if (Module.luaValueCache.has(ref))
        {
            return Module.luaValueCache.get(ref);
        }

        const obj = {
            [Module.LUA_VALUE]: {
                ref,
                type,
                state,
                toString() {
                    return "[LuaReference " + type + " " + ref + "]";
                },
                persistentRef() {
                    return Module.LuaValue(this.state, this.type, Module.ccall('luaCloneref', 'int', [ 'number', 'number' ], [ this.state, this.ref ]));
                },
                release() {
                    if (this.released)
                    {
                        return;
                    }
                    Module.ccall('luaUnref', 'void', [ 'number', 'number' ], [ this.state, this.ref ]);
                    this.released = true;
                }
            }
        };

        var luaValue = obj;

        if (type == "lfunction") {
            luaValue = new Proxy(function(){}, {
                apply(target, thisArg, args) {
                    return Module.callLuaFunction(obj, args);
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
                return Module.indexLuaTable(obj, key);
            };

            obj['set'] = function(key, value) {
                return Module.newIndexLuaTable(obj, key, value);
            };

            luaValue = new Proxy({}, {
                get(target, prop, receiver) {
                    if (prop in obj) {
                        return obj[prop];
                    }

                    return Module.indexLuaTable(obj, prop);
                },
                set(target, prop, value, receiver) {
                    return Module.newIndexLuaTable(obj, prop, value);
                },
                has(target, prop) {
                    if (prop in obj) {
                        return true;
                    }
                    
                    return Module.indexLuaTable(obj, prop) != null;
                },
                ownKeys(target) {
                    return Reflect.ownKeys(obj);
                },
                getOwnPropertyDescriptor(target, prop) {
                    return Object.getOwnPropertyDescriptor(obj, prop);
                }
            });
        };

        Module.luaValueCache.set(ref, luaValue);

        return luaValue;
    };

    Module.callLuaFunction = function(luaFunction, args) {
        const luaFunctionData = luaFunction[Module.LUA_VALUE];

        if (luaFunctionData.released) {
            throw new GlueError("attempt to call released function");
            return;
        }

        const trimmed = args.slice(0, args.findLastIndex(x => x != undefined) + 1);
        const argDataKey = Module.transactionData.length;
        Module.transactionData[argDataKey] = trimmed;

        const status = Module.ccall("luaPcall", 'void', [ 'number', 'number', 'number' ], [ luaFunctionData.state, luaFunctionData.ref, argDataKey ]);

        const multretData = Module.transactionData[argDataKey];
        delete Module.transactionData[argDataKey];

        const argData = multretData.map(v => Module.luauToJsValue(luaFunctionData.state, v));
        
        if (status != 0) {
            throw new LuaError(argData[0] ? argData[0] : "No output from Luau");
        }
        
        return argData;
    };

    Module.indexLuaTable = function(luaTable, key) {
        const luaTableData = luaTable[Module.LUA_VALUE];

        if (luaTableData.released) {
            throw new GlueError("attempt to index released table");
            return;
        }

        const [type, value] = Module.jsToLuauValue(key);

        const transactionIdx = Module.ccall("luaIndex", "number", [ "number", "number", "string", "string" ], [ luaTableData.state, luaTableData.ref, type, value ]);

        const transactionData = Module.transactionData[transactionIdx];
        delete Module.transactionData[transactionIdx];

        const luauValue = Module.luauToJsValue(luaTableData.state, transactionData);

        return luauValue
    };

    Module.newIndexLuaTable = function(luaTable, key, value) {
        const luaTableData = luaTable[Module.LUA_VALUE];

        if (luaTableData.released) {
            throw new GlueError("attempt to newindex released table");
            return;
        }

        const [KT, KV] = Module.jsToLuauValue(key);
        const [VT, VV] = Module.jsToLuauValue(value);

        const modified = Module.ccall("luaNewIndex", "number", [ "number", "number", "string", "string", "string", "string" ], [ luaTableData.state, luaTableData.ref, KT, KV, VT, VV ]);

        return modified == 1;
    };

    Module.getPersistentRef = function(jsValue) {
        if (Module.jsValueReverse.has(jsValue)) {
            return Module.jsValueReverse.get(jsValue);
        }

        const ref = Module.nextJSRef++;
        const obj = {
            [Module.JS_VALUE]: {
                ref,
                value: jsValue,
                released: false,
                release() {
                    if (this.released) return;
                    Module.jsValueCache.delete(ref);
                    Module.jsValueReverse.delete(jsValue);
                    this.released = true;
                }
            }
        };

        Module.jsValueCache.set(ref, obj);
        Module.jsValueReverse.set(jsValue, ref);

        return ref;
    };

    Module.luauToJsValue = function(L_ptr, v)
    {
        if (typeof v == "undefined" || typeof v.type == "undefined" || typeof v.value == "undefined") {
            return null;
        }

        switch (v.type)
        {
        //--> value types
        case "string":
            return v.value;
        case "number":
            return (typeof v.value == "number" ? v.value : Number(v.value));
        case "boolean":
            return (typeof v.value == "boolean" ? v.value == true : v.value == "true");
        case "nil":
        case "undefined":
            return null;
        //--> reference types
        case "table":
        case "function":
        case "userdata":
        case "thread":
        case "buffer":
        {
            const ref = parseInt(v.value, 10);
            return Module.LuaValue(L_ptr, "l" + v.type, ref);
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

    Module.jsToLuauValue = function(value) {
        let type = "unknown";

        if (!value || typeof value == "undefined") {
            type = "nil";
            value = "nil";
        }
        else if (typeof value == "number")
        {
            type = "number";
            value = value.toString();
        }
        else if (typeof value == "string") {
            type = "string";
            value = value.toString();
        }
        else if (typeof value == "boolean")
        {
            type = "boolean";
            value = value.toString();
        }
        else if (typeof value == "function" && !(Module.safeIn(Module.LUA_VALUE, value)) && !(Module.safeIn(Module.JS_VALUE, value)))
        {
            type = "function";
            value = Module.getPersistentRef(value).toString();
        }
        else if (typeof value == "object" || (Module.safeIn(Module.LUA_VALUE, value) || Module.safeIn(Module.JS_VALUE, value)))
        {
            if (Module.safeIn(Module.LUA_VALUE, value)) {
                const data = value[Module.LUA_VALUE];
                if (!data.released) {
                    type = data.type;
                    value = data.ref.toString();
                } else {
                    Module.fprintwarn("illegal operation: will not pass released reference");
                    type = "nil";
                    value = "nil";
                }
            } else if (Module.safeIn(Module.JS_VALUE, value)) {
                const data = value[Module.JS_VALUE];
                type = "object";
                value = data.ref.toString();
            } else {
                type = "object";
                value = Module.getPersistentRef(value).toString();
            }
        }
        else if (value == undefined)
        {
            type = "nil";
            value = "nil";
        } else {
            Module.fprintwarn(`illegal j2l conversion: unsupported type '${typeof value}', defaulted to nil: ${value}`);
            type = "nil";
            value = "nil";
        }

        return [type, value];
    }
});

EM_JS(void, getJSProperty, (int L_ptr, int envId, const char* pathCStr, const char* keyCStr), {
    const path = JSON.parse(UTF8ToString(pathCStr));
    const key = JSON.parse(UTF8ToString(keyCStr));
    const data = Module.jsValueCache.get(path);

    if (data) {
        const keyData = Module.luauToJsValue(L_ptr, key);
        if (!data[Module.JS_VALUE]) {
            Module.fprintwarn("illegal state: js callback on non js data");  
            return;
        };

        const [type, value] = Module.jsToLuauValue(data[Module.JS_VALUE].value[keyData]);

        Module.ccall('pushValueToLuaWrapper', 'void', [ 'number', 'string', 'string', 'string' ], [ L_ptr, type, value, `${keyData}` ]);
    }
});

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
        const char* str = lua_tostring(L, index);
        std::string escapedStr;
        for (size_t i = 0; str[i]; i++)
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
    case LUA_TTABLE:
    case LUA_TFUNCTION:
    case LUA_TUSERDATA:
    case LUA_TTHREAD:
    case LUA_TBUFFER:
    {
        const char* typeName = luauTypeName(valueType);
        lua_pushvalue(L, index);

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
}

EM_JS(int, pushTransactionString, (const char* str), {
    const transactionKey = Module.transactionData.length;
    Module.transactionData[transactionKey] = UTF8ToString(str);

    return transactionKey;
});

extern "C" int getLuaValue(lua_State* L, int index) {
    int ref = LUA_NOREF;
    std::string value = serializeLuaValue(L, index, &ref);

    return pushTransactionString(value.c_str());
}

int proxy_index(lua_State* L)
{
    const char* path = lua_tostring(L, lua_upvalueindex(1));
 
    int keyType = lua_type(L, 2);

    if (isValueType(keyType) || isReferenceType(keyType))
    {
        int ref = LUA_REFNIL;
        std::string keyJson = serializeLuaValue(L, 2, &ref);

        int envId = getEnvId(L);

        if (envId != -1)
        {
            getJSProperty((int)L, envId, path, keyJson.c_str());
        }
        else
        {
            fprinterr("illegal state: no environment id found for lua state");
            lua_pushnil(L);
        }

        lua_unref(L, ref);
        return 1;
    }
    else
    {
        fprintwarn("illegal type: unsupported key type '%s' for object '%s'", luauTypeName(keyType), path);
        return 0;
    }
}

EM_JS(int, callJSFunction, (int L_ptr, int envId, const char* path, const char* argsJson), {
    const pathStr = UTF8ToString(path);
    const argsStr = UTF8ToString(argsJson);

    const rawArgs = JSON.parse(argsStr);
    const args = rawArgs.map(arg=>Module.luauToJsValue(L_ptr, arg));

    const key = JSON.parse(pathStr);
    var trmimed = [];

    if (Module.jsValueCache.has(key)) {
        const data = Module.jsValueCache.get(key)[Module.JS_VALUE];

        if (data && data.value) {
            const func = data.value;
            const returns = func(...args);

            args.forEach(arg => arg?.[Module.LUA_VALUE]?.release?.());

            const returnData = returns instanceof Array ? returns : [returns];
            trimmed = returnData;
        }
    }

    const returnDataKey = Module.transactionData.length;
    Module.transactionData[returnDataKey] = trimmed;

    return returnDataKey;
});

EM_JS(int, retrieveRetc, (int returnDataKey), {
    const returnData = Module.transactionData[returnDataKey];
    const count = Array.isArray(returnData) ? returnData.length : 0;
    return count;
});

EM_JS(int, pushRetData, (int L_ptr, int returnDataKey, int argc), {
    const returnData = Module.transactionData[returnDataKey];
    if (!returnData) {
        Module.fprintwarn(`illegal state: no return data for key '${returnDataKey}' but pushed with nonzero argc '${argc}'`);
        return 0;
    }

    const count = Array.isArray(returnData) ? returnData.length : 1;
    delete Module.transactionData[returnDataKey];

    returnData.forEach((data) => {
        const [type, value] = Module.jsToLuauValue(data);
        Module.ccall('pushValueToLuaWrapper', 'void', [ 'number', 'string', 'string', 'string' ], [ L_ptr, type, value, `${value}` ]);
    });

    return returnData.length;
});

int proxy_call(lua_State* L)
{
    const char* path = lua_tostring(L, lua_upvalueindex(1));
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

    int returnDataKey = callJSFunction((int)L, envId, path, argsJson.c_str());

    int retc = retrieveRetc(returnDataKey);

    lua_settop(L, argc);

    if (retc >= 1) {
        return pushRetData((int)L, returnDataKey, retc);
    } else {
        return 0;
    }
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
    else if (strcmp(type, "object") == 0)
    {
        lua_newtable(L);

        lua_newtable(L);

        lua_pushstring(L, value);
        lua_pushcclosurek(L, proxy_index, NULL, 1, NULL);
        lua_setfield(L, -2, "__index");

        lua_pushstring(L, "The metatable is locked");
        lua_setfield(L, -2, "__metatable");
        
        lua_setreadonly(L, -1, true);

        lua_setmetatable(L, -2);

        lua_setreadonly(L, -1, true);
    }
    else if (strcmp(type, "function") == 0)
    {
        lua_pushstring(L, value);
        lua_pushcclosurek(L, proxy_call, lua_tostring(L, -1), 1, NULL);
    }
    else
    {
        fprintwarn("illegal push: unsupported type '%s' for key '%s' with value '%s'", type ? type : "unknown", key ? key : "unknown", value ? value : "unknown");
        lua_pushnil(L);
    }
}

extern "C" void pushGlobalToLua(lua_State* L, const char* key, const char* type, const char* value)
{
    if (!L || !key || !type || !value)
    {
        return;
    }

    pushValueToLua(L, type, value, key);
    lua_setglobal(L, key);
}

extern "C" void pushValueToLuaWrapper(lua_State* L, const char* type, const char* value, const char* key)
{
    pushValueToLua(L, type, value, key);
}

EM_JS(int, pushArgs, (int L_int, int argIdx), {
    const argData = Module.transactionData[argIdx];
    argData.forEach((data) => {
        const [type, value] = Module.jsToLuauValue(data);
        Module.ccall("pushValueToLuaWrapper", 'void', [ 'number', 'string', 'string', 'string' ], [ L_int, type, value, '<callarg>' ]);
    });

    return argData.length;
});

EM_JS(void, setMultretData, (const char* multretJson, int argIdx), {
    const multretData = JSON.parse(UTF8ToString(multretJson));

    Module.transactionData[argIdx] = multretData;
})

extern "C" int luaPcall(lua_State* L, int ref, int argIdx) {
    int top = lua_gettop(L);

    if (ref != LUA_NOREF) {
        lua_getref(L, ref);
    }

    int nargs = pushArgs((int)L, argIdx);

    int status = lua_pcall(L, nargs, LUA_MULTRET, 0);

    std::string retJson = "[";

    if (status == LUA_OK) {
        int nresults = lua_gettop(L) - top;

        for (int i = 1; i <= nresults; i++) {
            int ref = LUA_NOREF;
            retJson += serializeLuaValue(L, top + i, &ref);
            if (i < nresults) retJson += ",";
        }
    } else {
        int ref = LUA_NOREF;
        retJson += serializeLuaValue(L, -1, &ref);
    }

    retJson += "]";

    lua_settop(L, top);
    setMultretData(retJson.c_str(), argIdx);
    
    return status;
}

extern "C" int luaCloneref(lua_State* L, int ref)
{
    lua_getref(L, ref);
    int persistentRef = lua_ref(L, -1);
    return persistentRef;
}

extern "C" void luaUnref(lua_State* L, int ref)
{
    if (!L || ref <= 0) {
        return;
    }

    lua_unref(L, ref);
}

EM_JS(int, sendValueToJS, (const char* valueJson), {
    const value = JSON.parse(UTF8ToString(valueJson));
    const key = Module.transactionData.length;

    Module.transactionData[key] = value;

    return key;
});

extern "C" int luaIndex(lua_State* L, int lref, const char* KT, const char* KV) {
    lua_getref(L, lref);
    pushValueToLua(L, KT, KV, "<indexarg>");
    
    lua_rawget(L, -2);

    int ref = LUA_NOREF;
    std::string valueJson = serializeLuaValue(L, -1, &ref);
    
    lua_pop(L, 2);
    return sendValueToJS(valueJson.c_str());
}

extern "C" bool luaNewIndex(lua_State* L, int lref, const char* KT, const char* KV, const char* VT, const char* VV) {
    lua_getref(L, lref);
    if (lua_getreadonly(L, -3) == 1) {
        return false;
    }

    pushValueToLua(L, KT, KV, "<indexarg>");
    pushValueToLua(L, VT, VV, "<valuearg>");

    lua_rawset(L, -3);
    
    lua_pop(L, 1);
    return true;
}

#endif

static void setupState(lua_State* L)
{
    luaL_openlibs(L);

    luaL_sandbox(L);
}

extern "C" lua_State* makeLuaState(int envId) {
    // setup flags
    for (Luau::FValue<bool>* flag = Luau::FValue<bool>::list; flag; flag = flag->next)
        if (strncmp(flag->name, "Luau", 4) == 0)
            flag->value = true;

    // create new state
    lua_State* L = luaL_newstate();


    // setup state
    setupState(L);

    // sandbox thread
    luaL_sandboxthread(L);

// check for env (only for web/emscripten)
#ifdef __EMSCRIPTEN__
    ensureInterop();

    if (envId != 0)
    {
        setEnvId(L, envId);
        setEnvFromJS(envId, (int)L);
    }
#endif

    return L;
}

EM_JS(char*, acceptStringTransaction, (int transactionIdx), {
    const source = Module.transactionData[transactionIdx] || "none";
    delete Module.transactionData[transactionIdx];

    const length = lengthBytesUTF8(source) + 1;
    const ptr = _malloc(length);
    stringToUTF8(source, ptr, length);

    return ptr;
});

extern "C" int luauLoad(lua_State* L, int sourceIdx, int chunkNameIdx) {
    size_t bytecodeSize = 0;

    char* source = acceptStringTransaction(sourceIdx);
    if (!source || source == nullptr) {
        lua_pushstring(L, "failed to accept source from transaction");
        return -1;
    }

    char* chunkName = acceptStringTransaction(chunkNameIdx);
    if (!chunkName || chunkName == nullptr) {
        lua_pushstring(L, "failed to accept chunkName from transaction");
        return -1;
    }

    char* bytecode = luau_compile(source, strlen(source), nullptr, &bytecodeSize);
    free(source);

    int result = luau_load(L, chunkName, bytecode, bytecodeSize, 0);
    free(bytecode);

    return result;
}

extern "C" void luauClose(lua_State* L) {
    lua_close(L);
}