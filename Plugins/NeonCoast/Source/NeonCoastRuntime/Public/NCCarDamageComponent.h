#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NCCarDamageComponent.generated.h"

/**
 * Crash deformation: drives the morph targets Damage_Front / Rear / Left / Right / Roof
 * of the vehicle skeletal mesh from physics hits. Server computes, values replicate.
 */
UCLASS(ClassGroup = (NeonCoast), meta = (BlueprintSpawnableComponent))
class NEONCOASTRUNTIME_API UNCCarDamageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNCCarDamageComponent();

	/** Impulse (kg*cm/s) below which nothing deforms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage") float MinImpulse = 150000.f;
	/** Morph weight added per unit of impulse above the threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage") float Sensitivity = 1.2e-6f;
	/** Engine stops working once the front is this damaged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Damage") float EngineFailAt = 0.95f;

	/** Front, Rear, Left, Right, Roof in 0..1 */
	UPROPERTY(ReplicatedUsing = OnRep_Damage, BlueprintReadOnly, Category = "Damage")
	TArray<float> Damage;

	UFUNCTION(BlueprintCallable, Category = "Damage") void Repair();
	UFUNCTION(BlueprintCallable, Category = "Damage") void AddDamageAt(const FVector& WorldPoint, float Amount);
	UFUNCTION(BlueprintPure, Category = "Damage") float GetTotalDamage() const;

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnHit(UPrimitiveComponent* HitComp, AActor* Other, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);
	UFUNCTION() void OnRep_Damage();
	void ApplyMorphs();
	float LastHitTime = -10.f;
};
