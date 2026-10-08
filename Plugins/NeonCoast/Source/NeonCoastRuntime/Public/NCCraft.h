#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "Engine/NetSerialization.h"
#include "NCCraft.generated.h"

class UBoxComponent;
class USpotLightComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;
class ANCCraftManager;

UENUM(BlueprintType)
enum class ENCCraftKind : uint8
{
	Boat,
	Sail,
	JetSki,
	Plane,
	Heli,
	Sub,
	Tank
};

/** One kind of drivable craft (filled by the import script from Data/craft.json). */
USTRUCT(BlueprintType)
struct FNCCraftType
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") ENCCraftKind Kind = ENCCraftKind::Boat;
	/** Hull (origin on the keel, bow along -Y as imported from Blender). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") TObjectPtr<UStaticMesh> Hull = nullptr;
	/** Moving part (sail + boom) and its pivot in hull-mesh space (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") TObjectPtr<UStaticMesh> Part = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") FVector PartPivot = FVector::ZeroVector;
	/** Spin / swing axis of the part in mesh space: 0 yaw (Z), 1 pitch (Y), 2 roll (X). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") int32 PartAxis = 0;
	/** Second part (tail rotor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") TObjectPtr<UStaticMesh> Part2 = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") FVector Part2Pivot = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") int32 Part2Axis = 2;
	/** Aircraft: flying speed (cm/s) below which the wings stop lifting, and the height of the box (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float StallSpeed = 2600.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Height = 300.f;
	/** cm/s */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float MaxSpeed = 2500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Reverse = 400.f;
	/** Seconds from 0 to about 100 km/h (bigger = lazier). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Accel = 6.f;
	/** deg/s at full rudder. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float TurnRate = 50.f;
	/** Hull size and draft (keel below the waterline), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Length = 1000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Width = 280.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Draft = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Deck = 120.f;
	/** Seats in craft space (cm, X forward, Y right, Z up from the keel); [0] = helm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") TArray<FVector> Seats;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float CameraDistance = 1000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Health = 700.f;
};

/** Compact movement state sent by whoever drives (owner client / server). */
USTRUCT()
struct FNCCraftNet
{
	GENERATED_BODY()
	UPROPERTY() FVector_NetQuantize10 Loc;
	UPROPERTY() FVector_NetQuantize10 Vel;
	UPROPERTY() float Yaw = 0.f;
	UPROPERTY() float YawRate = 0.f;
	UPROPERTY() float VZ = 0.f;
	/** Sail angle / propeller / rotor. */
	UPROPERTY() float Aux = 0.f;
	UPROPERTY() float Pitch = 0.f;
	UPROPERTY() float Roll = 0.f;
	UPROPERTY() uint8 bAir : 1;
	FNCCraftNet() : bAir(false) {}
};

/**
 * Drivable boat / jet ski / sailboat riding the ANCWaterManager waves (same wave function as the
 * ocean material). Arcade handling: thrust + drag, lateral grip, rudder that needs speed,
 * planing (bow up), heel into turns, jumps off wave crests; sailboats sail by the wind.
 * Multiplayer: the driver's machine simulates and sends its state (ServerMove), others
 * interpolate; the heave / pitch / roll is computed from the waves everywhere.
 * Enter / leave with F; W/S throttle (sailboat: W sheets the sail in), A/D rudder, Shift boost
 * (sailboat: motor), mouse camera.
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCCraftPawn : public APawn
{
	GENERATED_BODY()
public:
	ANCCraftPawn();
	void RequestExit() { ServerExit(); }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Craft") TObjectPtr<UBoxComponent> Box;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Craft") TObjectPtr<UStaticMeshComponent> Hull;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Craft") TObjectPtr<UStaticMeshComponent> Part;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Craft") TObjectPtr<UStaticMeshComponent> Part2;
	/** Police helicopter searchlight. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Craft") TObjectPtr<USpotLightComponent> Searchlight;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Craft") TObjectPtr<USpringArmComponent> SpringArm;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Craft") TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(ReplicatedUsing = OnRep_Type) int32 TypeIndex = INDEX_NONE;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Craft") TObjectPtr<APawn> Driver;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Craft") TArray<TObjectPtr<APawn>> Passengers;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Craft") float Health = 700.f;
	UPROPERTY(ReplicatedUsing = OnRep_Wrecked, BlueprintReadOnly, Category = "Craft") bool bWrecked = false;
	/** Sea traffic: route index in the manager (-1 = free). Moves by the server clock everywhere. */
	UPROPERTY(Replicated) int32 Route = INDEX_NONE;
	UPROPERTY(Replicated) FNCCraftNet Net;
	/** AI police helicopter: the suspect it hunts (null = heads home and disappears). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Craft") TObjectPtr<APawn> ChaseTarget;
	bool bPoliceAI = false;
	/** Server: the player hunted (the target pawn changes when they switch cars). */
	TWeakObjectPtr<AController> ChaseController;
	FVector PoliceSearchLocation = FVector::ZeroVector;
	bool bPoliceSearching = false;
	UPROPERTY(Transient) TObjectPtr<ANCCraftManager> Manager;
	int32 SpawnIndex = INDEX_NONE;

	const FNCCraftType* Type() const;
	void Init(ANCCraftManager* InManager, int32 InType, int32 InSpawn, int32 InRoute);

	/** Server: character takes the helm / a seat. */
	bool EnterCraft(APawn* Character);
	void RemoveLocalControls(class APlayerController* PC);
	bool EnterAsPassenger(APawn* Character);
	void ExitPassenger(APawn* Character);
	/** Speed over water, km/h (HUD). */
	UFUNCTION(BlueprintPure, Category = "Craft") float SpeedKmh() const { return Vel.Size2D() * 0.036f; }
	/** Sailboat: 0..1 how well the sail draws (HUD). */
	float SailEfficiency = 0.f;
	float WindAngle = 0.f;
	float Throttle = 0.f;
	/** Aircraft: engine power 0..1 (plane throttle lever / rotor speed). */
	float Power = 0.f;
	bool IsAir() const;
	/** Moves freely in Z (aircraft, submarine): replicates height, pitch and roll. */
	bool FreeZ() const;
	/** Height above the ground under the craft (cm, HUD altimeter). */
	float Altitude = 0.f;


	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual float TakeDamage(float Damage, const FDamageEvent& Event, AController* Inst, AActor* Causer) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnRep_Type();
	UFUNCTION() void OnRep_Wrecked();
	UFUNCTION(Server, Unreliable) void ServerMove(FNCCraftNet State);
	UFUNCTION(Server, Reliable) void ServerExit();
	void ApplyType();
	void ExitCraft(APawn* Who, bool bDriverSeat);
	bool ExitSpot(APawn* Who, FVector& Out) const;
	void AdvanceTick(float Dt);

	// ---- movement
	FVector Vel = FVector::ZeroVector;
	float YawRate = 0.f;
	float VZ = 0.f;
	bool bAir = false;
	float Pitch = 0.f;
	float Roll = 0.f;
	float LastSurface = 0.f;
	float Aux = 0.f;
	float SendTimer = 0.f;
	float WreckTime = 0.f;
	float LookIdle = 10.f;
	float Steer = 0.f;
	bool bBoost = false;
	void Simulate(float Dt, float InThrottle, float InSteer, bool bInBoost);
	void SimulatePlane(float Dt, float InThrottle, float InRoll, float InPitch, float InYaw);
	void SimulateHeli(float Dt, float InForward, float InTurn, float InRise, float InSide);
	void ThinkPolice(float Dt);
	void SimulateSub(float Dt, float InThrottle, float InSteer, float InRise);
	void SimulateTank(float Dt, float InThrottle, float InSteer);
	UFUNCTION(Server, Reliable) void ServerFireGun(FVector_NetQuantizeNormal Dir);
	void OnFire(const FInputActionValue& V);
	void UpdateAudio(float Dt);
	UPROPERTY(Transient) TObjectPtr<class UAudioComponent> EngineSound;
	bool bWasAir = false;
	float TrackDist = 0.f;
	float GunCooldown = 0.f;
	float AimPitch = 0.f;
	/** Air: sweep-move, land on wheels / skids, crash when it hits hard. */
	void MoveAir(float Dt, const FQuat& Q, float Crash);
	bool GroundBelow(float& OutZ) const;
	float Aux2 = 0.f;
	float Rise = 0.f;
	float YawIn = 0.f;
	float OrbitAngle = 0.f;
	float FireTimer = 2.f;
	float LeaveTime = 0.f;
	void SelfDamage(float Amount);
	UFUNCTION(Server, Reliable) void ServerSelfDamage(float Amount);
	UFUNCTION(NetMulticast, Unreliable) void MulticastShot(FVector_NetQuantize From, FVector_NetQuantize To, bool bFlesh);
	void Heave(float Dt, float Fwd);
	void FollowNet(float Dt);
	void FollowRoute(float Dt);
	void Pack(FNCCraftNet& Out) const;

	// ---- input
	void CreateInput();
	void OnThrottle(const FInputActionValue& V) { Throttle = V.Get<float>(); }
	void OnSteer(const FInputActionValue& V) { Steer = V.Get<float>(); }
	void OnBoost(const FInputActionValue& V) { bBoost = V.Get<bool>(); }
	void OnRise(const FInputActionValue& V) { Rise = V.Get<float>(); }
	void OnYaw(const FInputActionValue& V) { YawIn = V.Get<float>(); }
	void OnLook(const FInputActionValue& V);
	void OnExit(const FInputActionValue& V) { ServerExit(); }
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SteerAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> BoostAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RiseAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> YawAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> FireAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ExitAction;
};

/** A place where a craft waits (marina berth, beach) - world cm, bow heading in UE yaw. */
USTRUCT(BlueprintType)
struct FNCCraftSpawn
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") int32 Type = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") FVector Location = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Yaw = 0.f;
};

/** Sea traffic: a boat going round a circle (Radius 0 = riding at anchor). */
USTRUCT(BlueprintType)
struct FNCSeaRoute
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") int32 Type = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") FVector Center = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Radius = 0.f;
	/** cm/s */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Speed = 800.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Phase = 0.f;
	/** +1 counter-clockwise, -1 clockwise (seen from above). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") int32 Dir = 1;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float Yaw = 0.f;

	/** Position + heading at time T. */
	void Eval(double T, FVector& OutLoc, float& OutYaw) const;
};

/**
 * Server: spawns every drivable craft at its berth and the sea traffic boats, puts them back
 * when they sank or were left far away (nobody within RespawnDistance). Also holds the wind.
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCCraftManager : public AActor
{
	GENERATED_BODY()
public:
	ANCCraftManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") TArray<FNCCraftType> Types;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") TArray<FNCCraftSpawn> Spawns;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") TArray<FNCSeaRoute> Routes;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Craft") float RespawnDistance = 20000.f;
	/** Wind the sailboats use: direction it blows TOWARDS (UE yaw) and strength 0..2. */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Craft") float WindYaw = 40.f;
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Craft") float WindStrength = 1.f;

	static ANCCraftManager* Get(const UWorld* World);
	/** Server: a police helicopter comes for Target (3+ stars). */
	ANCCraftPawn* SpawnPoliceHeli(APawn* Target);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(Transient) TArray<TObjectPtr<ANCCraftPawn>> SpawnCraft;
	UPROPERTY(Transient) TArray<TObjectPtr<ANCCraftPawn>> RouteCraft;
	TArray<double> GoneSince;
	float Accum = 0.f;
	ANCCraftPawn* SpawnOne(int32 Type, const FVector& Loc, float Yaw, int32 Spawn, int32 Route);
};

/** Air traffic: an aircraft flying a closed loop; points are (x, y, z cm, time s). */
USTRUCT(BlueprintType)
struct FNCAirRoute
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Air") TObjectPtr<UStaticMesh> Mesh = nullptr;
	/** Craft type for rotors / propeller (-1 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Air") int32 CraftType = INDEX_NONE;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Air") TArray<FVector4> Points;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Air") float Period = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Air") float Offset = 0.f;
};

/**
 * Airliners doing touch-and-go circuits at the airport, a light plane round the coast,
 * helicopters over downtown and the resort. Pure function of the server clock (like the road
 * traffic), drawn locally on every machine - nothing is replicated.
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCAirTraffic : public AActor
{
	GENERATED_BODY()
public:
	ANCAirTraffic();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Air") TArray<FNCAirRoute> Routes;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Bodies;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Rotors;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> TailRotors;
	TArray<float> LastYaw;
	float Spin = 0.f;
	static FVector Eval(const FNCAirRoute& R, double T);
};
