# Linux: fix crash when loading the plugin in hosts that export Lua

[← Overview](README.md) · **PR:** [plugdata-team/plugdata#2430](https://github.com/plugdata-team/plugdata/pull/2430) · plugdata `develop` ← `topemalheiro:pr/host-fixes-lua` · 1 commit, +11 in `CMakeLists.txt` · needs nothing first

## What it does

**What you run into without it.** On Linux, add plugdata (LV2, VST3 or CLAP) to a track in Ardour and Ardour aborts before plugdata even shows up, with `PANIC: unprotected error in call to Lua API` in the terminal. I reproduced it with the stock Ardour 9.7 from Arch's packages, every time.

**Why.** plugdata has LuaJIT built in, for pdlua. Ardour exports its own Lua 5.3 from `libardour.so`, with the same function names, such as `lua_setfield`. The Linux plugins exported the symbols of the static libraries they link and called them through the dynamic symbol table, where the host's symbols come first. So pdlua's Lua calls landed in Ardour's Lua, which can't read LuaJIT's state, and it panicked.

**What changes.** Two linker options for the Linux plugin targets. One stops exporting the symbols of the static libraries plugdata links. The other binds plugdata's own calls to its own code. With them, Ardour loads plugdata, saves the session and closes normally. macOS and Windows don't resolve calls this way, so nothing changes there.

## The code, in reading order

All of it is in `CMakeLists.txt`.

1. **[CMakeLists.txt:769-771](https://github.com/topemalheiro/plugdata/blob/0fcef173b148260f71c4344bd3ad3328e7a99f83/CMakeLists.txt#L769-L771)** is a comment saying why the lines below exist.
2. **[CMakeLists.txt:772-778](https://github.com/topemalheiro/plugdata/blob/0fcef173b148260f71c4344bd3ad3328e7a99f83/CMakeLists.txt#L772-L778)**: `if(UNIX AND NOT APPLE)` limits everything inside to Linux and BSD.
3. **[CMakeLists.txt:773-777](https://github.com/topemalheiro/plugdata/blob/0fcef173b148260f71c4344bd3ad3328e7a99f83/CMakeLists.txt#L773-L777)** goes through the six plugin targets: the instrument and the effect, each as VST3, CLAP and LV2. `if(TARGET ${target})` skips any that aren't being built.
4. **[CMakeLists.txt:775](https://github.com/topemalheiro/plugdata/blob/0fcef173b148260f71c4344bd3ad3328e7a99f83/CMakeLists.txt#L775)** adds the two options to that target's link step. This line is the fix.

## The two options (block 4)

```cmake
target_link_options(${target} PRIVATE "LINKER:-Bsymbolic" "LINKER:--exclude-libs,ALL")
```

- `--exclude-libs,ALL`: don't export any symbol that comes from a static library. LuaJIT's symbols stay inside the plugin.
- `-Bsymbolic`: bind the plugin's references to its own global symbols at link time, instead of looking them up when it's loaded.

Together they mean plugdata's Lua calls can't end up anywhere else. The entry points a host needs are still exported: the LV2 now exports 8 symbols, including `lv2_descriptor`; the VST3 exports 18 and the CLAP 3.

How it was tested, and what wasn't, is in the PR description.
