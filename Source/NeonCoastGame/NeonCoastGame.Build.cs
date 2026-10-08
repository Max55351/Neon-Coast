using UnrealBuildTool;
public class NeonCoastGame : ModuleRules { public NeonCoastGame(ReadOnlyTargetRules T) : base(T) { PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs; PublicDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine"}); } }
