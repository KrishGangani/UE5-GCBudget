# 04 · C++ Fundamentals: GCBudget

**Rule:** this file explains the general language features the project's code relies on, in the order they first appear. Unreal-specific APIs are in `06`. **Current to:** Phase 4. Phase 4 has almost no C++, so most of this file covers the *other* languages the foundation is written in (C# build rules, Windows batch, Python). Phase 5 adds the C++ topics the stores and handles need (pointers vs indices, templates, bit arrays, `const` correctness, `static_assert`).

---

## 1. The preprocessor: `#include` and macros

- `#include "Modules/ModuleManager.h"` pastes that header's text in before compilation, so the declarations in it (`IMPLEMENT_MODULE`, `FDefaultModuleImpl`) are known.
- Quotes (`"…"`) search the module's include paths, which UBT builds from the `Build.cs` dependencies (A3). That is why a missing dependency shows up as "cannot open include file".
- A **macro** such as `IMPLEMENT_MODULE(FDefaultModuleImpl, GCBudgetCore)` is text substitution done before the compiler runs. It expands into a static object and an `extern "C"` function (`06` §1).
- Unreal uses macros for boilerplate that must be identical in every module and that C++ templates can't express: generating uniquely named symbols from a module name with `##`.
- *The cost:* errors inside a macro point at the macro line, not at what it expanded to. Keep macro arguments simple.

## 2. Static objects and registration

- `IMPLEMENT_MODULE` declares a `static` object at file scope. Such objects are constructed before `main` (or when the DLL loads), and their constructor registers the module with the module manager.
- This is a common C++ pattern for "plug-in" registration without a central list that every module would have to edit.
- *The risk:* the order in which different files' static objects are constructed is unspecified. That is why the engine only *registers* here and creates the module later, at its loading phase.

## 3. C# in the build rules: classes, constructors and `base(...)`

- `public class GCBudgetTarget : TargetRules` declares a class that **inherits** from `TargetRules`: it gets every property of the base class and sets some of them.
- `public GCBudgetTarget(TargetInfo Target) : base(Target)` is a **constructor** that first runs the base class's constructor with the same argument. Only then does its own body set `Type`, `DefaultBuildSettings` and so on.
- `new string[] { "Core", "CoreUObject", "Engine" }` creates an array inline. `AddRange` appends all its elements to a list.
- UBT compiles these files itself at build time. A C# error in a `Build.cs` stops the build before any C++ is compiled.

## 4. Windows batch essentials (the `Tools\*.bat` scripts)

| Construct | Meaning | Where it matters |
|---|---|---|
| `@echo off` | Don't print each command before running it | All scripts |
| `setlocal` | Variables set after this disappear when the script ends | All scripts: nothing leaks into the queue's shell |
| `set "NAME=value"` | Quotes around the whole assignment keep trailing spaces out and allow spaces in values | Paths under `D:\UNREAL PORTFOLIO` contain a space |
| `%~1`, `%~dp0` | `%~1` is argument 1 without quotes; `%~dp0` is the drive and folder of argument 0, the script itself | Locating the project relative to the script |
| `shift` | Moves every argument one place down: `%1` becomes `%0`… **including `%0`** | The Phase 4 bug: after `shift`, `%~dp0` no longer meant the script's folder (`05` A9, `07`) |
| `call x.bat` | Runs another batch file and **returns** to this one | `build.bat`, `package.bat` call the engine's scripts and then pass on the exit code |
| `exit /b %ERRORLEVEL%` | Ends the script and hands the last exit code to the caller | The queue reports failure only if the exit code is non-zero |
| `goto label` / `:label` | Jump to a label | `smoke_game.bat`'s two modes |
| `a && b` | Run `b` only if `a` exited with 0 | The queued Phase 4 job chains the steps this way, so a failure stops the chain |

**Line endings:** the repository keeps `.bat` files in CRLF (`.gitattributes`). `cmd.exe` can mis-handle labels in LF-only batch files.

## 5. Python essentials (the `Tools\*.py` scripts)

- `ctypes` calls Windows API functions directly: `ctypes.WinDLL("kernel32", use_last_error=True)` loads the library once at import (`KERNEL32`), and `KERNEL32.GetLogicalProcessorInformationEx` calls into it.
  - Setting `.argtypes` and `.restype` tells Python the C types, so 64-bit handles and masks aren't truncated to 32 bits.
  - `ctypes.c_size_t(0)` + `ctypes.byref(...)` passes a pointer to a variable the function fills in (how `GetProcessAffinityMask` returns two masks).
- `struct.unpack_from("<II", raw, offset)` reads two little-endian 32-bit unsigned integers from a byte buffer at an offset. That is how the variable-length processor records are walked.
- `GetCurrentProcess()` returns a *pseudo-handle* to the calling process: it needs no `CloseHandle`, which is why pinning the runner itself is simpler than opening and pinning each child process.
- `try … finally` (used by the Phase 5 runner) guarantees the game's process tree is killed even if a run fails or times out.
- `python -I` (isolated mode) ignores environment variables and the user's site-packages, and does not put the script's folder first on the import path. Scripts behave the same on every machine and can't pick up a stray module by accident.
