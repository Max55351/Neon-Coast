#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCTypes.h"
#include "NCCrowdManager.generated.h"

class UInstancedStaticMeshComponent;
class USkeletalMeshComponent;

/**
 * Pedestrians walking the exported sidewalks (traffic.json "sidewalks").
 * Positions are a function of server time (identical for all players).
 * Near the camera they are animated skeletal meshes (pooled), further away cheap
 * static instances in the walk pose, beyond FarRadius they are not updated.
 * Panic (shots, explosions, killings): the server multicasts where and when; everyone within
 * the radius runs away along the sidewalk, then walks on - still a pure function of time, so
 * every machine agrees without replicating positions.
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCCrowdManager : public AActor
{
	GENERATED_BODY()

public:
	ANCCrowdManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") FString DataFile = TEXT("NeonCoast/Data/traffic.json");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") TArray<FNCPedestrianType> Types;
	/** Pedestrians per 100 m of sidewalk. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") float Density = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") float WalkSpeed = 130.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") float NearRadius = 6000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") float FarRadius = 40000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") int32 MaxAnimated = 80;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") int32 Seed = 7;
	/** Static crowd meshes face -Y after import (+90), skeletal meshes face +X (0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") float StaticYawOffset = 90.f;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Server: first pedestrian hit by the segment A->B (1 m wide body), distance along the ray. */
	bool TraceRay(const FVector& A, const FVector& B, int32& OutPed, float& OutDist) const;
	/** Server: pedestrians standing within R of C (explosions, cars running people over). */
	TArray<int32> PedsInRadius(const FVector& C, float R) const;
	/** A nearby living witness must have an unobstructed view of the crime. */
	bool HasWitness(const FVector& Where, AActor* Ignore = nullptr) const;
	/** Server: kills a pedestrian (ragdoll on every machine). */
	void KillPed(int32 Index, const FVector& Impulse);
	FVector PedLocation(int32 Index) const { return Peds.IsValidIndex(Index) ? Peds[Index].Loc : FVector::ZeroVector; }
	/** Max ragdoll bodies kept around. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") int32 MaxBodies = 30;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") float RunSpeed = 480.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") float FleeSeconds = 11.f;

	/** Server: something scary happened at Where (gunshot, explosion, a killing). Pedestrians within
	 *  Radius flee, animals run or attack. Cheap to call often (throttled). */
	static void PanicAt(UWorld* World, const FVector& Where, float Radius);
	/** Is anyone running from a recent panic near Where (police use it as "witnesses called it in"). */
	bool IsPanicking(int32 Index) const;
	/** Local: the pedestrian currently saying something next to the player (HUD speech bubble). */
	bool GetBark(FVector& OutLoc, FString& OutText) const;
	bool AddInteriorResidents(FName Place,const FBox& Bounds,const FVector& Counter);


protected:
	friend class ANCGameMode;
	struct FFallingBody { TWeakObjectPtr<USkeletalMeshComponent> Mesh; FVector Start; FVector End; float Yaw = 0.f; float Age = 0.f; };
	TArray<FFallingBody> FallingBodies;
	UFUNCTION(NetMulticast, Reliable) void MulticastKillPed(int32 Index, FVector Impulse);
	UFUNCTION(NetMulticast, Reliable) void MulticastPanic(FVector Where, float Radius, double T0);
	/** Server: recent panics (xyz, w = time) so a minigun does not send one RPC per bullet. */
	TArray<FVector4> RecentPanics;
	UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> Bodies;
	double NowT() const;

	struct FPed
	{
		int32 Path = 0;
		double Offset = 0.0;        // unfolded ping-pong coordinate at T = 0
		float Speed = 130.f;
		float Run = 480.f;
		// flight: U(t) = FleeU0 + FleeSign * Run * (t - FleeStart) until FleeEnd
		double FleeStart = 0.0;
		double FleeEnd = 0.0;
		double FleeU0 = 0.0;
		int32 FleeSign = 1;
		bool bRunAnim = false;
		int32 Type = 0;
		int32 Instance = INDEX_NONE;
		int32 Slot = INDEX_NONE;     // pooled skeletal component when near
		FVector Loc = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		bool bDead = false;
		bool bInterior = false;
		TWeakObjectPtr<class UCapsuleComponent> InteriorBody;
	};
	TArray<FNCPath> Paths;
	TArray<FPed> Peds;
	TSet<FName> PopulatedInteriors;
	UPROPERTY(Transient) TArray<TObjectPtr<class UCapsuleComponent>> InteriorBodies;
	UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> Pools;
	UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> Slots;
	TArray<int32> SlotOwner;
	float ReassignTimer = 0.f;

	void Evaluate(FPed& P, double T) const;
	void UpdateBarks(float Dt, double T);
	int32 BarkPed = INDEX_NONE;
	FString BarkText;
	double BarkUntil = 0.0;
	float NextBark = 4.f;
	/** Unfolded path coordinate at T (+ direction of travel along it). Ends a finished flight. */
	double UAt(FPed& P, double T, int32& OutSign) const;
	void Place(const FNCPath& Path, double U, bool bReverse, FVector& OutLoc, FVector& OutDir) const;
	bool Fleeing(const FPed& P, double T) const { return P.FleeEnd > T && T >= P.FleeStart; }
};
