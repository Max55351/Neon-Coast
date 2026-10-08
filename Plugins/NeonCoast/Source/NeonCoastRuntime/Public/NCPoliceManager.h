#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCTypes.h"
#include "NCPoliceManager.generated.h"

class ANCVehiclePawn;
class ANCPoliceOfficer;
class UAnimSequence;

/** Restricted area (military base): entering it gives at least Stars. */
USTRUCT(BlueprintType)
struct FNCZone
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone") int32 Stars = 4;
	/** Polygon in world XY (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Zone") TArray<FVector2D> Poly;
};

/** Looks of one kind of officer (police / SWAT / soldier). */
USTRUCT(BlueprintType)
struct FNCOfficerLook
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") TObjectPtr<USkeletalMesh> Mesh = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") TObjectPtr<UAnimSequence> Walk = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") TObjectPtr<UAnimSequence> Idle = nullptr;
};

/**
 * Wanted level brain (server): crimes -> stars, who sees the player, losing the cops, and the
 * response per star: 1-2 patrol cars, 3 more cars, 4 SWAT, 5 army. Patrol cars chase with
 * sirens (ANCPoliceDriver), officers get out near the suspect (ANCPoliceOfficer): arrest an
 * unarmed 1-star suspect ("BUSTED"), otherwise shoot. Also: restricted zones with soldiers,
 * respawn points (hospital / police station) and the weapon-in-hand offset.
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCPoliceManager : public AActor
{
	GENERATED_BODY()
public:
	ANCPoliceManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") TSubclassOf<ANCVehiclePawn> PoliceCarClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") FNCOfficerLook PoliceLook;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") FNCOfficerLook SwatLook;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") FNCOfficerLook SoldierLook;
	/** Russo gang gunmen in missions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") FNCOfficerLook GangsterLook;
	/** Vehicle blueprints by asset name (missions spawn cars from it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") TMap<FName, TSubclassOf<ANCVehiclePawn>> VehicleClasses;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") TArray<FNCZone> Zones;
	/** Soldier guard posts of the military base (x, y, z, yaw). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") TArray<FTransform> BaseGuards;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") FTransform HospitalSpawn;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") FTransform PoliceSpawn;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Police") FString DataFile = TEXT("NeonCoast/Data/traffic.json");
	/** Weapon attach offset on the right hand bone (tune if guns look wrong in the hand). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapons") FVector WeaponHandLocation = FVector(-9.f, 3.5f, 1.5f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapons") FRotator WeaponHandRotation = FRotator(0.f, 0.f, -90.f);

	/** Server: a crime by Criminal at Where. With bNeedWitness it only counts if police are close
	 *  or pedestrians saw it (they call the cops). bMinimum: raise to at least Stars. */
	static void ReportCrime(UWorld* World, AController* Criminal, int32 Stars, const FVector& Where, bool bNeedWitness,
		bool bMinimum = false, bool bPoliceOnly = false);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: police car + crew / soldiers currently in play. */
	UPROPERTY(Transient) TArray<TObjectPtr<ANCVehiclePawn>> Cars;
	UPROPERTY(Transient) TArray<TObjectPtr<ANCPoliceOfficer>> Officers;
	UPROPERTY(Transient) TArray<TObjectPtr<class ANCCraftPawn>> Helis;

protected:
	TArray<FNCPath> Lanes;
	TArray<FNCPath> PatrolWalkways;
	double NextAmbientPatrol = 0.0;
	void UpdateAmbientPatrols();
	float Accum = 0.f;
	float CalmTime = 0.f;
	bool bGuardsSpawned = false;
	struct FResponseTimers { double Car = 0.0; double Heli = 0.0; double Crime = -100.0; };
	TMap<TWeakObjectPtr<APlayerController>, FResponseTimers> ResponseTimers;
	void UpdatePlayer(class APlayerController* PC, float Dt);
	bool SpawnCarNear(APawn* Target, int32 Stars);
	ANCPoliceOfficer* SpawnOfficer(const FVector& Where, const FRotator& Rot, int32 Rank, APawn* Target);
	void DismountNear(APawn* Target);
	void Cleanup(const TArray<APawn*>& Suspects);
	bool CanSee(const AActor* From, const APawn* Target) const;
};
