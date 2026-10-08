#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "NCAnimal.generated.h"

class UAnimSequence;
class UCapsuleComponent;
class UInstancedStaticMeshComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class ANCAnimalManager;

UENUM(BlueprintType)
enum class ENCAnimalKind : uint8
{
	Dog,
	Cat,
	Croc,
	Shark,
	Gull
};

/** One animal species (mesh, animations, stats). The importer fills these from Data/Animals. */
USTRUCT(BlueprintType)
struct FNCAnimalSpecies
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") ENCAnimalKind Kind = ENCAnimalKind::Dog;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") TObjectPtr<USkeletalMesh> Mesh = nullptr;
	/** Walk / trot (gull: flapping, shark: swimming). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") TObjectPtr<UAnimSequence> Walk = nullptr;
	/** Standing (gull: gliding). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") TObjectPtr<UAnimSequence> Idle = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") float Health = 60.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") float WalkSpeed = 110.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") float RunSpeed = 650.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") float Radius = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") float HalfHeight = 30.f;
	/** Bite damage (0 = harmless). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") float Bite = 0.f;
};

USTRUCT(BlueprintType)
struct FNCAnimalSpawn
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") int32 Species = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") FVector Location = FVector::ZeroVector;
};

/** Cosmetic swarms around a point: 0 butterflies (day), 1 fireflies (night), 2 fish school (under water). */
USTRUCT(BlueprintType)
struct FNCInsectSwarm
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") int32 Kind = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") FVector Location = FVector::ZeroVector;
};

/**
 * A live animal. The server runs its little brain (wander / idle / flee / attack) and sends
 * location + yaw + mode; clients glide the mesh towards it and pick the animation.
 *  dogs, cats: roam around home, run from gunfire, explosions and (cats) people; a dog that gets
 *              hit bites back.
 *  crocodile:  lazes in the wetland, attacks anyone on foot who comes close.
 *  shark:      circles offshore with the fin out of the water, attacks swimmers.
 *  gull:       circles over the beach, flapping and gliding.
 * Bullets, melee, explosions and cars kill them (ragdoll).
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCAnimal : public AActor
{
	GENERATED_BODY()
public:
	ANCAnimal();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animals") TObjectPtr<UCapsuleComponent> Capsule;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animals") TObjectPtr<USkeletalMeshComponent> Mesh;

	UPROPERTY(ReplicatedUsing = OnRep_Species) int32 Species = INDEX_NONE;
	/** 0 idle, 1 walk, 2 run, 3 dead. */
	UPROPERTY(ReplicatedUsing = OnRep_Mode) uint8 Mode = 0;
	UPROPERTY(Replicated) FVector_NetQuantize NetLoc;
	UPROPERTY(Replicated) float NetYaw = 0.f;
	/** The level's manager (found on clients by OnRep_Species). */
	UPROPERTY(Transient) TObjectPtr<ANCAnimalManager> Manager;

	/** Server: set up a freshly spawned animal. */
	void Init(ANCAnimalManager* InManager, int32 InSpecies, int32 InSpawn, const FVector& InHome);
	/** Server: something scary at From (gunshot, explosion). */
	void Scare(const FVector& From);
	bool IsDead() const { return Mode == 3; }
	int32 SpawnIndex = INDEX_NONE;

	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& Event, AController* Inst, AActor* Causer) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnRep_Species();
	UFUNCTION() void OnRep_Mode();
	UFUNCTION(NetMulticast, Reliable) void MulticastDie(FVector Impulse);

	const FNCAnimalSpecies* Def() const;
	ENCAnimalKind Kind() const;
	void ApplyLook();
	void UpdateAnim();
	void Die(const FVector& Impulse);

	// ---- server brain
	enum class EState : uint8 { Idle, Wander, Flee, Attack };
	EState State = EState::Idle;
	FVector Home = FVector::ZeroVector;
	FVector Goal = FVector::ZeroVector;
	FVector Threat = FVector::ZeroVector;
	float StateTime = 0.f;
	float BiteCooldown = 0.f;
	float HitCheck = 0.f;
	float Health = 60.f;
	float Angle = 0.f;          // gull / shark: position on the circle
	float Phase = 0.f;
	TWeakObjectPtr<APawn> Prey;
	void Think(float Dt);
	void ThinkGround(float Dt, const FNCAnimalSpecies& D);
	void ThinkShark(float Dt, const FNCAnimalSpecies& D);
	void ThinkGull(float Dt, const FNCAnimalSpecies& D);
	bool PickWanderGoal(float Radius);
	bool GroundZ(const FVector& At, float& OutZ) const;
	bool Blocked(const FVector& Dir, float Dist) const;
	APawn* NearestOnFootPlayer(float MaxDist, float& OutDist) const;
	void CheckRunOver();
	void TryBite(APawn* Target, float Dmg);
	void SetMode(uint8 M);
};

/**
 * Places the animals of the map (Data: layout "animals") around the players: spawned on the
 * server when a player comes within SpawnRadius, removed when everyone is far away, a killed
 * animal comes back after RespawnSeconds. Also draws the butterfly / firefly swarms (local
 * cosmetics on every machine, fireflies only at night).
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCAnimalManager : public AActor
{
	GENERATED_BODY()
public:
	ANCAnimalManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") TArray<FNCAnimalSpecies> Species;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") TArray<FNCAnimalSpawn> Spawns;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") TArray<FNCInsectSwarm> Swarms;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") TObjectPtr<UStaticMesh> ButterflyA = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") TObjectPtr<UStaticMesh> ButterflyB = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") TObjectPtr<UStaticMesh> Firefly = nullptr;
	/** Reef fish (one species per school). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") TArray<TObjectPtr<UStaticMesh>> Fish;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") float SpawnRadius = 12000.f;
	/** Sharks and gulls are seen from further away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") float WideSpawnRadius = 25000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") int32 MaxAlive = 60;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") float RespawnSeconds = 120.f;
	/** Sea level (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") float WaterZ = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animals") float InsectRadius = 6000.f;

	/** Server: scare every animal within Radius of Where. */
	static void Scare(UWorld* World, const FVector& Where, float Radius);
	/** Server: an animal was removed (killed or despawned). */
	void OnAnimalGone(int32 SpawnIndex, bool bKilled);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	TArray<TWeakObjectPtr<ANCAnimal>> Alive;
	TArray<double> NotBefore;
	float Accum = 0.f;
	void UpdateSpawns();

	// ---- insects
	UPROPERTY(Transient) TArray<TObjectPtr<UInstancedStaticMeshComponent>> InsectPools;
	TArray<int32> ActiveSwarms;
	float InsectScan = 0.f;
	bool bNight = false;
	void UpdateInsects(float Dt);
};
