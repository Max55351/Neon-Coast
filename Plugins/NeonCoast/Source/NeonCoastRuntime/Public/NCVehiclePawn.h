#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "NCTypes.h"
#include "NCTuning.h"
#include "WheeledVehiclePawn.h"
#include "NCVehiclePawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
class UNCCarDamageComponent;
class UChaosWheeledVehicleMovementComponent;
class UParticleSystem;
class UParticleSystemComponent;
class UMaterialInstanceDynamic;

/**
 * Drivable Neon Coast car (Chaos Vehicles). Works in listen-server multiplayer:
 * inputs are sent by the Chaos movement component, possession is server-side.
 * Skeletal mesh must have bones root + wheel_fl / wheel_fr / wheel_rl / wheel_rr
 * (the exported SK_NC2_Car_* meshes do) and should face +X.
 */
/** Replicated physical state of the car body (doors, lids, wheels, glass, fire). */
USTRUCT(BlueprintType)
struct FNCVehicleBodyState
{
	GENERATED_BODY()
	friend class ANCGameMode;

	/** bit i = part i open (door_fl, door_fr, door_rl, door_rr, hood, trunk) */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle") uint8 OpenMask = 0;
	/** bit i = part i torn off */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle") uint8 DetachedMask = 0;
	/** bit i = wheel i (fl, fr, rl, rr) torn off */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle") uint8 WheelsDetachedMask = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle") bool bGlassBroken = false;
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle") bool bOnFire = false;
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle") bool bWrecked = false;
	/** bit i = tyre i (fl, fr, rl, rr) burst: the car sits down on that corner and slides */
	UPROPERTY(BlueprintReadOnly, Category = "Vehicle") uint8 FlatMask = 0;
};

UCLASS(Blueprintable)
class NEONCOASTRUNTIME_API ANCVehiclePawn : public AWheeledVehiclePawn
{
	GENERATED_BODY()
	friend class ANCGameMode;

public:
	ANCVehiclePawn(const FObjectInitializer& ObjectInitializer);

	/** Skeletal mesh of this model (set by the import script / in the Blueprint). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NeonCoast")
	TObjectPtr<USkeletalMesh> VehicleMesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NeonCoast")
	FNCVehicleSpec Spec;

	/** Drift: live score of the running drift (local driver), combo x1..x5, slip angle in degrees. */
	UPROPERTY(BlueprintReadOnly, Category = "Drift") float DriftPoints = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Drift") int32 DriftCombo = 1;
	UPROPERTY(BlueprintReadOnly, Category = "Drift") float DriftAngle = 0.f;
	/** Handbrake + full throttle at standstill: rear tyres spin and smoke, release = launch. */
	UPROPERTY(BlueprintReadOnly, Category = "Drift") bool bBurnout = false;
	/** Tyre wear 0..255 per wheel (fl, fr, rl, rr): burning rubber wears them out, 255 = burst. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Drift") TArray<uint8> TyreWear;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drift") TObjectPtr<UParticleSystem> TireSmokeFX;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Drift") TObjectPtr<UMaterialInterface> SkidDecal;

	/** Static mesh used by the traffic system for this model (for returning a car to traffic). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NeonCoast")
	TObjectPtr<UStaticMesh> TrafficMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NeonCoast")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NeonCoast")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NeonCoast")
	TObjectPtr<UNCCarDamageComponent> Damage;

	/** Character that entered the car (hidden while driving). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "NeonCoast")
	TObjectPtr<APawn> Driver;

	/** Passengers (multiplayer): index = seat - 1. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "NeonCoast")
	TArray<TObjectPtr<APawn>> Passengers;

	/** Seat positions in vehicle space (cm, X forward, Y right); [0] = driver. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NeonCoast")
	TArray<FVector> Seats;

	/** Detachable parts: door_fl, door_fr, door_rl, door_rr, hood, trunk, wheel -> static mesh debris. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NeonCoast")
	TMap<FName, TObjectPtr<UStaticMesh>> PartMeshes;

	/** Optional particle systems (Starter Content paths are tried if empty). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NeonCoast") TObjectPtr<UParticleSystem> FireFX;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NeonCoast") TObjectPtr<UParticleSystem> ExplosionFX;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "NeonCoast") TObjectPtr<UParticleSystem> GlassFX;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NeonCoast")
	TObjectPtr<UCameraComponent> InteriorCamera;

	UPROPERTY(ReplicatedUsing = OnRep_Body, BlueprintReadOnly, Category = "NeonCoast")
	FNCVehicleBodyState Body;

	/** Seconds a burning car keeps burning before it explodes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeonCoast") float BurnTime = 12.f;

	/** Tuning parts available for this model (import script) and the replicated tuning state. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tuning") FNCTuningCatalog TuningCatalog;
	/** Material slot that carries the body paint (e.g. M_NC2_Car_Red). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tuning") FName PaintSlot;
	UPROPERTY(ReplicatedUsing = OnRep_Tuning, BlueprintReadOnly, Category = "Tuning") FNCTuning Tuning;

	/** Client: show a tuning combination without buying it. */
	void PreviewTuning(const FNCTuning& T) { ApplyTuning(T); }
	UFUNCTION(Server, Reliable) void ServerBuyTuning(const FNCTuning& NewTuning);
	/** Server: set tuning for free (cheats, mission rewards). */
	void SetTuningFree(const FNCTuning& NewTuning);
	/** Server: full repair (morphs, glass, fire). */
	UFUNCTION(BlueprintCallable, Category = "NeonCoast") void RepairAll();
	/** Anything to fix (worn / burst tyres, glass, lost parts, dents)? + price at the workshop. */
	bool NeedsRepair() const;
	int32 RepairCost() const;
	UFUNCTION(Server, Reliable) void ServerRepairPaid(int32 Cost);

	/** Server: seat a character as passenger. Returns false if the car is full. */
	bool EnterAsPassenger(APawn* Character);
	/** Server: remove a passenger (F while riding). */
	void ExitPassenger(APawn* Character);

	/** Server-side body damage hooks (called by UNCCarDamageComponent). */
	void OnBodyImpact(const FVector& LocalPoint, const FVector& WorldPoint, float Strength);
	UFUNCTION(BlueprintCallable, Category = "NeonCoast") void SetPartOpen(int32 Part, bool bOpen);
	UFUNCTION(BlueprintCallable, Category = "NeonCoast") void DetachPart(int32 Part, const FVector& Impulse);
	UFUNCTION(BlueprintCallable, Category = "NeonCoast") void DetachWheel(int32 Wheel, const FVector& Impulse);
	UFUNCTION(BlueprintCallable, Category = "NeonCoast") void BreakGlass();
	UFUNCTION(BlueprintCallable, Category = "NeonCoast") void Ignite();
	/** Server: blow the car up right now (cheats, explosives). */
	UFUNCTION(BlueprintCallable, Category = "NeonCoast") void BlowUp() { Explode(); }

	/** Bullets / explosions: tyres pop when shot, glass breaks, a shot-up engine catches fire. */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator,
		AActor* DamageCauser) override;
	/** Police lights + flashing (replicated). */
	UFUNCTION(BlueprintCallable, Category = "NeonCoast") void SetSiren(bool bOn);
	UPROPERTY(ReplicatedUsing = OnRep_Siren, BlueprintReadOnly, Category = "NeonCoast") bool bSiren = false;

	static const FName PartBones[6];
	void RequestExit() { ServerExit(); }

	/** 4x4 low range: more torque, low top speed (pickups only). Toggle: L / D-pad down. */
	UPROPERTY(ReplicatedUsing = OnRep_Drive, BlueprintReadOnly, Category = "NeonCoast")
	bool bLowRange = false;

	/** Locked centre differential (50:50 split, no traction control). Toggle: K / D-pad up. */
	UPROPERTY(ReplicatedUsing = OnRep_Drive, BlueprintReadOnly, Category = "NeonCoast")
	bool bDiffLock = false;

	UFUNCTION(BlueprintCallable, Category = "NeonCoast")
	void ApplySpec();

	UFUNCTION(BlueprintPure, Category = "NeonCoast")
	float GetSpeedKmh() const;

	/** Server: seat a character as driver (called by UNCInteractionComponent). */
	void EnterVehicle(APawn* Character);
	void RemoveLocalControls(class APlayerController* PC);

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PreRegisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UChaosWheeledVehicleMovementComponent* Move() const;

	void CreateInput();
	void OnThrottle(const FInputActionValue& V);
	void OnBrake(const FInputActionValue& V);
	void OnSteer(const FInputActionValue& V);
	void OnHandbrake(const FInputActionValue& V);
	void OnLook(const FInputActionValue& V);
	void OnExit(const FInputActionValue& V);
	void OnLowRange(const FInputActionValue& V);
	void OnDiffLock(const FInputActionValue& V);
	virtual void OnReset(const FInputActionValue& V);

	UFUNCTION(Server, Reliable) void ServerExit();
	UFUNCTION(Server, Reliable) void ServerToggleDoor();
	void OnToggleCamera(const FInputActionValue& V);
	void OnToggleDoor(const FInputActionValue& V);
	UFUNCTION() void OnRep_Body();
	UFUNCTION() void OnRep_Tuning();
	void ApplyTuning(const FNCTuning& T);
	float EffectiveTorque() const;
	void OnMenuUp(const FInputActionValue& V);
	void OnMenuDown(const FInputActionValue& V);
	void OnMenuLeft(const FInputActionValue& V);
	void OnMenuRight(const FInputActionValue& V);
	void OnMenuAccept(const FInputActionValue& V);
	void OnMenuBack(const FInputActionValue& V);
	class ANCHUD* MenuHud() const;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> TuneComps;
	UPROPERTY(Transient) TObjectPtr<class URectLightComponent> NeonLight;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MenuUpAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MenuDownAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MenuLeftAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MenuRightAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MenuAcceptAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MenuBackAction;
	void ApplyBodyVisuals();
	void Explode();
	void SpawnDebris(FName Bone, UStaticMesh* Mesh, const FVector& Impulse);
	FTimerHandle DoorTimer;
	FTimerHandle FireTimer;
	FNCVehicleBodyState AppliedBody;
	UPROPERTY(Transient) TObjectPtr<UParticleSystemComponent> FireComp;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> GlassMIDs;
	UPROPERTY(Transient) TObjectPtr<UInputAction> CameraAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> DoorAction;
	UFUNCTION(Server, Reliable) void ServerToggleLowRange();
	UFUNCTION(Server, Reliable) void ServerToggleDiffLock();
	UFUNCTION(Server, Reliable) void ServerReset();
	UFUNCTION() void OnRep_Drive();

	void UpdateSurfaceGrip();

	// drift / burnout / tyre marks / air control (NCVehiclePawn.cpp, "drift" section)
	void InitDriftFX();
	void UpdateDrift(float Dt);
	void DropSkid(int32 Wheel, bool bSlip);
	UFUNCTION(Server, Reliable) void ServerBankDrift(int32 Points);
	UFUNCTION(NetMulticast, Unreliable) void MulticastTyrePop(int32 Wheel);
	void ApplyFlatTyres();
	float WearF[4] = {0.f, 0.f, 0.f, 0.f};
	float LaunchSpinT = 1.f;
	float EngineDamage = 0.f;
	float ShotDamage = 0.f;
	float RunOverTimer = 0.f;
	FVector LastRunOverLocation = FVector::ZeroVector;
	bool bRunOverLocationValid = false;
	UPROPERTY(Transient) TObjectPtr<AController> LastDamager;
	UFUNCTION() void OnRep_Siren();
	UPROPERTY(Transient) TArray<TObjectPtr<class UPointLightComponent>> SirenLights;
	void UpdateSirenAndRunOver(float Dt);
	float BaseFriction[4] = {3.f, 3.f, 3.f, 3.f};
	float DriftCalm = 0.f;
	float DriftTime = 0.f;
	float AirTime = 0.f;
	float BurnoutTime = 0.f;
	float LaunchBoost = 0.f;
	float LastSpeed = 0.f;
	FVector LastSkidPos[4];
	float LastSkidTime[4] = {0.f, 0.f, 0.f, 0.f};
	UPROPERTY(Transient) TArray<TObjectPtr<UParticleSystemComponent>> SmokeComps;
	UPROPERTY(Transient) TArray<TObjectPtr<class UDecalComponent>> Skids;

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ThrottleAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> BrakeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> SteerAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> HandbrakeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LookAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ExitAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LowRangeAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> DiffLockAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> ResetAction;

	float SurfaceTimer = 0.f;
	void UpdateAudio(float Dt);
	UPROPERTY(Transient) TObjectPtr<class UAudioComponent> EngineSound;
	UPROPERTY(Transient) TObjectPtr<class UAudioComponent> ScreechSound;
	UPROPERTY(Transient) TObjectPtr<class UAudioComponent> SirenSound;
	bool bExplodeHeard = false;
public:
	/** Fuel 0..100 %. Full throttle for ~15 minutes empties the tank; refuel at NEON FUEL. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "NeonCoast") float Fuel = 100.f;
};
