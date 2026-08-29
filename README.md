## README

### Brief
This is a baseline stack VM with a naive JIT tier, using AsmJit and C++26. Maybe I'll add a dynamic language frontend later.

### Usage
 1. Ensure you have CMake 3.31+, a recent version of Clang supporting C++26, and AsmJit's entire source in `deps/asmjit`.
 2. Enable `./project.sh`.
 3. Run `./project.sh build debug-build <your build tool>`. The executable will be at path `./build/toyjit`.

### Design
 - No concurrency in the JIT for simplicity.
 - JITed native code pieces are called stubs.
    - Helper natives _must_ interface with the VM / JIT via the same signature below, but hidden behind `void*`.
    - All these stubs _must_ interface with the VM via `using StubFn = Value(*)(VMState* vm, Value* locals, const Value* cvp, const void* helpers)`.
    - All stubs return a `Value` via `rax` back to their VM / native caller.
    -  RDI: `vm` points to the VM context which can be used by any native functions passed in `helpers` for complex operations.
    - RSI: `locals` points to the 1st local slot just above the saved RIP and RBP natively...
    - RDX: `cvp` points to the 1st chunk constant and more...
    - RCX: `helpers` points to the 1st native helper or more...
    - Stubs emulate temporary value computations of the VM stack on the native stack for simplicity.
    - Scratch regs used: `r8, r9, r10, r11`
 - Singly pass over a view of a hot bytecode chunk to compile a native code stub.
 - Native code stubs are trampolined. Trampolines are short bytecode chunks that have `{NATIVE_CALL <JIT-stub-ID>, RET}, {<no constants>}`.
