// P04 GCBudget - primary game module: the item workload, the baseline stores and the benchmark director only.
// The reusable system lives in the GCBudgetCore plugin; this module depends on it, never the reverse.

using UnrealBuildTool;

public class GCBudget : ModuleRules
{
	public GCBudget(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// GCBudgetCore is public: a public header (GCBStructItemStores.h) includes its GCBudgetHandle.h.
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GCBudgetCore" });
	}
}
