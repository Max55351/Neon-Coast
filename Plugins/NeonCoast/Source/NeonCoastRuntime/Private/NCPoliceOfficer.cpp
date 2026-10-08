#pragma warning(disable: 4456 4457 4458 4459)
#include "NCPoliceOfficer.h"

#include "Animation/AnimSequence.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "NCGameMode.h"
#include "NCHealthComponent.h"
#include "NCPlayerState.h"
#include "NCPoliceManager.h"
#include "NCVehiclePawn.h"
#include "NCWeaponComponent.h"
#include "Net/UnrealNetwork.h"

ANCPoliceOfficer::ANCPoliceOfficer()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();
	GetCapsuleComponent()->InitCapsuleSize(38.f, 90.f);
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
	GetMesh()->SetCollisionProfileName(TEXT("CharacterMesh"));
	GetMesh()->bEnableUpdateRateOptimizations = true;
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	GetCharacterMovement()->MaxWalkSpeed = 430.f;
	GetCharacterMovement()->bOrientRotationToMovement = false;
	bUseControllerRotationYaw = false;
	Health = CreateDefaultSubobject<UNCHealthComponent>(TEXT("Health"));
	Gun = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Gun"));
	Gun->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Gun->SetupAttachment(GetMesh());
}

void ANCPoliceOfficer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANCPoliceOfficer, Rank);
	DOREPLIFETIME(ANCPoliceOfficer, Target);
	DOREPLIFETIME(ANCPoliceOfficer,bAmbientPatrol);
	DOREPLIFETIME(ANCPoliceOfficer, LookMesh);
	DOREPLIFETIME(ANCPoliceOfficer, WalkAnim);
	DOREPLIFETIME(ANCPoliceOfficer, IdleAnim);
}

void ANCPoliceOfficer::Init(int32 InRank, USkeletalMesh* Mesh, UAnimSequence* Walk, UAnimSequence* Idle, APawn* InTarget)
{
	Rank = InRank;
	LookMesh = Mesh;
	WalkAnim = Walk;
	IdleAnim = Idle;
	Target = InTarget;
	PursuedController = InTarget ? InTarget->GetController() : nullptr;
	Health->MaxHealth = Rank == 0 ? 120.f : (Rank == 1 ? 180.f : 220.f);
	Health->Health = Health->MaxHealth;
	Health->Armor = Rank == 0 ? 0.f : 60.f;
	GetCharacterMovement()->MaxWalkSpeed = Rank == 0 ? 430.f : 470.f;
	ApplyLook();
}

bool ANCPoliceOfficer::IsDead() const
{
	return Health && Health->bDead;
}

void ANCPoliceOfficer::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Health->OnDeath.AddDynamic(this, &ANCPoliceOfficer::OnDied);
	}
	ApplyLook();
}

void ANCPoliceOfficer::OnRep_Look()
{
	ApplyLook();
}

void ANCPoliceOfficer::ApplyLook()
{
	if (LookMesh && GetMesh()->GetSkeletalMeshAsset() != LookMesh)
	{
		GetMesh()->SetSkeletalMeshAsset(LookMesh);
		bWalkingAnim = false;
		if (IdleAnim)
		{
			GetMesh()->PlayAnimation(IdleAnim, true);
		}
	}
	const TCHAR* GunMesh = Rank == 0 ? TEXT("NC2_Weapon_Pistol") : (Rank == 1 ? TEXT("NC2_Weapon_Rifle") : TEXT("NC2_Weapon_Rifle"));
	Gun->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/NeonCoast/Meshes/SM_%s.SM_%s"), GunMesh, GunMesh)));
	// MakeHuman skeletons call the hand "wrist.R", the UE mannequin "hand_r"
	for (const TCHAR* Bone : {TEXT("hand_r"), TEXT("wrist_R"), TEXT("wrist.R"), TEXT("wrist_r"), TEXT("hand_R")})
	{
		if (GetMesh()->GetBoneIndex(Bone) != INDEX_NONE)
		{
			Gun->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Bone);
			Gun->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));
			break;
		}
	}
}

void ANCPoliceOfficer::OnDied(AActor* Victim, AController* Killer)
{
	if (bGangster)
	{
		Gun->SetVisibility(false);
		SetLifeSpan(25.f);
		return;                                                          // nobody calls the cops over a dead mobster
	}
	// killing law enforcement: at least 3 stars (soldiers 4)
	ANCPoliceManager::ReportCrime(GetWorld(), Killer, Rank == 2 ? 4 : 3, GetActorLocation(), false, true);
	Gun->SetVisibility(false);
	SetLifeSpan(25.f);
}

void ANCPoliceOfficer::MulticastShot_Implementation(FVector_NetQuantize Muzzle, FVector_NetQuantize Hit, bool bFlesh)
{
	UNCWeaponComponent::ShotFX(GetWorld(), Muzzle, Hit, bFlesh);
}

void ANCPoliceOfficer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsDead())
	{
		return;
	}
	// walk / idle loop on every machine
	const bool bMoving = GetVelocity().Size2D() > 40.f;
	if (bMoving != bWalkingAnim && LookMesh)
	{
		bWalkingAnim = bMoving;
		if (UAnimSequence* A = bMoving ? WalkAnim.Get() : IdleAnim.Get())
		{
			GetMesh()->PlayAnimation(A, true);
			GetMesh()->SetPlayRate(bMoving ? FMath::Clamp(GetVelocity().Size2D() / 140.f, 1.f, 2.6f) : 1.f);
		}
	}
	if (!HasAuthority())
	{
		return;
	}
	UWorld* W = GetWorld();
	if (bAmbientPatrol && !Target && !bSearching && !PatrolPoints.IsEmpty())
	{
		PatrolPause-=DeltaSeconds;
		if (PatrolPause>0.f) return;
		const FVector To=PatrolPoints[PatrolPoint]-GetActorLocation();
		PatrolStuck=FVector::DistSquared2D(LastPatrolPosition,GetActorLocation())<1.f?PatrolStuck+DeltaSeconds:0.f;
		LastPatrolPosition=GetActorLocation();
		if (To.SizeSquared2D()<FMath::Square(100.f) || PatrolStuck>5.f)
		{
			PatrolPoint=(PatrolPoint+1)%PatrolPoints.Num(); PatrolPause=FMath::FRandRange(1.f,3.f); PatrolStuck=0.f;
		}
		else
		{
			SetActorRotation(FMath::RInterpTo(GetActorRotation(),FRotator(0,To.Rotation().Yaw,0),DeltaSeconds,4.f));
			AddMovementInput(To.GetSafeNormal2D(),.3f);
		}
		return;
	}
	// During a search, never aim or shoot at the hidden player's real location.
	if (bSearching && !bGuard && !bGangster)
	{
		const FVector ToSearch = SearchLocation - GetActorLocation();
		if (ToSearch.SizeSquared2D() > FMath::Square(250.f))
		{
			SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0,ToSearch.Rotation().Yaw,0), DeltaSeconds, 5.f));
			AddMovementInput(ToSearch.GetSafeNormal2D(), .6f);
		}
		ArrestTime = 0.f;
		return;
	}
	// guards pick up anybody wanted (or trespassing) who comes close
	if (!Target && bGuard)
	{
		for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator(); It; ++It)
		{
			APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr;
			const ANCPlayerState* PS = It->Get() ? It->Get()->GetPlayerState<ANCPlayerState>() : nullptr;
			if (P && PS && PS->Wanted > 0 && FVector::Dist(P->GetActorLocation(), GetActorLocation()) < 6000.f)
			{
				Target = P;
			}
		}
	}
	const UNCHealthComponent* TH = UNCHealthComponent::Of(Target);
	if (!Target || (TH && TH->bDead))
	{
		Target = nullptr;
		return;
	}
	const ANCPlayerState* PS = Target->GetPlayerState<ANCPlayerState>();
	if (PS && PS->Wanted <= 0 && !bGuard && !bGangster)
	{
		Target = nullptr;
		return;
	}
	const FVector To = Target->GetActorLocation() - GetActorLocation();
	const float Dist = To.Size();
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, To.Rotation().Yaw, 0.f), DeltaSeconds, 8.f));
	LosTimer -= DeltaSeconds;
	if (LosTimer <= 0.f)
	{
		LosTimer = 0.3f;
		FHitResult H;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(NCOfficerLos), false, this);
		Q.AddIgnoredActor(Target);
		if (AActor* Parent = Target->GetAttachParentActor())
		{
			Q.AddIgnoredActor(Parent);
		}
		bLos = !W->LineTraceSingleByChannel(H, GetActorLocation() + FVector(0, 0, 60.f), Target->GetActorLocation() + FVector(0, 0, 40.f),
			ECC_Visibility, Q);
	}
	// an unarmed 1-star suspect on foot gets arrested instead of shot
	const UNCWeaponComponent* TW = UNCWeaponComponent::Of(Target);
	const bool bOnFoot = Cast<ACharacter>(Target) != nullptr;
	const bool bUnarmed = !TW || TW->CurrentDef().Kind == ENCWeaponKind::Melee;
	if (PS && PS->Wanted == 1 && bOnFoot && bUnarmed && !bGuard && !bGangster)
	{
		if (Dist > 140.f)
		{
			AddMovementInput(To.GetSafeNormal2D(), 1.f);
			ArrestTime = 0.f;
		}
		else if (bLos && Target->GetVelocity().Size() < 250.f)
		{
			ArrestTime += DeltaSeconds;
			if (ArrestTime > 1.5f)
			{
				ArrestTime = 0.f;
				if (ANCGameMode* GM = W->GetAuthGameMode<ANCGameMode>())
				{
					if (APlayerController* PC = Cast<APlayerController>(Target->GetController()))
					{
						GM->Busted(PC);
					}
				}
			}
		}
		else ArrestTime = 0.f;
		return;
	}
	const float Range = Rank == 0 ? 3500.f : 5500.f;
	if (bLos && Dist < Range)
	{
		if (W->GetTimeSeconds() >= NextShot)
		{
			NextShot = W->GetTimeSeconds() + (Rank == 0 ? 0.9f : 0.32f) * FMath::FRandRange(0.8f, 1.3f);
			const float SpreadDeg = (Rank == 0 ? 4.5f : 2.6f) + Target->GetVelocity().Size() / 400.f;
			const FVector Muzzle = GetActorLocation() + GetActorForwardVector() * 50.f + FVector(0, 0, 55.f);
			const FVector Aim = (Target->GetActorLocation() + FVector(0, 0, 30.f) - Muzzle).GetSafeNormal();
			bool bFlesh = false;
			const FVector Hit = UNCWeaponComponent::FireBullet(this, GetController(), Muzzle,
				FMath::VRandCone(Aim, FMath::DegreesToRadians(SpreadDeg)), Range * 1.3f, Rank == 0 ? 12.f : 17.f, bFlesh);
			MulticastShot(Muzzle, Hit, bFlesh);
		}
		if (Dist > 1800.f)
		{
			AddMovementInput(To.GetSafeNormal2D(), 0.5f);
		}
	}
	else if (Dist > 250.f)
	{
		AddMovementInput(To.GetSafeNormal2D(), 1.f);
		// hop over kerbs, low walls and car bonnets
		FHitResult H;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(NCOfficerStep), false, this);
		if (W->LineTraceSingleByChannel(H, GetActorLocation() - FVector(0, 0, 50.f),
			GetActorLocation() - FVector(0, 0, 50.f) + To.GetSafeNormal2D() * 90.f, ECC_Visibility, Q) && CanJump())
		{
			Jump();
		}
	}
}

// ================================================================== police car driver
ANCPoliceDriver::ANCPoliceDriver()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = .05f;
	bWantsPlayerState = false;
}

void ANCPoliceDriver::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	StuckTime = 0.f;
	ReverseTime = 0.f;
}

void ANCPoliceDriver::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ANCVehiclePawn* V = Cast<ANCVehiclePawn>(GetPawn());
	UChaosWheeledVehicleMovementComponent* M = V ? Cast<UChaosWheeledVehicleMovementComponent>(V->GetVehicleMovementComponent()) : nullptr;
	if (!M)
	{
		return;
	}
	if ((!Target && !bSearching) || V->Body.bWrecked)
	{
		M->SetThrottleInput(0.f);
		M->SetBrakeInput(1.f);
		M->SetHandbrakeInput(true);
		M->SetSteeringInput(0.f);
		return;
	}
	M->SetHandbrakeInput(false);
	const FTransform T = V->GetActorTransform();
	const FVector L = T.InverseTransformPositionNoScale(bSearching ? SearchLocation : Target->GetActorLocation());
	const float Ang = FMath::RadiansToDegrees(FMath::Atan2(L.Y, L.X));
	const float Dist = L.Size();
	const float Speed = V->GetSpeedKmh();
	if (ReverseTime > 0.f)
	{
		ReverseTime -= DeltaSeconds;
		M->SetThrottleInput(0.f);
		M->SetBrakeInput(1.f);                          // brake at standstill = reverse
		M->SetSteeringInput(-FMath::Sign(Ang));
		return;
	}
	float Steer = FMath::Clamp(Ang / 30.f, -1.f, 1.f);
	// obstacle feelers: something solid ahead (not the suspect) -> swerve to the freer side
	UWorld* W = GetWorld();
	FCollisionQueryParams Q(SCENE_QUERY_STAT(NCCopFeel), false, V);
	Q.AddIgnoredActor(Target);
	const FVector Fwd = V->GetActorForwardVector();
	const FVector From = V->GetActorLocation() + Fwd * 260.f + FVector(0, 0, 70.f);
	const float Look = 900.f + Speed * 12.f;
	FHitResult H;
	if (Dist > 1500.f && W->LineTraceSingleByChannel(H, From, From + Fwd * Look, ECC_Visibility, Q))
	{
		FHitResult HL, HR;
		const FVector Lf = Fwd.RotateAngleAxis(-35.f, FVector::UpVector);
		const FVector Rt = Fwd.RotateAngleAxis(35.f, FVector::UpVector);
		const float DL = W->LineTraceSingleByChannel(HL, From, From + Lf * Look, ECC_Visibility, Q) ? HL.Distance : Look;
		const float DR = W->LineTraceSingleByChannel(HR, From, From + Rt * Look, ECC_Visibility, Q) ? HR.Distance : Look;
		Steer = DR >= DL ? 1.f : -1.f;
	}
	float Throttle = Dist > 3000.f ? 1.f : (Dist > 1200.f ? 0.65f : 0.3f);
	float Brake = 0.f;
	if (FMath::Abs(Ang) > 110.f && Dist < 3000.f)
	{
		Throttle = 0.6f;                                  // turn round in a wide arc
		Steer = FMath::Sign(Ang);
	}
	if (Dist < 900.f && (bSearching || (Target && Target->GetVelocity().Size() < 300.f)))
	{
		Throttle = 0.f;
		Brake = 1.f;
	}
	if (Speed > 120.f && FMath::Abs(Ang) > 25.f)
	{
		Throttle = 0.2f;                                  // slow for corners
	}
	StuckTime = (Throttle > 0.3f && Speed < 4.f) ? StuckTime + DeltaSeconds : 0.f;
	if (StuckTime > 2.f)
	{
		StuckTime = 0.f;
		ReverseTime = 1.6f;
	}
	M->SetThrottleInput(Throttle);
	M->SetBrakeInput(Brake);
	M->SetSteeringInput(Steer);
}
