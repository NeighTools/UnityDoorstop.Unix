# UnityDoorstop for Linux and macOS

[![Build Status](https://dev.azure.com/ghorsington/UnityDoorstop/_apis/build/status/NeighTools.UnityDoorstop.Unix?branchName=master)](https://dev.azure.com/ghorsington/UnityDoorstop/_build/latest?definitionId=2&branchName=master)

This is [UnityDoorstop](https://github.com/NeighTools/UnityDoorstop) equivalent for Linux and macOS.  
The library makes use of `LD_PRELOAD` and `DYLD_INSERT_LIBRARIES` injection. 

Hooking is done with the help of [plthook](https://github.com/kubo/plthook) modified to work better with older Unity builds.

## How to build

You need gcc and make to build this binary

To build both x64 and x86 binaries, run `make`. This can fail on newer macOSes that don't support building x86 binaries.

To build a specific version, use `make build_x64` or `make build_x86` depending on your architecture.

## How to use

1. Put the built `doorstop.so` (or `.dylib`) and `run.sh` to the same directory as the game
2. Edit `run.sh` to configure Doorstop to work correctly
3. Run `run.sh` when you want to run your game modded
4. When you want to run vanilla game, run the game normally

## Note for macOS users

You cannot directly run the `Game.app`, as it is just a folder. Instead, you must edit `run.sh` to execute `Game.app/Contents/MacOS/Game` (or other game executable depending on the Unity game).
