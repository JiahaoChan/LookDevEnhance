// Copyright TA.Jiahao.Chan, Individual. All Rights Reserved.

using UnrealBuildTool;

public class LookDevEnhance : ModuleRules
{
	public LookDevEnhance(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"ContentBrowser",
				"DeveloperSettings",
				"ToolMenus",
				"InputCore",
				"CommonMenuExtensions",
				"PropertyEditor",
				"SubobjectEditor",
				"SubobjectDataInterface",
				"ImageCore",
				"ImageWrapper",
				"Projects",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
		);
	}
}
