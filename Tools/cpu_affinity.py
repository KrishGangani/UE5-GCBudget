"""P04 GCBudget - pin timed runs to the performance cores of a hybrid CPU.

Copied from the portfolio's reference project (UE5-UpdateBudget, Tools/run_bench.py) so this repository stays
standalone. How the benchmark runner (Phase 5) uses it:
  1. Before recording the environment or launching anything, it pins ITSELF: pin_self(performance_core_mask()).
     Every process it starts afterwards inherits the mask from its first instruction, so each game runs pinned for
     its whole life, engine start-up included.
  2. It records applied_mask() (read back from Windows, not the mask requested) in the environment record.
  3. It launches the real game exe directly with subprocess.Popen
     (Build/Windows/GCBudget/Binaries/Win64/GCBudget.exe, not the bootstrap), waits with a timeout, and kills the
     process tree in a `finally` block.
Never launch through `cmd /c start /affinity`: it always returns exit code 0, so failed runs would go unnoticed.

Usage:  python -I cpu_affinity.py      prints the performance-core mask of this machine (or "none")
"""
from __future__ import annotations

import ctypes
import struct
import sys

KERNEL32 = None
if sys.platform == "win32":
    KERNEL32 = ctypes.WinDLL("kernel32", use_last_error=True)
    KERNEL32.GetCurrentProcess.restype = ctypes.c_void_p
    KERNEL32.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    KERNEL32.GetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]


def performance_core_mask() -> int | None:
    """Affinity mask of the performance cores on a hybrid CPU; None on a non-hybrid CPU or off Windows.

    Why: on this i7-14650HX (8 P-cores + 8 E-cores) the reference project's baseline batch showed runs that sat at
    two stable levels about 1.6x apart: Windows had placed the game thread on an E-core. Pinning to the P-cores gives
    every run what a foreground game normally gets.
    Source of truth: GetLogicalProcessorInformationEx(RelationProcessorCore) reports an EfficiencyClass per physical
    core; the P-cores have the higher class. Only processor group 0 exists on a machine with < 64 logical CPUs.
    """
    if KERNEL32 is None:
        return None
    length = ctypes.c_ulong(0)
    KERNEL32.GetLogicalProcessorInformationEx(0, None, ctypes.byref(length))
    buffer = ctypes.create_string_buffer(length.value)
    if not length.value or not KERNEL32.GetLogicalProcessorInformationEx(0, buffer, ctypes.byref(length)):
        return None
    raw, offset, masks = buffer.raw, 0, {}          # efficiency class -> combined mask of its cores
    while offset < length.value:
        relationship, size = struct.unpack_from("<II", raw, offset)
        if relationship == 0 and size >= 48:                     # RelationProcessorCore
            efficiency_class = raw[offset + 9]                   # PROCESSOR_RELATIONSHIP.EfficiencyClass
            mask, group = struct.unpack_from("<QH", raw, offset + 32)   # GroupMask[0]: KAFFINITY Mask, WORD Group
            if group == 0:
                masks[efficiency_class] = masks.get(efficiency_class, 0) | mask
        offset += size
    return masks[max(masks)] if len(masks) > 1 else None


def pin_self(mask: int) -> bool:
    """Restricts this process to the CPUs in mask. Every process it starts afterwards inherits the mask from its
    first instruction, so each game runs pinned for its whole life, engine start-up included."""
    return bool(KERNEL32.SetProcessAffinityMask(KERNEL32.GetCurrentProcess(), mask))


def applied_mask() -> str:
    """The affinity this process actually has (read back from Windows, not the one requested)."""
    if KERNEL32 is None:
        return "not Windows"
    process_mask, system_mask = ctypes.c_size_t(0), ctypes.c_size_t(0)
    KERNEL32.GetProcessAffinityMask(KERNEL32.GetCurrentProcess(), ctypes.byref(process_mask), ctypes.byref(system_mask))
    if process_mask.value == system_mask.value:
        return f"0x{process_mask.value:X} (all logical processors)"
    return f"0x{process_mask.value:X} (performance cores only; hybrid CPU)"


if __name__ == "__main__":
    found = performance_core_mask()
    print(f"0x{found:X}" if found else "none")
