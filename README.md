## README

### Brief
This is a baseline stack VM with a naive JIT tier, using AsmJit and C++26. Maybe I'll add a dynamic language frontend later.

### Usage
 1. Ensure you have CMake 3.31+, a recent version of Clang supporting C++26, and AsmJit's entire source in `deps/asmjit`.
 2. Enable `./project.sh`.
 3. Run `./project.sh build debug-build <your build tool>`. The executable will be at path `./build/toyjit`.

### Design Basics
 - Async future launches for JIT compilation of a function's chunk... It's only awaited if the saved future has a ready stub. Otherwise, existing trampolines can be reused for the targeted chunk if found.
 - JITed native code pieces are called stubs.
    - Helper natives _must_ interface with the VM / JIT via the same signature below, but behind `void(*)(VM* vm, Value* dest, Value* args, Value *extra_args)`.
    - All these stubs _must_ interface with the VM via `using StubFn = Value(*)(VMState* vm, Value* locals, const Value* cvp, const void* helpers)`.
    - All stubs return a `Value` via `rax` back to their VM / native caller.
    -  RDI: `vm` points to the VM context which can be used by any native functions passed in `helpers` for complex operations.
    - RSI: `locals` points to the argument-locals of the stub.
    - RDX: `cvp` points to the corresponding chunk constants.
    - RCX: `helpers` points to the 1st native helper or more...
    - Stubs emulate temporary value computations of the VM stack on the native stack for simplicity.
 - Singly pass over a view of a hot bytecode chunk to compile a native code stub.
 - Native code stubs are trampolined. Trampolines are short bytecode chunks that have `{<type guards...>, NATIVE_CALL <JIT-stub-ID>, RET}, {<original constant copies...>}` that wrap the JIT call to avoid extra branching per general call.

### Design of Deopt
 - **Purpose:** cleanly recover if a type-specialized stub cannot be run due to any reason.
   - During interpretation of the trampoline, if a type guard is failed before any JIT call, know the original chunk's ID to fall back to.
   - Assume the VM stack has the N argument Values before every trampoline / bytecode call. This means the call helper should push arguments from the native stack to VM stack before trampolining. Then the result is returned if ok, but an "oops" value indicates guard failure. Thus, a guard failure will revert to a generic bytecode chunk to get the callee's result.
      - **NOTE** Pretend we have a black-box either way: just call something and then get a Value result to push on any operand stack.
 - **Consequences - Type Guard Failure of Arguments**
   - The deopt logic runs, removing the VM trampoline's `CallFrame` and taking the slow, generic path through the original bytecode chunk to recover.

### TODOs
 1. Add multiplication & division JIT support.
 2. Add nullish check JIT support.
 3. Add more deopt logic to release a `StubFn` ptr from the AsmJit runtime and "reset" the `Prof` and `StubResult` of that original chunk ID. Then _revert_ all invalidated trampoline call sites. **TODO**
 4. Add initial, modifed _TBasic_ dialect frontend & bytecode compiler.
    - Support to TBasic `0.4.0` and diverge from there.
    - Support superglobals.
    - All objects are shape-IC optimized collections of Values. Semantically, they act like metatables.
    - Support object method calls.
