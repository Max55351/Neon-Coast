#pragma warning(disable: 4456 4457 4458 4459)
#include "NCVehiclePawn.h"

#include "Camera/CameraComponent.h"
#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Net/UnrealNetwork.h"
#include "NCCarDamageComponent.h"
#include "NCVehicleAnimInstance.h"
#include "NCWheels.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "TimerManager.h"
#include "Components/RectLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NCHUD.h"
#include "NCPlayerState.h"
#include "Components/DecalComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "NCCrowdManager.h"
#include "NCSound.h"
#include "Components/AudioComponent.h"
#include "NCPoliceManager.h"
#include "NCTrafficManager.h"
#include "NCWater.h"
#include "NCInteractionComponent.h"
#include "Materials/MaterialInterface.h"

static const FName WheelBones[4] = {TEXT("wheel_fl"), TEXT("wheel_fr"), TEXT("wheel_rl"), TEXT("wheel_rr")};
const FName ANCVehiclePawn::PartBones[6] = {TEXT("door_fl"), TEXT("door_fr"), TEXT("door_rl"), TEXT("door_rr"), TEXT("hood"), TEXT("trunk")};

static void RestoreVehicleMaterials(USkeletalMeshComponent* Mesh)
{
	if (!Mesh) return;
	const TArray<FName> Slots = Mesh->GetMaterialSlotNames();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		UMaterialInterface* Current = Mesh->GetMaterial(i);
		if (Current && !Current->GetPathName().Contains(TEXT("WorldGridMaterial"))) continue;
		// Blueprint component overrides can retain the importer's placeholder
		// even after the skeletal mesh asset itself has been repaired.
		FString Name = Slots[i].ToString();
		if (!Name.StartsWith(TEXT("M_NC"))) continue;
		Name = TEXT("MI_") + Name.RightChop(2);
		const FString Path = TEXT("/Game/NeonCoast/Materials/Instances/") + Name + TEXT(".") + Name;
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Path))
		{
			Mesh->SetMaterial(i, Material);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[NeonCoast] missing vehicle material %s"), *Path);
		}
	}
}

ANCVehiclePawn::ANCVehiclePawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetCollisionProfileName(FName("Vehicle"));
	GetMesh()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	GetMesh()->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Block);
	GetMesh()->BodyInstance.bUseCCD = true;
	GetMesh()->SetAnimInstanceClass(UNCVehicleAnimInstance::StaticClass());
	GetMesh()->BodyInstance.bNotifyRigidBodyCollision = true;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(GetMesh());
	SpringArm->TargetArmLength = 720.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 210.f);
	SpringArm->SetRelativeRotation(FRotator(-12.f, 0.f, 0.f));
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritRoll = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->CameraLagSpeed = 7.f;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->CameraRotationLagSpeed = 5.f;
	SpringArm->bUsePawnControlRotation = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm);
	Camera->FieldOfView = 85.f;

	InteriorCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("InteriorCamera"));
	InteriorCamera->SetupAttachment(GetMesh());
	InteriorCamera->SetRelativeLocation(FVector(-10.f, -38.f, 115.f));
	InteriorCamera->FieldOfView = 90.f;
	InteriorCamera->SetAutoActivate(false);

	Damage = CreateDefaultSubobject<UNCCarDamageComponent>(TEXT("Damage"));

	if (UChaosWheeledVehicleMovementComponent* M = Move())
	{
		M->WheelSetups.SetNum(4);
		for (int32 i = 0; i < 4; ++i)
		{
			M->WheelSetups[i].WheelClass = i < 2 ? UNCWheelFront::StaticClass() : UNCWheelRear::StaticClass();
			M->WheelSetups[i].BoneName = WheelBones[i];
		}
		M->ChassisHeight = 140.f;
		M->DragCoefficient = 0.31f;
		M->DownforceCoefficient = 0.3f;
		M->bEnableCenterOfMassOverride = true;
		M->CenterOfMassOverride = FVector(0.f, 0.f, 30.f);
		M->EngineSetup.MaxRPM = 6500.f;
		M->EngineSetup.EngineIdleRPM = 900.f;
		M->EngineSetup.EngineBrakeEffect = 0.2f;
		M->EngineSetup.EngineRevUpMOI = 5.f;
		M->EngineSetup.EngineRevDownRate = 600.f;
	{
		// a torque curve on the default object, otherwise Chaos (5.8) switches the engine off for good at spawn
		FRichCurve* C0 = M->EngineSetup.TorqueCurve.GetRichCurve();
		C0->Reset();
		C0->AddKey(0.f, 0.55f);
		C0->AddKey(0.35f, 0.9f);
		C0->AddKey(0.7f, 1.0f);
		C0->AddKey(1.0f, 0.8f);
	}
		M->TransmissionSetup.bUseAutomaticGears = true;
		M->TransmissionSetup.bUseAutoReverse = true;
		M->SteeringSetup.SteeringType = ESteeringType::AngleRatio;
		M->SteeringSetup.AngleRatio = 0.7f;
	}
}

UChaosWheeledVehicleMovementComponent* ANCVehiclePawn::Move() const
{
	return Cast<UChaosWheeledVehicleMovementComponent>(const_cast<ANCVehiclePawn*>(this)->GetVehicleMovementComponent());
}

#include "PhysicsEngine/PhysicsAsset.h"
#include "Components/CapsuleComponent.h"
#include "AnimationRuntime.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/BoxElem.h"
#include "UObject/Package.h"
#include "RenderingThread.h"
#if WITH_EDITOR
#include "SkinnedAssetCompiler.h"
#endif

// UE 5.8's importer leaves the car meshes without a physics asset; Chaos vehicles need a
// simulated chassis body, so build one box from the mesh bounds (above the wheels) once per mesh.
static void EnsureChassisBody(USkeletalMesh* SKM)
{
	if (!SKM || SKM->GetPhysicsAsset() || SKM->GetRefSkeleton().GetNum() == 0)
	{
		return;
	}
	// Chaos works in unscaled root-body centimetres. Imported FBX roots carry
#if WITH_EDITOR
	USkinnedAsset* Assets[] = {SKM};
	FSkinnedAssetCompilingManager::Get().FinishCompilation(Assets);
#endif
	// The render thread can already reference a newly loaded Blueprint's mesh.
	// Never rebuild its inverse bind matrices concurrently with skinning.
	FlushRenderingCommands();
	// 100x scale and a 90-degree yaw; otherwise wheel offsets become 1/100 size
	// and the physics vehicle's forward axis differs from its visible chassis.
	// Normalize the reference hierarchy in memory while preserving every joint
	// position and the bind-pose geometry (rebuild inverse bind matrices below).
	FReferenceSkeleton& Reference = SKM->GetRefSkeleton();
	const int32 ChassisIndex = Reference.FindBoneIndex(TEXT("root"));
	const int32 ChassisBone = ChassisIndex == INDEX_NONE ? 0 : ChassisIndex;
	const FTransform ImportedRoot = FAnimationRuntime::GetComponentSpaceTransformRefPose(Reference, ChassisBone);
	if (!ImportedRoot.GetScale3D().Equals(FVector::OneVector, 0.01f) || !ImportedRoot.GetRotation().Equals(FQuat::Identity, 0.01f))
	{
		TArray<FTransform> Normalized;
		for (int32 i = 0; i < Reference.GetRawBoneNum(); ++i)
		{
			FTransform Pose = FAnimationRuntime::GetComponentSpaceTransformRefPose(Reference, i);
			Pose.SetScale3D(FVector::OneVector);
			if (i == ChassisBone || i == 0) Pose.SetRotation(FQuat::Identity);
			Normalized.Add(Pose);
		}
		{
			FReferenceSkeletonModifier Modifier(Reference, SKM->GetSkeleton());
			for (int32 i = 0; i < Normalized.Num(); ++i)
			{
				const int32 Parent = Reference.GetParentIndex(i);
				Modifier.UpdateRefPoseTransform(i, Parent == INDEX_NONE ? Normalized[i] : Normalized[i].GetRelativeTransform(Normalized[Parent]));
			}
		}
		SKM->GetRefBasesInvMatrix().Reset();
		SKM->CalculateInvRefMatrices();
	}
	const FBoxSphereBounds B = SKM->GetImportedBounds();
	const FVector Min = B.Origin - B.BoxExtent;
	const float H = 2.f * B.BoxExtent.Z;
	UPhysicsAsset* PA = NewObject<UPhysicsAsset>(GetTransientPackage(), NAME_None, RF_Transient);
	USkeletalBodySetup* BS = NewObject<USkeletalBodySetup>(PA, NAME_None, RF_Transient);
	// the FBX armature bone (index 0) carries the 100x import scale - a body on it becomes a 450 m giant,
	// so the chassis goes on the real 'root' bone (unit scale) when there is one
	const int32 RootIdx = SKM->GetRefSkeleton().FindBoneIndex(TEXT("root"));
	BS->BoneName = SKM->GetRefSkeleton().GetBoneName(RootIdx != INDEX_NONE ? RootIdx : 0);
	BS->PhysicsType = PhysType_Default;
	BS->CollisionTraceFlag = CTF_UseSimpleAsComplex;
	// the imported bones carry the FBX unit scale (and maybe a rotation): build the box in component
	// space and convert it into the bone's own space so it ends up the size of the car
	const int32 BoneIdx = RootIdx != INDEX_NONE ? RootIdx : 0;
	const FTransform BT = FAnimationRuntime::GetComponentSpaceTransformRefPose(SKM->GetRefSkeleton(), BoneIdx);
	const FVector Sc = BT.GetScale3D();
	const float US = FMath::Max(KINDA_SMALL_NUMBER, FMath::Abs(Sc.X));
	UE_LOG(LogTemp, Display, TEXT("[NCSELFTEST] chassis bone %s ref pose loc=%s scale=%s rot=%s"), *BS->BoneName.ToString(), *BT.GetLocation().ToCompactString(), *Sc.ToCompactString(), *BT.GetRotation().Rotator().ToCompactString());
	FKBoxElem Box(2.f * B.BoxExtent.X * 0.95f / US, 2.f * B.BoxExtent.Y * 0.9f / US, H * 0.6f / US);
	Box.Center = BT.InverseTransformPosition(FVector(B.Origin.X, B.Origin.Y, Min.Z + H * 0.38f + H * 0.3f));
	Box.Rotation = BT.GetRotation().Inverse().Rotator();
	BS->AggGeom.BoxElems.Add(Box);
	BS->CreatePhysicsMeshes();
	PA->SkeletalBodySetups.Add(BS);
	PA->UpdateBodySetupIndexMap();
	PA->UpdateBoundsBodiesArray();
	SKM->SetPhysicsAsset(PA);
}
void ANCVehiclePawn::PreRegisterAllComponents()
{
	// the Chaos movement component looks up the wheel bones while registering, so the mesh
	// has to be there before that (OnConstruction runs too late for spawned cars)
	if (VehicleMesh && GetMesh() && GetMesh()->GetSkeletalMeshAsset() != VehicleMesh)
	{
		GetMesh()->SetSkeletalMeshAsset(VehicleMesh);
	}
	if (GetMesh())
	{
		EnsureChassisBody(GetMesh()->GetSkeletalMeshAsset());
	}
	Super::PreRegisterAllComponents();
	RestoreVehicleMaterials(GetMesh());
}

void ANCVehiclePawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (VehicleMesh && GetMesh()->GetSkeletalMeshAsset() != VehicleMesh)
	{
		GetMesh()->SetSkeletalMeshAsset(VehicleMesh);
	}
}

void ANCVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	RestoreVehicleMaterials(GetMesh());
	// the wheels are driven by the vehicle simulation, not by physics bodies
	// Older Blueprint defaults can place the camera at the chassis origin or
	// leave the interior camera active. Reset the chase camera in centimetres.
	SpringArm->SetRelativeLocation(FVector(0.f, 0.f, 110.f));
	SpringArm->SetRelativeRotation(FRotator(-12.f, 0.f, 0.f));
	SpringArm->SetRelativeScale3D(FVector::OneVector);
	SpringArm->TargetArmLength = 720.f;
	SpringArm->SocketOffset = FVector(0.f, 0.f, 90.f);
	Camera->AttachToComponent(SpringArm, FAttachmentTransformRules::SnapToTargetNotIncludingScale, USpringArmComponent::SocketName);
	Camera->SetRelativeTransform(FTransform::Identity);
	Camera->SetActive(true);
	InteriorCamera->SetActive(false);
	for (const FName& B : WheelBones)
	{
		if (GetMesh()->GetBodyInstance(B))
		{
			GetMesh()->TermBodiesBelow(B);
		}
	}
	ApplySpec();
	if (Seats.Num() > 0)
	{
		InteriorCamera->SetRelativeLocation(Seats[0] + FVector(-12.f, 0.f, 72.f));
	}
	Passengers.SetNum(FMath::Max(0, Seats.Num() - 1));
	// dynamic glass materials so windows can shatter
	const TArray<FName> Slots = GetMesh()->GetMaterialSlotNames();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].ToString().Contains(TEXT("Glass")))
		{
			GlassMIDs.Add(GetMesh()->CreateAndSetMaterialInstanceDynamic(i));
		}
	}
	if (!FireFX) FireFX = LoadObject<UParticleSystem>(nullptr, TEXT("/Game/StarterContent/Particles/P_Fire.P_Fire"));
	if (!ExplosionFX) ExplosionFX = LoadObject<UParticleSystem>(nullptr, TEXT("/Game/StarterContent/Particles/P_Explosion.P_Explosion"));
	if (!GlassFX) GlassFX = LoadObject<UParticleSystem>(nullptr, TEXT("/Game/StarterContent/Particles/P_Sparks.P_Sparks"));
	if (TyreWear.Num() != 4)
	{
		TyreWear.Init(0, 4);
	}
	InitDriftFX();
	ApplyBodyVisuals();
}

void ANCVehiclePawn::ApplySpec()
{
	UChaosWheeledVehicleMovementComponent* M = Move();
	if (!M)
	{
		return;
	}
	const bool bAWD = Spec.Drivetrain == ENCDrivetrain::AWD_4x4 || Spec.Drivetrain == ENCDrivetrain::AWD_4x4_LowRange;
	M->Mass = Spec.MassKg;
	GetMesh()->SetMassOverrideInKg(NAME_None, Spec.MassKg, true);
	M->EngineSetup.MaxTorque = Spec.MaxTorqueNm;
	M->EngineSetup.MaxRPM = Spec.MaxRPM;
	FRichCurve* Curve = M->EngineSetup.TorqueCurve.GetRichCurve();
	Curve->Reset();
	Curve->AddKey(0.f, 0.55f);
	Curve->AddKey(0.35f, 0.9f);
	Curve->AddKey(0.7f, 1.0f);
	Curve->AddKey(1.0f, 0.8f);
	M->DifferentialSetup.DifferentialType = bAWD ? EVehicleDifferential::AllWheelDrive
		: (Spec.Drivetrain == ENCDrivetrain::FWD ? EVehicleDifferential::FrontWheelDrive : EVehicleDifferential::RearWheelDrive);
	M->DifferentialSetup.FrontRearSplit = bAWD ? 0.45f : 0.5f;
	M->ChassisHeight = Spec.bOffroad ? 175.f : 140.f;
	M->EnableMechanicalSim(true);
	M->RecreatePhysicsState();

	const float TravelCm = Spec.SuspensionTravelM * 100.f;
	for (int32 i = 0; i < 4; ++i)
	{
		M->SetWheelRadius(i, Spec.WheelRadiusM * 100.f);
		{ const UChaosVehicleWheel* Wh = M->Wheels.IsValidIndex(i) ? M->Wheels[i].Get() : nullptr; M->SetSuspensionParams(Wh ? Wh->SpringRate : 250.f, Wh ? Wh->SuspensionDampingRatio : 0.5f, Wh ? Wh->SpringPreload : 50.f, TravelCm * 0.45f, TravelCm * 0.55f, i); }
		if (i < 2)
		{
			M->SetWheelMaxSteerAngle(i, Spec.MaxSteerDeg);
		}
		const bool bDriven = bAWD || (Spec.Drivetrain == ENCDrivetrain::FWD ? i < 2 : i >= 2);
		M->SetAffectedByEngine(i, bDriven);
	}
	OnRep_Drive();
}

void ANCVehiclePawn::OnRep_Drive()
{
	UChaosWheeledVehicleMovementComponent* M = Move();
	if (!M)
	{
		return;
	}
	M->SetMaxEngineTorque(EffectiveTorque());
	// diff lock: no traction control (wheels may spin together), extra grip in UpdateSurfaceGrip
	for (int32 i = 0; i < 4; ++i)
	{
		M->SetTractionControlEnabled(i, !bDiffLock);
	}
}

float ANCVehiclePawn::GetSpeedKmh() const
{
	const UChaosWheeledVehicleMovementComponent* M = Move();
	return M ? FMath::Abs(M->GetForwardSpeed()) * 0.036f : 0.f;
}

void ANCVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Move() && ANCWaterManager::Get(GetWorld()))
	{
		const auto Bounds=GetMesh()->Bounds;
		const float Surface=ANCWaterManager::SurfaceZ(GetWorld(),Bounds.Origin);
		const float Wet=FMath::Clamp((Surface-(Bounds.Origin.Z-Bounds.BoxExtent.Z))/FMath::Max(1.f,Bounds.BoxExtent.Z*2.f),0.f,1.f);
		if (Wet>.6f) { Move()->SetThrottleInput(0.f); Move()->SetBrakeInput(0.f); }
		// Cars retain gravity (no boat buoyancy), while water damps their momentum.
		if (Wet>0.f && GetMesh()->IsSimulatingPhysics() && (HasAuthority() || IsLocallyControlled()))
			GetMesh()->AddForce(-GetMesh()->GetPhysicsLinearVelocity()*GetMesh()->GetMass()*Wet*.8f);
	}
	SurfaceTimer -= DeltaSeconds;
	if (SurfaceTimer <= 0.f)
	{
		SurfaceTimer = 0.2f;
		UpdateSurfaceGrip();
	}
	// low range caps the speed like a real transfer case
	if (bLowRange && IsLocallyControlled() && GetSpeedKmh() > 45.f)
	{
		Move()->SetThrottleInput(0.f);
	}
	// top speed limiter from the spec
	if (IsLocallyControlled() && GetSpeedKmh() > Spec.TopSpeedKmh)
	{
		Move()->SetThrottleInput(0.f);
	}
	UpdateDrift(DeltaSeconds);
	UpdateSirenAndRunOver(DeltaSeconds);
	UpdateAudio(DeltaSeconds);
	// fuel: burns with the throttle; an empty tank only coasts (police / AI cars never run dry)
	if (Move())
	{
		const bool bAI = GetController() && !GetController()->IsPlayerController();
		if (HasAuthority() && !bAI)
		{
			Fuel = FMath::Max(0.f, Fuel - FMath::Abs(Move()->GetThrottleInput()) * DeltaSeconds * 0.11f);
		}
		if (Fuel <= 0.f && IsLocallyControlled() && !bAI)
		{
			Move()->SetThrottleInput(0.f);
		}
	}
}

void ANCVehiclePawn::UpdateAudio(float Dt)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UChaosWheeledVehicleMovementComponent* M = Move();
	if (!EngineSound)
	{
		const FName Id = Spec.MaxTorqueNm > 900.f ? FName(TEXT("Engine_Truck")) : (Spec.TopSpeedKmh > 230.f ? FName(TEXT("Engine_Sport")) : FName(TEXT("Engine_Car")));
		EngineSound = ANCSoundBank::Loop(GetMesh(), Id, 0.f);
		ScreechSound = ANCSoundBank::Loop(GetMesh(), TEXT("Screech"), 0.f);
		SirenSound = ANCSoundBank::Loop(GetMesh(), TEXT("Siren"), 0.f);
		if (!EngineSound)
		{
			return;
		}
	}
	// engine: pitch follows the rpm, louder under throttle; off when parked, empty or burnt out
	const bool bOn = !Body.bWrecked && (Driver != nullptr || GetController() != nullptr);
	const float Rpm = M ? M->GetEngineRotationSpeed() : 0.f;
	const float MaxRpm = M ? FMath::Max(1000.f, M->GetEngineMaxRotationSpeed()) : 6000.f;
	const float Thr = M ? FMath::Abs(M->GetThrottleInput()) : 0.f;
	EngineSound->SetPitchMultiplier(FMath::Clamp(0.55f + 1.6f * Rpm / MaxRpm, 0.5f, 2.4f));
	EngineSound->SetVolumeMultiplier(FMath::FInterpTo(EngineSound->VolumeMultiplier, bOn ? 0.35f + 0.55f * Thr : 0.f, Dt, 4.f));
	if (ScreechSound)
	{
		const float Squeal = (bBurnout ? 1.f : FMath::Clamp((FMath::Abs(DriftAngle) - 10.f) / 25.f, 0.f, 1.f)) * (GetSpeedKmh() > 8.f || bBurnout ? 1.f : 0.f);
		ScreechSound->SetVolumeMultiplier(FMath::FInterpTo(ScreechSound->VolumeMultiplier, Squeal * 0.8f, Dt, 8.f));
	}
	if (SirenSound)
	{
		SirenSound->SetVolumeMultiplier(bSiren ? 0.9f : 0.f);
	}
	if (Body.bWrecked && !bExplodeHeard)
	{
		bExplodeHeard = true;
		ANCSoundBank::Play(this, TEXT("Explosion"), GetActorLocation(), 1.2f);
	}
}

void ANCVehiclePawn::UpdateSurfaceGrip()
{
	UChaosWheeledVehicleMovementComponent* M = Move();
	if (!M)
	{
		return;
	}
	for (int32 i = 0; i < M->Wheels.Num(); ++i)
	{
		UChaosVehicleWheel* W = M->Wheels[i];
		if (!W)
		{
			continue;
		}
		const UPhysicalMaterial* PM = W->GetContactSurfaceMaterial();
		const FString S = PM ? PM->GetName() : FString();
		float Grip = 1.f;   // asphalt / concrete
		if (S.Contains(TEXT("Sand")))
		{
			Grip = Spec.bOffroad ? 0.85f : 0.45f;
		}
		else if (S.Contains(TEXT("Dirt")) || S.Contains(TEXT("Grass")) || S.Contains(TEXT("Mud")))
		{
			Grip = Spec.bOffroad ? 0.95f : 0.6f;
		}
		if (bDiffLock)
		{
			Grip *= 1.1f;
		}
		if (Body.FlatMask & (1 << i))
		{
			Grip *= 0.35f;                     // burst tyre: rim on the road
		}
		const float F = 3.0f * Grip * (1.f + 0.1f * Tuning.Tires) * (1.f + 0.05f * Tuning.Suspension);
		if (i < 4)
		{
			BaseFriction[i] = F;              // drift / burnout scale the rear pair from this
		}
		M->SetWheelFrictionMultiplier(i, F);
	}
}

// ------------------------------------------------------------------ input
void ANCVehiclePawn::CreateInput()
{
	if (Mapping)
	{
		return;
	}
	auto Make = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = Type;
		return A;
	};
	ThrottleAction = Make(TEXT("IA_NC_Throttle"), EInputActionValueType::Axis1D);
	BrakeAction = Make(TEXT("IA_NC_Brake"), EInputActionValueType::Axis1D);
	SteerAction = Make(TEXT("IA_NC_Steer"), EInputActionValueType::Axis1D);
	HandbrakeAction = Make(TEXT("IA_NC_Handbrake"), EInputActionValueType::Boolean);
	LookAction = Make(TEXT("IA_NC_Look"), EInputActionValueType::Axis2D);
	ExitAction = Make(TEXT("IA_NC_Exit"), EInputActionValueType::Boolean);
	LowRangeAction = Make(TEXT("IA_NC_LowRange"), EInputActionValueType::Boolean);
	DiffLockAction = Make(TEXT("IA_NC_DiffLock"), EInputActionValueType::Boolean);
	ResetAction = Make(TEXT("IA_NC_Reset"), EInputActionValueType::Boolean);
	CameraAction = Make(TEXT("IA_NC_Camera"), EInputActionValueType::Boolean);
	DoorAction = Make(TEXT("IA_NC_Door"), EInputActionValueType::Boolean);
	MenuUpAction = Make(TEXT("IA_NC_MenuUp"), EInputActionValueType::Boolean);
	MenuDownAction = Make(TEXT("IA_NC_MenuDown"), EInputActionValueType::Boolean);
	MenuLeftAction = Make(TEXT("IA_NC_MenuLeft"), EInputActionValueType::Boolean);
	MenuRightAction = Make(TEXT("IA_NC_MenuRight"), EInputActionValueType::Boolean);
	MenuAcceptAction = Make(TEXT("IA_NC_MenuAccept"), EInputActionValueType::Boolean);
	MenuBackAction = Make(TEXT("IA_NC_MenuBack"), EInputActionValueType::Boolean);

	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_NC_Vehicle"));
	Mapping->MapKey(ThrottleAction, EKeys::W);
	Mapping->MapKey(ThrottleAction, EKeys::Gamepad_RightTriggerAxis);
	Mapping->MapKey(BrakeAction, EKeys::S);
	Mapping->MapKey(BrakeAction, EKeys::Gamepad_LeftTriggerAxis);
	Mapping->MapKey(SteerAction, EKeys::D);
	FEnhancedActionKeyMapping& Left = Mapping->MapKey(SteerAction, EKeys::A);
	Left.Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
	Mapping->MapKey(SteerAction, EKeys::Gamepad_LeftX);
	Mapping->MapKey(HandbrakeAction, EKeys::SpaceBar);
	Mapping->MapKey(HandbrakeAction, EKeys::Gamepad_FaceButton_Right);
	Mapping->MapKey(LookAction, EKeys::Mouse2D);
	Mapping->MapKey(LookAction, EKeys::Gamepad_Right2D);
	// F / Y belongs to the persistent player controller, not a replaced pawn input.
	Mapping->MapKey(LowRangeAction, EKeys::L);
	Mapping->MapKey(LowRangeAction, EKeys::Gamepad_DPad_Down);
	Mapping->MapKey(DiffLockAction, EKeys::K);
	Mapping->MapKey(DiffLockAction, EKeys::Gamepad_DPad_Up);
	Mapping->MapKey(ResetAction, EKeys::R);
	Mapping->MapKey(ResetAction, EKeys::Gamepad_Special_Left);
	Mapping->MapKey(CameraAction, EKeys::V);
	Mapping->MapKey(CameraAction, EKeys::Gamepad_RightThumbstick);
	Mapping->MapKey(DoorAction, EKeys::O);
	Mapping->MapKey(DoorAction, EKeys::Gamepad_LeftShoulder);
	Mapping->MapKey(MenuUpAction, EKeys::Up);
	Mapping->MapKey(MenuDownAction, EKeys::Down);
	Mapping->MapKey(MenuLeftAction, EKeys::Left);
	Mapping->MapKey(MenuLeftAction, EKeys::Gamepad_DPad_Left);
	Mapping->MapKey(MenuRightAction, EKeys::Right);
	Mapping->MapKey(MenuRightAction, EKeys::Gamepad_DPad_Right);
	Mapping->MapKey(MenuAcceptAction, EKeys::Enter);
	Mapping->MapKey(MenuAcceptAction, EKeys::Gamepad_FaceButton_Bottom);
	Mapping->MapKey(MenuBackAction, EKeys::BackSpace);
	Mapping->MapKey(MenuBackAction, EKeys::Gamepad_Special_Right);
}

void ANCVehiclePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
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
		EIC->BindAction(ThrottleAction, ETriggerEvent::Triggered, this, &ANCVehiclePawn::OnThrottle);
		EIC->BindAction(ThrottleAction, ETriggerEvent::Completed, this, &ANCVehiclePawn::OnThrottle);
		EIC->BindAction(BrakeAction, ETriggerEvent::Triggered, this, &ANCVehiclePawn::OnBrake);
		EIC->BindAction(BrakeAction, ETriggerEvent::Completed, this, &ANCVehiclePawn::OnBrake);
		EIC->BindAction(SteerAction, ETriggerEvent::Triggered, this, &ANCVehiclePawn::OnSteer);
		EIC->BindAction(SteerAction, ETriggerEvent::Completed, this, &ANCVehiclePawn::OnSteer);
		EIC->BindAction(HandbrakeAction, ETriggerEvent::Triggered, this, &ANCVehiclePawn::OnHandbrake);
		EIC->BindAction(HandbrakeAction, ETriggerEvent::Completed, this, &ANCVehiclePawn::OnHandbrake);
		EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ANCVehiclePawn::OnLook);
		EIC->BindAction(ExitAction, ETriggerEvent::Started, this, &ANCVehiclePawn::OnExit);
		EIC->BindAction(LowRangeAction, ETriggerEvent::Started, this, &ANCVehiclePawn::OnLowRange);
		EIC->BindAction(DiffLockAction, ETriggerEvent::Started, this, &ANCVehiclePawn::OnDiffLock);
		EIC->BindAction(ResetAction, ETriggerEvent::Started, this, &ANCVehiclePawn::OnReset);
		EIC->BindAction(CameraAction, ETriggerEvent::Started, this, &ANCVehiclePawn::OnToggleCamera);
		EIC->BindAction(DoorAction, ETriggerEvent::Started, this, &ANCVehiclePawn::OnToggleDoor);
		EIC->BindAction(MenuUpAction, ETriggerEvent::Started, this, &ANCVehiclePawn::OnMenuUp);
		EIC->BindAction(MenuDownAction, ETriggerEvent::Started, this, &ANCVehiclePawn::OnMenuDown);
		EIC->BindAction(MenuLeftAction, ETriggerEvent::Started, this, &ANCVehiclePawn::OnMenuLeft);
		EIC->BindAction(MenuRightAction, ETriggerEvent::Started, this, &ANCVehiclePawn::OnMenuRight);
		EIC->BindAction(MenuAcceptAction, ETriggerEvent::Started, this, &ANCVehiclePawn::OnMenuAccept);
		EIC->BindAction(MenuBackAction, ETriggerEvent::Started, this, &ANCVehiclePawn::OnMenuBack);
	}
}

void ANCVehiclePawn::OnThrottle(const FInputActionValue& V) { Move()->SetThrottleInput(V.Get<float>()); }
void ANCVehiclePawn::OnBrake(const FInputActionValue& V) { Move()->SetBrakeInput(V.Get<float>()); }
void ANCVehiclePawn::OnSteer(const FInputActionValue& V) { Move()->SetSteeringInput(V.Get<float>()); }
void ANCVehiclePawn::OnHandbrake(const FInputActionValue& V) { Move()->SetHandbrakeInput(V.Get<bool>()); }

void ANCVehiclePawn::OnLook(const FInputActionValue& V)
{
	const FVector2D L = V.Get<FVector2D>();
	FRotator R = SpringArm->GetRelativeRotation();
	R.Yaw += L.X * 2.f;
	R.Pitch = FMath::Clamp(R.Pitch + L.Y * 2.f, -60.f, 10.f);
	SpringArm->SetRelativeRotation(R);
}

void ANCVehiclePawn::OnExit(const FInputActionValue&) { ServerExit(); }
void ANCVehiclePawn::OnLowRange(const FInputActionValue&)
{
	if (ANCHUD* H = MenuHud()) { H->MenuMove(1, 0); return; }
	ServerToggleLowRange();
}
void ANCVehiclePawn::OnDiffLock(const FInputActionValue&)
{
	if (ANCHUD* H = MenuHud()) { H->MenuMove(-1, 0); return; }
	ServerToggleDiffLock();
}

ANCHUD* ANCVehiclePawn::MenuHud() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	ANCHUD* H = PC ? Cast<ANCHUD>(PC->GetHUD()) : nullptr;
	return (H && H->IsTuningOpen()) ? H : nullptr;
}

void ANCVehiclePawn::OnMenuUp(const FInputActionValue&) { if (ANCHUD* H = MenuHud()) H->MenuMove(-1, 0); }
void ANCVehiclePawn::OnMenuDown(const FInputActionValue&) { if (ANCHUD* H = MenuHud()) H->MenuMove(1, 0); }
void ANCVehiclePawn::OnMenuLeft(const FInputActionValue&) { if (ANCHUD* H = MenuHud()) H->MenuMove(0, -1); }
void ANCVehiclePawn::OnMenuRight(const FInputActionValue&) { if (ANCHUD* H = MenuHud()) H->MenuMove(0, 1); }
void ANCVehiclePawn::OnMenuAccept(const FInputActionValue&) { if (ANCHUD* H = MenuHud()) H->MenuAccept(); }
void ANCVehiclePawn::OnMenuBack(const FInputActionValue&) { if (ANCHUD* H = MenuHud()) H->CloseTuning(false); }
void ANCVehiclePawn::OnReset(const FInputActionValue&) { ServerReset(); }

void ANCVehiclePawn::ServerToggleLowRange_Implementation()
{
	if (Spec.Drivetrain == ENCDrivetrain::AWD_4x4_LowRange)
	{
		bLowRange = !bLowRange;
		OnRep_Drive();
	}
}

void ANCVehiclePawn::ServerToggleDiffLock_Implementation()
{
	if (Spec.Drivetrain == ENCDrivetrain::AWD_4x4 || Spec.Drivetrain == ENCDrivetrain::AWD_4x4_LowRange)
	{
		bDiffLock = !bDiffLock;
		OnRep_Drive();
	}
}

void ANCVehiclePawn::ServerReset_Implementation()
{
	// put the car back on its wheels (after a rollover)
	FRotator R = GetActorRotation();
	R.Pitch = 0.f;
	R.Roll = 0.f;
	SetActorLocationAndRotation(GetActorLocation() + FVector(0, 0, 150.f), R, false, nullptr, ETeleportType::TeleportPhysics);
	GetMesh()->SetPhysicsLinearVelocity(FVector::ZeroVector);
	GetMesh()->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
}

// ------------------------------------------------------------------ enter / exit
void ANCVehiclePawn::EnterVehicle(APawn* Character)
{
	if (!HasAuthority() || !Character || Driver)
	{
		return;
	}
	AController* C = Character->GetController();
	if (!C)
	{
		return;
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
	SetPartOpen(0, true);
	GetWorldTimerManager().SetTimer(DoorTimer, [this]() { SetPartOpen(0, false); }, 1.2f, false);
}

void ANCVehiclePawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	Move()->SetThrottleInput(0.f);
	Move()->SetBrakeInput(0.f);
	Move()->SetSteeringInput(0.f);
	Move()->SetHandbrakeInput(false);
}

void ANCVehiclePawn::UnPossessed()
{
	// Enhanced Input contexts outlive the pawn's input component. Leaving this
	// high-priority context installed consumed W/A/S/D after returning on foot.
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (PC->GetLocalPlayer())
		{
			if (auto* Sub = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (Mapping) Sub->RemoveMappingContext(Mapping);
			}
		}
	}
	Super::UnPossessed();
}

void ANCVehiclePawn::RemoveLocalControls(APlayerController* PC)
{
	if (PC && PC->IsLocalController() && PC->GetPawn()!=this && PC->GetLocalPlayer())
		if (auto* Sub=PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>()) if (Mapping) Sub->RemoveMappingContext(Mapping);
}

static bool FindVehicleExit(ANCVehiclePawn* Vehicle, APawn* Character, FVector& Result)
{
	const auto* Ch=Cast<ACharacter>(Character);
	const float H=Ch?Ch->GetCapsuleComponent()->GetScaledCapsuleHalfHeight():90.f;
	const float R=Ch?Ch->GetCapsuleComponent()->GetScaledCapsuleRadius():34.f;
	const auto* Mesh=Vehicle->GetMesh();
	const FBox Bounds=Mesh->CalcBounds(FTransform::Identity).GetBox();
	const FTransform Transform=Mesh->GetComponentTransform();
	const FVector Center=Bounds.GetCenter();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(NCValidatedCarExit),true,Character);
	FCollisionQueryParams GroundQuery=Query; GroundQuery.AddIgnoredActor(Vehicle);
	for (float Extra : {90.f,180.f,320.f})
	{
		const FVector Scale=Transform.GetScale3D().GetAbs();
		const float DX=(R+Extra)/FMath::Max(.01f,Scale.X), DY=(R+Extra)/FMath::Max(.01f,Scale.Y);
		const FVector Candidates[]={FVector(Center.X,Bounds.Min.Y-DY,Center.Z),FVector(Center.X,Bounds.Max.Y+DY,Center.Z),
			FVector(Bounds.Min.X-DX,Center.Y,Center.Z),FVector(Bounds.Max.X+DX,Center.Y,Center.Z)};
		for (const FVector& Local : Candidates)
		{
			FVector Point=Transform.TransformPosition(Local);
			FHitResult Ground;
			if (Vehicle->GetWorld()->LineTraceSingleByChannel(Ground,Point+FVector(0,0,450),Point-FVector(0,0,900),ECC_Pawn,GroundQuery))
			{
				if (Ground.ImpactNormal.Z<.65f) continue;
				Point=Ground.ImpactPoint+FVector(0,0,H+10.f);
			}
			else if (!(ANCWaterManager::Get(Vehicle->GetWorld()) && Point.Z<ANCWaterManager::SurfaceZ(Vehicle->GetWorld(),Point))) continue;
			// Unlike the old test, the vehicle itself remains in the clearance query.
			if (!Vehicle->GetWorld()->OverlapBlockingTestByChannel(Point,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(R,H),Query))
			{ Result=Point; return true; }
		}
	}
	return false;
}

void ANCVehiclePawn::ServerExit_Implementation()
{
	APawn* D = Driver;
	AController* C = GetController();
	if (!D || !C)
	{
		return;
	}
	FVector ValidatedExit;
	if (!FindVehicleExit(this,D,ValidatedExit))
	{
		if (auto* PS=C->GetPlayerState<ANCPlayerState>()) PS->ClientBanner(TEXT("Vedle auta neni volne misto. Popojed a zkus F znovu."),false);
		return;
	}
	Move()->SetThrottleInput(0.f);
	Move()->SetHandbrakeInput(true);
	D->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	D->SetActorLocation(ValidatedExit, false, nullptr, ETeleportType::TeleportPhysics);
	D->SetActorRotation(FRotator(0.f, GetActorRotation().Yaw, 0.f));
	D->SetActorHiddenInGame(false);
	D->SetActorEnableCollision(true);
	if (ACharacter* Ch = Cast<ACharacter>(D))
	{
		Ch->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		Ch->GetCharacterMovement()->SetUpdatedComponent(Ch->GetCapsuleComponent());
		Ch->GetCharacterMovement()->SetComponentTickEnabled(true);
		Ch->GetCharacterMovement()->Activate(true);
		Ch->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	}
	Driver = nullptr;
	C->Possess(D);
	if (APlayerController* PC = Cast<APlayerController>(C))
	{
		PC->ResetIgnoreInputFlags();
		D->EnableInput(PC);
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetViewTarget(D);
	}
	if (auto* Interaction=D->FindComponentByClass<UNCInteractionComponent>()) Interaction->RestoreAfterExit();
	SetPartOpen(0, true);
	GetWorldTimerManager().SetTimer(DoorTimer, [this]() { SetPartOpen(0, false); }, 1.2f, false);
}

void ANCVehiclePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANCVehiclePawn, Driver);
	DOREPLIFETIME(ANCVehiclePawn, bLowRange);
	DOREPLIFETIME(ANCVehiclePawn, bDiffLock);
	DOREPLIFETIME(ANCVehiclePawn, Passengers);
	DOREPLIFETIME(ANCVehiclePawn, Body);
	DOREPLIFETIME(ANCVehiclePawn, Tuning);
	DOREPLIFETIME(ANCVehiclePawn, TyreWear);
	DOREPLIFETIME(ANCVehiclePawn, bSiren);
	DOREPLIFETIME(ANCVehiclePawn, Fuel);
}

// ------------------------------------------------------------------ tuning
float ANCVehiclePawn::EffectiveTorque() const
{
	return Spec.MaxTorqueNm * (bLowRange ? 2.4f : 1.f) * (1.f + 0.15f * Tuning.Engine) * (1.f + 0.2f * Tuning.Turbo);
}

void ANCVehiclePawn::ServerBuyTuning_Implementation(const FNCTuning& NewTuning)
{
	const int32 Price = NCTune::Price(Tuning, NewTuning);
	APawn* Buyer = Driver ? Driver.Get() : this;
	ANCPlayerState* PS = GetController() ? GetController()->GetPlayerState<ANCPlayerState>() : nullptr;
	if (!PS && Buyer && Buyer->GetController())
	{
		PS = Buyer->GetController()->GetPlayerState<ANCPlayerState>();
	}
	if (Price > 0 && (!PS || !PS->Spend(Price)))
	{
		ApplyTuning(Tuning);      // cannot afford: keep the old setup
		return;
	}
	SetTuningFree(NewTuning);
}

void ANCVehiclePawn::SetTuningFree(const FNCTuning& NewTuning)
{
	if (!HasAuthority())
	{
		return;
	}
	Tuning = NewTuning;
	ApplyTuning(Tuning);
	OnRep_Drive();
}

void ANCVehiclePawn::OnRep_Tuning()
{
	ApplyTuning(Tuning);
	OnRep_Drive();
}

void ANCVehiclePawn::ApplyTuning(const FNCTuning& T)
{
	USkeletalMeshComponent* SkM = GetMesh();
	for (UStaticMeshComponent* C : TuneComps)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	TuneComps.Reset();
	// paint
	const int32 PaintIdx = PaintSlot.IsNone() ? INDEX_NONE : SkM->GetMaterialIndex(PaintSlot);
	FLinearColor Paint = T.Paint;
	if (PaintIdx != INDEX_NONE)
	{
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(SkM->GetMaterial(PaintIdx));
		if (!MID)
		{
			MID = SkM->CreateAndSetMaterialInstanceDynamic(PaintIdx);
		}
		if (MID)
		{
			if (T.Paint.A > 0.f)
			{
				MID->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(T.Paint.R, T.Paint.G, T.Paint.B, 1.f));
			}
			else
			{
				MID->ClearParameterValues();
				MID->GetVectorParameterValue(FMaterialParameterInfo(TEXT("BaseColor")), Paint);
			}
		}
	}
	auto AddPart = [&](UStaticMesh* Mesh, FName Socket, const FTransform& Rel)
	{
		if (!Mesh)
		{
			return (UStaticMeshComponent*)nullptr;
		}
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetStaticMesh(Mesh);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(SkM, Socket);
		C->SetRelativeTransform(Rel);
		C->RegisterComponent();
		// body-coloured parts follow the paint
		for (int32 m = 0; m < C->GetNumMaterials(); ++m)
		{
			if (C->GetMaterialSlotNames().IsValidIndex(m) && C->GetMaterialSlotNames()[m].ToString().Contains(TEXT("Tune_Paint")))
			{
				if (UMaterialInstanceDynamic* D = C->CreateAndSetMaterialInstanceDynamic(m))
				{
					D->SetVectorParameterValue(TEXT("BaseColor"), Paint);
				}
			}
		}
		TuneComps.Add(C);
		return C;
	};
	const FNCTuningCatalog& Cat = TuningCatalog;
	if (T.Spoiler > 0 && Cat.Spoilers.IsValidIndex(T.Spoiler - 1)) AddPart(Cat.Spoilers[T.Spoiler - 1], NAME_None, FTransform::Identity);
	if (T.Front > 0 && Cat.Fronts.IsValidIndex(T.Front - 1)) AddPart(Cat.Fronts[T.Front - 1], NAME_None, FTransform::Identity);
	if (T.Skirts > 0 && Cat.Skirts.IsValidIndex(T.Skirts - 1)) AddPart(Cat.Skirts[T.Skirts - 1], NAME_None, FTransform::Identity);
	for (int32 k = 0; k < Cat.Offroad.Num(); ++k)
	{
		if (T.OffroadMask & (1 << k))
		{
			AddPart(Cat.Offroad[k], NAME_None, FTransform::Identity);
		}
	}
	if (T.Rims > 0 && Cat.Rims.IsValidIndex(T.Rims - 1))
	{
		const float S = Spec.WheelRadiusM;
		for (int32 w = 0; w < 4; ++w)
		{
			AddPart(Cat.Rims[T.Rims - 1], WheelBones[w], FTransform(Cat.RimRotation, FVector::ZeroVector, FVector(S)));
		}
	}
	// neon underglow: emissive strips + a real light under the car
	if (NeonLight)
	{
		NeonLight->DestroyComponent();
		NeonLight = nullptr;
	}
	if (T.Neon > 0 && NCTune::NeonColors().IsValidIndex(T.Neon - 1))
	{
		const FLinearColor NC = NCTune::NeonColors()[T.Neon - 1];
		if (UStaticMeshComponent* C = AddPart(Cat.NeonStrip, NAME_None, FTransform::Identity))
		{
			for (int32 m = 0; m < C->GetNumMaterials(); ++m)
			{
				if (UMaterialInstanceDynamic* D = C->CreateAndSetMaterialInstanceDynamic(m))
				{
					D->SetVectorParameterValue(TEXT("EmissiveColor"), NC);
					D->SetVectorParameterValue(TEXT("BaseColor"), NC);
				}
			}
		}
		NeonLight = NewObject<URectLightComponent>(this);
		NeonLight->SetupAttachment(SkM);
		NeonLight->SetRelativeLocationAndRotation(FVector(0.f, 0.f, 12.f), FRotator(-90.f, 0.f, 0.f));
		NeonLight->SetSourceWidth(380.f);
		NeonLight->SetSourceHeight(160.f);
		NeonLight->SetIntensity(4000.f);
		NeonLight->SetAttenuationRadius(450.f);
		NeonLight->SetLightColor(NC);
		NeonLight->SetCastShadows(false);
		NeonLight->RegisterComponent();
	}
	// window tint
	for (UMaterialInstanceDynamic* MID : GlassMIDs)
	{
		if (MID && !Body.bGlassBroken)
		{
			MID->SetScalarParameterValue(TEXT("Opacity"), 0.22f + 0.3f * T.Tint);
		}
	}
	// suspension setup
	if (UChaosWheeledVehicleMovementComponent* M = Move())
	{
		const float TravelCm = Spec.SuspensionTravelM * 100.f * (1.f - 0.25f * T.Suspension);
		for (int32 i = 0; i < 4; ++i)
		{
			{ const UChaosVehicleWheel* Wh = M->Wheels.IsValidIndex(i) ? M->Wheels[i].Get() : nullptr; M->SetSuspensionParams(Wh ? Wh->SpringRate : 250.f, Wh ? Wh->SuspensionDampingRatio : 0.5f, Wh ? Wh->SpringPreload : 50.f, TravelCm * 0.45f, TravelCm * 0.55f, i); }
		}
	}
}

void ANCVehiclePawn::RepairAll()
{
	if (!HasAuthority())
	{
		return;
	}
	if (Damage)
	{
		Damage->Repair();
	}
	GetWorldTimerManager().ClearTimer(FireTimer);
	Body.bGlassBroken = false;
	Body.bOnFire = false;
	Body.FlatMask = 0;
	for (int32 k = 0; k < 4; ++k)
	{
		WearF[k] = 0.f;
	}
	TyreWear.Init(0, 4);
	ApplyFlatTyres();
	UpdateSurfaceGrip();
	for (UMaterialInstanceDynamic* MID : GlassMIDs)
	{
		if (MID)
		{
			MID->SetScalarParameterValue(TEXT("Opacity"), 0.22f + 0.3f * Tuning.Tint);
		}
	}
	ApplyBodyVisuals();
	OnRep_Drive();
}

// ------------------------------------------------------------------ camera / doors
void ANCVehiclePawn::OnToggleCamera(const FInputActionValue&)
{
	const bool bInside = !InteriorCamera->IsActive();
	InteriorCamera->SetActive(bInside);
	Camera->SetActive(!bInside);
}

void ANCVehiclePawn::OnToggleDoor(const FInputActionValue&) { ServerToggleDoor(); }

void ANCVehiclePawn::ServerToggleDoor_Implementation()
{
	SetPartOpen(0, !(Body.OpenMask & 1));
}

void ANCVehiclePawn::SetPartOpen(int32 Part, bool bOpen)
{
	if (!HasAuthority() || Part < 0 || Part > 5)
	{
		return;
	}
	if (bOpen)
	{
		Body.OpenMask |= (1 << Part);
	}
	else
	{
		Body.OpenMask &= ~(1 << Part);
	}
	ApplyBodyVisuals();
}

// ------------------------------------------------------------------ passengers
bool ANCVehiclePawn::EnterAsPassenger(APawn* Character)
{
	if (!HasAuthority() || !Character)
	{
		return false;
	}
	for (int32 i = 0; i < Passengers.Num(); ++i)
	{
		if (!Passengers[i])
		{
			Passengers[i] = Character;
			Character->SetActorEnableCollision(false);
			Character->SetActorHiddenInGame(true);
			if (ACharacter* Ch = Cast<ACharacter>(Character))
			{
				Ch->GetCharacterMovement()->DisableMovement();
			}
			Character->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			Character->SetActorRelativeLocation(Seats.IsValidIndex(i + 1) ? Seats[i + 1] : FVector::ZeroVector);
			if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
			{
				PC->SetViewTargetWithBlend(this, 0.4f);
			}
			const int32 Door = FMath::Clamp(i + 1, 0, 3);
			SetPartOpen(Door, true);
			FTimerHandle H;
			GetWorldTimerManager().SetTimer(H, [this, Door]() { SetPartOpen(Door, false); }, 1.2f, false);
			return true;
		}
	}
	return false;
}

void ANCVehiclePawn::ExitPassenger(APawn* Character)
{
	const int32 i = Passengers.IndexOfByKey(Character);
	if (!HasAuthority() || i == INDEX_NONE)
	{
		return;
	}
	FVector ValidatedExit;
	if (!FindVehicleExit(this,Character,ValidatedExit)) return;
	Passengers[i] = nullptr;
	Character->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Character->SetActorLocation(ValidatedExit, false, nullptr, ETeleportType::TeleportPhysics);
	Character->SetActorHiddenInGame(false);
	Character->SetActorEnableCollision(true);
	if (ACharacter* Ch = Cast<ACharacter>(Character))
	{
		Ch->GetCharacterMovement()->Velocity = FVector::ZeroVector;
		Ch->GetCharacterMovement()->SetUpdatedComponent(Ch->GetCapsuleComponent());
		Ch->GetCharacterMovement()->SetComponentTickEnabled(true);
		Ch->GetCharacterMovement()->Activate(true);
		Ch->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	}
	if (APlayerController* PC = Cast<APlayerController>(Character->GetController()))
	{
		PC->SetViewTargetWithBlend(Character, 0.4f);
		PC->ResetIgnoreInputFlags();
		PC->SetInputMode(FInputModeGameOnly());
	}
	if (auto* Interaction=Character->FindComponentByClass<UNCInteractionComponent>()) Interaction->RestoreAfterExit();
}

// ------------------------------------------------------------------ body damage
void ANCVehiclePawn::OnBodyImpact(const FVector& LocalPoint, const FVector& WorldPoint, float Strength)
{
	if (!HasAuthority() || Body.bWrecked)
	{
		return;
	}
	const FBoxSphereBounds B = GetMesh()->CalcBounds(FTransform::Identity);
	const FVector Rel(LocalPoint.X / FMath::Max(B.BoxExtent.X, 1.0), LocalPoint.Y / FMath::Max(B.BoxExtent.Y, 1.0), 0.0);
	const FVector Push = (GetActorLocation() - WorldPoint).GetSafeNormal() * 400.f;
	if (Strength > 0.12f)
	{
		BreakGlass();
	}
	if (Strength > 0.25f && FMath::Abs(Rel.Y) > 0.55f)
	{
		const bool bLeft = Rel.Y < 0.f;
		const bool bRear = Rel.X < -0.1f && PartMeshes.Contains(PartBones[2]);
		DetachPart((bRear ? 2 : 0) + (bLeft ? 0 : 1), Push);
	}
	if (Strength > 0.3f && Rel.X > 0.7f && Damage && Damage->Damage.IsValidIndex(0) && Damage->Damage[0] > 0.6f)
	{
		DetachPart(4, Push + FVector(0, 0, 600.f));
	}
	if (Strength > 0.3f && Rel.X < -0.7f && Damage && Damage->Damage.IsValidIndex(1) && Damage->Damage[1] > 0.6f)
	{
		DetachPart(5, Push + FVector(0, 0, 500.f));
	}
	if (Strength > 0.35f)
	{
		for (int32 w = 0; w < 4; ++w)
		{
			const FVector WL = GetMesh()->GetSocketLocation(WheelBones[w]);
			if (FVector::Dist(WL, WorldPoint) < 90.f)
			{
				DetachWheel(w, Push);
			}
		}
	}
	if (Damage && Damage->GetTotalDamage() > 0.55f)
	{
		Ignite();
	}
}

void ANCVehiclePawn::SpawnDebris(FName Bone, UStaticMesh* Mesh, const FVector& Impulse)
{
	if (!Mesh)
	{
		return;
	}
	const FVector Loc = GetMesh()->GetSocketLocation(Bone);
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* A = GetWorld()->SpawnActor<AStaticMeshActor>(Loc, GetActorRotation(), SP);
	if (!A)
	{
		return;
	}
	A->SetMobility(EComponentMobility::Movable);
	A->GetStaticMeshComponent()->SetStaticMesh(Mesh);
	A->GetStaticMeshComponent()->SetSimulatePhysics(true);
	A->GetStaticMeshComponent()->SetCollisionProfileName(FName("PhysicsActor"));
	A->SetReplicates(true);
	A->SetReplicateMovement(true);
	A->SetLifeSpan(90.f);
	A->GetStaticMeshComponent()->AddImpulse(Impulse + GetVelocity() * 0.8f, NAME_None, true);
}

void ANCVehiclePawn::DetachPart(int32 Part, const FVector& Impulse)
{
	if (!HasAuthority() || Part < 0 || Part > 5 || (Body.DetachedMask & (1 << Part)))
	{
		return;
	}
	if (GetMesh()->GetBoneIndex(PartBones[Part]) == INDEX_NONE)
	{
		return;
	}
	Body.DetachedMask |= (1 << Part);
	SpawnDebris(PartBones[Part], PartMeshes.FindRef(PartBones[Part]), Impulse);
	ApplyBodyVisuals();
}

void ANCVehiclePawn::DetachWheel(int32 Wheel, const FVector& Impulse)
{
	if (!HasAuthority() || Wheel < 0 || Wheel > 3 || (Body.WheelsDetachedMask & (1 << Wheel)))
	{
		return;
	}
	Body.WheelsDetachedMask |= (1 << Wheel);
	SpawnDebris(WheelBones[Wheel], PartMeshes.FindRef(FName("wheel")), Impulse);
	if (UChaosWheeledVehicleMovementComponent* M = Move())
	{
		M->SetWheelRadius(Wheel, 8.f);                  // the axle drags on the ground
		M->SetWheelFrictionMultiplier(Wheel, 0.3f);
		M->SetAffectedByEngine(Wheel, false);
	}
	ApplyBodyVisuals();
}

void ANCVehiclePawn::BreakGlass()
{
	if (HasAuthority() && !Body.bGlassBroken)
	{
		Body.bGlassBroken = true;
		ApplyBodyVisuals();
	}
}

void ANCVehiclePawn::Ignite()
{
	if (!HasAuthority() || Body.bOnFire || Body.bWrecked)
	{
		return;
	}
	Body.bOnFire = true;
	ApplyBodyVisuals();
	GetWorldTimerManager().SetTimer(FireTimer, this, &ANCVehiclePawn::Explode, BurnTime, false);
}

void ANCVehiclePawn::Explode()
{
	if (!HasAuthority() || Body.bWrecked)
	{
		return;
	}
	Body.bWrecked = true;
	Body.bOnFire = false;
	// everybody out
	for (APawn* P : Passengers)
	{
		if (P)
		{
			ExitPassenger(P);
		}
	}
	if (Driver)
	{
		ServerExit_Implementation();
	}
	UGameplayStatics::ApplyRadialDamage(this, 250.f, GetActorLocation(), 900.f, nullptr, TArray<AActor*>(), this, GetController(), true);
	ANCCrowdManager::PanicAt(GetWorld(), GetActorLocation(), 6000.f);
	// the blast throws pedestrians around and catches the traffic next to it
	for (TActorIterator<ANCCrowdManager> It(GetWorld()); It; ++It)
	{
		for (int32 Ped : It->PedsInRadius(GetActorLocation(), 700.f))
		{
			It->KillPed(Ped, (It->PedLocation(Ped) - GetActorLocation()).GetSafeNormal2D() * 700.f + FVector(0, 0, 600.f));
		}
	}
	for (TActorIterator<ANCTrafficManager> It(GetWorld()); It; ++It)
	{
		for (int32 Car : It->CarsInRadius(GetActorLocation(), 450.f))
		{
			if (ANCVehiclePawn* V = It->TakeVehicle(Car))
			{
				V->GetMesh()->AddImpulse((V->GetActorLocation() - GetActorLocation()).GetSafeNormal() * 500.f + FVector(0, 0, 400.f),
					NAME_None, true);
			}
		}
	}
	ANCPoliceManager::ReportCrime(GetWorld(), LastDamager, 1, GetActorLocation(), true);
	GetMesh()->AddImpulse(FVector(0, 0, 900.f), NAME_None, true);
	if (UChaosWheeledVehicleMovementComponent* M = Move())
	{
		M->SetMaxEngineTorque(0.f);
	}
	for (int32 p = 0; p < 6; ++p)
	{
		if (FMath::RandBool())
		{
			DetachPart(p, FVector(FMath::RandRange(-800.f, 800.f), FMath::RandRange(-800.f, 800.f), 900.f));
		}
	}
	ApplyBodyVisuals();
}

void ANCVehiclePawn::OnRep_Body()
{
	ApplyBodyVisuals();
	ApplyFlatTyres();
}

void ANCVehiclePawn::ApplyBodyVisuals()
{
	USkeletalMeshComponent* SkM = GetMesh();
	for (int32 p = 0; p < 6; ++p)
	{
		if ((Body.DetachedMask & (1 << p)) && !(AppliedBody.DetachedMask & (1 << p)) && SkM->GetBoneIndex(PartBones[p]) != INDEX_NONE)
		{
			SkM->HideBoneByName(PartBones[p], EPhysBodyOp::PBO_None);
		}
	}
	for (int32 w = 0; w < 4; ++w)
	{
		if ((Body.WheelsDetachedMask & (1 << w)) && !(AppliedBody.WheelsDetachedMask & (1 << w)))
		{
			SkM->HideBoneByName(WheelBones[w], EPhysBodyOp::PBO_None);
		}
	}
	if (Body.bGlassBroken && !AppliedBody.bGlassBroken)
	{
		for (UMaterialInstanceDynamic* MID : GlassMIDs)
		{
			if (MID)
			{
				MID->SetScalarParameterValue(TEXT("Opacity"), 0.f);
			}
		}
		if (GlassFX)
		{
			UGameplayStatics::SpawnEmitterAtLocation(this, GlassFX, GetActorLocation() + FVector(0, 0, 120.f));
		}
	}
	if (Body.bOnFire && !FireComp && FireFX)
	{
		FireComp = UGameplayStatics::SpawnEmitterAttached(FireFX, SkM, NAME_None, FVector(140.f, 0.f, 100.f),
			FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
	}
	if (!Body.bOnFire && FireComp)
	{
		FireComp->DeactivateSystem();
		FireComp = nullptr;
	}
	if (Body.bWrecked && !AppliedBody.bWrecked)
	{
		if (ExplosionFX)
		{
			UGameplayStatics::SpawnEmitterAtLocation(this, ExplosionFX, GetActorLocation(), FRotator::ZeroRotator, FVector(3.f));
		}
		// burnt-out shell
		const TArray<FName> Slots = SkM->GetMaterialSlotNames();
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			if (!Slots[i].ToString().Contains(TEXT("Glass")))
			{
				if (UMaterialInstanceDynamic* MID = SkM->CreateAndSetMaterialInstanceDynamic(i))
				{
					MID->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.018f, 0.016f, 0.015f));
					MID->SetScalarParameterValue(TEXT("Roughness"), 0.95f);
					MID->SetScalarParameterValue(TEXT("Metallic"), 0.2f);
					MID->SetScalarParameterValue(TEXT("EmissiveStrength"), 0.f);
				}
			}
		}
		if (FireFX)
		{
			UGameplayStatics::SpawnEmitterAttached(FireFX, SkM, NAME_None, FVector(0.f, 0.f, 80.f), FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset, true);
		}
	}
	AppliedBody = Body;
}

// ================================================================== drift
void ANCVehiclePawn::InitDriftFX()
{
	if (!TireSmokeFX)
	{
		TireSmokeFX = LoadObject<UParticleSystem>(nullptr, TEXT("/Game/StarterContent/Particles/P_Smoke.P_Smoke"));
	}
	if (!SkidDecal)
	{
		SkidDecal = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/NeonCoast/Materials/M_NC_SkidDecal.M_NC_SkidDecal"));
	}
	if (TireSmokeFX && GetNetMode() != NM_DedicatedServer)
	{
		for (int32 k = 2; k < 4; ++k)
		{
			UParticleSystemComponent* P = UGameplayStatics::SpawnEmitterAttached(TireSmokeFX, GetMesh(), WheelBones[k],
				FVector(0.f, 0.f, -20.f), FRotator::ZeroRotator, FVector(0.55f), EAttachLocation::KeepRelativeOffset,
				false, EPSCPoolMethod::None, false);
			if (P)
			{
				SmokeComps.Add(P);
			}
		}
	}
	for (int32 k = 0; k < 4; ++k)
	{
		LastSkidPos[k] = FVector(FLT_MAX);
	}
}

void ANCVehiclePawn::DropSkid(int32 Wheel, bool bSlip)
{
	if (!bSlip || !SkidDecal || GetNetMode() == NM_DedicatedServer)
	{
		LastSkidPos[Wheel] = FVector(FLT_MAX);
		return;
	}
	const float R = Spec.WheelRadiusM * 100.f;
	const FVector P = GetMesh()->GetSocketLocation(WheelBones[Wheel]) - GetActorUpVector() * (R - 4.f);
	const float Now = GetWorld()->GetTimeSeconds();
	const float D = FVector::Dist(P, LastSkidPos[Wheel]);
	if (D < 45.f && Now - LastSkidTime[Wheel] < 0.25f)
	{
		return;
	}
	// one dark rubber strip from the last mark to here (burnouts stack up on the same spot)
	const FVector A = D < 200.f ? LastSkidPos[Wheel] : P;
	const FVector Mid = (A + P) * 0.5f;
	const FVector Dir = (P - A).GetSafeNormal2D().IsNearlyZero() ? GetActorForwardVector() : (P - A).GetSafeNormal2D();
	const float Len = FMath::Max(FVector::Dist2D(A, P), 30.f);
	// decal projects along its X (pitch -90 = straight down); local Z then runs along the travel
	// direction, local Y across it: size = (depth, half tyre width, half strip length)
	UDecalComponent* Dc = UGameplayStatics::SpawnDecalAtLocation(this, SkidDecal, FVector(40.f, 11.f, Len * 0.5f + 4.f),
		Mid, FRotator(-90.f, Dir.Rotation().Yaw, 0.f), 45.f);
	if (Dc)
	{
		Dc->SetFadeOut(30.f, 15.f, false);
		Dc->SetFadeScreenSize(0.002f);
		Skids.Add(Dc);
		if (Skids.Num() > 320)
		{
			if (Skids[0])
			{
				Skids[0]->DestroyComponent();
			}
			Skids.RemoveAt(0);
		}
	}
	LastSkidPos[Wheel] = P;
	LastSkidTime[Wheel] = Now;
}

void ANCVehiclePawn::UpdateDrift(float Dt)
{
	UChaosWheeledVehicleMovementComponent* M = Move();
	if (!M || Body.bWrecked || Dt <= 0.f)
	{
		return;
	}
	const FVector V = GetVelocity();
	const FVector Fwd = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	const float Speed = V.Size2D() * 0.036f;
	const float PrevSpeed = LastSpeed;
	LastSpeed = Speed;
	const float Fv = FVector::DotProduct(V, Fwd);
	const float Rv = FVector::DotProduct(V, Right);
	DriftAngle = Speed > 8.f ? FMath::RadiansToDegrees(FMath::Atan2(Rv, FMath::Abs(Fv))) : 0.f;
	int32 Grounded = 0;
	bool bAsphalt = false;
	for (int32 i = 0; i < M->Wheels.Num(); ++i)
	{
		if (M->Wheels[i] && !M->Wheels[i]->IsInAir())
		{
			++Grounded;
			const UPhysicalMaterial* PM = M->Wheels[i]->GetContactSurfaceMaterial();
			bAsphalt |= !PM || PM->GetName().Contains(TEXT("Asphalt"));
		}
	}
	const bool bSim = HasAuthority() || IsLocallyControlled();
	const float Throttle = M->GetThrottleInput();
	const float Brake = M->GetBrakeInput();
	const float Steer = M->GetSteeringInput();
	const bool bHand = static_cast<float>(M->GetHandbrakeInput()) > 0.5f;
	const bool bRearDriven = Spec.Drivetrain != ENCDrivetrain::FWD;
	UPrimitiveComponent* Root = GetMesh();

	// ---- burnout: handbrake + throttle at standstill -> rear tyres spin, release -> launch
	const bool bWasBurnout = bBurnout;
	bBurnout = bRearDriven && Grounded >= 3 && bHand && Throttle > 0.7f && Speed < 12.f;
	if (bBurnout)
	{
		BurnoutTime += Dt;
		if (bSim)
		{
			M->SetHandbrakeInput(false);                     // let the driven rear wheels spin freely
			M->SetWheelFrictionMultiplier(2, BaseFriction[2] * 0.12f);
			M->SetWheelFrictionMultiplier(3, BaseFriction[3] * 0.12f);
			Root->AddForce(-V * 8.f, NAME_None, true);        // front brakes hold the car on the line
		}
	}
	else if (bWasBurnout)
	{
		LaunchBoost = FMath::Clamp(BurnoutTime, 0.5f, 2.0f);  // hot tyres: a stronger start
		BurnoutTime = 0.f;
		UpdateSurfaceGrip();
	}
	if (LaunchBoost > 0.f)
	{
		LaunchBoost -= Dt;
		if (bSim && Throttle > 0.5f && Speed < 110.f && Grounded >= 3)
		{
			Root->AddForce(Fwd * 380.f, NAME_None, true);
		}
	}

	// ---- drift assist: handbrake / power-slide loosens the rear, keeps momentum, stops spin-outs
	const bool bSliding = FMath::Abs(DriftAngle) > 12.f && Speed > 30.f && Grounded >= 3;
	if (!bBurnout && Grounded >= 3 && bSim)
	{
		float RearScale = 1.f;
		if (bHand && Speed > 25.f)
		{
			RearScale = 0.5f;
		}
		else if (bSliding && Throttle > 0.3f && bRearDriven)
		{
			RearScale = 0.72f;
		}
		if (RearScale < 1.f || bSliding)
		{
			M->SetWheelFrictionMultiplier(2, BaseFriction[2] * RearScale);
			M->SetWheelFrictionMultiplier(3, BaseFriction[3] * RearScale);
		}
		if (bSliding)
		{
			const FVector VDir = V.GetSafeNormal2D();
			Root->AddForce(VDir * 160.f * Throttle, NAME_None, true);                 // hold speed through the slide
			const float Excess = FMath::Abs(DriftAngle) - 42.f;
			float YawAccel = Steer * 0.6f;                                           // steer the angle
			if (Excess > 0.f)
			{
				YawAccel += FMath::Sign(DriftAngle) * Excess * 0.08f;                // catch it before a spin
			}
			Root->AddTorqueInRadians(FVector::UpVector * YawAccel, NAME_None, true);
		}
	}

	// ---- air control: W/S pitch, A/D roll (like GTA) so you can land on the wheels
	if (Grounded == 0)
	{
		AirTime += Dt;
		if (AirTime > 0.25f && bSim)
		{
			const float Pitch = Throttle - Brake;                                     // W = nose down
			const FVector W = Root->GetPhysicsAngularVelocityInRadians();
			const FVector Acc = Right * (Pitch * 2.4f) + Fwd * (-Steer * 2.4f) - W * 0.6f;
			Root->AddTorqueInRadians(Acc, NAME_None, true);
		}
	}
	else
	{
		AirTime = 0.f;
	}

	// ---- tyre smoke + rubber marks (visual, every machine)
	const bool bRearSlip = bAsphalt && Grounded >= 3 && (bBurnout || (FMath::Abs(DriftAngle) > 16.f && Speed > 22.f));
	const bool bBrakeSlip = bAsphalt && Grounded >= 3 && Brake > 0.85f && Speed > 45.f;
	for (UParticleSystemComponent* P : SmokeComps)
	{
		if (P && bRearSlip != P->IsActive())
		{
			bRearSlip ? P->ActivateSystem(true) : P->DeactivateSystem();
		}
	}
	// a hard launch spins the driven rear tyres for a moment; a pulled handbrake drags the locked rears
	static constexpr float LaunchWindow = 0.9f;
	const bool bLaunchSpin = bAsphalt && bRearDriven && Grounded >= 3 && Throttle > 0.9f && Speed > 1.f && Speed < 35.f
		&& LaunchBoost <= 0.f && GetGameTimeSinceCreation() > 2.f && (PrevSpeed < 4.f || LaunchSpinT < LaunchWindow);
	LaunchSpinT = bLaunchSpin ? LaunchSpinT + Dt : (Throttle < 0.5f ? 0.f : LaunchWindow);
	const bool bHandDrag = bAsphalt && Grounded >= 3 && bHand && !bBurnout && Speed > 18.f;
	for (int32 k = 0; k < 4; ++k)
	{
		const bool bRear = k >= 2;
		DropSkid(k, bBrakeSlip || (bRear && (bRearSlip || bLaunchSpin || bHandDrag)) || ((Body.FlatMask & (1 << k)) != 0 && Speed > 10.f));
	}
	// tyre wear (server): burnouts eat rubber fast, drifting slowly; a worn-out tyre bursts
	if (HasAuthority())
	{
		const float Res = 1.f / (1.f + 0.35f * Tuning.Tires);
		for (int32 k = 0; k < 4; ++k)
		{
			const bool bRear = k >= 2;
			float Rate = 0.f;
			if (bRear && bBurnout) Rate += 0.11f;
			if (bRear && bRearSlip && !bBurnout) Rate += 0.012f * FMath::Abs(DriftAngle) / 25.f;
			if (bRear && (bLaunchSpin || bHandDrag)) Rate += 0.02f;
			if (bBrakeSlip) Rate += 0.015f;
			if (Rate <= 0.f || (Body.FlatMask & (1 << k)))
			{
				continue;
			}
			WearF[k] = FMath::Min(1.f, WearF[k] + Rate * Res * Dt);
			const uint8 Q = static_cast<uint8>(WearF[k] * 255.f);
			if (TyreWear.IsValidIndex(k) && TyreWear[k] != Q)
			{
				TyreWear[k] = Q;
			}
			if (WearF[k] >= 1.f)
			{
				Body.FlatMask |= 1 << k;
				MulticastTyrePop(k);
				ApplyFlatTyres();
				UpdateSurfaceGrip();
			}
		}
	}

	// ---- score (local driver): angle x speed x combo; banked as cash after the car settles
	if (IsLocallyControlled())
	{
		const bool bCrash = PrevSpeed - Speed > 25.f;
		if (bCrash && DriftPoints > 0.f)
		{
			DriftPoints = 0.f;
			DriftTime = 0.f;
			DriftCombo = 1;
			if (const APlayerController* PC = Cast<APlayerController>(GetController()))
			{
				if (ANCHUD* H = Cast<ANCHUD>(PC->GetHUD()))
				{
					H->ShowMessage(TEXT("DRIFT ZNICEN"), 2.f);
				}
			}
		}
		else if (bSliding && Throttle > 0.1f)
		{
			DriftTime += Dt;
			DriftCalm = 0.f;
			DriftCombo = FMath::Clamp(1 + static_cast<int32>(DriftTime / 2.5f), 1, 5);
			DriftPoints += FMath::Abs(DriftAngle) * Speed * 0.06f * DriftCombo * Dt;
		}
		else if (DriftPoints > 0.f)
		{
			DriftCalm += Dt;
			if (DriftCalm > 1.2f)
			{
				const int32 Pts = FMath::RoundToInt(DriftPoints);
				ServerBankDrift(Pts);
				if (const APlayerController* PC = Cast<APlayerController>(GetController()))
				{
					if (ANCHUD* H = Cast<ANCHUD>(PC->GetHUD()))
					{
						H->ShowMessage(FString::Printf(TEXT("DRIFT %d   +$%d"), Pts, FMath::Clamp(Pts / 10, 0, 10000)), 2.5f);
					}
				}
				DriftPoints = 0.f;
				DriftTime = 0.f;
				DriftCombo = 1;
			}
		}
	}
}

void ANCVehiclePawn::ServerBankDrift_Implementation(int32 Points)
{
	if (ANCPlayerState* PS = GetPlayerState<ANCPlayerState>())
	{
		PS->Earn(FMath::Clamp(Points / 10, 0, 10000));
	}
}

// ================================================================== tyres + workshop repair
void ANCVehiclePawn::ApplyFlatTyres()
{
	UChaosWheeledVehicleMovementComponent* M = Move();
	if (!M)
	{
		return;
	}
	const float R = Spec.WheelRadiusM * 100.f;
	for (int32 i = 0; i < FMath::Min(4, M->Wheels.Num()); ++i)
	{
		M->SetWheelRadius(i, (Body.FlatMask & (1 << i)) ? R * 0.8f : R);   // sits down on the rim
	}
}

void ANCVehiclePawn::MulticastTyrePop_Implementation(int32 Wheel)
{
	if (GlassFX && Wheel >= 0 && Wheel < 4)
	{
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), GlassFX, GetMesh()->GetSocketLocation(WheelBones[Wheel]),
			FRotator::ZeroRotator, FVector(0.6f));
	}
	if (IsLocallyControlled())
	{
		if (const APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (ANCHUD* H = Cast<ANCHUD>(PC->GetHUD()))
			{
				H->ShowMessage(TEXT("PRASKLA PNEUMATIKA!  Oprav auto v dilne"), 3.5f);
			}
		}
	}
}

bool ANCVehiclePawn::NeedsRepair() const
{
	int32 Worn = 0;
	for (const uint8 W : TyreWear)
	{
		Worn += W > 64 ? 1 : 0;
	}
	return Body.FlatMask || Worn || Body.bGlassBroken || Body.DetachedMask || Body.WheelsDetachedMask || Body.bOnFire
		|| (Damage && Damage->GetTotalDamage() > 0.05f);
}

int32 ANCVehiclePawn::RepairCost() const
{
	int32 C = 250;
	for (int32 k = 0; k < 4; ++k)
	{
		C += (Body.FlatMask & (1 << k)) ? 220 : (TyreWear.IsValidIndex(k) ? TyreWear[k] : 0);
	}
	C += Body.bGlassBroken ? 300 : 0;
	C += 180 * FMath::CountBits(Body.DetachedMask) + 250 * FMath::CountBits(Body.WheelsDetachedMask);
	C += Damage ? FMath::RoundToInt(Damage->GetTotalDamage() * 900.f) : 0;
	return C;
}

void ANCVehiclePawn::ServerRepairPaid_Implementation(int32 Cost)
{
	ANCPlayerState* PS = GetPlayerState<ANCPlayerState>();
	if (!PS || PS->Spend(FMath::Max(Cost, RepairCost())))
	{
		RepairAll();
	}
}

// ================================================================== bullets, sirens, running people over
float ANCVehiclePawn::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator,
	AActor* DamageCauser)
{
	const float D = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || Body.bWrecked || DamageAmount <= 0.f)
	{
		return D;
	}
	if (EventInstigator)
	{
		LastDamager = EventInstigator;
	}
	if (DamageAmount >= 300.f)
	{
		Explode();                                        // rocket / grenade on the car
		return D;
	}
	FVector HitLoc = GetActorLocation();
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PE = static_cast<const FPointDamageEvent&>(DamageEvent);
		if (!PE.HitInfo.ImpactPoint.IsNearlyZero())
		{
			HitLoc = PE.HitInfo.ImpactPoint;
		}
	}
	ShotDamage += DamageAmount;
	// a bullet next to a wheel pops the tyre
	for (int32 w = 0; w < 4; ++w)
	{
		if (!(Body.FlatMask & (1 << w)) && FVector::Dist(HitLoc, GetMesh()->GetSocketLocation(WheelBones[w])) < 55.f)
		{
			Body.FlatMask |= 1 << w;
			MulticastTyrePop(w);
			ApplyFlatTyres();
			UpdateSurfaceGrip();
		}
	}
	if (ShotDamage > 60.f && !Body.bGlassBroken && FMath::FRand() < 0.35f)
	{
		BreakGlass();
	}
	const FVector L = GetActorTransform().InverseTransformPositionNoScale(HitLoc);
	EngineDamage += DamageAmount * (L.X > 90.f ? 1.6f : 0.6f);   // the engine sits in front
	if (EngineDamage > 900.f && !Body.bOnFire)
	{
		Ignite();
	}
	return D;
}

void ANCVehiclePawn::SetSiren(bool bOn)
{
	bSiren = bOn;
	OnRep_Siren();
}

void ANCVehiclePawn::OnRep_Siren()
{
	if (bSiren && SirenLights.Num() == 0 && GetNetMode() != NM_DedicatedServer)
	{
		for (int32 s = 0; s < 2; ++s)
		{
			UPointLightComponent* L = NewObject<UPointLightComponent>(this);
			L->SetupAttachment(GetMesh());
			L->SetRelativeLocation(FVector(-10.f, s ? 45.f : -45.f, 175.f));
			L->SetLightColor(s ? FLinearColor(0.1f, 0.3f, 1.f) : FLinearColor(1.f, 0.05f, 0.05f));
			L->SetAttenuationRadius(1400.f);
			L->SetCastShadows(false);
			L->SetIntensity(0.f);
			L->RegisterComponent();
			SirenLights.Add(L);
		}
	}
	for (UPointLightComponent* L : SirenLights)
	{
		if (L)
		{
			L->SetVisibility(bSiren);
		}
	}
}

void ANCVehiclePawn::UpdateSirenAndRunOver(float Dt)
{
	if (bSiren && SirenLights.Num() == 2)
	{
		const float T = GetWorld()->GetTimeSeconds() * 7.f;
		const bool bA = FMath::Frac(T * 0.5f) < 0.5f;
		SirenLights[0]->SetIntensity(bA ? 90000.f : 0.f);
		SirenLights[1]->SetIntensity(bA ? 0.f : 90000.f);
	}
	if (!HasAuthority() || Body.bWrecked)
	{
		return;
	}
	RunOverTimer -= Dt;
	if (!bRunOverLocationValid || GetSpeedKmh() < 8.f)
	{
		LastRunOverLocation = GetActorLocation();
		bRunOverLocationValid = true;
		return;
	}
	if (RunOverTimer > 0.f)
	{
		return;
	}
	RunOverTimer = 0.1f;
	const FVector Front = GetActorLocation() + GetActorForwardVector() * 140.f;
	const FVector Start = LastRunOverLocation;
	LastRunOverLocation = GetActorLocation();
	for (TActorIterator<ANCCrowdManager> It(GetWorld()); It; ++It)
	{
		for (int32 Ped : It->PedsInRadius((Start+Front)*0.5f, FVector::Dist(Start,Front)*0.5f+200.f))
		{
			const FVector PedLocation = It->PedLocation(Ped);
			const FVector Closest = FMath::ClosestPointOnSegment(PedLocation,Start,Front);
			if (FVector::Dist2D(PedLocation,Closest) > 150.f || FMath::Abs(PedLocation.Z-Closest.Z) > 160.f) continue;
			It->KillPed(Ped, GetVelocity() * 0.7f + FVector(0, 0, 350.f));
			if (GetController() && GetController()->IsPlayerController())
			{
				ANCPoliceManager::ReportCrime(GetWorld(), GetController(), 2, Front, true);
			}
		}
	}
}
