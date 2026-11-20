// GCBudgetCore - the reusable runtime module (GC telemetry, generational handles for struct-array storage).
// It must not depend on any game module.

using UnrealBuildTool;

public class GCBudgetCore : ModuleRules
{
	public GCBudgetCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
