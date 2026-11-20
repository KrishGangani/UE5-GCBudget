// P04 GCBudget - game target (standalone/packaged builds).

using UnrealBuildTool;

public class GCBudgetTarget : TargetRules
{
	public GCBudgetTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("GCBudget");
	}
}
