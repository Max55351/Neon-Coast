using UnrealBuildTool;
public class NeonCoastGameTarget : TargetRules { public NeonCoastGameTarget(TargetInfo T) : base(T) { Type = TargetType.Game; DefaultBuildSettings = BuildSettingsVersion.Latest; IncludeOrderVersion = EngineIncludeOrderVersion.Latest; ExtraModuleNames.Add("NeonCoastGame"); } }
