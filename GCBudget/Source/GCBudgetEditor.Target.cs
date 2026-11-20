// P04 GCBudget - editor target.

using UnrealBuildTool;

public class GCBudgetEditorTarget : TargetRules
{
	public GCBudgetEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("GCBudget");
	}
}
