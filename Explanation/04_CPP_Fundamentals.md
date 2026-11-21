# 04 · C++ Fundamentals: GCBudget

**Rule:** this file explains the general language features the project's code relies on, in the order they first appear. Unreal-specific APIs are in `06`. **Current to:** Phase 5 step S5. Sections 1–5 cover the foundation (mostly C# build rules, Windows batch and Python); sections 6 onward add the C++ each Phase 5 step introduces.

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

S1 needs no lambdas. They arrive in S4 (the probe loop): §23.

## C++ added in step S3 (handles and their tests)

## 14. Constructors: `= default` and a member initialiser list

- `FGCBudgetHandle() = default;` asks the compiler for the ordinary default constructor. Together with the default member initialisers (`Index = INDEX_NONE`, `Generation = 0`, §8), a default handle is always the invalid one.
- `FGCBudgetHandle(int32 InIndex, int32 InGeneration) : Index(InIndex), Generation(InGeneration) {}` initialises the members directly in the **member initialiser list**, instead of assigning them inside the body.
- Declaring any constructor stops the compiler from generating the default one, which is why `= default` is written out.

## 15. Operator overloading and a hidden `friend`

- `operator==` and `operator!=` make `A == B` compare both fields. `!=` is written in terms of `==`, so the two can never disagree.
- `friend uint32 GetTypeHash(const FGCBudgetHandle&)` is defined **inside** the struct. A friend defined there is a free function, not a member, and is found only through the argument's type ("argument-dependent lookup"). That is how `TSet`/`TMap` find a key's hash.
- Inside it, `::GetTypeHash(Handle.Index)` with the leading `::` calls the global overload for `int32`, not this function again.

## 16. Sentinel values and integer wrap-around

- `INDEX_NONE` (−1) means "no index". Generation 0 means "never issued": slots start at generation 1, so a default handle can never match a slot.
- Signed integer overflow is **undefined behaviour** in C++: `MAX_int32 + 1` may do anything. `Release` therefore tests for `MAX_int32` *before* incrementing and wraps to 1 by hand, never to 0.

## 17. Inline functions in a header, and `const` member functions

- `IsValid`, `Resolve`, `IsSlotAlive`, `GetHandle`, `Num` are defined in the class body, which makes them implicitly `inline`. Every caller sees the body and the compiler can fold them into the caller's loop: one bounds check, one bit, one compare per lookup.
- `Allocate`, `Release`, `Reserve`, `Reset` change state and are not on the per-lookup path, so they live in the `.cpp`.
- A `const` after the parameter list (`bool IsValid(FGCBudgetHandle) const`) promises the function doesn't change the object, so it can be called through a `const` reference.

## 18. A stack as a free list, and a bit array

- `FreeSlots` is used as a **stack**: `Push` on release, `Pop` on allocate. The most recently freed slot comes back first (LIFO). Its memory is the most likely to still be in the CPU cache, and in this workload the array stays dense.
- `TBitArray<>` stores one flag per slot in one bit. A `TArray<bool>` would use a byte per flag: eight times the memory for the same information.

## 19. Compile-time constants and conditional compilation

- `constexpr EAutomationTestFlags Flags = …` is computed by the compiler. The `|` on this enum is a `constexpr` operator (`ENUM_CLASS_FLAGS`, `Runtime/Core/Public/Misc/EnumClassFlags.h:8, 12`), so the whole expression is a constant.
- `#if WITH_DEV_AUTOMATION_TESTS … #endif` removes the tests from builds without development tests. The build tool defines the macro, as 1 in every configuration except Test and Shipping (`Programs/UnrealBuildTool/Configuration/UEBuildTarget.cs:6315, 6331`).

## 20. Testing against a reference model

- The random test drives the allocator with one million random operations from a **fixed seed** (`FRandomStream(1234)`), so every run performs the same sequence and a failure can be reproduced.
- After each operation it compares the allocator with a deliberately simple **model**: an array of live handles and a sample of dead ones. The model is too slow for real use but obviously correct, so any disagreement is a bug in the allocator.

## C++ added in step S4 (the workload and the UObject stores)

## 21. Abstract classes and pure virtual functions

- `UGCBItemStore` declares `Update`, `RunProbe`, `Checksum`, `PopulateItems` and `CaptureProbeRef` as functions every store **must** provide. In plain C++ that is `virtual int32 Update(int32) = 0;`, which makes the class abstract: it can't be instantiated.
- Unreal must be able to construct every `UCLASS`, at least once, for its default object. So it uses `PURE_VIRTUAL(Name, return 0;)` instead: a body that stops the program with a fatal error if it is ever called (`06` §20), and `UCLASS(Abstract)` so the editor never offers the class itself.

## 22. Function templates and member templates

- `template <typename ResolveFunction> FGCBProbeResult ProbeLoop(int32 Lookups, ResolveFunction&& Resolve)` is a **template**: the compiler generates one version of the function for each type it is called with.
- Each store passes its own lambda (§23). Every lambda has its own type, so each store gets its own copy of the loop with that store's lookup **inlined** into it: no function-pointer or virtual call per lookup.
- `ResolveFunction&&` on a template parameter is a *forwarding reference*: it accepts a temporary lambda without copying it.

## 23. Lambdas

- `[this](int32 PoolIndex, int32& OutMagnitude) -> int32 { … }` is an unnamed function object written where it is used.
  - `[this]` *captures* the store, so the body can use its members (`ProbeRefs`, `Slots`).
  - `-> int32` names the return type.
- In S5's struct stores, `[this, &Items]` also captures the item array by reference. That is safe because the lambda is called only inside `ProbeLoop` and never outlives the array.

## 24. Fixed-point arithmetic and 64-bit intermediates

- Q16.16 stores a number times 65,536 in an `int32`: 64.0 is `64 << 16`. Addition and subtraction are ordinary integer operations.
- A product of two Q16.16 values has 32 fractional bits and can overflow 32 bits. `AgeItem` therefore multiplies in `int64` and shifts right by 16 to get back to Q16.16.
- *Why not `float`?* The same formula, compiled into different loops (one per store), can be vectorised or contracted into fused multiply-adds differently, and round differently. Integer arithmetic is exact, so every store computes the same bits and the checksum can prove the work was identical.

## 25. Unsigned arithmetic for hashing, and `static_cast`

- Unsigned integers wrap around by definition (modulo 2³²); signed ones must not overflow (§16). The workload's hash (`Hash32`, MurmurHash3's finaliser) therefore works on `uint32` and relies on that wrap.
- `static_cast<uint32>(Lane)` is an explicit, compile-time-checked conversion. It makes every signed/unsigned change visible in the code, instead of happening implicitly.

## 26. Integer division and remainder for paging

- `Lane / UGCBItemPage::Capacity` is the page and `Lane % UGCBItemPage::Capacity` the slot in it. Integer division rounds toward zero, so with non-negative lanes the two together address every lane exactly once.
- `(NumLanes + Capacity - 1) / Capacity` is the usual way to round a division **up**: the number of pages needed for `NumLanes` items.

## 27. `static` locals and why the review removed one

- A `static` variable inside a function is initialised once, the first time the function runs, and then lives until the program ends (thread-safe since C++11).
- `PrepareForGC` keeps one for the console-variable lookup: that pointer is the same for the whole process.
- The first version also kept a "warned already" flag that way. Because it lives as long as the process, a second run in the same editor session would have stayed silent, so the flag became a member of the store.

## C++ added in step S5 (the struct store with references)

## 28. Overloading on the element type, and references to array elements

- `GCBStructItem::GetDef` has one overload per element type. The shared templates call `GCBStructItem::GetDef(Item, Defs)`, and the compiler picks the overload from the type of `Item` when it instantiates the template for that store.
- `TItem& Item = Items[Slot];` is a **reference** to the element in the array, not a copy: writes through it change the array. It is only safe while the array doesn't reallocate. Nothing in the loop adds elements (an expired slot is reused in place), so it never does.

## 29. Two-phase lookup: why the overloads come first

- A template is checked twice: once where it is defined, and again for each type it is instantiated with.
- A **qualified** call such as `GCBStructItem::GetDef(Item, Defs)` is looked up where the template is **defined**. An overload declared after the template, for a new element type, would not be found.
- That is why every element type's `GetDef`/`SetDef` pair sits above `UGCBStructItemStoreBase` in the header.
