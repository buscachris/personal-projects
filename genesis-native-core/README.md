# Native Runtime Scaffold

I started this C execution core to move parts of my SoniCkity runtime out of Python. The interface gives the editor and debugger a stable way to call native code as I move individual components.

Purpose:

- keep Python as the IDE/editor/debugger layer
- move hot execution/runtime paths into native code
- establish a stable ABI before porting the full 68K/Z80/VDP runtime

Current state:

- `devkity_native.h` defines the initial C ABI
- `devkity_native.c` implements a minimal execution scaffold
- `build_native.sh` currently builds the legacy-named `libdevkity_native.so` ABI shim

This is not yet a full Genesis emulator core. It is the first concrete boundary
for migrating the all-Python runtime into a native execution engine.

Planned native migration order:

1. frame/clock and scheduler ownership
2. bus/memory hot paths
3. 68K execution core
4. Z80 execution core
5. VDP timing/composition
6. save/export interfaces for the Python IDE shell
