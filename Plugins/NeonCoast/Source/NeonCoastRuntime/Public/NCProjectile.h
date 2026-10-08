#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCProjectile.generated.h"

class UProjectileMovementComponent;
class UStaticMeshComponent;
class USphereComponent;
class UPointLightComponent;

/** Rocket (straight, explodes on impact), grenade (bounces, 3 s fuse) or molotov (bursts into fire). */
UCLASS()
class NEONCOASTRUNTIME_API ANCProjectile : public AActor
{
	GENERATED_BODY()
public:
	ANCProjectile();
	/** 0 rocket, 1 grenade, 2 molotov */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Projectile") uint8 Kind = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Projectile") float Damage = 400.f;
	UPROPERTY(BlueprintReadOnly, Category = "Projectile") float Radius = 800.f;
	UPROPERTY(VisibleAnywhere, Category = "Projectile") TObjectPtr<USphereComponent> Sphere;
	UPROPERTY(VisibleAnywhere, Category = "Projectile") TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere, Category = "Projectile") TObjectPtr<UProjectileMovementComponent> Move;

	/** Server: spawn + launch. */
	static ANCProjectile* Launch(UWorld* W, uint8 Kind, const FVector& From, const FVector& Velocity, float Damage, float Radius,
		AActor* Owner, APawn* Instigator);

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnHit(UPrimitiveComponent* HitComp, AActor* Other, UPrimitiveComponent* OtherComp, FVector Impulse, const FHitResult& Hit);
	void Detonate();
	bool bDone = false;
	FTimerHandle Fuse;
};

/** Bullet tracer + muzzle flash (cosmetic, local, lives a few frames). */
UCLASS()
class NEONCOASTRUNTIME_API ANCTracer : public AActor
{
	GENERATED_BODY()
public:
	ANCTracer();
	void Setup(const FVector& From, const FVector& To, bool bFlash);
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Beam;
	UPROPERTY(Transient) TObjectPtr<UPointLightComponent> Flash;
};

/** Burning ground (molotov, burning wrecks): hurts characters inside, kills pedestrians. */
UCLASS()
class NEONCOASTRUNTIME_API ANCFireArea : public AActor
{
	GENERATED_BODY()
public:
	ANCFireArea();
	UPROPERTY(Replicated) float Radius = 400.f;
	float DamagePerSecond = 30.f;
	UPROPERTY() TObjectPtr<AController> Starter;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
