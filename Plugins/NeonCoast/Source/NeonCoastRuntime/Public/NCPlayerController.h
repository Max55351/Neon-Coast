#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "NCPlayerController.generated.h"

class ANCMenuCamera;

/** Lives for the whole game session (survives map travel): remembers that the menu was left. */
UCLASS()
class NEONCOASTRUNTIME_API UNCSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	bool bMenuDone = false;
	int32 TimeOfDay = 0;
};

/**
 * Player controller with the main menu: on the first start (standalone / host) the view
 * flies over the city on ANCMenuCamera while the HUD draws the menu; "Hrat" blends the
 * camera down to the player. Joining clients and "?nomenu" maps skip the menu.
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	UFUNCTION(Server, Reliable) void ServerVehicleInteraction();
	void VehicleInteraction();
	virtual void AutoManageActiveCameraTarget(AActor* SuggestedTarget) override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void PlayerTick(float DeltaTime) override;
	UFUNCTION(Server, Reliable) void ServerContinueStory();
	UFUNCTION(Server, Reliable) void ServerSkipStoryScene();
	UFUNCTION(Client, Reliable) void ClientStoryScene(float Seconds);
	UFUNCTION(Client, Reliable) void ClientEndStoryScene();
	bool IsStoryScene() const { return bStoryScene; }

	bool IsInMenu() const { return bInMenu; }
	bool IsPauseMenu() const { return bPauseMenu; }
	void OpenPauseMenu();
	void ResumeGame();
	void StartGame();
	void HostGame();
	void JoinGame(const FString& Address);
	void QuitGame();
	/** 0 = day, 1 = golden hour, 2 = neon night (local lighting, also used by the menu). */
	void SetTimeOfDay(int32 Preset);
	int32 GetTimeOfDay() const { return TimeOfDay; }

	/** "WASTED" / "BUSTED" screen on the owning client (drawn by NCHUD for a few seconds). */
	UFUNCTION(Client, Reliable) void ClientDeathScreen(bool bBusted);
	float DeathScreenStart = -100.f;
	bool bDeathBusted = false;

protected:
	bool bInMenu = false;
	bool bPauseMenu = false;
	bool bPausedWorldByMenu = false;
	bool bStoryScene = false;
	float SceneStart = 0.f;
	float SceneDuration = 0.f;
	UPROPERTY(Transient) TObjectPtr<class ACameraActor> StoryCamera;
	TWeakObjectPtr<AActor> PreviousSceneView;
	TWeakObjectPtr<APawn> ScenePawn;
	int32 TimeOfDay = 0;
	UPROPERTY() TObjectPtr<ANCMenuCamera> MenuCamera;
	void OpenMainMenu();
	void LockPawn(bool bLock);
};
