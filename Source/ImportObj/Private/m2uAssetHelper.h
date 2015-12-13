#pragma once

#include "AssetToolsModule.h"
#include "MessageLog.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "AssetRegistryModule.h"
#include "NotificationManager.h"
#include "SNotificationList.h"
#include "UnrealEd.h"



// This file contains functios that do asset-importing & exporting stuff
// they are in big parts identical to those found in FAssetTools
// the problem, FAssetTools does not what we want, and the subsidiary
// functions like "CheckForDeletedPackage" are all module-private.
// I could try more to get the private AssetTools.h included here
// and probably I would have to specify the in build.cs hard-linking against the
// AssetTools module or stuff, but I currently don't really want to invest more
// time in coping with how the UnrealBuild tool handles importing stuff
// I can nearly point it to AssetTools.h explicitly and it won't be included...
// so i'm doing my own stuff here for now... with black jack and hookers!

namespace m2uAssetHelper
{

#define LOCTEXT_NAMESPACE "AssetTools"

/**
   Function pointer signature for ImportAsset's user-input-callback
   TODO: this will be a Deleagate once we go object-oriented with the command
   executing functions and everything is nicely wrapped up in classes.

   The FString you will receive will begin with one of:
   (NOTE: handling of return value for OtherFile is not implemented yet)

   "UsedByMap": There already exists a map file with that name
   The expected return value is one of:
   - "Skip" if to skip the Asset.
   - "OtherFile" followed by a new file path to import instead

   "Overwrite": There already exists an object with same name of same class
   or
   "Replace": There already exists an object with same name but different class
   The expected return value is one of:
   - "YesAll": overwrite and replace all occasions, don't ask again
   - "Yes": overwrite this one occasion
   - "Skip": don't overwrite, skip this asset
   - "SkipAll": don't overwrite or replace any occasion, don't ask again
   - "OtherFile": followed by a new file path to import instead

   If the original object could not be deleted, or the asset can not be imported
   we won't create popup dialogs but silently write to the log instead.
   ... don't hassle with the user ;)
*/
	//DECLARE_DELEGATE_RetVal_OneParam(FString, FRequestUserInputFunc, const FString&)
	typedef FString (*RequestUserInputFunc)(const FString&);


/**
   get all files in a directory hierarchy
   associate each source file path with a destination path
   copied from FAssetTools::ExpandDirectories
*/
	void ExpandDirectories(const TArray<FString>& Files, const FString& DestinationPath, TArray<TPair<FString, FString>>& FilesAndDestinations)
	{
		// Iterate through all files in the list, if any folders are found, recurse and expand them.
		for ( int32 FileIdx = 0; FileIdx < Files.Num(); ++FileIdx )
		{
			const FString& Filename = Files[FileIdx];

			// If the file being imported is a directory, just include all sub-files and skip the directory.
			if ( IFileManager::Get().DirectoryExists(*Filename) )
			{
				FString FolderName = FPaths::GetCleanFilename(Filename);

				// Get all files & folders in the folder.
				FString SearchPath = Filename / FString(TEXT("*"));
				TArray<FString> SubFiles;
				IFileManager::Get().FindFiles(SubFiles, *SearchPath, true, true);

				// FindFiles just returns file and directory names, so we need to tack on the root path to get the full path.
				TArray<FString> FullPathItems;
				for ( FString& SubFile : SubFiles )
				{
					FullPathItems.Add(Filename / SubFile);
				}

				// Expand any sub directories found.
				FString NewSubDestination = DestinationPath / FolderName;
				ExpandDirectories(FullPathItems, NewSubDestination, FilesAndDestinations);
			}
			else
			{
				// Add any files and their destination path.
				FilesAndDestinations.Add(TPairInitializer<FString, FString>(Filename, DestinationPath));
			}
		}
	}



/**
 * Import the files as assets into UE
 *
 * @param Files Array of file-paths (or directories) to import
 * @param RootDestinationPath The root for the package file structure of where
 *        to import the Assets to.
 * @param bUseEditorImportFunc Use the default In-Editor way of importing assets,
 *        this may create popup-dialogs for overwrite warnings. This is not what
 *        we want when controlling the Editor from Maya or so.
 * @param bForceNoOverwrite Do not reimport the Asset if already exists. Do not
 *        use the InputGetter to decide otherwise or so.
 * @param InputGetter a function that gets or simulates user-interaction when
 *        problems occur. See: RequestUserInputFunc()
 *
 * Why this function and not just use the Editor function? The code, so what the
 * functions do, is generally the same. But this function allows us to not have
 * editor popups asking the user if he wants to overwrite or replace assets.
 * This is espcially true for FBX files, which create their own popup dialog
 * although the FBX importer automatically can decide for StaticMesh or SkelMesh.
 * Therefore, whenever we import an FBX file, we will use our m2uFbxFactory explicitly
 */
	TArray<UObject*> ImportAssets(const TArray<FString>& Files, const FString& RootDestinationPath )
	{
		// get the FAssetTools instance from the AssetToolsModule
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		UE_LOG(LogTemp, Warning, TEXT("Importing Assets the non-standard way"));

		// Following this point, most parts are copied from FAssetTools::ImportAssets
		// the main differnce is that instead of creating popup dialogs, a call to the
		// InputGetter function is made, if available.

		TArray<UObject*> ReturnObjects;
		TMap< FString, TArray<UFactory*> > ExtensionToFactoriesMap;
		GWarn->BeginSlowTask(LOCTEXT("ImportSlowTask", "Importing"), true);

		UE_LOG(LogTemp, Warning, TEXT("UFactory::ResetState()"));
		// Reset the 'Do you want to overwrite the existing object?' Yes to All /
		// No to All prompt, to make sure the user gets a chance to select something
		UFactory::ResetState();

		UE_LOG(LogTemp, Warning, TEXT("ExpandDirectories"));
		TArray<TPair<FString, FString>> FilesAndDestinations;
		//((FAssetTools*)&AssetTools) -> ExpandDirectories(Files, RootDestinationPath, FilesAndDestinations);
		// NOTE: FAssetTools::ExpandDirectories will add subfolders from the
		// import paths as subfolders for the desination paths, importing a whole
		// file structure will retain the same paths this way (as long as the names
		// are valid I suppose).
		ExpandDirectories(Files, RootDestinationPath, FilesAndDestinations);

		UE_LOG(LogTemp, Warning, TEXT("for( TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt )"));
// First instantiate one factory for each file extension encountered that supports the extension
		for( TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt )
		{
			UE_LOG(LogTemp, Warning, TEXT("if( (*ClassIt)->IsChildOf(UFactory::StaticClass()) && !((*ClassIt)->HasAnyClassFlags(CLASS_Abstract)) )"));
			if( (*ClassIt)->IsChildOf(UFactory::StaticClass()) && !((*ClassIt)->HasAnyClassFlags(CLASS_Abstract)) )
			{
				UE_LOG(LogTemp, Warning, TEXT("UFactory* Factory = Cast<UFactory>( (*ClassIt)->GetDefaultObject() );"));
				UFactory* Factory = Cast<UFactory>( (*ClassIt)->GetDefaultObject() );
				UE_LOG(LogTemp, Warning, TEXT("if( Factory->bEditorImport )"));
				if( Factory->bEditorImport )
				{
					TArray<FString> FactoryExtensions;
					UE_LOG(LogTemp, Warning, TEXT("Factory->GetSupportedFileExtensions(FactoryExtensions)"));
					Factory->GetSupportedFileExtensions(FactoryExtensions);

					UE_LOG(LogTemp, Warning, TEXT("for ( auto& FileDest : FilesAndDestinations )"));
					for ( auto& FileDest : FilesAndDestinations )
					{
						UE_LOG(LogTemp, Warning, TEXT("const FString FileExtension = FPaths::GetExtension(FileDest.Key);"));
						const FString FileExtension = FPaths::GetExtension(FileDest.Key);

						UE_LOG(LogTemp, Warning, TEXT("FactoryExtensions.Contains(FileExtension)"));
						// Case insensitive string compare with supported formats of this factory
						if ( FactoryExtensions.Contains(FileExtension) )
						{
							UE_LOG(LogTemp, Warning, TEXT("TArray<UFactory*>& ExistingFactories = ExtensionToFactoriesMap.FindOrAdd(FileExtension);"));
							TArray<UFactory*>& ExistingFactories = ExtensionToFactoriesMap.FindOrAdd(FileExtension);

							UE_LOG(LogTemp, Warning, TEXT("for ( auto FoundFactoryIt = ExistingFactories.CreateConstIterator(); FoundFactoryIt; ++FoundFactoryIt )"));
							// Do not remap extensions, just reuse the existing UFactory.
							// There may be multiple UFactories, so we will keep track of all of them
							bool bFactoryAlreadyInMap = false;
							for ( auto FoundFactoryIt = ExistingFactories.CreateConstIterator(); FoundFactoryIt; ++FoundFactoryIt )
							{
								if ( (*FoundFactoryIt)->GetClass() == Factory->GetClass() )
								{
									bFactoryAlreadyInMap = true;
									break;
								}
							}

							UE_LOG(LogTemp, Warning, TEXT("if ( !bFactoryAlreadyInMap )"));
							if ( !bFactoryAlreadyInMap )
							{
								UE_LOG(LogTemp, Warning, TEXT("UFactory* NewFactory = NewObject<UFactory>( Factory->GetClass() );"));
								// We found a factory for this file, it can be imported!
								// Create a new factory of the same class and make sure it doesn't get GCed.
								// The object will be removed from the root set at the end of this function.
								UFactory* NewFactory = NewObject<UFactory>( Factory->GetClass() );
								UE_LOG(LogTemp, Warning, TEXT("NewFactory->ConfigureProperties()"));
								if ( NewFactory->ConfigureProperties() )
								{
									UE_LOG(LogTemp, Warning, TEXT("NewFactory->AddToRoot();"));
									NewFactory->AddToRoot();
									UE_LOG(LogTemp, Warning, TEXT("ExistingFactories.Add(NewFactory);"));
									ExistingFactories.Add(NewFactory);
								}
							}
						}
					}
				}
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("Created all factories"));

// Some flags to keep track of what the user decided when asked about overwriting or replacing
		// if there is no function to get user input, overwriting is default behaviour
		// for replacing, the opposite is the case, don't replace anything without
		// user input.
		bool bOverwriteAll = true;
		bool bReplaceAll = false;
		bool bDontOverwriteAny = false;
		bool bDontReplaceAny = false;

		// Now iterate over the input files and use the same factory object for each file with the same extension
		UE_LOG(LogTemp, Warning, TEXT("for ( int32 FileIdx = 0; FileIdx < FilesAndDestinations.Num(); ++FileIdx )"));
		for ( int32 FileIdx = 0; FileIdx < FilesAndDestinations.Num(); ++FileIdx )
		{
			const FString& Filename = FilesAndDestinations[FileIdx].Key;
			const FString& DestinationPath = FilesAndDestinations[FileIdx].Value;

			FString FileExtension = FPaths::GetExtension(Filename);

			const TArray<UFactory*>* FactoriesPtr = ExtensionToFactoriesMap.Find(FileExtension);
			UFactory* Factory = NULL;
			UE_LOG(LogTemp, Warning, TEXT("if ( FactoriesPtr )"));
			if ( FactoriesPtr )
			{
				const TArray<UFactory*>& Factories = *FactoriesPtr;

				// Handle the potential of multiple factories being found
				UE_LOG(LogTemp, Warning, TEXT("if( Factories.Num() > 0 )"));
				if( Factories.Num() > 0 )
				{
					// This makes sure SOME factory is set. Later a call to any factory
					// will probably be ok, because the basic Factory class actually
					// implements its own way of finding the appropriate factory for a
					//file extension
					Factory = Factories[0];
					UE_LOG(LogTemp, Warning, TEXT("for( auto FactoryIt = Factories.CreateConstIterator(); FactoryIt; ++FactoryIt )"));
					for( auto FactoryIt = Factories.CreateConstIterator(); FactoryIt; ++FactoryIt )
					{
						UFactory* TestFactory = *FactoryIt;
						UE_LOG(LogTemp, Warning, TEXT("if( TestFactory->FactoryCanImport( Filename ) )"));
						if( TestFactory->FactoryCanImport( Filename ) )
						{
							// haha, wow, we will never arrive here for FBX factories,
							// actually, FactoryCanImport will return false
							// for anything but .t3d, as no Factory overrides it...
							Factory = TestFactory;
							break;
						}
					}
				}
			}
			else
			{
				const FText Message = FText::Format( LOCTEXT("ImportFailed_UnknownExtension", "Failed to import '{0}'. Unknown extension '{1}'."), FText::FromString( Filename ), FText::FromString( FileExtension ) );
				//FNotificationInfo Info(Message);
				//Info.ExpireDuration = 3.0f;
				//Info.bUseLargeFont = false;
				//Info.bFireAndForget = true;
				//Info.bUseSuccessFailIcons = true;
				//FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
				UE_LOG(LogTemp, Warning, TEXT("else..."));

				UE_LOG(LogTemp, Warning, TEXT("%s"), *Message.ToString() );
			}

			UE_LOG(LogTemp, Warning, TEXT("if ( Factory != NULL )"));
			if ( Factory != NULL )
			{
				UClass* ImportAssetType = Factory->SupportedClass;
				bool bImportSucceeded = false;
				bool bImportWasCancelled = false;
				FDateTime ImportStartTime = FDateTime::UtcNow();

				FString Name = ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename(Filename));
				FString PackageName = DestinationPath + TEXT("/") + Name;

				// We can not create assets that share the name of a map file in the same location
				UE_LOG(LogTemp, Warning, TEXT("if ( FEditorFileUtils::IsMapPackageAsset(PackageName) )"));
				if ( FEditorFileUtils::IsMapPackageAsset(PackageName) )
				{
					const FText Message = FText::Format( LOCTEXT("AssetNameInUseByMap", "You can not create an asset named '{0}' because there is already a map file with this name in this folder."), FText::FromString( Name ) );
					UE_LOG(LogTemp, Warning, TEXT("%s"), *Message.ToString());
					//OnNewImportRecord(ImportAssetType, FileExtension, bImportSucceeded, bImportWasCancelled, ImportStartTime);
					continue;
				}

				UE_LOG(LogTemp, Warning, TEXT("UPackage* Pkg = CreatePackage(NULL, *PackageName);"));
				UPackage* Pkg = CreatePackage(NULL, *PackageName);
				UE_LOG(LogTemp, Warning, TEXT("if ( !ensure(Pkg) )"));
				if ( !ensure(Pkg) )
				{
					UE_LOG(LogTemp, Warning, TEXT("continue"));
					// Failed to create the package to hold this asset for some reason
					//OnNewImportRecord(ImportAssetType, FileExtension, bImportSucceeded, bImportWasCancelled, ImportStartTime);
					continue;
				}

				UE_LOG(LogTemp, Warning, TEXT("Pkg->FullyLoad();"));
				// Make sure the destination package is loaded
				Pkg->FullyLoad();

				// Check for an existing object
				UE_LOG(LogTemp, Warning, TEXT("UObject* ExistingObject = StaticFindObject( UObject::StaticClass(), Pkg, *Name );"));
				UObject* ExistingObject = StaticFindObject( UObject::StaticClass(), Pkg, *Name );
				UE_LOG(LogTemp, Warning, TEXT("if( ExistingObject != NULL )"));
				if( ExistingObject != NULL )
				{
					// If the object is supported by the factory we are using, ask if we want to overwrite the asset
					// Otherwise, prompt to replace the object
					UE_LOG(LogTemp, Warning, TEXT("if ( Factory->DoesSupportClass(ExistingObject->GetClass()) )"));
					if ( Factory->DoesSupportClass(ExistingObject->GetClass()) )
					{
						// The factory can overwrite this object, ask if that is okay, unless "Yes To All" or "No To All" was already selected

						bool bWantOverwrite = bOverwriteAll && !bDontOverwriteAny;
						if( ! bOverwriteAll && ! bDontOverwriteAny )
						{
							FString Result = TEXT("YesAll");
							if( Result.StartsWith(TEXT("YesAll")) )
							{
								bOverwriteAll = true;
								bWantOverwrite = true;
							}
							else if( Result.StartsWith(TEXT("Yes")) )
							{
								bWantOverwrite = true;
							}
							else if( Result.StartsWith(TEXT("SkipAll")) )
							{
								bDontOverwriteAny = true;
								bOverwriteAll = false; // prbly don't need to set this
								bWantOverwrite = false;
							}
							else if( Result.StartsWith(TEXT("Skip")) )
							{
								bWantOverwrite = false;
							}
							// TODO: parse result for OtherFile
						}

						if( !bWantOverwrite )
						{
							// User chose not to replace the package
							bImportWasCancelled = true;
							//OnNewImportRecord(ImportAssetType, FileExtension, bImportSucceeded, bImportWasCancelled, ImportStartTime);
							continue;
						}
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("else2"));
						// The factory can't overwrite this asset, ask if we should delete the object then import the new one. Only do this if "Yes To All" or "No To All" was not already selected.

						bool bWantReplace = bReplaceAll;
						if( ! bReplaceAll && ! bDontReplaceAny )
						{
							FString Result = TEXT("YesAll");
							if( Result.StartsWith(TEXT("YesAll")) )
							{
								bReplaceAll = true;
								bWantReplace = true;
							}
							else if( Result.StartsWith(TEXT("Yes")) )
							{
								bWantReplace = true;
							}
							else if( Result.StartsWith(TEXT("SkipAll")) )
							{
								bDontReplaceAny = true;
								bReplaceAll = false; // prbly don't need to set this
								bWantReplace = false;
							}
							else if( Result.StartsWith(TEXT("Skip")) )
							{
								bWantReplace = false;
							}
							// TODO: parse result for OtherFile
						}

						if( bWantReplace )
						{
							// Delete the existing object
							int32 NumObjectsDeleted = 0;
							TArray< UObject* > ObjectsToDelete;
							ObjectsToDelete.Add(ExistingObject);

							// Dont let the package get garbage collected (just in case we are deleting the last asset in the package)
							UE_LOG(LogTemp, Warning, TEXT("Pkg->AddToRoot();"));
							Pkg->AddToRoot();
							UE_LOG(LogTemp, Warning, TEXT("NumObjectsDeleted = ObjectTools::DeleteObjects( ObjectsToDelete, /*bShowConfirmation=*/false );"));
							NumObjectsDeleted = ObjectTools::DeleteObjects( ObjectsToDelete, /*bShowConfirmation=*/false );
							UE_LOG(LogTemp, Warning, TEXT("Pkg->RemoveFromRoot();"));
							Pkg->RemoveFromRoot();

							const FString QualifiedName = PackageName + TEXT(".") + Name;
							FText Reason;
							if( NumObjectsDeleted == 0 || !IsUniqueObjectName( *QualifiedName, ANY_PACKAGE, Reason ) )
							{
								// Original object couldn't be deleted
								const FText Message = FText::Format( LOCTEXT("ImportDeleteFailed", "Failed to delete '{0}'. The asset is referenced by other content."), FText::FromString( PackageName ) );
								//FMessageDialog::Open( EAppMsgType::Ok, Message );
								UE_LOG(LogTemp, Warning, TEXT("%s"), *Message.ToString());
								//OnNewImportRecord(ImportAssetType, FileExtension, bImportSucceeded, bImportWasCancelled, ImportStartTime);
								continue;
							}
						}
						else
						{
							// User chose not to replace the package
							bImportWasCancelled = true;
							//OnNewImportRecord(ImportAssetType, FileExtension, bImportSucceeded, bImportWasCancelled, ImportStartTime);
							continue;
						}
					}
				}

				// Check for a package that was marked for delete in source control
				//if ( !CheckForDeletedPackage(Pkg) )
				//{
				//	//OnNewImportRecord(ImportAssetType, FileExtension, bImportSucceeded, bImportWasCancelled, ImportStartTime);
				//	continue;
				//}

				UE_LOG(LogTemp, Warning, TEXT("ImportAssetType = Factory->ResolveSupportedClass();"));
				ImportAssetType = Factory->ResolveSupportedClass();
				UE_LOG(LogTemp, Warning, TEXT("UObject* Result = UFactory::StaticImportObject("));
				UObject* Result = UFactory::StaticImportObject( ImportAssetType, Pkg, FName( *Name ), RF_Public|RF_Standalone, bImportWasCancelled, *Filename, NULL, Factory );

				UE_LOG(LogTemp, Warning, TEXT("if(!bImportWasCancelled)"));
				// Do not report any error if the operation was canceled.
				if(!bImportWasCancelled)
				{
					if ( Result )
					{
						ReturnObjects.Add( Result );

						UE_LOG(LogTemp, Warning, TEXT("FAssetRegistryModule::AssetCreated(Result);"));
						// Notify the asset registry
						FAssetRegistryModule::AssetCreated(Result);
						UE_LOG(LogTemp, Warning, TEXT("GEditor->BroadcastObjectReimported(Result);"));
						GEditor->BroadcastObjectReimported(Result);

						bImportSucceeded = true;
					}
					else
					{
						const FText Message = FText::Format( LOCTEXT("ImportFailed_Generic", "Failed to import '{0}'. Failed to create asset '{1}'"), FText::FromString( Filename ), FText::FromString( PackageName ) );
						//FMessageDialog::Open( EAppMsgType::Ok, Message );
						UE_LOG(LogTemp, Warning, TEXT("%s"), *Message.ToString());
					}
				}

				// Refresh the supported class.  Some factories (e.g. FBX) only resolve their type after reading the file
				UE_LOG(LogTemp, Warning, TEXT("ImportAssetType = Factory->ResolveSupportedClass();"));
				ImportAssetType = Factory->ResolveSupportedClass();
				//OnNewImportRecord(ImportAssetType, FileExtension, bImportSucceeded, bImportWasCancelled, ImportStartTime);
			}
			else
			{
				// A factory or extension was not found. The extension warning is above. If a factory was not found, the user likely canceled a factory configuration dialog.
			}
		}

		// Clean up and remove the factories we created from the root set
		UE_LOG(LogTemp, Warning, TEXT("for ( auto ExtensionIt = ExtensionToFactoriesMap.CreateConstIterator(); ExtensionIt; ++ExtensionIt)"));
		for ( auto ExtensionIt = ExtensionToFactoriesMap.CreateConstIterator(); ExtensionIt; ++ExtensionIt)
		{
			UE_LOG(LogTemp, Warning, TEXT("for ( auto FactoryIt = ExtensionIt.Value().CreateConstIterator(); FactoryIt; ++FactoryIt )"));
			for ( auto FactoryIt = ExtensionIt.Value().CreateConstIterator(); FactoryIt; ++FactoryIt )
			{
				UE_LOG(LogTemp, Warning, TEXT("(*FactoryIt)->CleanUp();"));
				(*FactoryIt)->CleanUp();
				UE_LOG(LogTemp, Warning, TEXT("(*FactoryIt)->RemoveFromRoot();"));
				(*FactoryIt)->RemoveFromRoot();
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("GWarn->EndSlowTask();"));
		GWarn->EndSlowTask();

		// Sync content browser to the newly created assets
		UE_LOG(LogTemp, Warning, TEXT("if ( ReturnObjects.Num() )"));
		if ( ReturnObjects.Num() )
		{
			//FAssetTools::Get().SyncBrowserToAssets(ReturnObjects);
		}

		UE_LOG(LogTemp, Warning, TEXT("return ReturnObjects"));
		return ReturnObjects;

		// ...added our own implementation here.
		// the code will be exactly what the AssetTools function does, but whenever
		// there is a problem asking for user input, we will call the InputGetter
		// function to get the user input via the TCP input from the Program side
		// this is important to not get the focus off from the users current
		// application, and maybe we need to re-export or rename an exportet file
		// so the file-structure on-disk and the file-structure in-engine match
		// a matching file-structure is a very important part of a waterproof asset
		// pipeline and especially the main means of how m2u can create associations
		// between objects in the Program and objects in the Editor.
		// If InputGetter is NULL, we assume that user-input is not wanted, instead
		// we decide to always overwrite and replace objects if the case comes up.
		// since we can't assure that the names on-disk and in-engine will match up
		// then, this is not the preferred way to use for a synced pipeline
		// but probably the best way for "just-build-the-shit" functionality
		// It is up to the specific implementation of the InputGetter function
		// if really user-input is requested. If the function always tells us
		// to just go on, you can automate the whole process of overwriting while
		// still getting the chance to rename a file on disk or create a
		// "disk-name"->"engine-name" association so you can be sure to be able
		// to find your assets by name after they were imported.
	}// ImportAssets()

/**
   Find an asset.
 */
	UObject* GetAssetFromPath(FString AssetPath)
	{
		// If there is no dot, add a dot and repeat the object name.
		// /Game/Meshes/MyStaticMesh.MyStaticMesh would be the actual path
		// to the object, while the MyStaticMesh before the dot is the package
		// copied from ConstructorHelpers
		int32 PackageDelimPos = INDEX_NONE;
		AssetPath.FindChar( TCHAR('.'), PackageDelimPos );
		if( PackageDelimPos == INDEX_NONE )
		{
			int32 ObjectNameStart = INDEX_NONE;
			AssetPath.FindLastChar( TCHAR('/'), ObjectNameStart );
			if( ObjectNameStart != INDEX_NONE )
			{
				const FString ObjectName = AssetPath.Mid( ObjectNameStart+1 );
				AssetPath += TCHAR('.');
				AssetPath += ObjectName;
			}
		}


		// try to find the asset
		UE_LOG(LogTemp, Log, TEXT("Trying to find Asset %s."), *AssetPath);
		//UObject* Asset = FindObject<UObject>( ANY_PACKAGE, *AssetPath, false );
		UObject* Asset = StaticLoadObject(UObject::StaticClass(), NULL, *AssetPath);
		if( Asset == NULL)
		{			
			UE_LOG(LogTemp, Log, TEXT("Failed to find Asset %s."), *AssetPath);
			return NULL;
		}
		return Asset;
	}


/**
   Try to export the asset found at AssetPath to a file on disk specified by
   ExportPath.
   Fails if the location on disk is not writable.
 */
	void ExportAsset(FString AssetPath, FString& ExportPath)
	{
		UObject* Asset = GetAssetFromPath(AssetPath);
		if(Asset != NULL)
		{
			TArray<UObject*> Assets;
			Assets.Add(Asset);
			ObjectTools::ExportObjects(Assets, false, &ExportPath, true);
		}
	}// ExportAsset()


	TArray<UObject*> ImportAssets2(const TArray<FString>& Files, const FString& RootDestinationPath, UFactory* SpecifiedFactory, bool bSyncToBrowser = true)
	{
		// get the FAssetTools instance from the AssetToolsModule
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		TArray<UObject*> ReturnObjects;
		TMap< FString, TArray<UFactory*> > ExtensionToFactoriesMap;

		FScopedSlowTask SlowTask(Files.Num() + 3, LOCTEXT("ImportSlowTask", "Importing"));
		SlowTask.MakeDialog();

		// Reset the 'Do you want to overwrite the existing object?' Yes to All / No to All prompt, to make sure the
		// user gets a chance to select something
		UFactory::ResetState();

		SlowTask.EnterProgressFrame();

		TArray<TPair<FString, FString>> FilesAndDestinations;
		ExpandDirectories(Files, RootDestinationPath, FilesAndDestinations);

		SlowTask.EnterProgressFrame(1, LOCTEXT("Import_DeterminingImportTypes", "Determining asset types"));

		if (SpecifiedFactory == NULL)
		{
			// First instantiate one factory for each file extension encountered that supports the extension
			for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
			{
				if ((*ClassIt)->IsChildOf(UFactory::StaticClass()) && !((*ClassIt)->HasAnyClassFlags(CLASS_Abstract)))
				{
					UFactory* Factory = Cast<UFactory>((*ClassIt)->GetDefaultObject());
					if (Factory->bEditorImport)
					{
						TArray<FString> FactoryExtensions;
						Factory->GetSupportedFileExtensions(FactoryExtensions);
						for (auto& FileDest : FilesAndDestinations)
						{
							const FString FileExtension = FPaths::GetExtension(FileDest.Key);
							UE_LOG(LogTemp, Log, TEXT("EXTENSION: %s."), *FileExtension);

							// Case insensitive string compare with supported formats of this factory
							if (FactoryExtensions.Contains(FileExtension))
							{
								
								TArray<UFactory*>& ExistingFactories = ExtensionToFactoriesMap.FindOrAdd(FileExtension);

								// Do not remap extensions, just reuse the existing UFactory.
								// There may be multiple UFactories, so we will keep track of all of them
								bool bFactoryAlreadyInMap = false;
								for (auto FoundFactoryIt = ExistingFactories.CreateConstIterator(); FoundFactoryIt; ++FoundFactoryIt)
								{
									if ((*FoundFactoryIt)->GetClass() == Factory->GetClass())
									{
										bFactoryAlreadyInMap = true;
										break;
									}
								}

								if (!bFactoryAlreadyInMap)
								{
									UE_LOG(LogTemp, Log, TEXT("FOUND FACTORY: %s."), *Factory->GetClass()->GetName());
									// We found a factory for this file, it can be imported!
									// Create a new factory of the same class and make sure it doesn't get GCed.
									// The object will be removed from the root set at the end of this function.
									UFactory* NewFactory = NewObject<UFactory>(GetTransientPackage(), Factory->GetClass());
									if (NewFactory->ConfigureProperties())
									{
										NewFactory->AddToRoot();
										ExistingFactories.Add(NewFactory);
									}
								}
							}
						}
					}
				}
			}
		}
		else
		{
			if (SpecifiedFactory->bEditorImport)
			{
				TArray<FString> FactoryExtensions;
				SpecifiedFactory->GetSupportedFileExtensions(FactoryExtensions);
				for (auto FileIt = Files.CreateConstIterator(); FileIt; ++FileIt)
				{
					const FString FileExtension = FPaths::GetExtension(*FileIt);

					// Case insensitive string compare with supported formats of this factory
					if (FactoryExtensions.Contains(FileExtension))
					{
						TArray<UFactory*>& ExistingFactories = ExtensionToFactoriesMap.FindOrAdd(FileExtension);

						// Do not remap extensions, just reuse the existing UFactory.
						// There may be multiple UFactories, so we will keep track of all of them
						bool bFactoryAlreadyInMap = false;
						for (auto FoundFactoryIt = ExistingFactories.CreateConstIterator(); FoundFactoryIt; ++FoundFactoryIt)
						{
							if ((*FoundFactoryIt)->GetClass() == SpecifiedFactory->GetClass())
							{
								bFactoryAlreadyInMap = true;
								break;
							}
						}

						if (!bFactoryAlreadyInMap)
						{
							// We found a factory for this file, it can be imported!
							// Create a new factory of the same class and make sure it doesnt get GCed.
							// The object will be removed from the root set at the end of this function.
							UFactory* NewFactory = NewObject<UFactory>(GetTransientPackage(), SpecifiedFactory->GetClass());
							if (NewFactory->ConfigureProperties())
							{
								NewFactory->AddToRoot();
								ExistingFactories.Add(NewFactory);
							}
						}
					}
				}
			}
		}

		// We need to sort the factories so that they get tested in priority order
		for (auto& ExtensionToFactories : ExtensionToFactoriesMap)
		{
			ExtensionToFactories.Value.Sort(&UFactory::SortFactoriesByPriority);
		}

		// Some flags to keep track of what the user decided when asked about overwriting or replacing
		bool bOverwriteAll = true; //false;
		bool bReplaceAll = true; // false;
		bool bDontOverwriteAny = false;
		bool bDontReplaceAny = false;

		UE_LOG(LogTemp, Log, TEXT("FILE CYCLE"));
		// Now iterate over the input files and use the same factory object for each file with the same extension
		for (int32 FileIdx = 0; FileIdx < FilesAndDestinations.Num(); ++FileIdx)
		{
			const FString& Filename = FilesAndDestinations[FileIdx].Key;
			const FString& DestinationPath = FilesAndDestinations[FileIdx].Value;

			SlowTask.EnterProgressFrame(1, FText::Format(LOCTEXT("Import_ImportingFile", "Importing \"{0}\"..."), FText::FromString(FPaths::GetBaseFilename(Filename))));

			FString FileExtension = FPaths::GetExtension(Filename);

			const TArray<UFactory*>* FactoriesPtr = ExtensionToFactoriesMap.Find(FileExtension);
			UFactory* Factory = NULL;
			if (FactoriesPtr)
			{
				const TArray<UFactory*>& Factories = *FactoriesPtr;

				// Handle the potential of multiple factories being found
				if (Factories.Num() > 0)
				{
					Factory = Factories[0];

					for (auto FactoryIt = Factories.CreateConstIterator(); FactoryIt; ++FactoryIt)
					{
						UFactory* TestFactory = *FactoryIt;
						if (TestFactory->FactoryCanImport(Filename))
						{
							Factory = TestFactory;
							break;
						}
					}
				}
			}
			else
			{
				if (FEngineAnalytics::IsAvailable())
				{
					TArray<FAnalyticsEventAttribute> Attribs;
					Attribs.Add(FAnalyticsEventAttribute(TEXT("FileExtension"), FileExtension));

					FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.ImportFailed"), Attribs);
				}

				const FText Message = FText::Format(LOCTEXT("ImportFailed_UnknownExtension", "Failed to import '{0}'. Unknown extension '{1}'."), FText::FromString(Filename), FText::FromString(FileExtension));
				FNotificationInfo Info(Message);
				Info.ExpireDuration = 3.0f;
				Info.bUseLargeFont = false;
				Info.bFireAndForget = true;
				Info.bUseSuccessFailIcons = true;
				FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);

				UE_LOG(LogTemp, Warning, TEXT("%s"), *Message.ToString());
			}

			if (Factory != NULL)
			{
				UClass* ImportAssetType = Factory->SupportedClass;
				bool bImportSucceeded = false;
				bool bImportWasCancelled = false;
				FDateTime ImportStartTime = FDateTime::UtcNow();

				FString Name = ObjectTools::SanitizeObjectName(FPaths::GetBaseFilename(Filename));
				FString PackageName = DestinationPath + TEXT("/") + Name;

				// We can not create assets that share the name of a map file in the same location
				if (FEditorFileUtils::IsMapPackageAsset(PackageName))
				{
					const FText Message = FText::Format(LOCTEXT("AssetNameInUseByMap", "You can not create an asset named '{0}' because there is already a map file with this name in this folder."), FText::FromString(Name));
					FMessageDialog::Open(EAppMsgType::Ok, Message);
					UE_LOG(LogTemp, Warning, TEXT("%s"), *Message.ToString());
					continue;
				}

				UPackage* Pkg = CreatePackage(NULL, *PackageName);
				if (!ensure(Pkg))
				{
					// Failed to create the package to hold this asset for some reason
					continue;
				}

				// Make sure the destination package is loaded
				Pkg->FullyLoad();

				// Check for an existing object
				UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), Pkg, *Name);
				if (ExistingObject != NULL)
				{
					// If the existing object is one of the imports we've just created we can't replace or overwrite it
					if (ReturnObjects.Contains(ExistingObject))
					{
						if (ImportAssetType == nullptr)
						{
							// The factory probably supports multiple types and cant be determined yet without asking the user or actually loading it
							// We just need to generate an unused name so object should do fine.
							ImportAssetType = UObject::StaticClass();
						}
						// generate a unique name for this import
						Name = MakeUniqueObjectName(Pkg, ImportAssetType, *Name).ToString();
					}
					else
					{
						// If the object is supported by the factory we are using, ask if we want to overwrite the asset
						// Otherwise, prompt to replace the object
						if (Factory->DoesSupportClass(ExistingObject->GetClass()))
						{
							// The factory can overwrite this object, ask if that is okay, unless "Yes To All" or "No To All" was already selected
							EAppReturnType::Type UserResponse;

							if (bOverwriteAll || GIsAutomationTesting)
							{
								UserResponse = EAppReturnType::YesAll;
							}
							else if (bDontOverwriteAny)
							{
								UserResponse = EAppReturnType::NoAll;
							}
							else
							{
								UserResponse = FMessageDialog::Open(
									EAppMsgType::YesNoYesAllNoAll,
									FText::Format(LOCTEXT("ImportObjectAlreadyExists_SameClass", "Do you want to overwrite the existing asset?\n\nAn asset already exists at the import location: {0}"), FText::FromString(PackageName)));

								bOverwriteAll = UserResponse == EAppReturnType::YesAll;
								bDontOverwriteAny = UserResponse == EAppReturnType::NoAll;
							}

							const bool bWantOverwrite = UserResponse == EAppReturnType::Yes || UserResponse == EAppReturnType::YesAll;

							if (!bWantOverwrite)
							{
								// User chose not to replace the package
								bImportWasCancelled = true;
								continue;
							}
						}
						else
						{
							// The factory can't overwrite this asset, ask if we should delete the object then import the new one. Only do this if "Yes To All" or "No To All" was not already selected.
							EAppReturnType::Type UserResponse;

							if (bReplaceAll)
							{
								UserResponse = EAppReturnType::YesAll;
							}
							else if (bDontReplaceAny)
							{
								UserResponse = EAppReturnType::NoAll;
							}
							else
							{
								UserResponse = FMessageDialog::Open(
									EAppMsgType::YesNoYesAllNoAll,
									FText::Format(LOCTEXT("ImportObjectAlreadyExists_DifferentClass", "Do you want to replace the existing asset?\n\nAn asset already exists at the import location: {0}"), FText::FromString(PackageName)));

								bReplaceAll = UserResponse == EAppReturnType::YesAll;
								bDontReplaceAny = UserResponse == EAppReturnType::NoAll;
							}

							const bool bWantReplace = UserResponse == EAppReturnType::Yes || UserResponse == EAppReturnType::YesAll;

							if (bWantReplace)
							{
								// Delete the existing object
								int32 NumObjectsDeleted = 0;
								TArray< UObject* > ObjectsToDelete;
								ObjectsToDelete.Add(ExistingObject);

								// Dont let the package get garbage collected (just in case we are deleting the last asset in the package)
								Pkg->AddToRoot();
								NumObjectsDeleted = ObjectTools::DeleteObjects(ObjectsToDelete, /*bShowConfirmation=*/false);
								Pkg->RemoveFromRoot();

								const FString QualifiedName = PackageName + TEXT(".") + Name;
								FText Reason;
								if (NumObjectsDeleted == 0 || !IsUniqueObjectName(*QualifiedName, ANY_PACKAGE, Reason))
								{
									// Original object couldn't be deleted
									const FText Message = FText::Format(LOCTEXT("ImportDeleteFailed", "Failed to delete '{0}'. The asset is referenced by other content."), FText::FromString(PackageName));
									FMessageDialog::Open(EAppMsgType::Ok, Message);
									UE_LOG(LogTemp, Warning, TEXT("%s"), *Message.ToString());
									continue;
								}
								else
								{
									// succeed, recreate package since it has been deleted
									Pkg = CreatePackage(NULL, *PackageName);
								}
							}
							else
							{
								// User chose not to replace the package
								bImportWasCancelled = true;
								continue;
							}
						}
					}
				}

				// Check for a package that was marked for delete in source control
				//if (!CheckForDeletedPackage(Pkg))
				//{
				//	continue;
				//}

				ImportAssetType = Factory->ResolveSupportedClass();
				UObject* Result = UFactory::StaticImportObject(ImportAssetType, Pkg, FName(*Name), RF_Public | RF_Standalone, bImportWasCancelled, *Filename, NULL, Factory);

				// Do not report any error if the operation was canceled.
				if (!bImportWasCancelled)
				{
					if (Result)
					{
						ReturnObjects.Add(Result);

						// Notify the asset registry
						FAssetRegistryModule::AssetCreated(Result);
						GEditor->BroadcastObjectReimported(Result);

						bImportSucceeded = true;
					}
					else
					{
						const FText Message = FText::Format(LOCTEXT("ImportFailed_Generic", "Failed to import '{0}'. Failed to create asset '{1}'"), FText::FromString(Filename), FText::FromString(PackageName));
						FMessageDialog::Open(EAppMsgType::Ok, Message);
						UE_LOG(LogTemp, Warning, TEXT("%s"), *Message.ToString());
					}
				}

				// Refresh the supported class.  Some factories (e.g. FBX) only resolve their type after reading the file
				ImportAssetType = Factory->ResolveSupportedClass();
			}
			else
			{
				// A factory or extension was not found. The extension warning is above. If a factory was not found, the user likely canceled a factory configuration dialog.
			}
		}

		SlowTask.EnterProgressFrame(1);

		// Clean up and remove the factories we created from the root set
		for (auto ExtensionIt = ExtensionToFactoriesMap.CreateConstIterator(); ExtensionIt; ++ExtensionIt)
		{
			for (auto FactoryIt = ExtensionIt.Value().CreateConstIterator(); FactoryIt; ++FactoryIt)
			{
				(*FactoryIt)->CleanUp();
				(*FactoryIt)->RemoveFromRoot();
			}
		}

		return ReturnObjects;
	}

#undef LOCTEXT_NAMESPACE
} // namespace m2uAssetHelper