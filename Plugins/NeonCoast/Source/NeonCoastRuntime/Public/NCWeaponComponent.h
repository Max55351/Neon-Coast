#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "NCWeaponTypes.h"
#include "NCWeaponComponent.generated.h"

class UInputAction;
class UInputMappingContext;
class UStaticMeshComponent;
struct FInputActionValue;

/**
 * Weapons for a character (player; police / soldiers use the static helpers).
 * Inventory + ammo replicate; the owning client aims with the camera and asks the server to
 * fire, the server traces (world, pedestrians, traffic), applies damage, reports the crime to the
 * police and multicasts tracers / impacts.
 *
 * Controls: LMB / RT fire, RMB / LT aim (over-the-shoulder zoom, sniper scope), R / X reload,
 * mouse wheel / LB RB switch, 1-9 weapon slots, G quick grenade.
 */
UCLASS(ClassGroup = (NeonCoast), meta = (BlueprintSpawnableComponent))
class NEONCOASTRUNTIME_API UNCWeaponComponent : public UActorComponent
{
	GENERATED_BODY()
	friend class ANCGameMode;
public:
	UNCWeaponComponent();

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Weapons") TArray<FName> Owned;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Weapons") TArray<int32> Clip;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Weapons") TArray<int32> Reserve;
	UPROPERTY(ReplicatedUsing = OnRep_Current, BlueprintReadOnly, Category = "Weapons") int32 Current = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Weapons") bool bReloading = false;

	UFUNCTION(BlueprintPure, Category = "Weapons") bool IsAiming() const { return bAiming; }
	const FNCWeaponDef& CurrentDef() const;
	int32 CurrentClip() const { return Clip.IsValidIndex(Current) ? Clip[Current] : 0; }
	int32 CurrentReserve() const { return Reserve.IsValidIndex(Current) ? Reserve[Current] : 0; }
	/** Last time a shot of ours hit something that can bleed (HUD hit marker). */
	float LastHitMarker = -100.f;

	/** Server: add a weapon (or ammo for one already owned). */
	void Give(FName Id, int32 Rounds);
	void GiveAll();
	/** Server: busted -> only fists remain. */
	void Strip();
	UFUNCTION(Server, Reliable) void ServerBuy(FName Id, bool bAmmoOnly);
	/** 24/7 food: item index into FoodMenu(), heals. */
	UFUNCTION(Server, Reliable) void ServerBuyFood(int32 Item);
	struct FFood { const TCHAR* Name; int32 Price; float Heal; };
	static const TArray<FFood>& FoodMenu();

	static UNCWeaponComponent* Of(const AActor* A) { return A ? A->FindComponentByClass<UNCWeaponComponent>() : nullptr; }

	/**
	 * Server: one hitscan bullet from From along Dir: world geometry, walking pedestrians and
	 * traffic cars (whichever is nearest). Returns the end point for the tracer.
	 */
	static FVector FireBullet(AActor* Shooter, AController* Instigator, const FVector& From, const FVector& Dir,
		float Range, float Damage, bool& bOutFlesh);
	/** Server: explosion (rockets, grenades, exploding cars): radial damage, ragdolls, traffic, FX. */
	static void Explode(UWorld* World, const FVector& Where, float Radius, float Damage, AActor* Causer, AController* Instigator,
		bool bIncendiary);
	/** Every machine: tracer + impact + muzzle flash. */
	static void ShotFX(UWorld* World, const FVector& Muzzle, const FVector& Hit, bool bFlesh);

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION(Server, Reliable) void ServerSelect(int32 Index);
	UFUNCTION(Server, Reliable) void ServerFire(FVector_NetQuantize CamLoc, FVector_NetQuantizeNormal CamDir, bool bAim, int32 Seed);
	UFUNCTION(Server, Reliable) void ServerReload();
	UFUNCTION(Server, Reliable) void ServerSetAim(bool bOn);
	UFUNCTION(NetMulticast, Unreliable) void MulticastShots(FVector_NetQuantize Muzzle, const TArray<FVector_NetQuantize>& Ends,
		const TArray<bool>& Flesh);
	UFUNCTION() void OnRep_Current();

	void SetupInput();
	void OnFirePressed(const FInputActionValue& V);
	void OnFireReleased(const FInputActionValue& V);
	void OnAimPressed(const FInputActionValue& V);
	void OnAimReleased(const FInputActionValue& V);
	void OnReload(const FInputActionValue& V);
	void OnNext(const FInputActionValue& V);
	void OnPrev(const FInputActionValue& V);
	void OnSlot(int32 Slot);
	void OnGrenade(const FInputActionValue& V);
	bool CanUseInput() const;
	void TryFireLocal();
	void Cycle(int32 Dir);
	void UpdateHeldMesh();
	void UpdateAimCamera(float Dt);
	int32 IndexOf(FName Id) const { return Owned.IndexOfByKey(Id); }
	FVector MuzzleLocation() const;
	void FinishReload();

	bool bInputReady = false;
	UPROPERTY(Replicated) bool bAiming = false;
	bool bTrigger = false;
	float NextFireTime = 0.f;
	float DefaultArm = -1.f;
	float DefaultFOV = 90.f;
	FVector DefaultSocketOffset = FVector::ZeroVector;
	bool bDefaultYaw = false;
	bool bDefaultOrient = true;
	float Recoil = 0.f;
	FTimerHandle ReloadTimer;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Held;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> Actions;
};
