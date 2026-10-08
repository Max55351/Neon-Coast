#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCPickup.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UBoxComponent;

/** Spinning pickup (weapon + ammo / health / armour) that comes back after RespawnTime. */
UCLASS()
class NEONCOASTRUNTIME_API ANCPickup : public AActor
{
	GENERATED_BODY()
public:
	ANCPickup();
	/** 0 weapon, 1 health, 2 armour, 3 sunken treasure ($2500) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup") int32 Kind = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup") FName Weapon = TEXT("Pistol");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pickup") float RespawnTime = 60.f;
	UPROPERTY(ReplicatedUsing = OnRep_Active, BlueprintReadOnly, Category = "Pickup") bool bActive = true;
	UPROPERTY(VisibleAnywhere, Category = "Pickup") TObjectPtr<USphereComponent> Sphere;
	UPROPERTY(VisibleAnywhere, Category = "Pickup") TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere, Category = "Pickup") TObjectPtr<UStaticMeshComponent> Ring;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Index, bool bSweep,
		const FHitResult& Hit);
	UFUNCTION() void OnRep_Active();
	void Reactivate();
	FTimerHandle Timer;
};

/**
 * Shop counter trigger: walking in opens the shop menu on the local player's HUD.
 * ShopType 0 = gun store (NEON ARMS), 1 = 24/7 food (heals).
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCShopZone : public AActor
{
	GENERATED_BODY()
public:
	ANCShopZone();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop") int32 ShopType = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop") FString Title = TEXT("NEON ARMS");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shop") bool bAutoOpen = true;
	UPROPERTY(VisibleAnywhere, Category = "Shop") TObjectPtr<UBoxComponent> Box;
	virtual void BeginPlay() override;
protected:
	UFUNCTION() void OnBegin(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Index, bool bSweep,
		const FHitResult& Hit);
	UFUNCTION() void OnEnd(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Index);
};
