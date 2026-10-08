#pragma warning(disable: 4456 4457 4458 4459)
#include "NCPlayerController.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "Misc/ConfigCacheIni.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "NCMenuCamera.h"
#include "NCMission.h"
#include "NCVehiclePawn.h"
#include "NCCraft.h"
#include "NCInteractionComponent.h"
#include "NCHealthComponent.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "NCPlayerState.h"
#include "TimerManager.h"

static UNCSessionSubsystem* Session(const UObject* O)
{
	const UGameInstance* GI = O ? UGameplayStatics::GetGameInstance(O) : nullptr;
	return GI ? GI->GetSubsystem<UNCSessionSubsystem>() : nullptr;
}

void ANCPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent) return;
	InputComponent->BindKey(EKeys::F,IE_Pressed,this,&ANCPlayerController::VehicleInteraction);
	InputComponent->BindKey(EKeys::Gamepad_FaceButton_Top,IE_Pressed,this,&ANCPlayerController::VehicleInteraction);
}

void ANCPlayerController::VehicleInteraction()
{
	if (!bInMenu && !bPauseMenu && !bStoryScene && GetPawn()) ServerVehicleInteraction();
}

void ANCPlayerController::ServerVehicleInteraction_Implementation()
{
	APawn* Current=GetPawn();
	if (!Current) return;
	if (const auto* Health=UNCHealthComponent::Of(Current); Health && Health->bDead) return;
	if (auto* Vehicle=Cast<ANCVehiclePawn>(Current)) { Vehicle->RequestExit(); return; }
	if (auto* Craft=Cast<ANCCraftPawn>(Current)) { Craft->RequestExit(); return; }
	if (auto* Interaction=Current->FindComponentByClass<UNCInteractionComponent>()) Interaction->Interact();
}

void ANCPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	// Migrate the old launcher's forced 60 FPS only once; later menu choices are retained.
	bool bSettingsMigrated=false;
	if (GConfig) GConfig->GetBool(TEXT("NeonCoastSettings"),TEXT("PauseMenuConfigured"),bSettingsMigrated,GGameUserSettingsIni);
	if (!bSettingsMigrated && GEngine && GConfig)
	{
		if (UGameUserSettings* Settings=GEngine->GetGameUserSettings())
		{
			if (Settings->GetFrameRateLimit()<=0.f) Settings->SetFrameRateLimit(60.f);
			Settings->ApplyNonResolutionSettings();
			Settings->SaveSettings();
			GConfig->SetBool(TEXT("NeonCoastSettings"),TEXT("PauseMenuConfigured"),true,GGameUserSettingsIni);
			GConfig->Flush(false,GGameUserSettingsIni);
		}
	}
	UNCSessionSubsystem* S = Session(this);
	if (S)
	{
		TimeOfDay = S->TimeOfDay;
		if (TimeOfDay != 0)
		{
			ANCPlayerState::ApplyTimeOfDay(GetWorld(), TimeOfDay);
		}
	}
	const AGameModeBase* GM = GetWorld()->GetAuthGameMode();
	const bool bNoMenu = GM && UGameplayStatics::HasOption(GM->OptionsString, TEXT("nomenu"));
	if (GetNetMode() != NM_Client && !bNoMenu && !(S && S->bMenuDone))
	{
		OpenMainMenu();
	}
}

void ANCPlayerController::OpenMainMenu()
{
	for (TActorIterator<ANCMenuCamera> It(GetWorld()); It; ++It)
	{
		MenuCamera = *It;
		break;
	}
	if (!MenuCamera)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		MenuCamera = GetWorld()->SpawnActor<ANCMenuCamera>(FVector(0, 0, 30000.f), FRotator::ZeroRotator, P);
	}
	bInMenu = true;
	SetViewTarget(MenuCamera);
	LockPawn(true);
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

void ANCPlayerController::AutoManageActiveCameraTarget(AActor* SuggestedTarget)
{
	if (bStoryScene && StoryCamera) { SetViewTarget(StoryCamera); return; }
	if (bInMenu && MenuCamera)
	{
		SetViewTarget(MenuCamera);           // keep flying over the city while the menu is open
		return;
	}
	Super::AutoManageActiveCameraTarget(SuggestedTarget);
}

void ANCPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (bInMenu)
	{
		LockPawn(true);
		if (MenuCamera)
		{
			SetViewTarget(MenuCamera);
		}
	}
}

void ANCPlayerController::LockPawn(bool bLock)
{
	if (bLock)
	{
		SetIgnoreMoveInput(true);
		SetIgnoreLookInput(true);
		if (APawn* P = GetPawn())
		{
			P->DisableInput(this);
		}
	}
	else
	{
		ResetIgnoreInputFlags();
		if (APawn* P = GetPawn())
		{
			P->EnableInput(this);
		}
	}
}

void ANCPlayerController::StartGame()
{
	if (!bInMenu)
	{
		return;
	}
	bInMenu = false;
	if (UNCSessionSubsystem* S = Session(this))
	{
		S->bMenuDone = true;
	}
	LockPawn(false);
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	SetInputMode(FInputModeGameOnly());
	if (APawn* P = GetPawn())
	{
		// cinematic swoop from the flyover down to the player
		SetViewTargetWithBlend(P, 2.5f, VTBlend_EaseInOut, 2.2f);
	}
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		if (MenuCamera && !bInMenu)
		{
			MenuCamera->SetActorTickEnabled(false);
		}
	}), 3.f, false);
}

void ANCPlayerController::HostGame()
{
	if (UNCSessionSubsystem* S = Session(this))
	{
		S->bMenuDone = true;
	}
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)), true, TEXT("listen"));
}

void ANCPlayerController::JoinGame(const FString& Address)
{
	if (UNCSessionSubsystem* S = Session(this))
	{
		S->bMenuDone = true;
	}
	ClientTravel(Address, TRAVEL_Absolute);
}

void ANCPlayerController::QuitGame()
{
	if (bPauseMenu) ResumeGame();
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

void ANCPlayerController::OpenPauseMenu()
{
	if (!IsLocalController() || bInMenu || bPauseMenu || bStoryScene) return;
	bPauseMenu = true;
	bShouldPerformFullTickWhenPaused = true; // keep cursor and HUD hit boxes responsive
	LockPawn(true);
	bPausedWorldByMenu = GetNetMode()==NM_Standalone && SetPause(true);
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
}

void ANCPlayerController::ResumeGame()
{
	if (!bPauseMenu) return;
	if (bPausedWorldByMenu) SetPause(false);
	bPausedWorldByMenu = false;
	bPauseMenu = false;
	bShouldPerformFullTickWhenPaused = false;
	LockPawn(false);
	bShowMouseCursor = false;
	bEnableClickEvents = false;
	bEnableMouseOverEvents = false;
	SetInputMode(FInputModeGameOnly());
}

void ANCPlayerController::SetTimeOfDay(int32 Preset)
{
	TimeOfDay = FMath::Clamp(Preset, 0, 2);
	if (UNCSessionSubsystem* S = Session(this))
	{
		S->TimeOfDay = TimeOfDay;
	}
	ANCPlayerState::ApplyTimeOfDay(GetWorld(), TimeOfDay);
}

void ANCPlayerController::ClientDeathScreen_Implementation(bool bBusted)
{
	if (bPauseMenu) ResumeGame();
	ClientEndStoryScene_Implementation();
	DeathScreenStart = GetWorld()->GetRealTimeSeconds();
	bDeathBusted = bBusted;
}

void ANCPlayerController::ServerContinueStory_Implementation()
{
	if (ANCMissionManager* M = ANCMissionManager::Get(this)) M->ContinueStory(this);
}

void ANCPlayerController::ServerSkipStoryScene_Implementation()
{
	if (ANCMissionManager* M = ANCMissionManager::Get(this)) M->SkipScene(this);
}

void ANCPlayerController::ClientStoryScene_Implementation(float Seconds)
{
	if (!IsLocalController() || bInMenu || !GetPawn()) return;
	ClientEndStoryScene_Implementation();
	PreviousSceneView = GetViewTarget();
	ScenePawn = GetPawn();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	StoryCamera = GetWorld()->SpawnActor<ACameraActor>(GetPawn()->GetActorLocation(), FRotator::ZeroRotator, Params);
	if (!StoryCamera) return;
	StoryCamera->GetCameraComponent()->SetFieldOfView(48.f);
	StoryCamera->GetCameraComponent()->bConstrainAspectRatio = false;
	SceneStart = GetWorld()->GetTimeSeconds();
	SceneDuration = FMath::Clamp(Seconds, 1.f, 180.f);
	bStoryScene = true;
	LockPawn(true);
	PlayerTick(0.f);
	SetViewTargetWithBlend(StoryCamera, 0.35f);
}

void ANCPlayerController::ClientEndStoryScene_Implementation()
{
	if (!bStoryScene && !StoryCamera) return;
	bStoryScene = false;
	LockPawn(false);
	AActor* Restore = PreviousSceneView.Get();
	if (GetPawn() != ScenePawn.Get()) Restore = GetPawn();
	if (!Restore) Restore = GetPawn();
	if (Restore) SetViewTarget(Restore);
	if (StoryCamera) StoryCamera->Destroy();
	StoryCamera = nullptr;
	PreviousSceneView.Reset();
	ScenePawn.Reset();
}

void ANCPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!bStoryScene) return;
	const float Elapsed = GetWorld()->GetTimeSeconds() - SceneStart;
	if (!StoryCamera || !ScenePawn.IsValid() || GetPawn()!=ScenePawn.Get() || Elapsed>SceneDuration+1.f)
	{
		ClientEndStoryScene_Implementation();
		return;
	}
	const FVector Focus = ScenePawn->GetActorLocation() + FVector(0,0,55.f);
	const int32 Shot = (int32(Elapsed / 5.f)) % 3;
	const float Angle = ScenePawn->GetActorRotation().Yaw + (Shot==0 ? 35.f : Shot==1 ? -55.f : 145.f);
	const FVector Direction = FRotator(0,Angle,0).Vector();
	const FVector Desired = Focus + Direction * (Shot==2 ? 470.f : 280.f) + FVector(0,0,Shot==2 ? 140.f : 35.f);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(StoryCamera), true, ScenePawn.Get());
	Query.AddIgnoredActor(StoryCamera);
	FHitResult Hit;
	FVector Safe = Desired;
	if (GetWorld()->SweepSingleByChannel(Hit, Focus, Desired, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(12.f), Query))
		Safe = Hit.Location + Hit.Normal*15.f;
	StoryCamera->SetActorLocation(Safe);
	StoryCamera->SetActorRotation((Focus-Safe).Rotation());
}
