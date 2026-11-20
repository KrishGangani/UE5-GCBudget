# 04 · C++ Fundamentals: GCBudget

**Rule:** this file explains the general language features the project's code relies on, in the order they first appear. Unreal-specific APIs are in `06`. **Current to:** Phase 5 step S1. Sections 1–5 cover the foundation (mostly C# build rules, Windows batch and Python); sections 6 onward add the C++ each Phase 5 step introduces.

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

---

## C++ added in step S1

## 6. Classes, inheritance, `virtual` and `override`

- `class UGCBudgetTelemetrySubsystem : public UWorldSubsystem` inherits everything the base class has and **overrides** some of its virtual functions (`Initialize`, `Deinitialize`, `DoesSupportWorldType`).
- `virtual` means "the call is decided at run time by the object's real type": the engine holds a `UWorldSubsystem*` and still calls *our* `Initialize`.
- `override` asks the compiler to check that a base function with exactly this signature exists. A typo or a wrong parameter type then fails to compile instead of silently creating a new, never-called function.
- `Super::Initialize(Collection)` calls the base version first. `Super` is a type alias that `GENERATED_BODY()` defines, so the code doesn't repeat the base class's name.

## 7. Access control: `public`, `protected`, `private`

- `public`: anyone can call it (`OnWorldBeginPlay`, `Tick`, `GetLastFrameStats`).
- `protected`: the class and its subclasses. `DoesSupportWorldType` is protected in the engine's base class, so our override is too.
- `private`: this class only. The callbacks and the per-frame state can't be changed from outside, so nothing else can break the bracket's bookkeeping.

## 8. `struct` vs `class`, and default member initialisers

- In C++ the only difference is the default access: `struct` is public, `class` is private. The project uses `struct` for plain data (`FGCBudgetFrameStats`, `FRunSettings`) and `class` for objects with behaviour.
- `double BracketMs = -1.0;` is a **default member initialiser**: every new object starts with that value, with no constructor needed. −1 is a deliberate "not measured" marker.

## 9. `enum class` with an underlying type

- `enum class EGCBConfig : uint8 { None, E0 };` is a scoped, strongly typed enumeration.
  - The names must be written `EGCBConfig::E0`, so they can't clash with other names.
  - It doesn't convert to `int` silently.
  - `: uint8` stores it in one byte.
- The ordinary C++ `enum` would leak `E0` into the surrounding scope and convert to an integer without a cast.

## 10. `static` functions in a `.cpp`: internal linkage

- `static void SetConsoleVariable(...)` inside `namespace GCBBench` in a `.cpp` file has **internal linkage**: it is visible in that file only.
- Two files can then define helpers with the same name without the linker complaining. This matters in unity builds, where UBT glues several `.cpp` files together.
- `const TCHAR* const ParkedTimerSeconds = …` is a constant pointer to constant text. A namespace-scope `const` also has internal linkage.

## 11. `double`, `int32`, `int64`, and printf-style format specifiers

- `double` holds about 15–16 significant digits. Timings are computed from `FPlatformTime::Seconds()` (seconds since some point in the past) as differences of two large numbers, so `float`'s 7 digits would lose sub-microsecond detail.
- `int64` for frame counters: at 60 fps an `int32` would last over a year, but a 64-bit counter can never wrap and costs nothing here.
- `FString::Printf` and `UE_LOG` use C printf formats:
  - `%d` for `int32`;
  - `%lld` for `int64` (`long long` on Windows);
  - `%.3f` for a `double` with three decimals;
  - `%s` with `*SomeFString` (the `*` gives a `TCHAR*`);
  - `0x%08X` for a `uint32` in hexadecimal.
  - A mismatched specifier is undefined behaviour, which is why each one matches its argument's type.

## 12. Member-function pointers and binding (`&ThisClass::HandlePostActorTick`)

- `&ThisClass::HandlePostActorTick` is a pointer to a member function. Calling it needs an object, which is why `AddUObject(this, &ThisClass::…)` takes both.
- The delegate stores the pair and calls `(this->*Function)(Args…)` when broadcast. Because it was bound with `AddUObject`, it also checks that the object still exists.
- `FDelegateHandle` is a small id for one binding, so exactly that binding can be removed later.

## 13. Lambdas are not used yet

S1 needs no lambdas. They arrive in S4 (the probe loop), where they are explained with the code.
