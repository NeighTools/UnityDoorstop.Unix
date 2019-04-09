# UnityDoorstop for Linux

This is a quick and dirty [UnityDoorstop](https://github.com/NeighTools/UnityDoorstop) equivalent for Linux.

## How to build

You need gcc to build this binary

```sh
gcc -shared -fPIC -o doorstop.so doorstop.c
```

## How to use

1. Put the built `doorstop.so` and `run.sh` to the same directory as the game
2. Edit `run.sh` to configure Doorstop to work correctly
3. Run `run.sh` when you want to run your game modded
4. When you want to run vanilla game, run the game normally