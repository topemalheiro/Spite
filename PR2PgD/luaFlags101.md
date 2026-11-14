# luaFlags101: the Lua link flags ([plugdata#2430](https://github.com/plugdata-team/plugdata/pull/2430))

**The links expect** `~/Projects/plugdata-prs/host-fixes` to be checked out on `review/host-fixes` (it is). That branch holds all three of my plugdata PRs together. The file this PR changes is identical there to the PR's own branch `pr/host-fixes-lua`, so checking that branch out instead gives the same lines.

## What this PR is, in plain words

**What you run into without it.** On Linux, add plugdata (LV2, VST3 or CLAP) to a track in Ardour and Ardour crashes before plugdata even shows up, with `PANIC: unprotected error in call to Lua API` in the terminal. I reproduced it with the normal Ardour 9.7 from Arch's packages, every time.

**Why.** plugdata has Lua built into it (LuaJIT, which pdlua uses). Ardour also has Lua built in, a different version (5.3), with the same function names, such as `lua_setfield`. On Linux, when a program loads a plugin, the plugin's calls are matched to functions by name, and the names the program already has come first. So plugdata's Lua calls landed in Ardour's Lua, which can't read plugdata's Lua data, and it panicked.

**What the PR changes.** Two options for the last step of building the Linux plugins, where the linker puts each plugin file together. One keeps the names of the libraries built into plugdata private. The other makes plugdata's calls always go to plugdata's own functions. With them, Ardour loads plugdata, saves the session and closes normally. macOS and Windows don't match functions by name like this, so nothing changes there.

**What it needs first.** Nothing.

## The code, in reading order

All of it is in `CMakeLists.txt`, the file that tells CMake how to build plugdata (see "CMake in five lines").

1. **[CMakeLists.txt:769-771](../../plugdata-prs/host-fixes/CMakeLists.txt#L769-L771)** is a comment saying why the lines below exist.
   Python: a `#` comment.
2. **[CMakeLists.txt:772-778](../../plugdata-prs/host-fixes/CMakeLists.txt#L772-L778)**: the `if(UNIX AND NOT APPLE)` on line 772 and the `endif()` on line 778 limit everything inside to Linux and BSD. macOS also counts as "UNIX", hence the `NOT APPLE`.
   Python: `if sys.platform.startswith(("linux", "freebsd")):`.
3. **[CMakeLists.txt:773-777](../../plugdata-prs/host-fixes/CMakeLists.txt#L773-L777)** goes through the six plugin builds: the instrument and the effect, each as VST3, CLAP and LV2. `if(TARGET ${target})` skips any that aren't being built this time.
   Python: `for target in ["plugdata_VST3", "plugdata_fx_VST3", "plugdata_CLAP", "plugdata_fx_CLAP", "plugdata_LV2", "plugdata_fx_LV2"]: if target in targets_being_built:`.
4. **[CMakeLists.txt:775](../../plugdata-prs/host-fixes/CMakeLists.txt#L775)** adds the two options to that plugin's link step (see "The two options").
   Python: `targets[target].link_options += ["-Wl,-Bsymbolic", "-Wl,--exclude-libs,ALL"]`.

## Names: how a call finds its function

Once compiled, code keeps its function names as "symbols": a table saying where each function's code starts. A plugin file (a `.so` on Linux) offers some of its names to whatever loads it.

When Ardour loads plugdata, Linux's loader connects plugdata's calls to functions by name. It looks first in the program and the libraries loaded before plugdata, and only then in plugdata itself. Ardour's `libardour.so` offers 137 Lua names, so plugdata's call to `lua_setfield` was connected to Ardour's `lua_setfield`.

Python comparison: it's as if your module did `from mylua import *` and then the host did `from hostlua import *` into the same namespace. The same names now point at the host's functions, and your code calls them without knowing.

## Static libraries: copied in, and offered by default

LuaJIT, the C++ standard library (libstdc++) and opus are copied into plugdata's plugin file when it's built. That's "static" linking, as opposed to libraries that are loaded separately when the program runs. By default, everything copied in is also offered to the outside: plugdata's LV2 offered 5,665 names, 143 of them Lua's.

Python comparison: vendoring a package inside yours is fine, but here every name from it was also published at the top level for anyone to replace.

## The two options (block 4)

```cmake
target_link_options(${target} PRIVATE "LINKER:-Bsymbolic" "LINKER:--exclude-libs,ALL")
```

- `--exclude-libs,ALL`: don't offer any name that comes from a library copied in. LuaJIT's names stay inside plugdata.
- `-Bsymbolic`: when plugdata calls one of its own functions, connect it to plugdata's own copy when the plugin is built, instead of looking the name up when it's loaded.

Together they mean plugdata's Lua calls can't end up anywhere else. The names a host does need are still offered: the LV2 now offers 8 names, including `lv2_descriptor`, which is how a host finds the plugin; the VST3 offers 18 and the CLAP 3. `LINKER:` is CMake's way to hand an option to the linker; it becomes `-Wl,-Bsymbolic`.

## CMake in five lines

- A **target** is one thing to build: `plugdata_LV2` is the LV2 plugin.
- `if(...)` ... `endif()` and `foreach(x a b c)` ... `endforeach()` are CMake's `if` and `for`, closed with a word instead of indentation.
- `${target}` is the value of the variable `target`, like `{target}` in a Python f-string.
- `if(TARGET name)` asks "is a target with this name being built?".
- `target_link_options(T PRIVATE ...)` adds options to the link step of target `T` only; `PRIVATE` means they don't spread to anything that uses `T`.

Block 4 (line 775) is the actual fix. Blocks 1 to 3 are the comment and the loop that apply it to each Linux plugin.
