#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCBusinessManager.generated.h"

class UMeshComponent;
class UMaterialInterface;
class ANCShopZone;
class APlayerController;

/** Existing city assets get actual entrances, counters and bounded hold-ups. */
UCLASS()
class NEONCOASTRUNTIME_API ANCBusinessManager : public AActor
{
 GENERATED_BODY()
public:
 ANCBusinessManager();
 virtual void BeginPlay() override;
 virtual void Tick(float Dt) override;
 static ANCBusinessManager* Get(const UObject* Context);
 bool Use(APawn* Pawn);
 bool StartRobbery(APlayerController* PC, const FVector& Where);
 bool NearService(const APawn* Pawn, int32 Type, float Radius = 700.f) const;
 FString Hint(const APawn* Pawn) const;
 void RepairMaterials(UMeshComponent* Mesh);
 struct FBusiness
 {
  FName Id;
  FString Title;
  int32 Type = -1;
  FVector Entrance, Inside, Counter;
  FBox Bounds;
  TWeakObjectPtr<ANCShopZone> Zone;
  double NextRobbery = 0.0;
 };
 const TArray<FBusiness>& Places() const { return Businesses; }
protected:
 void Setup();
 void RepairBatch();
 UMaterialInterface* Material(FName Name);
 TArray<FBusiness> Businesses;
 TArray<TWeakObjectPtr<UMeshComponent>> RepairQueue;
 int32 RepairIndex = 0;
 FTimerHandle SetupTimer, RepairTimer;
 UPROPERTY(Transient) TMap<FName,TObjectPtr<UMaterialInterface>> Materials;
 TMap<TWeakObjectPtr<APawn>,int32> InsidePlaces;
 struct FHoldUp { int32 Place = INDEX_NONE; float Time = 0.f; float Interrupted = 0.f; int32 Loot = 0; };
 TMap<TWeakObjectPtr<APlayerController>,FHoldUp> HoldUps;
};
