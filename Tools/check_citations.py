"""P04 GCBudget - check every `File:line` engine citation in the docs against the UE 5.8.4 source.

Usage (read-only; it opens engine files, so run it when no benchmark is running):
    python -I check_citations.py <md files...>

For each citation `Path/File.ext:lines` (and each shorthand `:lines` after one, in the same section):
  1. the file must resolve (a fixed map of short names, else a lookup under Engine/Source) and the lines must exist;
  2. at least one distinctive word from the same table row or line (an identifier, a CamelCase or code word of six or
     more letters, not a path component) must appear in the cited lines +-3.
A citation that fails (2) is printed for a manual check: the heuristic triages, a person decides. Exit code 1 if any
citation doesn't resolve. Phase 6 ran it over every doc (09, Phase 6).
"""
from __future__ import annotations

import io
import os
import re
import sys

ENG = r"C:\Program Files\Epic Games\UE_5.8\Engine"
R = os.path.join(ENG, "Source", "Runtime")
SHORT = {
 "GarbageCollection.cpp": R + r"\CoreUObject\Private\UObject\GarbageCollection.cpp",
 "GarbageCollection.h": R + r"\CoreUObject\Public\UObject\GarbageCollection.h",
 "UObjectClusters.cpp": R + r"\CoreUObject\Private\UObject\UObjectClusters.cpp",
 "UObjectArray.cpp": R + r"\CoreUObject\Private\UObject\UObjectArray.cpp",
 "UObjectArray.h": R + r"\CoreUObject\Public\UObject\UObjectArray.h",
 "UObjectBase.cpp": R + r"\CoreUObject\Private\UObject\UObjectBase.cpp",
 "UObjectBase.h": R + r"\CoreUObject\Public\UObject\UObjectBase.h",
 "UObjectBaseUtility.h": R + r"\CoreUObject\Public\UObject\UObjectBaseUtility.h",
 "UObjectGlobals.h": R + r"\CoreUObject\Public\UObject\UObjectGlobals.h",
 "UObjectStats.h": R + r"\CoreUObject\Public\UObject\UObjectStats.h",
 "ObjectPtr.h": R + r"\CoreUObject\Public\UObject\ObjectPtr.h",
 "ObjectHandle.h": R + r"\CoreUObject\Public\UObject\ObjectHandle.h",
 "ObjectHandleDefines.h": R + r"\CoreUObject\Public\UObject\ObjectHandleDefines.h",
 "ObjectHandleTracking.h": R + r"\CoreUObject\Public\UObject\ObjectHandleTracking.h",
 "WeakObjectPtr.h": R + r"\CoreUObject\Public\UObject\WeakObjectPtr.h",
 "ReachabilityAnalysis.h": R + r"\CoreUObject\Public\UObject\ReachabilityAnalysis.h",
 "ReachabilityAnalysisState.cpp": R + r"\CoreUObject\Private\UObject\ReachabilityAnalysisState.cpp",
 "GarbageCollectionInternalFlags.h": R + r"\CoreUObject\Private\UObject\GarbageCollectionInternalFlags.h",
 "GarbageCollectionVerification.cpp": R + r"\CoreUObject\Private\UObject\GarbageCollectionVerification.cpp",
 "GarbageCollectionVerification.h": R + r"\CoreUObject\Private\UObject\GarbageCollectionVerification.h",
 "Obj.cpp": R + r"\CoreUObject\Private\UObject\Obj.cpp",
 "LevelTick.cpp": R + r"\Engine\Private\LevelTick.cpp",
 "UnrealEngine.cpp": R + r"\Engine\Private\UnrealEngine.cpp",
 "Engine.h": R + r"\Engine\Classes\Engine\Engine.h",
 "Level.cpp": R + r"\Engine\Private\Level.cpp",
 "Actor.cpp": R + r"\Engine\Private\Actor.cpp",
 "EngineStats.h": R + r"\Engine\Public\EngineStats.h",
 "CsvProfiler.cpp": R + r"\Core\Private\ProfilingDebugging\CsvProfiler.cpp",
 "TraceAuxiliary.cpp": R + r"\Core\Private\ProfilingDebugging\TraceAuxiliary.cpp",
 "CountersTrace.h": R + r"\Core\Public\ProfilingDebugging\CountersTrace.h",
 "WindowsPlatformMisc.cpp": R + r"\Core\Private\Windows\WindowsPlatformMisc.cpp",
 "WindowsPlatformMemory.cpp": R + r"\Core\Private\Windows\WindowsPlatformMemory.cpp",
 "LowLevelMemTracker.h": R + r"\Core\Public\HAL\LowLevelMemTracker.h",
 "LowLevelMemTrackerDefines.h": R + r"\Core\Public\HAL\LowLevelMemTrackerDefines.h",
 "LLM.cpp": R + r"\Core\Private\HAL\LLM\LLM.cpp",
 "LogSuppressionInterface.cpp": R + r"\Core\Private\Logging\LogSuppressionInterface.cpp",
 "ModuleManager.h": R + r"\Core\Public\Modules\ModuleManager.h",
 "TargetRules.cs": ENG + r"\Source\Programs\UnrealBuildTool\Configuration\Rules\TargetRules.cs",
 "ModuleRules.cs": ENG + r"\Source\Programs\UnrealBuildTool\Configuration\Rules\ModuleRules.cs",
 "BaseEngine.ini": ENG + r"\Config\BaseEngine.ini",
 "InputSettings.cpp": R + r"\Engine\Private\UserInterface\InputSettings.cpp",
 "EnhancedInputSubsystems.cpp": ENG + r"\Plugins\EnhancedInput\Source\EnhancedInput\Private\EnhancedInputSubsystems.cpp",
 "GameEngine.cpp": R + r"\Engine\Private\GameEngine.cpp",
 "GameplayEffect.h": ENG + r"\Plugins\Runtime\GameplayAbilities\Source\GameplayAbilities\Public\GameplayEffect.h",
 "EntityHandle.h": R + r"\Mass\MassCore\Public\Mass\EntityHandle.h",
 "AndroidFileServer.uplugin": ENG + r"\Plugins\Runtime\AndroidFileServer\AndroidFileServer.uplugin",
 "SignificanceManager.uplugin": ENG + r"\Plugins\Runtime\SignificanceManager\SignificanceManager.uplugin",
}

SHORT.update({
    "CsvProfiler.h": R + r"\Core\Public\ProfilingDebugging\CsvProfiler.h",
    "LaunchEngineLoop.cpp": R + r"\Launch\Private\LaunchEngineLoop.cpp",
    "LaunchWindows.cpp": R + r"\Launch\Private\Windows\LaunchWindows.cpp",
    "IConsoleManager.h": R + r"\Core\Public\HAL\IConsoleManager.h",
    "BitArray.h": R + r"\Core\Public\Containers\BitArray.h",
    "AutomationTest.h": R + r"\Core\Public\Misc\AutomationTest.h",
    "AssertionMacros.h": R + r"\Core\Public\Misc\AssertionMacros.h",
    "WorldSubsystem.h": R + r"\Engine\Public\Subsystems\WorldSubsystem.h",
    "TraceAuxiliary.h": R + r"\Core\Public\ProfilingDebugging\TraceAuxiliary.h",
    "FastReferenceCollector.h": R + r"\CoreUObject\Public\UObject\FastReferenceCollector.h",
    "AssertionMacros.cpp": R + r"\Core\Private\Misc\AssertionMacros.cpp",
})

CITE = re.compile(r"`([A-Za-z0-9_./\-]+\.(?:cpp|h|ini|cs|uplugin|uproject|inl))(:[0-9][0-9, –/-]*)`")
TICK = re.compile(r"`([^`]+)`")
SHORTREF = re.compile(r"(?:`|\(|, )(:[0-9][0-9]*(?:[–-][0-9]+)?(?:, [0-9]+(?:[–-][0-9]+)?)*)(?=[`),;])")
CAMEL = re.compile(r"\b[A-Z][a-z0-9]+(?:[A-Z][A-Za-z0-9]*)+\b|\b[A-Z]{2,}[A-Za-z0-9_]{3,}\b|\b[a-z]+[A-Z][A-Za-z0-9]+\b")
STOP = {"GCBudget", "GCBudgetCore", "Runtime", "Private", "Public", "Source", "Engine", "UnrealEngine", "Phase",
        "Development", "Shipping", "Insights", "TObjectPtr", "UObject", "UObjects"}


def resolve(f: str) -> str | None:
    base = os.path.basename(f.replace("\\", "/"))
    loc = SHORT.get(base)
    if loc:
        return loc
    for root in (R, os.path.join(ENG, "Source"), ENG, os.path.join(ENG, "Plugins")):
        p = os.path.join(root, f.replace("/", "\\"))
        if os.path.isfile(p):
            return p
    return None


def ranges(spec: str) -> list[tuple[int, int]]:
    out = []
    for part in re.split(r",\s*", spec.strip(":").strip()):
        nums = [int(x) for x in re.findall(r"\d+", part)]
        if nums:
            out.append((nums[0], nums[-1]))
    return out


COMMON = {"because", "should", "before", "after", "during", "every", "other", "without", "within", "between", "which",
          "there", "their", "these", "those", "return", "static", "const", "default", "engine", "source", "public",
          "private", "always", "never", "where", "while", "first", "second", "third", "change", "changes", "number",
          "values", "value", "called", "calls", "frame", "frames", "object", "objects", "line", "lines", "function"}


def tokens(context: str, cite_text: str) -> set[str]:
    paths = set()
    for c in CITE.finditer(context):
        paths.update(re.split(r"[/\.]", c.group(1)))
    context = CITE.sub(" ", context)
    found = {w for w in re.findall(r"[A-Za-z_][A-Za-z0-9_]{5,}", context)
             if w.lower() not in COMMON and w not in paths and w not in STOP}
    for t in TICK.findall(context):
        if t == cite_text.strip("`") or CITE.fullmatch(f"`{t}`"):
            continue
        for piece in re.split(r"[\s:.(),<>\[\]=*&/\\\"']+", t):
            if len(piece) >= 4 and piece not in STOP and not piece.isdigit():
                found.add(piece)
    for w in CAMEL.findall(context):
        if w not in STOP and w not in paths:
            found.add(w)
    return found


cache: dict[str, list[str]] = {}
checked = flagged = missing = 0
for md in sys.argv[1:]:
    last_file = None
    for line_no, line in enumerate(io.open(md, encoding="utf-8").read().split("\n"), 1):
        if line.startswith("#"):
            last_file = None   # a shorthand citation refers to the last file named in the same section
        items = sorted([(m.start(), m.group(1), m.group(2), m.group(0)) for m in CITE.finditer(line)] +
                       [(m.start(), None, m.group(1), m.group(0)) for m in SHORTREF.finditer(line)])
        for start, f, spec, whole in items:
            if f is None:
                if last_file is None:
                    continue
                f = last_file
            else:
                last_file = f
            loc = resolve(f)
            if not loc:
                missing += 1
                print(f"{os.path.basename(md)}:{line_no}: {f}{spec}  FILE NOT FOUND")
                continue
            if loc not in cache:
                cache[loc] = io.open(loc, encoding="utf-8", errors="replace").read().split("\n")
            src_lines = cache[loc]
            if any(b > len(src_lines) for _, b in ranges(spec)):
                if whole.startswith("`") and CITE.fullmatch(whole):
                    missing += 1
                    print(f"{os.path.basename(md)}:{line_no}: {f}{spec}  BEYOND {len(src_lines)} LINES")
                else:   # a shorthand citation whose file the section names elsewhere: a person checks it
                    flagged += 1
                    print(f"{os.path.basename(md)}:{line_no}: {spec} (shorthand; the section's last full citation is {f})  CHECK BY HAND")
                continue
            context = line   # the whole table row: the claim is often in another cell than the citation
            want = tokens(context, whole)
            window = ""
            for a, b in ranges(spec):
                window += "\n".join(src_lines[max(0, a - 4):min(len(src_lines), b + 3)]) + "\n"
            checked += 1
            low = window.lower()
            hit = [t for t in want if t.lower() in low]
            if not hit:
                flagged += 1
                first = src_lines[ranges(spec)[0][0] - 1].strip()[:110] if ranges(spec) and ranges(spec)[0][0] <= len(src_lines) else "?"
                print(f"{os.path.basename(md)}:{line_no}: {f}{spec}  words={sorted(want)[:6]}  line1='{first}'")
print(f"{checked} citations content-checked, {flagged} for manual review, {missing} unresolved")
sys.exit(1 if missing else 0)
