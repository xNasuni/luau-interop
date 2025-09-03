Luau for Web
====

Luau (lowercase u, /ˈlu.aʊ/) is a fast, small, safe, gradually typed embeddable scripting language derived from [Lua](https://lua.org).

This fork is designed to enhance the interop you get while embedding Luau in a website, Node.JS, or Typescript. For examples, you can check out [the wiki](https://github.com/xNasuni/luau-web/wiki).

# Usage

Luau is an embeddable programming language, but now it comes with an extra argument for the WebAssembly API allowing you to provide a custom environment for the script that is executed, alongside interop allowing JS to call functions from Lua, and Lua to call functions from JS.



## Building Luau for Web

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

Building Luau and targeting web/node requires `emcmake` which is different from `cmake`.

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