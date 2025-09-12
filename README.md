Luau Interop
====

<img src="./assets/LuauInterop.png" width="130px" align="right"/>

Luau (lowercase u, /ˈlu.aʊ/) is a fast, small, safe, gradually typed embeddable scripting language derived from [Lua](https://lua.org).

This fork is designed to overhaul the interop you get while embedding Luau in a website, Node.JS, or Typescript. For examples and documentation for Web/Node, you can check out [the wiki](https://github.com/xNasuni/luau-web/wiki).

# Usage

Luau is an embeddable programming language, this fork rewrites the WASM execution API and actually implements interop allowing you to provide a custom environment for the script that is executed, as well as interop allowing JS to call functions from Lua, and Lua to call functions from JS.

## Building Luau Interop (not Web/Node)
```shell
make
```

## Building Luau Interop for Web/Node

> ### Linux / MacOS

```shell
emcmake cmake . -DLUAU_BUILD_WEB=ON -DCMAKE_BUILD_TYPE=Release
cmake --build . --target Luau.Web -j2
```

> ### Windows

```bat
emcmake cmake . -DLUAU_BUILD_WEB=ON
cmake --build . --target Luau.Web --config Release -j2
```

---

Building Luau Interop targeting Web/Node requires `emcmake` which is different from `cmake`.

## Installing Emcmake

> ### Linux / MacOS
```shell
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

> ### Winows
```bat
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
emsdk install latest
emsdk activate latest
emsdk_env.bat
```
