#! /bin/bash
xmake m -b
xmake project -k compile_commands
xmake m -e expc

xmake m -b
xmake f -c
xmake c
xmake f -m debug --toolchain=clang
xmake m -e dccfg


xmake m -b
xmake f -c
xmake c
xmake f -m debug --toolchain=gcc
xmake m -e dgcfg