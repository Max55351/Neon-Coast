#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "NCGameMode.generated.h"

/**
 * Listen-server friendly game mode: uses the Third Person template character if the
 * project has it and gives every player pawn the vehicle interaction, health and weapon
 * components. Death -> "WASTED" -> hospital; arrest -> "BUSTED" -> police station.
 * Host:  open NeonCoast?listen     Join:  open <host IP>
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ANCGameMode();
	virtual void FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation) override;
	virtual void BeginPlay() override;

	/** Server: arrest (called by an officer standing next to an unarmed 1-star suspect). */
	void Busted(APlayerController* PC);

	/** Money lost in hospital / at the police station. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeonCoast") int32 HospitalBill = 500;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeonCoast") int32 BustedFine = 1000;

protected:
	UFUNCTION() void OnPawnDied(AActor* Victim, AController* Killer);
	void Respawn(APlayerController* PC, bool bBusted);
	void SpawnCabriolet();
	FTimerHandle CabrioletTimer;
	TWeakObjectPtr<class ANCCabrioletPawn> ParkedCabriolet;
	TSet<TWeakObjectPtr<APlayerController>> Pending;
	/** Headless diagnostics: run the game with -NCSelfTest (drives a car, exits it, fires a shot; results in the log). */
	void SelfTestTick();
	FTimerHandle SelfTestTimer;
	int32 STState = 0;
	int32 STTick = 0;
	int32 STFailures = 0;
	FVector STWalkStart = FVector::ZeroVector;
	TWeakObjectPtr<class ANCTrafficManager> STTraffic;
	TWeakObjectPtr<AActor> STObstacle;
	int32 STTrafficCar = INDEX_NONE;
	float STTrafficStart = 0.f;
	TWeakObjectPtr<class ANCVehiclePawn> STCar;
	TWeakObjectPtr<APawn> STHero;
	TWeakObjectPtr<class USkeletalMeshComponent> STPedBody;
};
