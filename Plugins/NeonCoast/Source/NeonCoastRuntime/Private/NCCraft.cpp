#pragma warning(disable: 4456 4457 4458 4459)
#include "NCCraft.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DamageEvents.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "NCPlayerState.h"
#include "NCPoliceManager.h"
#include "NCSound.h"
#include "Components/AudioComponent.h"
#include "NCProjectile.h"
#include "NCWater.h"
#include "NCWeaponComponent.h"
#include "NCInteractionComponent.h"
#include "Net/UnrealNetwork.h"
#include "Particles/ParticleSystem.h"

namespace
{
	float CraftYawOf(const AActor* A) { return A->GetActorRotation().Yaw; }

	/** Distance from the local camera (clients) - far craft skip their wave work. */
	float LocalCameraDist(const UWorld* W, const FVector& P)
	{
		const APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (!PC || !PC->PlayerCameraManager)
		{
			return 0.f;
		}
		return FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), P);
	}
}

// ====================================================================== route
void FNCSeaRoute::Eval(double T, FVector& OutLoc, float& OutYaw) const
{
	if (Radius < 1.f)
	{
		OutLoc = Center;
		OutYaw = Yaw + 25.f * FMath::Sin(static_cast<float>(FMath::Fmod(T * 0.05 + Phase, 2.0 * PI)));   // swinging at anchor
		return;
	}
	const double Ang = Phase + Dir * static_cast<double>(Speed) / Radius * T;
	OutLoc = Center + FVector(FMath::Cos(Ang) * Radius, FMath::Sin(Ang) * Radius, 0.0);
	const FVector Tan(-FMath::Sin(Ang) * Dir, FMath::Cos(Ang) * Dir, 0.0);
	OutYaw = Tan.Rotation().Yaw;
}

// ====================================================================== craft
ANCCraftPawn::ANCCraftPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(20.f);
	AutoPossessAI = EAutoPossessAI::Disabled;
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->InitBoxExtent(FVector(450.f, 130.f, 60.f));
	Box->SetCollisionObjectType(ECC_Vehicle);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	RootComponent = Box;
	Hull = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull"));
	Hull->SetupAttachment(Box);
	Hull->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));                // Blender +Y bow -> +X
	Hull->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Hull->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Hull->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
	Part = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Part"));
	Part->SetupAttachment(Hull);
	Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Part->SetCollisionResponseToAllChannels(ECR_Ignore);
	Part->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Part2 = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Part2"));
	Part2->SetupAttachment(Hull);
	Part2->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Searchlight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Searchlight"));
	Searchlight->SetupAttachment(Box);
	Searchlight->SetIntensityUnits(ELightUnits::Candelas);
	Searchlight->SetIntensity(400000.f);
	Searchlight->SetAttenuationRadius(12000.f);
	Searchlight->SetOuterConeAngle(9.f);
	Searchlight->SetInnerConeAngle(5.f);
	Searchlight->SetCastShadows(false);
	Searchlight->SetVisibility(false);
	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(Box);
	SpringArm->TargetArmLength = 1000.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 250.f);
	SpringArm->SetRelativeRotation(FRotator(-12.f, 0.f, 0.f));
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritRoll = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 6.f;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 5.f;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
}

void ANCCraftPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANCCraftPawn, TypeIndex);
	DOREPLIFETIME(ANCCraftPawn, Driver);
	DOREPLIFETIME(ANCCraftPawn, Passengers);
	DOREPLIFETIME(ANCCraftPawn, Health);
	DOREPLIFETIME(ANCCraftPawn, bWrecked);
	DOREPLIFETIME(ANCCraftPawn, Route);
	DOREPLIFETIME(ANCCraftPawn, ChaseTarget);
	DOREPLIFETIME_CONDITION(ANCCraftPawn, Net, COND_SkipOwner);
}

const FNCCraftType* ANCCraftPawn::Type() const
{
	return Manager && Manager->Types.IsValidIndex(TypeIndex) ? &Manager->Types[TypeIndex] : nullptr;
}

bool ANCCraftPawn::IsAir() const
{
	const FNCCraftType* D = Type();
	return D && (D->Kind == ENCCraftKind::Plane || D->Kind == ENCCraftKind::Heli);
}

bool ANCCraftPawn::FreeZ() const
{
	const FNCCraftType* D = Type();
	return D && (IsAir() || D->Kind == ENCCraftKind::Sub || D->Kind == ENCCraftKind::Tank);
}

void ANCCraftPawn::Init(ANCCraftManager* InManager, int32 InType, int32 InSpawn, int32 InRoute)
{
	Manager = InManager;
	TypeIndex = InType;
	SpawnIndex = InSpawn;
	Route = InRoute;
	if (const FNCCraftType* D = Type())
	{
		Health = D->Health;
	}
	ApplyType();
	Pack(Net);
}

void ANCCraftPawn::OnRep_Type()
{
	if (!Manager)
	{
		Manager = ANCCraftManager::Get(GetWorld());
	}
	ApplyType();
}

void ANCCraftPawn::ApplyType()
{
	const FNCCraftType* D = Type();
	if (!D)
	{
		return;
	}
	// Enforce these on existing Blueprint instances, not only new class defaults.
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
	Box->SetCollisionResponseToChannel(ECC_Vehicle,ECR_Block);
	if (IsAir() || D->Kind == ENCCraftKind::Tank)
	{
		// origin under the wheels / skids / tracks; the box floats 15 cm above them
		Box->SetBoxExtent(FVector(D->Length * 0.47f, D->Width * 0.47f, D->Height * 0.5f));
		Hull->SetRelativeLocation(FVector(0.f, 0.f, -D->Height * 0.5f - 15.f));
	}
	else
	{
		Box->SetBoxExtent(FVector(D->Length * 0.47f, D->Width * 0.45f, D->Deck * 0.5f));
		Hull->SetRelativeLocation(FVector(0.f, 0.f, -D->Deck * 0.5f));     // origin on the keel
	}
	Hull->SetStaticMesh(D->Hull);
	// Blueprint overrides from a previous craft must not replace the new hull's slots.
	Hull->EmptyOverrideMaterials();
	Hull->bDisallowNanite = true; // Boats mix opaque decks with translucent windows/pool water.
	Part->SetStaticMesh(D->Part);
	Part->EmptyOverrideMaterials();
	Part->bDisallowNanite = true;
	Part->SetRelativeLocation(D->PartPivot);
	Part->SetVisibility(D->Part != nullptr);
	Part2->SetStaticMesh(D->Part2);
	Part2->EmptyOverrideMaterials();
	Part2->bDisallowNanite = true;
	Part2->SetRelativeLocation(D->Part2Pivot);
	Part2->SetVisibility(D->Part2 != nullptr);
	const bool bPoliceHeli = D->Name.ToString().Contains(TEXT("Police"));
	Searchlight->SetVisibility(bPoliceHeli);
	Searchlight->SetRelativeLocation(FVector(D->Length * 0.2f, 0.f, -D->Height * 0.5f));
	SpringArm->TargetArmLength = D->CameraDistance;
	SpringArm->SocketOffset = FVector(0.f, 0.f, FMath::Max(150.f, D->CameraDistance * 0.22f));
}

// ---------------------------------------------------------------------- tick
void ANCCraftPawn::Tick(float Dt)
{
	Super::Tick(Dt);
	// Preserve elapsed time at low frame rates; integrate in bounded steps.
	const float Time = FMath::Clamp(Dt, 0.f, .25f);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(Time / .05f));
	for (int32 i=0;i<Steps;++i) AdvanceTick(Time / Steps);
}

void ANCCraftPawn::AdvanceTick(float Dt)
{
	const FNCCraftType* D = Type();
	if (!D || Dt <= 0.f)
	{
		return;
	}
	Dt = FMath::Min(Dt, 0.05f);
	if (bWrecked)
	{
		WreckTime += Dt;
		if (HasAuthority() && WreckTime > 30.f)
		{
			Destroy();
			return;
		}
	}
	const bool bLocal = IsLocallyControlled();
	if (Route != INDEX_NONE && !Driver)
	{
		FollowRoute(Dt);
	}
	else if (bPoliceAI && HasAuthority())
	{
		ThinkPolice(Dt);
		Pack(Net);
	}
	else if (bLocal)
	{
		if (D->Kind == ENCCraftKind::Plane)
		{
			SimulatePlane(Dt, bWrecked ? 0.f : Throttle, bWrecked ? 0.f : Steer, bWrecked ? 0.f : Rise, bWrecked ? 0.f : YawIn);
		}
		else if (D->Kind == ENCCraftKind::Heli)
		{
			SimulateHeli(Dt, bWrecked ? 0.f : Throttle, bWrecked ? 0.f : Steer, bWrecked ? 0.f : Rise, bWrecked ? 0.f : YawIn);
		}
		else if (D->Kind == ENCCraftKind::Sub)
		{
			SimulateSub(Dt, bWrecked ? 0.f : Throttle, bWrecked ? 0.f : Steer, bWrecked ? 0.f : Rise);
		}
		else if (D->Kind == ENCCraftKind::Tank)
		{
			SimulateTank(Dt, bWrecked ? 0.f : Throttle, bWrecked ? 0.f : Steer);
		}
		else
		{
			Simulate(Dt, bWrecked ? 0.f : Throttle, bWrecked ? 0.f : Steer, bBoost);
		}
		SendTimer -= Dt;
		if (SendTimer <= 0.f)
		{
			SendTimer = 0.05f;
			FNCCraftNet S;
			Pack(S);
			if (HasAuthority())
			{
				Net = S;
			}
			else
			{
				ServerMove(S);
			}
		}
	}
	else if (HasAuthority() && !Driver)
	{
		// parked or drifting: only worth simulating while it moves or someone may look at it
		if (Vel.SizeSquared() > 100.f || bWrecked || LocalCameraDist(GetWorld(), GetActorLocation()) < 30000.f)
		{
			if (D->Kind == ENCCraftKind::Plane)
			{
				SimulatePlane(Dt, -1.f, 0.f, 0.f, 0.f);            // parked / abandoned: engine off, it glides or rolls to a stop
			}
			else if (D->Kind == ENCCraftKind::Heli)
			{
				SimulateHeli(Dt, 0.f, 0.f, -1.f, 0.f);
			}
			else if (D->Kind == ENCCraftKind::Sub)
			{
				SimulateSub(Dt, 0.f, 0.f, 1.f);                          // empty submarine floats up
			}
			else if (D->Kind == ENCCraftKind::Tank)
			{
				SimulateTank(Dt, 0.f, 0.f);
			}
			else
			{
				Simulate(Dt, 0.f, 0.f, false);
			}
			Pack(Net);
		}
	}
	else
	{
		if (LocalCameraDist(GetWorld(), GetActorLocation()) < 60000.f || Driver)
		{
			FollowNet(Dt);
		}
	}
	auto Spin = [](int32 Axis, float A) { return Axis == 1 ? FRotator(A, 0.f, 0.f) : (Axis == 2 ? FRotator(0.f, 0.f, A) : FRotator(0.f, A, 0.f)); };
	if (D->Kind == ENCCraftKind::Sub)
	{
		const float P = (bLocal || HasAuthority()) ? Power : Net.Aux;
		Aux = FMath::Fmod(Aux + P * 1500.f * Dt, 360.f);
		Searchlight->SetVisibility(GetActorLocation().Z < ANCWaterManager::SurfaceZ(GetWorld(), GetActorLocation()));
	}
	if (IsAir())
	{
		// rotors / propeller spin with the power every machine knows
		const float P = (bLocal || HasAuthority()) ? Power : Net.Aux;
		Aux = FMath::Fmod(Aux + P * (D->Kind == ENCCraftKind::Heli ? 1300.f : 2600.f) * Dt, 360.f);
		Aux2 = FMath::Fmod(Aux2 + P * 2600.f * Dt, 360.f);
		if (Searchlight->IsVisible())
		{
			const APawn* T = ChaseTarget.Get();
			const FVector To = T ? T->GetActorLocation() : GetActorLocation() + GetActorForwardVector() * 3000.f - FVector(0, 0, 3000.f);
			Searchlight->SetWorldRotation((To - Searchlight->GetComponentLocation()).Rotation());
		}
	}
	if (Part && D->Part)
	{
		Part->SetRelativeRotation(Spin(D->PartAxis, Aux));
	}
	if (Part2 && D->Part2)
	{
		Part2->SetRelativeRotation(Spin(D->Part2Axis, Aux2));
	}
	UpdateAudio(Dt);
	if (bLocal)
	{
		LookIdle += Dt;
		if (LookIdle > 1.5f && D->Kind != ENCCraftKind::Tank)
		{
			// camera swings back behind the stern
			FRotator R = SpringArm->GetRelativeRotation();
			R.Yaw = FMath::FInterpTo(FRotator::NormalizeAxis(R.Yaw), 0.f, Dt, 1.5f);
			R.Pitch = FMath::FInterpTo(R.Pitch, -12.f, Dt, 1.5f);
			SpringArm->SetRelativeRotation(R);
		}
	}
}

void ANCCraftPawn::UpdateAudio(float Dt)
{
	const FNCCraftType* D = Type();
	if (!D || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!EngineSound)
	{
		static const TCHAR* Ids[] = {TEXT("Engine_Boat"), TEXT("Engine_Boat"), TEXT("Engine_Jetski"), TEXT("Engine_Prop"), TEXT("Rotor"),
			TEXT("Engine_Sub"), TEXT("Engine_Tank")};
		FName Id = Ids[FMath::Clamp(static_cast<int32>(D->Kind), 0, 6)];
		if (D->Kind == ENCCraftKind::Plane && D->MaxSpeed > 15000.f)
		{
			Id = TEXT("Jet");
		}
		EngineSound = ANCSoundBank::Loop(Box, Id, 0.f);
		if (!EngineSound)
		{
			return;
		}
	}
	// louder / higher with speed or power; silent when nobody runs it
	const bool bRunning = !bWrecked && (Driver != nullptr || bPoliceAI || Route != INDEX_NONE);
	const float Spd = FMath::Clamp(Vel.Size() / FMath::Max(1.f, D->MaxSpeed), 0.f, 1.f);
	const float Load = IsAir() ? FMath::Max(Power, Spd) : FMath::Max(Spd, FMath::Abs(Throttle) * 0.6f);
	const float WantVol = bRunning ? 0.35f + 0.65f * Load : 0.f;
	EngineSound->SetVolumeMultiplier(FMath::FInterpTo(EngineSound->VolumeMultiplier, WantVol, Dt, 3.f));
	EngineSound->SetPitchMultiplier(0.7f + 0.9f * Load);
	// splash when a boat lands after a jump
	if (bWasAir && !bAir && !FreeZ())
	{
		ANCSoundBank::Play(this, TEXT("Splash"), GetActorLocation(), 0.8f);
	}
	bWasAir = bAir;
}

void ANCCraftPawn::Pack(FNCCraftNet& Out) const
{
	const FVector L = GetActorLocation();
	const bool bFly = FreeZ();
	Out.Loc = FVector(L.X, L.Y, (bAir || bFly) ? L.Z : 0.f);   // on the water every machine takes Z from the waves
	Out.Pitch = bFly ? GetActorRotation().Pitch : 0.f;
	Out.Roll = bFly ? GetActorRotation().Roll : 0.f;
	Out.Vel = Vel;
	Out.Yaw = CraftYawOf(this);
	Out.YawRate = YawRate;
	Out.VZ = bAir ? VZ : 0.f;
	const FNCCraftType* D = Type();
	Out.Aux = (bFly && D && D->Kind != ENCCraftKind::Tank) ? Power : Aux;
	Out.bAir = bAir;
}

void ANCCraftPawn::ServerMove_Implementation(FNCCraftNet State)
{
	Net = State;
}

void ANCCraftPawn::Simulate(float Dt, float InThrottle, float InSteer, bool bInBoost)
{
	const FNCCraftType* D = Type();
	float Yaw = CraftYawOf(this);
	FVector Fwd = FRotator(0.f, Yaw, 0.f).Vector();
	FVector Right(-Fwd.Y, Fwd.X, 0.f);
	float F = FVector::DotProduct(Vel, Fwd);
	float Side = FVector::DotProduct(Vel, Right);
	const float A = 2778.f / FMath::Max(0.5f, D->Accel);          // 100 km/h in Accel seconds
	float Max = D->MaxSpeed * (bInBoost && D->Kind != ENCCraftKind::Sail ? 1.2f : 1.f);
	float Drive = 0.f;
	if (D->Kind == ENCCraftKind::Sail)
	{
		// points of sail: nothing head to wind, best on a beam reach
		const float WY = Manager ? Manager->WindYaw : 40.f;
		const float WS = Manager ? Manager->WindStrength : 1.f;
		WindAngle = FMath::FindDeltaAngleDegrees(Yaw, WY + 180.f);   // where the wind comes from, relative to the bow
		const float Al = FMath::Abs(WindAngle);
		const float Eff = Al < 35.f ? 0.f : (Al < 90.f ? (Al - 35.f) / 55.f : (Al < 150.f ? 1.f : 1.f - (Al - 150.f) / 30.f * 0.3f));
		const float Sheet = FMath::Max(InThrottle, 0.f);
		SailEfficiency = Eff * Sheet;
		Max = D->MaxSpeed * (0.4f + 0.6f * WS);
		Drive = A * WS * Eff * Sheet + (bInBoost ? A * 0.35f : 0.f);
		if (InThrottle < 0.f)
		{
			Drive = A * 0.4f * InThrottle;                          // motor astern
		}
		// boom out to leeward: tight when close-hauled, wide when running
		const float Boom = -FMath::Sign(WindAngle) * FMath::Clamp(Al * 0.5f, 8.f, 80.f) * (Sheet > 0.05f ? 1.f : 0.3f);
		Aux = FMath::FInterpTo(Aux, Boom, Dt, 2.f);
	}
	else
	{
		Drive = InThrottle >= 0.f ? A * InThrottle : (F > 50.f ? A * 1.5f * InThrottle : A * 0.5f * InThrottle);
		Aux += F * Dt * 0.5f;                                        // propeller / jet
	}
	if (!bAir)
	{
		F += Drive * Dt;
		const float K = A / (Max * Max);                             // drag: full drive tops out at Max
		F -= FMath::Sign(F) * F * F * K * Dt;
		if (FMath::Abs(InThrottle) < 0.05f && !bInBoost)
		{
			F *= FMath::Exp(-0.35f * Dt);
		}
		F = FMath::Max(F, -D->Reverse);
		const float Grip = D->Kind == ENCCraftKind::JetSki ? 3.f : (D->Kind == ENCCraftKind::Sail ? 2.5f : (D->Length > 2500.f ? 0.8f : 1.8f));
		Side *= FMath::Exp(-Grip * Dt);
	}
	else
	{
		F *= FMath::Exp(-0.05f * Dt);
	}
	// rudder bites with speed
	const float SpeedK = FMath::Clamp(FMath::Abs(F) / (0.3f * D->MaxSpeed), 0.15f, 1.f);
	const float Target = InSteer * D->TurnRate * SpeedK * (F < -30.f ? -1.f : 1.f) * (bAir ? 0.3f : 1.f);
	YawRate = FMath::FInterpTo(YawRate, Target, Dt, D->Length > 2500.f ? 1.2f : 4.f);
	Yaw += YawRate * Dt;
	Fwd = FRotator(0.f, Yaw, 0.f).Vector();
	Right = FVector(-Fwd.Y, Fwd.X, 0.f);
	Vel = Fwd * F + Right * Side;
	Vel.Z = 0.f;
	// horizontal move with collisions (quays, piers, other boats, beaches)
	FHitResult Hit;
	SetActorRotation(FRotator(Pitch, Yaw, Roll));
	SetActorLocation(GetActorLocation() + Vel * Dt, true, &Hit);
	if (Hit.bBlockingHit)
	{
		const FVector N = Hit.ImpactNormal.GetSafeNormal2D();
		const float Into = -FVector::DotProduct(Vel, N);
		if (Into > 0.f)
		{
			Vel += N * Into * 1.3f;
			YawRate *= 0.5f;
			if (Into > 900.f)
			{
				SelfDamage(Into * 0.15f);
			}
		}
	}
	Heave(Dt, F);
}

void ANCCraftPawn::Heave(float Dt, float F)
{
	const FNCCraftType* D = Type();
	UWorld* W = GetWorld();
	const FVector C = GetActorLocation();
	const float Yaw = CraftYawOf(this);
	const FVector Fwd = FRotator(0.f, Yaw, 0.f).Vector();
	const FVector Right(-Fwd.Y, Fwd.X, 0.f);
	const float HB = ANCWaterManager::SurfaceZ(W, C + Fwd * D->Length * 0.4f);
	const float HS = ANCWaterManager::SurfaceZ(W, C - Fwd * D->Length * 0.4f);
	const float HL = ANCWaterManager::SurfaceZ(W, C - Right * D->Width * 0.45f);
	const float HR = ANCWaterManager::SurfaceZ(W, C + Right * D->Width * 0.45f);
	float Surf = 0.25f * (HB + HS + HL + HR);
	// aground: the keel sits on the sea bed / beach
	FHitResult G;
	FCollisionObjectQueryParams O;
	O.AddObjectTypesToQuery(ECC_WorldStatic);
	bool bAground = false;
	if (W->LineTraceSingleByObjectType(G, FVector(C.X, C.Y, C.Z + 300.f), FVector(C.X, C.Y, C.Z - 3000.f), O))
	{
		if (G.ImpactPoint.Z + D->Draft > Surf)
		{
			Surf = G.ImpactPoint.Z + D->Draft;
			bAground = true;
		}
	}
	const float Off = D->Deck * 0.5f - D->Draft;                   // box centre above the waterline
	float TargetZ = Surf + Off;
	if (bWrecked)
	{
		TargetZ -= FMath::Min(WreckTime * 45.f, D->Deck + D->Draft + 600.f);   // sinking
	}
	float Z = C.Z;
	if (bAir)
	{
		VZ -= 981.f * Dt;
		Z += VZ * Dt;
		if (Z <= TargetZ)
		{
			bAir = false;
			Z = TargetZ;
			VZ *= 0.25f;
		}
	}
	else
	{
		const float SurfVel = (Surf - LastSurface) / Dt;
		VZ += ((TargetZ - Z) * 55.f - VZ * 9.f) * Dt;
		Z += VZ * Dt;
		// fast over a crest while the water falls away under the hull: take off
		if (!bAground && D->Length < 1500.f && FMath::Abs(F) > 1200.f && SurfVel < -120.f && VZ > 60.f)
		{
			bAir = true;
			VZ += FMath::Abs(F) * 0.05f;
		}
	}
	LastSurface = Surf;
	if (bAground && !bAir)
	{
		Vel *= FMath::Exp(-3.f * Dt);
		YawRate *= FMath::Exp(-3.f * Dt);
	}
	// attitude: follow the wave slope, bow up when planing, lean into turns, sails heel
	const float Big = D->Length > 2500.f ? 1.f : 0.f;
	float PitchT = FMath::RadiansToDegrees(FMath::Atan2(HB - HS, D->Length * 0.8f));
	if (D->Kind != ENCCraftKind::Sail)
	{
		PitchT += (Big > 0.f ? 1.5f : 6.f) * FMath::Clamp(F / D->MaxSpeed, 0.f, 1.f);
	}
	float RollT = FMath::RadiansToDegrees(FMath::Atan2(HL - HR, D->Width * 0.9f));
	const float Lean = D->Kind == ENCCraftKind::JetSki ? 28.f : (D->Kind == ENCCraftKind::Sail ? 4.f : (Big > 0.f ? 2.f : 12.f));
	RollT += Lean * FMath::Clamp(YawRate / FMath::Max(1.f, D->TurnRate), -1.f, 1.f) * FMath::Clamp(FMath::Abs(F) / (0.5f * D->MaxSpeed), 0.f, 1.f);
	if (D->Kind == ENCCraftKind::Sail)
	{
		RollT -= FMath::Sign(WindAngle) * 22.f * SailEfficiency * (Manager ? Manager->WindStrength : 1.f);
	}
	if (bWrecked)
	{
		PitchT -= FMath::Min(WreckTime * 2.f, 25.f);
		RollT += FMath::Min(WreckTime * 1.5f, 20.f);
	}
	if (bAir)
	{
		PitchT = FMath::Max(Pitch - 12.f * Dt, -25.f);
		RollT = Roll;
	}
	const float Rate = Big > 0.f ? 1.2f : 5.f;
	Pitch = FMath::FInterpTo(Pitch, PitchT, Dt, Rate);
	Roll = FMath::FInterpTo(Roll, RollT, Dt, Rate);
	SetActorLocationAndRotation(FVector(C.X, C.Y, Z), FRotator(Pitch, Yaw, Roll));
}

void ANCCraftPawn::FollowNet(float Dt)
{
	if (FreeZ())
	{
		if (const FNCCraftType* TD = Type(); TD && TD->Kind == ENCCraftKind::Tank)
		{
			Aux = FMath::FixedTurn(Aux, Net.Aux, 120.f * Dt);           // turret
		}
		const FVector Want = FVector(Net.Loc) + Net.Vel * 0.1f;
		const FQuat Q = FQuat::Slerp(GetActorQuat(), FRotator(Net.Pitch, Net.Yaw, Net.Roll).Quaternion(), FMath::Clamp(Dt * 8.f, 0.f, 1.f));
		SetActorLocationAndRotation(FMath::VInterpTo(GetActorLocation(), Want, Dt, 6.f), Q);
		Vel = Net.Vel;
		return;
	}
	// someone else drives: glide towards the (slightly extrapolated) state they sent
	const FVector L = GetActorLocation();
	const FVector Want = FVector(Net.Loc.X, Net.Loc.Y, 0.f) + Net.Vel * 0.1f;
	const FVector XY = FMath::VInterpTo(FVector(L.X, L.Y, 0.f), Want, Dt, 6.f);
	const float Yaw = FMath::FixedTurn(CraftYawOf(this), Net.Yaw + Net.YawRate * 0.1f, 720.f * Dt * 0.25f + FMath::Abs(Net.YawRate) * Dt * 2.f);
	Vel = Net.Vel;
	YawRate = Net.YawRate;
	Aux = FMath::FInterpTo(Aux, Net.Aux, Dt, 6.f);
	SetActorLocationAndRotation(FVector(XY.X, XY.Y, L.Z), FRotator(Pitch, Yaw, Roll));
	if (Net.bAir)
	{
		bAir = true;
		VZ = Net.VZ;
		SetActorLocation(FMath::VInterpTo(GetActorLocation(), FVector(XY.X, XY.Y, Net.Loc.Z), Dt, 10.f));
	}
	Heave(Dt, FVector::DotProduct(Vel, GetActorForwardVector()));
}

void ANCCraftPawn::FollowRoute(float Dt)
{
	if (!Manager || !Manager->Routes.IsValidIndex(Route))
	{
		return;
	}
	const FNCSeaRoute& R = Manager->Routes[Route];
	FVector P;
	float Yaw;
	R.Eval(ANCWaterManager::WaveTime(GetWorld()), P, Yaw);
	const FVector L = GetActorLocation();
	const float OldYaw = CraftYawOf(this);
	Vel = R.Radius > 1.f ? FRotator(0.f, Yaw, 0.f).Vector() * R.Speed : FVector::ZeroVector;
	YawRate = FMath::FindDeltaAngleDegrees(OldYaw, Yaw) / Dt;
	Aux += Vel.Size() * Dt * 0.5f;
	if (LocalCameraDist(GetWorld(), P) > 60000.f)
	{
		SetActorLocation(FVector(P.X, P.Y, L.Z));
		return;
	}
	SetActorLocationAndRotation(FVector(P.X, P.Y, L.Z), FRotator(Pitch, Yaw, Roll));
	Heave(Dt, Vel.Size());
}

// ---------------------------------------------------------------------- air
bool ANCCraftPawn::GroundBelow(float& OutZ) const
{
	FHitResult G;
	FCollisionObjectQueryParams O;
	O.AddObjectTypesToQuery(ECC_WorldStatic);
	O.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Q(SCENE_QUERY_STAT(NCAirGround), false, this);
	const FVector C = GetActorLocation();
	if (GetWorld()->LineTraceSingleByObjectType(G, C + FVector(0, 0, 100.f), C - FVector(0, 0, 200000.f), O, Q))
	{
		OutZ = G.ImpactPoint.Z;
		// the sea counts as ground too (ditching)
		OutZ = FMath::Max(OutZ, ANCWaterManager::SurfaceZ(GetWorld(), C));
		return true;
	}
	OutZ = ANCWaterManager::SurfaceZ(GetWorld(), C);
	return true;
}

void ANCCraftPawn::MoveAir(float Dt, const FQuat& Q, float Crash)
{
	const FNCCraftType* D = Type();
	const float Half = D->Height * 0.5f + 15.f;
	float GZ;
	GroundBelow(GZ);
	FVector NewLoc = GetActorLocation() + Vel * Dt;
	const float Water = ANCWaterManager::SurfaceZ(GetWorld(), NewLoc);
	if (NewLoc.Z - Half < GZ)
	{
		const float Sink = -Vel.Z;
		NewLoc.Z = GZ + Half;
		if (Sink > Crash || (GZ <= Water + 1.f && Vel.Size() > 1800.f))
		{
			// hard landing or hitting the sea fast
			SelfDamage(D->Health + 100.f);
		}
		Vel.Z = FMath::Max(Vel.Z, 0.f);
		if (GZ <= Water + 1.f)
		{
			Vel *= FMath::Exp(-2.f * Dt);                            // ditched: drifts on the water
		}
	}
	FHitResult Hit;
	SetActorLocationAndRotation(NewLoc, Q, true, &Hit);
	if (Hit.bBlockingHit)
	{
		const FVector N = Hit.ImpactNormal;
		const float Into = -FVector::DotProduct(Vel, N);
		if (Into > Crash || (Into > Crash * 0.5f && Vel.Size() > 2500.f))
		{
			SelfDamage(D->Health + 100.f);
		}
		if (Into > 0.f)
		{
			Vel += N * Into;
		}
	}
	GroundBelow(GZ);
	Altitude = FMath::Max(0.f, GetActorLocation().Z - Half - GZ);
}

void ANCCraftPawn::SimulatePlane(float Dt, float InThrottle, float InRoll, float InPitch, float InYaw)
{
	const FNCCraftType* D = Type();
	Power = bWrecked ? 0.f : FMath::Clamp(Power + InThrottle * 0.5f * Dt, 0.f, 1.f);
	FQuat Q = GetActorQuat();
	const FVector F = Q.GetForwardVector();
	const FVector U = Q.GetUpVector();
	const float V = FVector::DotProduct(Vel, F);
	const float Stall = FMath::Max(300.f, D->StallSpeed);
	const float A = 2778.f / FMath::Max(0.5f, D->Accel);
	const float K = A / FMath::Square(D->MaxSpeed);
	const float Eff = FMath::Clamp(V / Stall, 0.f, 1.3f);
	const float Half = D->Height * 0.5f + 15.f;
	float GZ;
	GroundBelow(GZ);
	const bool bOnGround = GetActorLocation().Z - Half <= GZ + 25.f && Vel.Z <= 60.f;
	// thrust, drag, gravity and lift (full lift a little above the stall speed)
	FVector Acc = F * (A * Power) - Vel.GetSafeNormal() * Vel.SizeSquared() * K + FVector(0.f, 0.f, -981.f);
	const float LiftG = bWrecked ? 0.f : FMath::Clamp(FMath::Square(FMath::Max(V, 0.f) / Stall) * 0.95f, 0.f, 3.f);
	Acc += U * 981.f * LiftG;
	Vel += Acc * Dt;
	if (!bOnGround && V > 0.f)
	{
		Vel = FMath::Lerp(Vel, F * Vel.Size(), FMath::Clamp(Eff * 1.2f * Dt, 0.f, 1.f));   // the air turns the flight path onto the nose
	}
	FRotator Delta(InPitch * D->TurnRate * 0.7f * Eff * Dt, InYaw * D->TurnRate * 0.3f * Eff * Dt, InRoll * D->TurnRate * 1.6f * Eff * Dt);
	if (bOnGround)
	{
		// on the wheels: no side slip, brakes, nose-wheel steering, rotate only near flying speed
		const FVector Fl = F.GetSafeNormal2D();
		float VF = FVector::DotProduct(Vel, Fl);
		VF *= FMath::Exp(-(InThrottle < 0.f && Power < 0.05f ? 1.2f : 0.04f) * Dt);
		Vel = Fl * VF + FVector(0.f, 0.f, FMath::Max(Vel.Z, 0.f));
		Delta.Yaw = (InRoll + InYaw) * 35.f * Dt * FMath::Clamp(FMath::Abs(VF) / 300.f, 0.f, 1.f);
		Delta.Roll = 0.f;
		if (V < Stall * 0.85f)
		{
			Delta.Pitch = FMath::Min(Delta.Pitch, 0.f);
		}
	}
	Q = Q * Delta.Quaternion();
	FRotator R = Q.Rotator();
	if (!bOnGround)
	{
		// banking turns the nose (coordinated turn); below the stall the nose drops
		const float Turn = FMath::RadiansToDegrees(981.f * FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(R.Roll, -75.f, 75.f))) / FMath::Max(V, Stall)) * Dt;
		Q = FQuat(FVector::UpVector, FMath::DegreesToRadians(Turn)) * Q;
		if (V < Stall * 0.8f)
		{
			Q = Q * FRotator(-30.f * Dt * (1.f - V / (Stall * 0.8f)), 0.f, 0.f).Quaternion();
		}
	}
	else
	{
		R.Roll = FMath::FInterpTo(R.Roll, 0.f, Dt, 5.f);
		if (V < Stall * 0.85f)
		{
			R.Pitch = FMath::FInterpTo(R.Pitch, 0.f, Dt, 4.f);
		}
		Q = R.Quaternion();
	}
	MoveAir(Dt, Q, 650.f);
}

void ANCCraftPawn::SimulateHeli(float Dt, float InForward, float InTurn, float InRise, float InSide)
{
	const FNCCraftType* D = Type();
	const bool bCrew = Driver != nullptr || bPoliceAI;
	Power = FMath::Clamp(Power + ((bCrew && !bWrecked) ? 0.3f : -0.15f) * Dt, 0.f, 1.f);
	const float Half = D->Height * 0.5f + 15.f;
	float GZ;
	GroundBelow(GZ);
	const bool bOnGround = GetActorLocation().Z - Half <= GZ + 20.f && Vel.Z <= 10.f;
	FRotator R = GetActorRotation();
	// cyclic: W/S tip the nose, Q/E bank sideways; A/D tail rotor turns
	const float PitchT = bOnGround ? 0.f : -InForward * 28.f;
	const float RollT = bOnGround ? 0.f : InSide * 25.f;
	R.Pitch = FMath::FInterpTo(R.Pitch, PitchT, Dt, 2.5f);
	R.Roll = FMath::FInterpTo(R.Roll, RollT, Dt, 2.5f);
	R.Yaw += InTurn * D->TurnRate * Dt * (Power > 0.5f ? 1.f : 0.f);
	const FQuat Q = R.Quaternion();
	const FVector U = Q.GetUpVector();
	// collective: hovers by itself, Space climbs, Ctrl sinks; tilting does not lose height
	const float Lift = 981.f * FMath::Square(Power) * (1.f + 0.8f * InRise) / FMath::Max(U.Z, 0.6f);
	Vel += (U * Lift + FVector(0.f, 0.f, -981.f)) * Dt;
	FVector H(Vel.X, Vel.Y, 0.f);
	const float Kd = 981.f * FMath::Tan(FMath::DegreesToRadians(28.f)) / FMath::Square(D->MaxSpeed);
	H -= H * H.Size() * Kd * Dt + H * 0.15f * Dt;
	Vel = FVector(H.X, H.Y, Vel.Z * FMath::Exp(-1.5f * Dt));
	if (bOnGround)
	{
		Vel.X *= FMath::Exp(-4.f * Dt);
		Vel.Y *= FMath::Exp(-4.f * Dt);
		Vel.Z = FMath::Max(Vel.Z, 0.f);
	}
	MoveAir(Dt, Q, 750.f);
}

void ANCCraftPawn::SelfDamage(float Amount)
{
	if (bWrecked)
	{
		return;
	}
	if (HasAuthority())
	{
		UGameplayStatics::ApplyDamage(this, Amount, GetController(), this, UDamageType::StaticClass());
	}
	else if (IsLocallyControlled())
	{
		ServerSelfDamage(Amount);                                     // the driver's machine saw the crash
	}
}

void ANCCraftPawn::ServerSelfDamage_Implementation(float Amount)
{
	UGameplayStatics::ApplyDamage(this, FMath::Clamp(Amount, 0.f, 5000.f), GetController(), this, UDamageType::StaticClass());
}

void ANCCraftPawn::ThinkPolice(float Dt)
{
	const FNCCraftType* D = Type();
	if (bWrecked)
	{
		SimulateHeli(Dt, 0.f, 0.f, -1.f, 0.f);                      // shot down: falls
		return;
	}
	Power = FMath::Min(1.f, Power + 0.5f * Dt);
	APawn* T = ChaseTarget.Get();
	const FVector C = GetActorLocation();
	FVector Goal;
	if ((T || bPoliceSearching) && !bWrecked)
	{
		// circle the suspect 35 m out, 35 m above whatever is below
		OrbitAngle += Dt * 0.15f;
		const FVector TL = bPoliceSearching ? PoliceSearchLocation : T->GetActorLocation();
		LeaveTime = 0.f;
		Goal = TL + FVector(FMath::Cos(OrbitAngle) * 3500.f, FMath::Sin(OrbitAngle) * 3500.f, 0.f);
		float GZ;
		GroundBelow(GZ);
		Goal.Z = FMath::Max(TL.Z + 3500.f, GZ + 3000.f);
	}
	else
	{
		Goal = C + FVector(0.f, 0.f, 3000.f) + GetActorForwardVector() * 20000.f;      // go home
		LeaveTime += Dt;
		if (LeaveTime > 25.f)
		{
			Destroy();
			return;
		}
	}
	const FVector Want = ((Goal - C) * 0.6f).GetClampedToMaxSize(D->MaxSpeed * 0.6f);
	Vel = FMath::VInterpTo(Vel, Want, Dt, 1.2f);
	const float FaceYaw = T ? (T->GetActorLocation() - C).Rotation().Yaw : Vel.Rotation().Yaw;
	const FVector Fwd = FRotator(0.f, GetActorRotation().Yaw, 0.f).Vector();
	const FVector Right(-Fwd.Y, Fwd.X, 0.f);
	const FRotator R(-FMath::Clamp(FVector::DotProduct(Vel, Fwd) / 3000.f * 20.f, -20.f, 20.f),
		FMath::FixedTurn(GetActorRotation().Yaw, FaceYaw, 60.f * Dt),
		FMath::Clamp(FVector::DotProduct(Vel, Right) / 3000.f * 20.f, -20.f, 20.f));
	FHitResult Hit;
	SetActorLocationAndRotation(C + Vel * Dt, FQuat::Slerp(GetActorQuat(), R.Quaternion(), FMath::Clamp(Dt * 3.f, 0.f, 1.f)), true, &Hit);
	if (Hit.bBlockingHit)
	{
		Vel.Z += 1500.f;                                             // pull up over the building
	}
	// the gunner in the door (3+ stars)
	FireTimer -= Dt;
	const AController* TC = ChaseController.IsValid() ? ChaseController.Get() : (T ? T->GetController() : nullptr);
	const ANCPlayerState* PS = TC ? TC->GetPlayerState<ANCPlayerState>() : nullptr;
	if (T && PS && PS->Wanted >= 3 && FireTimer <= 0.f && FVector::Dist(T->GetActorLocation(), C) < 9000.f)
	{
		FireTimer = FMath::FRandRange(0.9f, 1.6f);
		const FVector From = C - FVector(0.f, 0.f, D->Height * 0.5f) + Right * 120.f;
		const FVector Aim = (T->GetActorLocation() + FVector(0, 0, 40.f) - From).GetSafeNormal();
		bool bFlesh = false;
		const FVector End = UNCWeaponComponent::FireBullet(this, nullptr, From, FMath::VRandCone(Aim, FMath::DegreesToRadians(2.5f)), 12000.f, 16.f, bFlesh);
		MulticastShot(From, End, bFlesh);
	}
}

void ANCCraftPawn::MulticastShot_Implementation(FVector_NetQuantize From, FVector_NetQuantize To, bool bFlesh)
{
	if (const FNCCraftType* D = Type(); D && D->Kind == ENCCraftKind::Tank)
	{
		ANCSoundBank::Play(this, TEXT("Cannon"), From, 1.3f);
		return;
	}
	UNCWeaponComponent::ShotFX(GetWorld(), From, To, bFlesh);
}

// ---------------------------------------------------------------------- submarine / tank
void ANCCraftPawn::SimulateSub(float Dt, float InThrottle, float InSteer, float InRise)
{
	const FNCCraftType* D = Type();
	UWorld* W = GetWorld();
	Power = FMath::FInterpTo(Power, FMath::Abs(InThrottle), Dt, 2.f);
	const float Yaw = CraftYawOf(this) + InSteer * D->TurnRate * Dt;
	const FVector Fwd = FRotator(0.f, Yaw, 0.f).Vector();
	float F = FVector::DotProduct(Vel, Fwd);
	const float A = 2778.f / FMath::Max(0.5f, D->Accel);
	F += (InThrottle >= 0.f ? A * InThrottle : A * 0.5f * InThrottle) * Dt;
	F -= FMath::Sign(F) * F * F * A / FMath::Square(D->MaxSpeed) * Dt;
	F = FMath::Max(F, -D->Reverse);
	if (FMath::Abs(InThrottle) < 0.05f)
	{
		F *= FMath::Exp(-0.6f * Dt);
	}
	// ballast: Space surfaces, Ctrl dives
	VZ = FMath::FInterpTo(VZ, InRise * 280.f, Dt, 1.5f);
	Vel = Fwd * F;
	FVector C = GetActorLocation();
	const float Surf = ANCWaterManager::SurfaceZ(W, C) + D->Deck * 0.5f - D->Draft;     // floating like a boat
	float GZ;
	// Aircraft ground includes the sea surface. A submarine needs the actual seabed.
	FHitResult Bed;
	FCollisionQueryParams BedQuery(SCENE_QUERY_STAT(NCSubBed),true,this);
	GZ = W->LineTraceSingleByChannel(Bed,C,C-FVector(0,0,200000.f),ECC_WorldStatic,BedQuery) ? Bed.ImpactPoint.Z : C.Z-200000.f;
	const float Bottom = FMath::Min(Surf,GZ+D->Deck*.5f+20.f);
	float Z = FMath::Clamp(C.Z + VZ * Dt, Bottom, Surf);
	if (Z <= Bottom+1.f) VZ=FMath::Max(VZ,0.f);
	if (Z >= Surf - 1.f)
	{
		VZ = FMath::Min(VZ, 0.f);
	}
	const float PitchT = -FMath::Clamp(VZ / 280.f, -1.f, 1.f) * 12.f;
	Pitch = FMath::FInterpTo(Pitch, PitchT, Dt, 2.f);
	Roll = FMath::FInterpTo(Roll, InSteer * 6.f * FMath::Clamp(FMath::Abs(F) / 500.f, 0.f, 1.f), Dt, 2.f);
	FHitResult Hit;
	SetActorLocationAndRotation(FVector(C.X, C.Y, Z) + Vel * Dt, FRotator(Pitch, Yaw, Roll), true, &Hit);
	if (Hit.bBlockingHit)
	{
		const float Into = -FVector::DotProduct(Vel, Hit.ImpactNormal);
		if (Into > 0.f)
		{
			Vel += Hit.ImpactNormal * Into;
		}
	}
	Altitude = FMath::Max(0.f, ANCWaterManager::SurfaceZ(W, C) - GetActorLocation().Z);     // HUD: depth
}

void ANCCraftPawn::SimulateTank(float Dt, float InThrottle, float InSteer)
{
	const FNCCraftType* D = Type();
	UWorld* W = GetWorld();
	float Yaw = CraftYawOf(this);
	const FVector Fwd0 = FRotator(0.f, Yaw, 0.f).Vector();
	float F = FVector::DotProduct(Vel, Fwd0);
	const float A = 2778.f / FMath::Max(0.5f, D->Accel);
	if (FMath::Abs(InThrottle) > 0.05f)
	{
		F = FMath::FInterpConstantTo(F, InThrottle > 0.f ? D->MaxSpeed * InThrottle : D->Reverse * InThrottle, Dt, A);
	}
	else
	{
		F = FMath::FInterpConstantTo(F, 0.f, Dt, A * 2.f);
	}
	Yaw += InSteer * D->TurnRate * Dt * (F < -10.f ? -1.f : 1.f);            // tracks turn on the spot
	const FVector Fwd = FRotator(0.f, Yaw, 0.f).Vector();
	const FVector Right(-Fwd.Y, Fwd.X, 0.f);
	Vel = Fwd * F;
	// the turret follows the camera
	if (IsLocallyControlled())
	{
		Aux = FMath::FixedTurn(Aux, SpringArm->GetRelativeRotation().Yaw, 45.f * Dt);
	}
	GunCooldown -= Dt;
	// hull on the ground: 4 track corners
	auto G = [W, this](const FVector& P) {
		FHitResult H;
		FCollisionObjectQueryParams O;
		O.AddObjectTypesToQuery(ECC_WorldStatic);
		return W->LineTraceSingleByObjectType(H, P + FVector(0, 0, 400.f), P - FVector(0, 0, 3000.f), O) ? H.ImpactPoint.Z : P.Z - 100.f;
	};
	const FVector C = GetActorLocation();
	const float L2 = D->Length * 0.4f, W2 = D->Width * 0.4f;
	const float FL = G(C + Fwd * L2 - Right * W2), FR = G(C + Fwd * L2 + Right * W2);
	const float BL = G(C - Fwd * L2 - Right * W2), BR = G(C - Fwd * L2 + Right * W2);
	const float Ground = 0.25f * (FL + FR + BL + BR);
	const float PitchT = FMath::RadiansToDegrees(FMath::Atan2(0.5f * (FL + FR - BL - BR), 2.f * L2));
	const float RollT = FMath::RadiansToDegrees(FMath::Atan2(0.5f * (FL + BL - FR - BR), 2.f * W2));
	Pitch = FMath::FInterpTo(Pitch, PitchT, Dt, 6.f);
	Roll = FMath::FInterpTo(Roll, RollT, Dt, 6.f);
	const float TargetZ = Ground + D->Height * 0.5f + 15.f;
	float Z = C.Z;
	if (Z > TargetZ + 30.f)
	{
		VZ -= 981.f * Dt;                                                    // off a ledge
		Z += VZ * Dt;
		Z = FMath::Max(Z, TargetZ);
	}
	else
	{
		VZ = 0.f;
		Z = FMath::FInterpTo(Z, TargetZ, Dt, 10.f);
	}
	FHitResult Hit;
	SetActorLocationAndRotation(FVector(C.X, C.Y, Z) + Vel * Dt, FRotator(Pitch, Yaw, Roll), true, &Hit);
	if (Hit.bBlockingHit)
	{
		// 60 tonnes: shove cars, crush them when pushing hard
		if (AActor* O = Hit.GetActor())
		{
			if (UPrimitiveComponent* PC = Hit.GetComponent(); PC && PC->IsSimulatingPhysics())
			{
				PC->AddImpulseAtLocation(Vel * 25.f * PC->GetMass() / 1000.f, Hit.ImpactPoint);
				if (HasAuthority() && FMath::Abs(F) > 300.f)
				{
					UGameplayStatics::ApplyDamage(O, 40.f * Dt * 10.f, GetController(), this, UDamageType::StaticClass());
				}
			}
		}
		const float Into = -FVector::DotProduct(Vel, Hit.ImpactNormal.GetSafeNormal2D());
		if (Into > 0.f)
		{
			Vel += Hit.ImpactNormal.GetSafeNormal2D() * Into;
		}
	}
	// ruts in the ground behind the tracks
	TrackDist += FMath::Abs(F) * Dt;
	if (TrackDist > 150.f && !IsAir())
	{
		TrackDist = 0.f;
		for (float Side : {-1.f, 1.f})
		{
			ANCSoundBank::Track(W, FVector(C.X, C.Y, Ground) - Fwd * L2 * 1.1f + Right * Side * D->Width * 0.45f, FRotator(0.f, Yaw, 0.f), 70.f);
		}
	}
	Altitude = 0.f;
}

void ANCCraftPawn::OnFire(const FInputActionValue&)
{
	const FNCCraftType* D = Type();
	if (!D || D->Kind != ENCCraftKind::Tank || GunCooldown > 0.f || bWrecked)
	{
		return;
	}
	GunCooldown = 2.5f;
	const float TurretYaw = CraftYawOf(this) + Aux;
	ServerFireGun(FRotator(AimPitch, TurretYaw, 0.f).Vector());
}

void ANCCraftPawn::ServerFireGun_Implementation(FVector_NetQuantizeNormal Dir)
{
	const FNCCraftType* D = Type();
	if (!D || bWrecked)
	{
		return;
	}
	const FVector Pivot = Hull->GetComponentTransform().TransformPosition(D->PartPivot);
	const FVector Muzzle = Pivot + FVector(0, 0, 50.f) + FVector(Dir) * 720.f;
	ANCProjectile::Launch(GetWorld(), 0, Muzzle, FVector(Dir) * 9000.f, 450.f, 800.f, this, this);
	MulticastShot(Muzzle - FVector(Dir) * 60.f, Muzzle + FVector(Dir) * 200.f, false);
	ANCPoliceManager::ReportCrime(GetWorld(), GetController(), 3, Muzzle, true);
}

// ---------------------------------------------------------------------- seats
bool ANCCraftPawn::EnterCraft(APawn* Character)
{
	if (!HasAuthority() || !Character || Driver || bWrecked || !Type())
	{
		return false;
	}
	AController* C = Character->GetController();
	if (!C)
	{
		return false;
	}
	if (Route != INDEX_NONE)
	{
		// hijacked sea traffic becomes a normal boat
		Route = INDEX_NONE;
	}
	Driver = Character;
	Character->SetActorHiddenInGame(true);
	Character->SetActorEnableCollision(false);
	if (ACharacter* Ch = Cast<ACharacter>(Character))
	{
		Ch->GetCharacterMovement()->DisableMovement();
	}
	Character->AttachToActor(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	C->Possess(this);
	return true;
}

bool ANCCraftPawn::EnterAsPassenger(APawn* Character)
{
	const FNCCraftType* D = Type();
	if (!HasAuthority() || !Character || !D || bWrecked || Passengers.Num() >= FMath::Max(0, D->Seats.Num() - 1))
	{
		return false;
	}
	Passengers.Add(Character);
	if (ACharacter* Ch = Cast<ACharacter>(Character))
	{
		Ch->GetCharacterMovement()->DisableMovement();
	}
	Character->SetActorEnableCollision(false);
	Character->SetActorHiddenInGame(true);
	Character->AttachToActor(this, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	return true;
}

void ANCCraftPawn::ExitPassenger(APawn* Character)
{
	if (HasAuthority() && Passengers.Contains(Character))
	{
		ExitCraft(Character, false);
	}
}

bool ANCCraftPawn::ExitSpot(APawn* Who, FVector& Out) const
{
	const FNCCraftType* D = Type();
	const FVector C = GetActorLocation();
	if (!D || !Who) return false;
	const ACharacter* Ch=Cast<ACharacter>(Who);
	const float H=Ch?Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight():90.f;
	const float R=Ch?Ch->GetCapsuleComponent()->GetScaledCapsuleRadius():34.f;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(NCCraftExit),true,Who);
	// Keep the craft in overlap checks: never exit into its body.
	const float Water = ANCWaterManager::SurfaceZ(GetWorld(), C);
	const FVector Right = GetActorRightVector().GetSafeNormal2D();
	const FVector Fwd = GetActorForwardVector().GetSafeNormal2D();
	for (const FVector& Dir : {-Right, Right, Fwd, -Fwd})
	{
		for (float Extra : {150.f,350.f,600.f})
		{
			FVector P=C+Dir*((Dir==Fwd || Dir==-Fwd?D->Length:D->Width)*.5f+Extra);
			FHitResult Ground;
			FCollisionQueryParams GroundQuery=Query; GroundQuery.AddIgnoredActor(this);
			const bool Found=GetWorld()->LineTraceSingleByChannel(Ground,P+FVector(0,0,400.f),P-FVector(0,0,800.f),ECC_Pawn,GroundQuery);
			if (Found && Ground.ImpactNormal.Z>.5f && Ground.ImpactPoint.Z>Water+20.f) P.Z=Ground.ImpactPoint.Z+H+8.f;
			else if (!IsAir() && D->Kind!=ENCCraftKind::Tank) P.Z=FMath::Min(C.Z,Water-H*.55f);
			if (!GetWorld()->OverlapBlockingTestByChannel(P,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(R,H),Query)) { Out=P; return true; }
		}
	}
	return false;
}

void ANCCraftPawn::ExitCraft(APawn* Who, bool bDriverSeat)
{
	if (!Who)
	{
		return;
	}
	FVector Exit;
	if (!ExitSpot(Who,Exit)) return;
	Who->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Who->SetActorLocation(Exit, false, nullptr, ETeleportType::TeleportPhysics);
	Who->SetActorRotation(FRotator(0.f, CraftYawOf(this), 0.f));
	Who->SetActorHiddenInGame(false);
	Who->SetActorEnableCollision(true);
	if (ACharacter* Ch = Cast<ACharacter>(Who))
	{
		auto* M=Ch->GetCharacterMovement();
		M->StopMovementImmediately(); M->SetUpdatedComponent(Ch->GetCapsuleComponent());
		M->SetComponentTickEnabled(true); M->Activate(true); M->SetMovementMode(MOVE_Falling);
	}
	if (bDriverSeat)
	{
		AController* C = GetController();
		Driver = nullptr;
		if (C)
		{
			C->Possess(Who);
		}
	}
	if (APlayerController* PC=Cast<APlayerController>(Who->GetController()))
	{
		PC->ResetIgnoreInputFlags(); Who->EnableInput(PC);
		PC->SetInputMode(FInputModeGameOnly()); PC->SetViewTarget(Who);
	}
	Passengers.Remove(Who);
	if (auto* Interaction=Who->FindComponentByClass<UNCInteractionComponent>()) Interaction->RestoreAfterExit();
}

void ANCCraftPawn::RemoveLocalControls(APlayerController* PC)
{
	if (PC && PC->IsLocalController() && PC->GetPawn()!=this && PC->GetLocalPlayer())
		if (auto* Sub=PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) if (Mapping) Sub->RemoveMappingContext(Mapping);
}

void ANCCraftPawn::ServerExit_Implementation()
{
	if (Driver)
	{
		ExitCraft(Driver, true);
	}
}

void ANCCraftPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	Throttle = 0.f;
	Steer = 0.f;
}

void ANCCraftPawn::UnPossessed()
{
	if (const APlayerController* PC=Cast<APlayerController>(GetController()))
		if (PC->GetLocalPlayer()) if (auto* Sub=PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			if (Mapping) Sub->RemoveMappingContext(Mapping);
	Super::UnPossessed();
	Throttle = 0.f;
	Steer = 0.f;
	Rise = 0.f;
	YawIn = 0.f;
	bBoost = false;
}

// ---------------------------------------------------------------------- damage
float ANCCraftPawn::TakeDamage(float Damage, const FDamageEvent& Event, AController* Inst, AActor* Causer)
{
	const float Done = Super::TakeDamage(Damage, Event, Inst, Causer);
	if (!HasAuthority() || bWrecked || Damage <= 0.f)
	{
		return Done;
	}
	Health -= Damage;
	if (Health <= 0.f)
	{
		bWrecked = true;
		WreckTime = 0.f;
		UNCWeaponComponent::Explode(GetWorld(), GetActorLocation(), 700.f, 150.f, this, Inst, false);
		for (int32 i = Passengers.Num() - 1; i >= 0; --i)
		{
			APawn* P = Passengers[i];
			Passengers.RemoveAt(i);
			ExitCraft(P, false);
		}
		if (Driver)
		{
			ExitCraft(Driver, true);
		}
	}
	return Done;
}

void ANCCraftPawn::OnRep_Wrecked()
{
	WreckTime = 0.f;
	if (bWrecked && !HasAuthority())
	{
		if (UParticleSystem* FX = LoadObject<UParticleSystem>(nullptr, TEXT("/Game/StarterContent/Particles/P_Explosion.P_Explosion")))
		{
			UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), FX, GetActorLocation(), FRotator::ZeroRotator, FVector(1.4f));
		}
	}
}

// ---------------------------------------------------------------------- input
void ANCCraftPawn::CreateInput()
{
	if (Mapping)
	{
		return;
	}
	auto Make = [this](const TCHAR* Name, EInputActionValueType T)
	{
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = T;
		return A;
	};
	ThrottleAction = Make(TEXT("IA_NC_CraftThrottle"), EInputActionValueType::Axis1D);
	SteerAction = Make(TEXT("IA_NC_CraftSteer"), EInputActionValueType::Axis1D);
	BoostAction = Make(TEXT("IA_NC_CraftBoost"), EInputActionValueType::Boolean);
	LookAction = Make(TEXT("IA_NC_CraftLook"), EInputActionValueType::Axis2D);
	ExitAction = Make(TEXT("IA_NC_CraftExit"), EInputActionValueType::Boolean);
	RiseAction = Make(TEXT("IA_NC_CraftRise"), EInputActionValueType::Axis1D);
	YawAction = Make(TEXT("IA_NC_CraftYaw"), EInputActionValueType::Axis1D);
	FireAction = Make(TEXT("IA_NC_CraftFire"), EInputActionValueType::Boolean);
	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_NC_Craft"));
	Mapping->MapKey(ThrottleAction, EKeys::W);
	FEnhancedActionKeyMapping& Back = Mapping->MapKey(ThrottleAction, EKeys::S);
	Back.Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
	Mapping->MapKey(ThrottleAction, EKeys::Gamepad_RightTriggerAxis);
	FEnhancedActionKeyMapping& BackPad = Mapping->MapKey(ThrottleAction, EKeys::Gamepad_LeftTriggerAxis);
	BackPad.Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
	Mapping->MapKey(SteerAction, EKeys::D);
	FEnhancedActionKeyMapping& Left = Mapping->MapKey(SteerAction, EKeys::A);
	Left.Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
	Mapping->MapKey(SteerAction, EKeys::Gamepad_LeftX);
	Mapping->MapKey(BoostAction, EKeys::LeftShift);
	Mapping->MapKey(BoostAction, EKeys::Gamepad_FaceButton_Bottom);
	Mapping->MapKey(LookAction, EKeys::Mouse2D);
	Mapping->MapKey(LookAction, EKeys::Gamepad_Right2D);
	// air: Space / Ctrl = climb / sink (plane: pull up / push down), Q / E = rudder (helicopter: sideways)
	Mapping->MapKey(RiseAction, EKeys::SpaceBar);
	FEnhancedActionKeyMapping& Sink = Mapping->MapKey(RiseAction, EKeys::LeftControl);
	Sink.Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
	Mapping->MapKey(RiseAction, EKeys::Gamepad_RightShoulder);
	FEnhancedActionKeyMapping& SinkPad = Mapping->MapKey(RiseAction, EKeys::Gamepad_LeftShoulder);
	SinkPad.Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
	Mapping->MapKey(YawAction, EKeys::E);
	FEnhancedActionKeyMapping& YawLeft = Mapping->MapKey(YawAction, EKeys::Q);
	YawLeft.Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
	Mapping->MapKey(FireAction, EKeys::LeftMouseButton);
	Mapping->MapKey(FireAction, EKeys::Gamepad_RightTrigger);
	// F / Y belongs to the persistent player controller across all vehicle switches.
}

void ANCCraftPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	CreateInput();
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Sub->AddMappingContext(Mapping, 10);
		}
	}
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		EIC->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ANCCraftPawn::OnThrottle);
		EIC->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ANCCraftPawn::OnThrottle);
		EIC->BindAction(SteerAction, ETriggerEvent::Triggered, this, &ANCCraftPawn::OnSteer);
		EIC->BindAction(SteerAction, ETriggerEvent::Completed, this, &ANCCraftPawn::OnSteer);
		EIC->BindAction(BoostAction, ETriggerEvent::Triggered, this, &ANCCraftPawn::OnBoost);
		EIC->BindAction(BoostAction, ETriggerEvent::Completed, this, &ANCCraftPawn::OnBoost);
		EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ANCCraftPawn::OnLook);
		EIC->BindAction(ExitAction, ETriggerEvent::Started, this, &ANCCraftPawn::OnExit);
		EIC->BindAction(FireAction, ETriggerEvent::Started, this, &ANCCraftPawn::OnFire);
		EIC->BindAction(RiseAction, ETriggerEvent::Triggered, this, &ANCCraftPawn::OnRise);
		EIC->BindAction(RiseAction, ETriggerEvent::Completed, this, &ANCCraftPawn::OnRise);
		EIC->BindAction(YawAction, ETriggerEvent::Triggered, this, &ANCCraftPawn::OnYaw);
		EIC->BindAction(YawAction, ETriggerEvent::Completed, this, &ANCCraftPawn::OnYaw);
	}
}

void ANCCraftPawn::OnLook(const FInputActionValue& V)
{
	const FVector2D L = V.Get<FVector2D>();
	FRotator R = SpringArm->GetRelativeRotation();
	R.Yaw += L.X * 2.f;
	R.Pitch = FMath::Clamp(R.Pitch + L.Y * 2.f, -70.f, 15.f);
	AimPitch = FMath::Clamp(-R.Pitch - 12.f, -5.f, 25.f);
	SpringArm->SetRelativeRotation(R);
	LookIdle = 0.f;
}

// ====================================================================== manager
namespace
{
	TArray<TWeakObjectPtr<ANCCraftManager>> GCraftManagers;
}

ANCCraftManager::ANCCraftManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANCCraftManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANCCraftManager, WindYaw);
	DOREPLIFETIME(ANCCraftManager, WindStrength);
}

ANCCraftManager* ANCCraftManager::Get(const UWorld* W)
{
	for (int32 i = GCraftManagers.Num() - 1; i >= 0; --i)
	{
		ANCCraftManager* M = GCraftManagers[i].Get();
		if (!M)
		{
			GCraftManagers.RemoveAt(i);
		}
		else if (M->GetWorld() == W)
		{
			return M;
		}
	}
	for (TActorIterator<ANCCraftManager> It(const_cast<UWorld*>(W)); It; ++It)
	{
		GCraftManagers.Add(*It);
		return *It;
	}
	return nullptr;
}

ANCCraftPawn* ANCCraftManager::SpawnOne(int32 Type, const FVector& Loc, float Yaw, int32 Spawn, int32 RouteIdx)
{
	if (!Types.IsValidIndex(Type))
	{
		return nullptr;
	}
	const FNCCraftType& D = Types[Type];
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FVector At(Loc.X, Loc.Y, ANCWaterManager::SurfaceZ(GetWorld(), Loc) + D.Deck * 0.5f - D.Draft);
	if (D.Kind == ENCCraftKind::Plane || D.Kind == ENCCraftKind::Heli || D.Kind == ENCCraftKind::Tank)
	{
		FHitResult G;
		FCollisionObjectQueryParams O;
		O.AddObjectTypesToQuery(ECC_WorldStatic);
		const float Top = Loc.Z > 100.f ? Loc.Z + 500.f : 100000.f;
		At.Z = (GetWorld()->LineTraceSingleByObjectType(G, FVector(Loc.X, Loc.Y, Top), FVector(Loc.X, Loc.Y, -10000.f), O)
			? G.ImpactPoint.Z : Loc.Z) + D.Height * 0.5f + 20.f;
	}
	ANCCraftPawn* C = GetWorld()->SpawnActor<ANCCraftPawn>(ANCCraftPawn::StaticClass(), FTransform(FRotator(0.f, Yaw, 0.f), At), P);
	if (C)
	{
		C->Init(this, Type, Spawn, RouteIdx);
	}
	return C;
}

ANCCraftPawn* ANCCraftManager::SpawnPoliceHeli(APawn* Target)
{
	if (!Target)
	{
		return nullptr;
	}
	const int32 T = Types.IndexOfByPredicate([](const FNCCraftType& X) { return X.Name.ToString().Contains(TEXT("Heli_Police")); });
	if (T == INDEX_NONE)
	{
		return nullptr;
	}
	// comes in from 250 m away, 120 m up
	const FVector2D Dir = FVector2D(FMath::VRand()).GetSafeNormal();
	const FVector At = Target->GetActorLocation() + FVector(Dir.X * 25000.f, Dir.Y * 25000.f, 12000.f);
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ANCCraftPawn* C = GetWorld()->SpawnActor<ANCCraftPawn>(ANCCraftPawn::StaticClass(),
		FTransform((Target->GetActorLocation() - At).Rotation(), At), P);
	if (C)
	{
		C->Init(this, T, INDEX_NONE, INDEX_NONE);
		C->bPoliceAI = true;
		C->ChaseTarget = Target;
		C->Power = 1.f;
	}
	return C;
}

void ANCCraftManager::BeginPlay()
{
	Super::BeginPlay();
	GCraftManagers.AddUnique(this);
	if (!HasAuthority())
	{
		return;
	}
	for (int32 i = 0; i < Spawns.Num(); ++i)
	{
		SpawnCraft.Add(SpawnOne(Spawns[i].Type, Spawns[i].Location, Spawns[i].Yaw, i, INDEX_NONE));
	}
	for (int32 r = 0; r < Routes.Num(); ++r)
	{
		FVector P;
		float Yaw;
		Routes[r].Eval(ANCWaterManager::WaveTime(GetWorld()), P, Yaw);
		RouteCraft.Add(SpawnOne(Routes[r].Type, P, Yaw, INDEX_NONE, r));
	}
	UE_LOG(LogTemp, Log, TEXT("[NeonCoast] craft: %d types, %d at berths, %d sea traffic"), Types.Num(), Spawns.Num(), Routes.Num());
}

void ANCCraftManager::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!HasAuthority())
	{
		return;
	}
	Accum += Dt;
	if (Accum < 2.f)
	{
		return;
	}
	Accum = 0.f;
	TArray<FVector> Players;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr)
		{
			Players.Add(P->GetActorLocation());
		}
	}
	auto NobodyNear = [&Players, this](const FVector& A)
	{
		for (const FVector& P : Players)
		{
			if (FVector::Dist2D(P, A) < RespawnDistance)
			{
				return false;
			}
		}
		return true;
	};
	// berths: sunk, or taken and left somewhere far from everybody -> back home
	for (int32 i = 0; i < Spawns.Num(); ++i)
	{
		ANCCraftPawn* C = SpawnCraft[i];
		const bool bGone = !IsValid(C);
		const bool bAbandoned = !bGone && !C->Driver && C->Passengers.Num() == 0 && !C->bWrecked
			&& FVector::Dist2D(C->GetActorLocation(), Spawns[i].Location) > 3000.f && NobodyNear(C->GetActorLocation());
		if ((bGone || bAbandoned) && NobodyNear(Spawns[i].Location))
		{
			if (bAbandoned)
			{
				C->Destroy();
			}
			SpawnCraft[i] = SpawnOne(Spawns[i].Type, Spawns[i].Location, Spawns[i].Yaw, i, INDEX_NONE);
		}
	}
	// sea traffic: a hijacked or sunk boat is replaced once nobody is around its loop
	for (int32 r = 0; r < Routes.Num(); ++r)
	{
		ANCCraftPawn* C = RouteCraft[r];
		const bool bGone = !IsValid(C);
		const bool bTaken = !bGone && C->Route == INDEX_NONE;
		if (bTaken && !C->Driver && !C->bWrecked && NobodyNear(C->GetActorLocation()))
		{
			C->Destroy();
		}
		if ((bGone || bTaken) && NobodyNear(Routes[r].Center))
		{
			FVector P;
			float Yaw;
			Routes[r].Eval(ANCWaterManager::WaveTime(GetWorld()), P, Yaw);
			RouteCraft[r] = SpawnOne(Routes[r].Type, P, Yaw, INDEX_NONE, r);
		}
	}
}

// ====================================================================== air traffic
ANCAirTraffic::ANCAirTraffic()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANCAirTraffic::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
		return;
	}
	const ANCCraftManager* CM = ANCCraftManager::Get(GetWorld());
	auto MakePart = [this](UStaticMesh* M, USceneComponent* Parent, const FVector& Pivot) -> UStaticMeshComponent*
	{
		if (!M)
		{
			return nullptr;
		}
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(M);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(Parent);
		C->SetRelativeLocation(Pivot);
		C->RegisterComponent();
		return C;
	};
	for (const FNCAirRoute& R : Routes)
	{
		UStaticMeshComponent* B = MakePart(R.Mesh, RootComponent, FVector::ZeroVector);
		if (B)
		{
			B->SetUsingAbsoluteLocation(true);
			B->SetUsingAbsoluteRotation(true);
			B->SetCastShadow(true);
		}
		const FNCCraftType* T = CM && CM->Types.IsValidIndex(R.CraftType) ? &CM->Types[R.CraftType] : nullptr;
		Bodies.Add(B);
		Rotors.Add(B && T ? MakePart(T->Part, B, T->PartPivot) : nullptr);
		TailRotors.Add(B && T ? MakePart(T->Part2, B, T->Part2Pivot) : nullptr);
		LastYaw.Add(0.f);
	}
}

FVector ANCAirTraffic::Eval(const FNCAirRoute& R, double T)
{
	const int32 N = R.Points.Num();
	if (N == 0)
	{
		return FVector::ZeroVector;
	}
	if (N == 1)
	{
		return FVector(R.Points[0]);
	}
	const double TT = FMath::Fmod(FMath::Fmod(T + R.Offset, static_cast<double>(R.Period)) + R.Period, static_cast<double>(R.Period));
	int32 i = N - 1;
	for (int32 k = 0; k < N; ++k)
	{
		const double T0 = R.Points[k].W;
		const double T1 = k + 1 < N ? R.Points[k + 1].W : R.Period;
		if (TT >= T0 && TT < T1)
		{
			i = k;
			break;
		}
	}
	const double T0 = R.Points[i].W;
	const double T1 = i + 1 < N ? R.Points[i + 1].W : R.Period;
	const float S = static_cast<float>(FMath::Clamp((TT - T0) / FMath::Max(0.001, T1 - T0), 0.0, 1.0));
	const FVector P0(R.Points[(i - 1 + N) % N]), P1(R.Points[i]), P2(R.Points[(i + 1) % N]), P3(R.Points[(i + 2) % N]);
	// Catmull-Rom
	const float S2 = S * S, S3 = S2 * S;
	return 0.5f * ((2.f * P1) + (-P0 + P2) * S + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * S2 + (-P0 + 3.f * P1 - 3.f * P2 + P3) * S3);
}

void ANCAirTraffic::Tick(float Dt)
{
	Super::Tick(Dt);
	const double T = ANCWaterManager::WaveTime(GetWorld());
	Spin = FMath::Fmod(Spin + 1300.f * Dt, 360.f);
	for (int32 r = 0; r < Routes.Num(); ++r)
	{
		UStaticMeshComponent* B = Bodies[r];
		if (!B)
		{
			continue;
		}
		const FVector P = Eval(Routes[r], T);
		const FVector Ahead = Eval(Routes[r], T + 0.5);
		const FVector V = Ahead - P;
		const FRotator Dir = V.SizeSquared() > 1.f ? V.Rotation() : FRotator(0.f, LastYaw[r], 0.f);
		const float YawRate = FMath::FindDeltaAngleDegrees(LastYaw[r], Dir.Yaw) / FMath::Max(Dt, 0.001f);
		LastYaw[r] = Dir.Yaw;
		const bool bHeli = Rotors[r] != nullptr && TailRotors[r] != nullptr;
		const float Roll = FMath::Clamp(YawRate * (bHeli ? 0.8f : 1.6f), -35.f, 35.f);
		const float Pitch = bHeli ? -8.f : FMath::Clamp(Dir.Pitch, -15.f, 20.f);
		// static meshes face -Y after import: turn them onto the flight direction
		const FQuat Q = FRotator(Pitch, Dir.Yaw, Roll).Quaternion() * FRotator(0.f, 90.f, 0.f).Quaternion();
		B->SetWorldLocationAndRotation(P, Q);
		if (UStaticMeshComponent* M = Rotors[r])
		{
			M->SetRelativeRotation(bHeli ? FRotator(0.f, Spin, 0.f) : FRotator(Spin * 2.f, 0.f, 0.f));
		}
		if (UStaticMeshComponent* M = TailRotors[r])
		{
			M->SetRelativeRotation(FRotator(0.f, 0.f, Spin * 2.f));
		}
	}
}
