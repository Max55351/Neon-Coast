#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NCHealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNCOnDeath, AActor*, Victim, AController*, Killer);

/**
 * Health + body armour for characters (player, police, soldiers). Listens to the owner's
 * OnTakeAnyDamage (point / radial damage from NCWeaponComponent, explosions, cars), armour
 * soaks two thirds of every hit. At 0 the character ragdolls (all machines) and OnDeath fires
 * on the server (the game mode respawns players at the hospital: "WASTED").
 */
UCLASS(ClassGroup = (NeonCoast), meta = (BlueprintSpawnableComponent))
class NEONCOASTRUNTIME_API UNCHealthComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UNCHealthComponent();

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Health") float Health = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health") float MaxHealth = 200.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Health") float Armor = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health") float MaxArmor = 100.f;
	UPROPERTY(ReplicatedUsing = OnRep_Dead, BlueprintReadOnly, Category = "Health") bool bDead = false;
	/** Last time (world seconds) this character was hurt – HUD flashes red. */
	UPROPERTY(BlueprintReadOnly, Category = "Health") float LastHurtTime = -100.f;

	UPROPERTY(BlueprintAssignable, Category = "Health") FNCOnDeath OnDeath;

	/** Server. */
	UFUNCTION(BlueprintCallable, Category = "Health") void Heal(float Amount);
	UFUNCTION(BlueprintCallable, Category = "Health") void AddArmor(float Amount);
	UFUNCTION(BlueprintCallable, Category = "Health") void Hurt(float Amount, AController* Instigator, const FVector& Impulse);
	UFUNCTION(BlueprintCallable, Category = "Health") void Revive();

	static UNCHealthComponent* Of(const AActor* A) { return A ? A->FindComponentByClass<UNCHealthComponent>() : nullptr; }

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnAnyDamage(AActor* DamagedActor, float Damage, const class UDamageType* DamageType,
		AController* InstigatedBy, AActor* DamageCauser);
	UFUNCTION() void OnRep_Dead();
	UFUNCTION(NetMulticast, Reliable) void MulticastHurt(float NewHealth);
	void Ragdoll(const FVector& Impulse);
	FVector PendingImpulse = FVector::ZeroVector;
};
