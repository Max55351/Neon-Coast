using UnrealBuildTool;

public class NeonCoastRuntime : ModuleRules
{
	public NeonCoastRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Warning;
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
			"ChaosVehicles", "ChaosVehiclesCore", "PhysicsCore",
			"Json", "JsonUtilities", "NetCore", "AIModule", "GameplayTasks", "RenderCore"
		});
	}
}
