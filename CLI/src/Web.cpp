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

static std::unordered_map<lua_State*, int> emEnvMap;

extern "C" void setEnvId(lua_State* L, int envId)
{
    emEnvMap[L] = envId;
}

extern "C" int getEnvId(lua_State* L)
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
        let value = env[key];
        let type = "string";

        if (typeof value == "number")
        {
            type = "number";
            value = value.toString();
        }
        else if (typeof value == "boolean")
        {
            type = "boolean";
            value = value.toString();
        }
        else if (typeof value == "function")
        {
            type = "function";
            value = key;
        }
        else if (value == undefined) {
            type = "nil";
            value = "nil";
        }

        Module.ccall('pushGlobalToLua', 'void', [ 'number', 'string', 'string', 'string' ], [ L_ptr, key, type, value ]);
    }
});

extern "C" void pushGlobalToLua(lua_State* L, const char* key, const char* type, const char* value)
{
    if (!L || !key || !type || !value)
    {
        return;
    }

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
    else if (strcmp(type, "nil") == 0) {
        lua_pushnil(L);
    }
    else
    {
        fprintf(stderr, "\x1b[1;38;5;13m[luau-web] \x1b[38;5;11m[warn] \x1b[22mpushGlobalToLua: unsupported type '%s' for key '%s'\x1b[0m\n", type, key);
        lua_pushnil(L);
    }

    lua_setglobal(L, key);
}

#endif

static void setupState(lua_State* L)
{
    luaL_openlibs(L);

    luaL_sandbox(L);
}

static std::string runCode(lua_State* L, const std::string& source)
{
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(source.data(), source.length(), nullptr, &bytecodeSize);
    int result = luau_load(L, "=stdin", bytecode, bytecodeSize, 0);
    free(bytecode);

    if (result != 0)
    {
        size_t len;
        const char* msg = lua_tolstring(L, -1, &len);

        std::string error(msg, len);
        lua_pop(L, 1);

        return error;
    }

    lua_State* T = lua_newthread(L);

    lua_pushvalue(L, -2);
    lua_remove(L, -3);
    lua_xmove(L, T, 1);

    int status = lua_resume(T, NULL, 0);

    if (status == 0)
    {
        int n = lua_gettop(T);

        if (n)
        {
            luaL_checkstack(T, LUA_MINSTACK, "too many results to print");
            lua_getglobal(T, "print");
            lua_insert(T, 1);
            lua_pcall(T, n, 0, 0);
        }

        lua_pop(L, 1); // pop T
        return std::string();
    }
    else
    {
        std::string error;

        lua_Debug ar;
        if (lua_getinfo(L, 0, "sln", &ar))
        {
            error += ar.short_src;
            error += ':';
            error += std::to_string(ar.currentline);
            error += ": ";
        }

        if (status == LUA_YIELD)
        {
            error += "thread yielded unexpectedly";
        }
        else if (const char* str = lua_tostring(T, -1))
        {
            error += str;
        }

        error += "\nstack backtrace:\n";
        error += lua_debugtrace(T);

        lua_pop(L, 1); // pop T
        return error;
    }
}

extern "C" const char* executeScript(const char* source, int envId)
{
    // setup flags
    for (Luau::FValue<bool>* flag = Luau::FValue<bool>::list; flag; flag = flag->next)
        if (strncmp(flag->name, "Luau", 4) == 0)
            flag->value = true;

    // create new state
    std::unique_ptr<lua_State, void (*)(lua_State*)> globalState(luaL_newstate(), lua_close);
    lua_State* L = globalState.get();

    // setup state
    setupState(L);

    // sandbox thread
    luaL_sandboxthread(L);

// check for env (only for web/emscripten)
#ifdef __EMSCRIPTEN__
    if (envId != 0)
    {
        setEnvId(L, envId);
        setEnvFromJS(envId, (int)L);
    }
#endif

    // static string for caching result (prevents dangling ptr on function exit)
    static std::string result;

    // run code + collect error
    result = runCode(L, source);

    return result.empty() ? NULL : result.c_str();
}
