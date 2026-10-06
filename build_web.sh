#!/usr/bin/env bash
set -e

emcmake cmake -B build_web -DLUAU_BUILD_WEB=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build_web -j2 --target Luau.Web.JSPI
cmake --build build_web -j2 --target Luau.Web.Asyncify
