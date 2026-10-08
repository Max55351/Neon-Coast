#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCTypes.h"
#include "NCTrafficManager.generated.h"

class UInstancedStaticMeshComponent;

/**
 * City traffic along the exported lanes (Content/NeonCoast/Data/traffic.json).
 * Lane following with obstacle braking, queues and local physical collision bodies.
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCTrafficManager : public AActor
{
	GENERATED_BODY()

public:
	ANCTrafficManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") FString DataFile = TEXT("NeonCoast/Data/traffic.json");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") TArray<FNCTrafficType> Types;
	/** Cars per kilometre of lane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float Density = 16.f;
	/** Only cars within this distance of a local player are moved (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float UpdateRadius = 150000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") int32 Seed = 2026;
	/** Static meshes face -Y after import from Blender -> +90 deg yaw offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float MeshYawOffset = 90.f;

	/** Server: removes the nearest traffic car and spawns its drivable version. */
	class ANCVehiclePawn* TakeVehicleNear(const FVector& Location, float Radius);
	/** Server: turns traffic car CarIndex into a real physics car (shot, rammed, carjacked). */
	class ANCVehiclePawn* TakeVehicle(int32 CarIndex);
	/** Server: first traffic car hit by the segment A->B (oriented box test). */
	bool TraceRay(const FVector& A, const FVector& B, int32& OutCar, float& OutDist) const;
	/** Server: traffic cars within R of C (explosions). */
	TArray<int32> CarsInRadius(const FVector& C, float R) const;

	/** Cheat PINKTRAFFIC: recolour every traffic car. */
	void PaintAll(const FLinearColor& Color);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	friend class ANCGameMode;
	UFUNCTION() void TrafficHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
	UFUNCTION(NetMulticast, Reliable) void MulticastRemoveCar(int32 CarIndex);

	struct FCar
	{
		int32 Lane = 0;
		float Offset = 0.f;     // distance along the lane at t = 0
		float Speed = 0.f;
		float WaitingTime = 0.f;
		float UpdateAccum = 0.f;
		int32 Type = 0;
		int32 Instance = 0;
		bool bRemoved = false;
		FTransform Last;
	};
	TArray<FNCPath> Lanes;
	TArray<FCar> Cars;
	TArray<TArray<int32>> LaneCars;
	UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> Pools;
	UPROPERTY(Transient) TArray<TObjectPtr<class UBoxComponent>> CollisionBodies;
	struct FStopLine { float Distance = 0.f; bool bInitiallyRed = false; };
	TArray<TArray<FStopLine>> StopLines;
	struct FSignalVisual { int32 Material = 0; bool bInitiallyRed = false; uint8 Lamp = 0; };
	TArray<FSignalVisual> SignalVisuals;
	UPROPERTY(Transient) TArray<TObjectPtr<class UMaterialInstanceDynamic>> SignalMaterials;
	int32 LastSignalPhase = INDEX_NONE;
	void SetupSignals();
	void UpdateSignals(double Time);
	static uint8 SignalState(bool bInitiallyRed, double Time);

	double Now() const;
	FTransform CarTransform(const FCar& C, double T) const;
	FVector RoadPosition(const FVector& Position) const;
};
