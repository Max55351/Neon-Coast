using UnrealBuildTool;
public class NeonCoastGameEditorTarget : TargetRules { public NeonCoastGameEditorTarget(TargetInfo T) : base(T) { Type = TargetType.Editor; DefaultBuildSettings = BuildSettingsVersion.Latest; IncludeOrderVersion = EngineIncludeOrderVersion.Latest; ExtraModuleNames.Add("NeonCoastGame"); } }
