#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "NCPoliceOfficer.generated.h"

class UNCHealthComponent;
class UStaticMeshComponent;
class UAnimSequence;

/**
 * Police officer / SWAT / soldier on foot (server AI, no navmesh needed): runs straight at the
 * suspect (hops small obstacles), shoots when it has line of sight, arrests an unarmed 1-star
 * suspect by standing next to him. Rank: 0 police (pistol), 1 SWAT (rifle), 2 soldier (rifle/LMG).
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCPoliceOfficer : public ACharacter
{
	GENERATED_BODY()
public:
	ANCPoliceOfficer();

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Police") int32 Rank = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Police") TObjectPtr<APawn> Target;
	TWeakObjectPtr<AController> PursuedController;
	FVector SearchLocation = FVector::ZeroVector;
	bool bSearching = false;
	UPROPERTY(Replicated) bool bAmbientPatrol = false;
	TArray<FVector> PatrolPoints;
	int32 PatrolPoint = 0;
	float PatrolPause = 0.f;
	float PatrolStuck = 0.f;
	FVector LastPatrolPosition = FVector::ZeroVector;
	/** Guards stay at their post until a suspect comes close. */
	UPROPERTY(BlueprintReadOnly, Category = "Police") bool bGuard = false;
	/** Mission enemy (mob gunman): shoots, never arrests, its death is not a crime. */
	UPROPERTY(BlueprintReadOnly, Category = "Police") bool bGangster = false;
	UPROPERTY(VisibleAnywhere, Category = "Police") TObjectPtr<UNCHealthComponent> Health;
	UPROPERTY(VisibleAnywhere, Category = "Police") TObjectPtr<UStaticMeshComponent> Gun;
	UPROPERTY(ReplicatedUsing = OnRep_Look) TObjectPtr<USkeletalMesh> LookMesh;
	UPROPERTY(Replicated) TObjectPtr<UAnimSequence> WalkAnim;
	UPROPERTY(Replicated) TObjectPtr<UAnimSequence> IdleAnim;

	void Init(int32 InRank, USkeletalMesh* Mesh, UAnimSequence* Walk, UAnimSequence* Idle, APawn* InTarget);
	bool IsDead() const;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnRep_Look();
	UFUNCTION() void OnDied(AActor* Victim, AController* Killer);
	UFUNCTION(NetMulticast, Unreliable) void MulticastShot(FVector_NetQuantize Muzzle, FVector_NetQuantize Hit, bool bFlesh);
	void ApplyLook();
	float NextShot = 0.f;
	float ArrestTime = 0.f;
	float LosTimer = 0.f;
	bool bLos = false;
	bool bWalkingAnim = false;
};

/** Drives a police car at the suspect: pure pursuit + obstacle feelers + reverse when stuck. */
UCLASS()
class NEONCOASTRUNTIME_API ANCPoliceDriver : public AAIController
{
	GENERATED_BODY()
public:
	ANCPoliceDriver();
	UPROPERTY(BlueprintReadOnly, Category = "Police") TObjectPtr<APawn> Target;
	TWeakObjectPtr<AController> PursuedController;
	FVector SearchLocation = FVector::ZeroVector;
	bool bSearching = false;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;
protected:
	float StuckTime = 0.f;
	float ReverseTime = 0.f;
};
