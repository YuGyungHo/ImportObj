using UnrealBuildTool;
using System.IO;
 
public class ImportObj : ModuleRules
{
    public ImportObj(TargetInfo Target)
    {
		PublicIncludePaths.AddRange(
				new string[] {
					"Developer/AssetTools/Public",
					"Editor/UnrealEd/Public",
					"Editor/UnrealEd/Classes",					  
					// ... add public include paths required here ...
				}
				);

			PrivateIncludePaths.AddRange(
				new string[] {
					"Developer/m2uPlugin/Private",					
					"Editor/UnrealEd/Public",
					"Editor/UnrealEd/Classes",
						"Editor/UnrealEd/Private",
						"Editor/UnrealEd/Private/Fbx",
					"../../../ThirdParty/FBX/2016.1.1/include",
					// ... add other private include paths required here ...
				}
				);

			//AddThirdPartyPrivateStaticDependencies(Target, new string[] { "FBX" } );
			//AddThirdPartyPrivateStaticDependencies(Target, "FBX" );

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
				"Core",
				"CoreUObject",
                "InputCore",
                "EditorStyle",
				"Engine",
				"UnrealEd",
				"Sockets",
				"Networking",
				"Slate",
				"SlateCore",
					// ... add other public dependencies that you statically link with here ...
				}
				);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					// ... add private dependencies that you statically link with here ...
				}
				);

			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
					"AssetTools",
					// ... add any modules that your module loads dynamically here ...
				}
				);
    }
}