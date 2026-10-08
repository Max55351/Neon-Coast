#pragma warning(disable: 4456 4457 4458 4459)
#include "NCAnimal.h"

#include "Animation/AnimSequence.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NCTrafficManager.h"
#include "NCVehiclePawn.h"
#include "NCSound.h"
#include "Net/UnrealNetwork.h"

// ====================================================================== animal
ANCAnimal::ANCAnimal()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false);                  // own compact location + client smoothing
	SetNetUpdateFrequency(10.f);
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->InitCapsuleSize(30.f, 30.f);
	Capsule->SetCollisionObjectType(ECC_Pawn);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);     // bullets
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);           // melee sweeps, people
	RootComponent = Capsule;
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Capsule);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
}

void ANCAnimal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANCAnimal, Species);
	DOREPLIFETIME(ANCAnimal, Mode);
	DOREPLIFETIME(ANCAnimal, NetLoc);
	DOREPLIFETIME(ANCAnimal, NetYaw);
}

const FNCAnimalSpecies* ANCAnimal::Def() const
{
	return Manager && Manager->Species.IsValidIndex(Species) ? &Manager->Species[Species] : nullptr;
}

ENCAnimalKind ANCAnimal::Kind() const
{
	const FNCAnimalSpecies* D = Def();
	return D ? D->Kind : ENCAnimalKind::Dog;
}

void ANCAnimal::Init(ANCAnimalManager* InManager, int32 InSpecies, int32 InSpawn, const FVector& InHome)
{
	Manager = InManager;
	Species = InSpecies;
	SpawnIndex = InSpawn;
	Home = InHome;
	Goal = InHome;
	const FNCAnimalSpecies* D = Def();
	Health = D ? D->Health : 50.f;
	Phase = FMath::FRandRange(0.f, 2.f * PI);
	Angle = Phase;
	StateTime = FMath::FRandRange(1.f, 4.f);
	NetLoc = GetActorLocation();
	NetYaw = GetActorRotation().Yaw;
	ApplyLook();
}

void ANCAnimal::OnRep_Species()
{
	if (!Manager)
	{
		for (TActorIterator<ANCAnimalManager> It(GetWorld()); It; ++It)
		{
			Manager = *It;
			break;
		}
	}
	ApplyLook();
}

void ANCAnimal::ApplyLook()
{
	const FNCAnimalSpecies* D = Def();
	if (!D)
	{
		return;
	}
	Capsule->SetCapsuleSize(D->Radius, FMath::Max(D->Radius, D->HalfHeight));
	Mesh->SetSkeletalMeshAsset(D->Mesh);
	// models stand on their origin; sharks and gulls are centred on the body
	const bool bCentred = D->Kind == ENCAnimalKind::Shark || D->Kind == ENCAnimalKind::Gull;
	Mesh->SetRelativeLocation(FVector(0, 0, bCentred ? 0.f : -Capsule->GetScaledCapsuleHalfHeight()));
	UpdateAnim();
}

void ANCAnimal::OnRep_Mode()
{
	// voices: a dog barks when it bolts or attacks, a crocodile hisses
	if (Mode == 2)
	{
		const ENCAnimalKind K = Kind();
		if (K == ENCAnimalKind::Dog)
		{
			ANCSoundBank::Play(this, TEXT("Bark"), GetActorLocation(), 0.9f, FMath::FRandRange(0.85f, 1.15f));
		}
		else if (K == ENCAnimalKind::Croc)
		{
			ANCSoundBank::Play(this, TEXT("Hiss"), GetActorLocation(), 0.8f);
		}
	}
	if (Mode == 3 && !Mesh->IsSimulatingPhysics() && !Capsule->IsSimulatingPhysics())
	{
		// became relevant after it died (no death multicast seen): just lie there
		Mesh->Stop();
		Mesh->SetRelativeRotation(FRotator(0, 0, 90.f));
		return;
	}
	UpdateAnim();
}

void ANCAnimal::UpdateAnim()
{
	const FNCAnimalSpecies* D = Def();
	if (!D || Mode == 3)
	{
		return;
	}
	UAnimSequence* A = (Mode == 0 && D->Idle) ? D->Idle.Get() : D->Walk.Get();
	if (!A)
	{
		return;
	}
	Mesh->PlayAnimation(A, true);
	float Rate = 1.f;
	if (Mode == 2)
	{
		Rate = D->Kind == ENCAnimalKind::Gull ? 1.6f : FMath::Clamp(D->RunSpeed / FMath::Max(1.f, D->WalkSpeed) * 0.45f, 1.5f, 3.f);
	}
	Mesh->SetPlayRate(Rate);
}

void ANCAnimal::SetMode(uint8 M)
{
	if (Mode != M)
	{
		Mode = M;
		OnRep_Mode();                                                   // the host sees / hears it too
	}
}

// ---------------------------------------------------------------------- tick
void ANCAnimal::Tick(float Dt)
{
	Super::Tick(Dt);
	if (Mode == 3)
	{
		return;
	}
	if (HasAuthority())
	{
		Think(Dt);
		NetLoc = GetActorLocation();
		NetYaw = GetActorRotation().Yaw;
		return;
	}
	if (Kind() == ENCAnimalKind::Gull && FMath::FRand() < Dt * 0.12f)
	{
		ANCSoundBank::Play(this, TEXT("Gull"), GetActorLocation(), 0.7f, FMath::FRandRange(0.9f, 1.2f));
	}
	// clients: glide towards the last server position
	const FVector L = FMath::VInterpTo(GetActorLocation(), NetLoc, Dt, 8.f);
	const FRotator R = FMath::RInterpTo(GetActorRotation(), FRotator(0, NetYaw, 0), Dt, 8.f);
	SetActorLocationAndRotation(L, R);
}

void ANCAnimal::Think(float Dt)
{
	const FNCAnimalSpecies* D = Def();
	if (!D)
	{
		return;
	}
	StateTime -= Dt;
	BiteCooldown -= Dt;
	switch (D->Kind)
	{
	case ENCAnimalKind::Shark:
		ThinkShark(Dt, *D);
		break;
	case ENCAnimalKind::Gull:
		ThinkGull(Dt, *D);
		break;
	default:
		ThinkGround(Dt, *D);
		HitCheck -= Dt;
		if (HitCheck <= 0.f)
		{
			HitCheck = 0.1f;
			CheckRunOver();
		}
		break;
	}
}

bool ANCAnimal::GroundZ(const FVector& At, float& OutZ) const
{
	FHitResult H;
	FCollisionObjectQueryParams O;
	O.AddObjectTypesToQuery(ECC_WorldStatic);
	const bool bHit = GetWorld()->LineTraceSingleByObjectType(H, At + FVector(0, 0, 250.f), At - FVector(0, 0, 1500.f), O);
	OutZ = bHit ? H.ImpactPoint.Z : At.Z;
	return bHit;
}

bool ANCAnimal::Blocked(const FVector& Dir, float Dist) const
{
	FHitResult H;
	FCollisionObjectQueryParams O;
	O.AddObjectTypesToQuery(ECC_WorldStatic);
	O.AddObjectTypesToQuery(ECC_WorldDynamic);
	O.AddObjectTypesToQuery(ECC_Vehicle);
	const FVector From = GetActorLocation() + FVector(0, 0, 15.f);
	return GetWorld()->LineTraceSingleByObjectType(H, From, From + Dir * Dist, O);
}

bool ANCAnimal::PickWanderGoal(float Radius)
{
	const float WaterZ = Manager ? Manager->WaterZ : 0.f;
	const bool bSwims = Kind() == ENCAnimalKind::Croc;
	for (int32 Try = 0; Try < 5; ++Try)
	{
		const FVector2D R = FMath::RandPointInCircle(Radius);
		FVector G = Home + FVector(R.X, R.Y, 0.f);
		float Z;
		if (!GroundZ(G, Z))
		{
			continue;
		}
		if (!bSwims && Z < WaterZ + 10.f)
		{
			continue;                                 // dogs and cats stay out of the sea
		}
		G.Z = Z;
		Goal = G;
		return true;
	}
	return false;
}

APawn* ANCAnimal::NearestOnFootPlayer(float MaxDist, float& OutDist) const
{
	APawn* Best = nullptr;
	OutDist = MaxDist;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ACharacter* C = It->Get() ? Cast<ACharacter>(It->Get()->GetPawn()) : nullptr;
		if (!C)
		{
			continue;                                 // in a car (or dead / respawning)
		}
		const float Dd = FVector::Dist(C->GetActorLocation(), GetActorLocation());
		if (Dd < OutDist)
		{
			OutDist = Dd;
			Best = C;
		}
	}
	return Best;
}

void ANCAnimal::TryBite(APawn* Target, float Dmg)
{
	if (!Target || BiteCooldown > 0.f || Dmg <= 0.f)
	{
		return;
	}
	BiteCooldown = Kind() == ENCAnimalKind::Dog ? 0.9f : 1.4f;
	UGameplayStatics::ApplyDamage(Target, Dmg, nullptr, this, UDamageType::StaticClass());
}

void ANCAnimal::ThinkGround(float Dt, const FNCAnimalSpecies& D)
{
	const ENCAnimalKind K = D.Kind;
	const float WaterZ = Manager ? Manager->WaterZ : 0.f;
	float PD = 0.f;
	APawn* Near = NearestOnFootPlayer(K == ENCAnimalKind::Croc ? 900.f : (K == ENCAnimalKind::Cat ? 450.f : 300.f), PD);
	// reactions to people
	if (Near && State != EState::Attack && State != EState::Flee)
	{
		if (K == ENCAnimalKind::Croc)
		{
			Prey = Near;
			State = EState::Attack;
			StateTime = 20.f;
		}
		else if (K == ENCAnimalKind::Cat)
		{
			Scare(Near->GetActorLocation());
		}
	}
	if (State == EState::Attack)
	{
		APawn* P = Prey.Get();
		ACharacter* OnFoot = Cast<ACharacter>(P);
		if (!OnFoot || StateTime <= 0.f || FVector::Dist(Home, GetActorLocation()) > (K == ENCAnimalKind::Croc ? 2500.f : 6000.f))
		{
			Prey = nullptr;
			State = EState::Wander;
			Goal = Home;
			StateTime = 12.f;
		}
		else
		{
			Goal = P->GetActorLocation();
			if (FVector::Dist2D(Goal, GetActorLocation()) < D.Radius + 110.f)
			{
				TryBite(P, D.Bite);
			}
		}
	}
	else if (State == EState::Flee)
	{
		if (StateTime <= 0.f)
		{
			State = EState::Idle;
			StateTime = FMath::FRandRange(2.f, 5.f);
		}
		else
		{
			FVector Away = (GetActorLocation() - Threat).GetSafeNormal2D();
			if (Away.IsNearlyZero())
			{
				Away = GetActorForwardVector();
			}
			Goal = GetActorLocation() + Away * 800.f;
		}
	}
	else if (StateTime <= 0.f)
	{
		// idle <-> wander around home
		if (State == EState::Idle && PickWanderGoal(K == ENCAnimalKind::Cat ? 900.f : 1500.f))
		{
			State = EState::Wander;
			StateTime = 25.f;
		}
		else
		{
			State = EState::Idle;
			StateTime = FMath::FRandRange(3.f, K == ENCAnimalKind::Croc ? 20.f : 8.f);
		}
	}
	// move
	const bool bMove = State != EState::Idle;
	const float Speed = State == EState::Flee || State == EState::Attack ? D.RunSpeed : D.WalkSpeed;
	FVector Loc = GetActorLocation();
	FVector To = Goal - Loc;
	To.Z = 0.f;
	const float Dist = To.Size();
	if (State == EState::Wander && Dist < 60.f)
	{
		State = EState::Idle;
		StateTime = FMath::FRandRange(2.f, 7.f);
	}
	if (!bMove || Dist < 40.f)
	{
		SetMode(0);
		if (Near && K == ENCAnimalKind::Dog)
		{
			// dogs look at people passing by
			const float Yaw = (Near->GetActorLocation() - Loc).Rotation().Yaw;
			SetActorRotation(FRotator(0, FMath::FixedTurn(GetActorRotation().Yaw, Yaw, 180.f * Dt), 0));
		}
		return;
	}
	const FVector Dir = To / Dist;
	const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, Dir.Rotation().Yaw, (Speed > 300.f ? 360.f : 160.f) * Dt);
	const FVector Fwd = FRotator(0, Yaw, 0).Vector();
	if (Blocked(Fwd, D.Radius + 50.f))
	{
		// wall, car or fence ahead: turn away and try elsewhere
		SetActorRotation(FRotator(0, Yaw + FMath::FRandRange(100.f, 160.f) * (FMath::RandBool() ? 1.f : -1.f), 0));
		if (State == EState::Wander)
		{
			PickWanderGoal(1200.f);
		}
		return;
	}
	FVector Next = Loc + Fwd * FMath::Min(Speed * Dt, Dist);
	float Z;
	if (GroundZ(Next, Z))
	{
		const bool bSwims = K == ENCAnimalKind::Croc;
		if (!bSwims && Z < WaterZ + 10.f)
		{
			PickWanderGoal(900.f);                    // shore: go back
			return;
		}
		const float Surface = bSwims ? FMath::Max(Z, WaterZ - 20.f) : Z;
		if (FMath::Abs(Surface - (Loc.Z - Capsule->GetScaledCapsuleHalfHeight())) > 120.f)
		{
			PickWanderGoal(900.f);                    // cliff / wall top
			return;
		}
		Next.Z = FMath::FInterpTo(Loc.Z, Surface + Capsule->GetScaledCapsuleHalfHeight(), Dt, 12.f);
	}
	SetActorLocationAndRotation(Next, FRotator(0, Yaw, 0));
	SetMode(Speed > D.WalkSpeed * 1.5f ? 2 : 1);
}

void ANCAnimal::ThinkShark(float Dt, const FNCAnimalSpecies& D)
{
	const float WaterZ = Manager ? Manager->WaterZ : 0.f;
	FVector Loc = GetActorLocation();
	// swimmers (a player below the surface level, on foot) are prey
	APawn* Target = nullptr;
	float Best = 3500.f;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ACharacter* C = It->Get() ? Cast<ACharacter>(It->Get()->GetPawn()) : nullptr;
		if (C && C->GetActorLocation().Z < WaterZ + 60.f)
		{
			const float Dd = FVector::Dist(C->GetActorLocation(), Loc);
			if (Dd < Best)
			{
				Best = Dd;
				Target = C;
			}
		}
	}
	FVector Want;
	float Speed = D.WalkSpeed;
	if (Target)
	{
		Want = Target->GetActorLocation();
		Speed = D.RunSpeed;
		if (Best < D.Radius + 180.f)
		{
			TryBite(Target, D.Bite);
		}
	}
	else
	{
		// lazy figure-of-eight around home, fin just out of the water
		Angle += Dt * Speed / 3500.f;
		Want = Home + FVector(FMath::Cos(Angle) * 3500.f, FMath::Sin(2.f * Angle) * 1800.f, 0.f);
		Want.Z = WaterZ - 55.f;
	}
	if (State == EState::Flee && StateTime > 0.f)
	{
		Want = Loc + (Loc - Threat).GetSafeNormal2D() * 2000.f;
		Want.Z = WaterZ - 250.f;                     // dive
		Speed = D.RunSpeed;
	}
	FVector To = Want - Loc;
	const float Yaw = FMath::FixedTurn(GetActorRotation().Yaw, To.Rotation().Yaw, 70.f * Dt * (Target ? 2.f : 1.f));
	FVector Next = Loc + FRotator(0, Yaw, 0).Vector() * Speed * Dt;
	Next.Z = FMath::FInterpTo(Loc.Z, FMath::Min(Want.Z, WaterZ - 55.f), Dt, 1.5f);
	float Floor;
	if (GroundZ(Next, Floor) && Floor > Next.Z - 80.f)
	{
		Angle += 0.5f;                                // too shallow: swing back out
		return;
	}
	SetActorLocationAndRotation(Next, FRotator(0, Yaw, 0));
	SetMode(Speed > D.WalkSpeed * 1.5f ? 2 : 1);
}

void ANCAnimal::ThinkGull(float Dt, const FNCAnimalSpecies& D)
{
	const bool bScared = State == EState::Flee && StateTime > 0.f;
	const float R = bScared ? 4000.f : 2200.f;
	Angle += Dt * D.WalkSpeed / R * (bScared ? 1.8f : 1.f);
	const float T = GetWorld()->GetTimeSeconds() + Phase * 10.f;
	FVector Want = Home + FVector(FMath::Cos(Angle) * R, FMath::Sin(Angle) * R * 0.7f, (bScared ? 1500.f : 0.f) + 250.f * FMath::Sin(T * 0.3f));
	const FVector Loc = GetActorLocation();
	FVector Next = FMath::VInterpTo(Loc, Want, Dt, 1.5f);
	const FVector V = Next - Loc;
	const float Yaw = V.SizeSquared2D() > 1.f ? V.Rotation().Yaw : GetActorRotation().Yaw;
	// bank into the turn
	const float Roll = FMath::Clamp(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw, Yaw) * 8.f, -35.f, 35.f);
	SetActorLocationAndRotation(Next, FRotator(0, Yaw, Roll));
	// flap while climbing / scared, glide otherwise
	SetMode(bScared ? 2 : (V.Z > 0.5f * Dt * 60.f || FMath::Sin(T * 0.7f) > 0.4f ? 1 : 0));
}

void ANCAnimal::CheckRunOver()
{
	const FVector Loc = GetActorLocation();
	for (TActorIterator<ANCVehiclePawn> It(GetWorld()); It; ++It)
	{
		const FVector V = It->GetVelocity();
		if (V.SizeSquared() > FMath::Square(350.f) && FVector::DistSquared2D(It->GetActorLocation(), Loc) < FMath::Square(260.f)
			&& FMath::Abs(It->GetActorLocation().Z - Loc.Z) < 200.f)
		{
			Die(V * 0.6f + FVector(0, 0, 350.f));
			return;
		}
	}
	for (TActorIterator<ANCTrafficManager> It(GetWorld()); It; ++It)
	{
		if (It->CarsInRadius(Loc, 180.f).Num() > 0)
		{
			Die(GetActorForwardVector() * -600.f + FVector(0, 0, 300.f));
			return;
		}
	}
}

void ANCAnimal::Scare(const FVector& From)
{
	if (!HasAuthority() || Mode == 3)
	{
		return;
	}
	const ENCAnimalKind K = Kind();
	if (State == EState::Attack && (K == ENCAnimalKind::Croc || K == ENCAnimalKind::Shark))
	{
		return;                                       // predators on the hunt do not care
	}
	if (K == ENCAnimalKind::Croc && FMath::FRand() < 0.5f)
	{
		return;
	}
	Threat = From;
	State = EState::Flee;
	StateTime = FMath::FRandRange(5.f, 9.f);
}

float ANCAnimal::TakeDamage(float Damage, const FDamageEvent& Event, AController* Inst, AActor* Causer)
{
	const float Done = Super::TakeDamage(Damage, Event, Inst, Causer);
	if (!HasAuthority() || Mode == 3 || Damage <= 0.f)
	{
		return Done;
	}
	Health -= Damage;
	FVector Dir = Causer ? (GetActorLocation() - Causer->GetActorLocation()).GetSafeNormal() : FVector::UpVector;
	if (Event.IsOfType(FPointDamageEvent::ClassID))
	{
		Dir = static_cast<const FPointDamageEvent&>(Event).ShotDirection;
	}
	if (Health <= 0.f)
	{
		Die(Dir * FMath::Clamp(Damage * 12.f, 300.f, 1500.f) + FVector(0, 0, 200.f));
		return Done;
	}
	// wounded: dogs and predators go for whoever did it, the rest run
	APawn* Attacker = Inst ? static_cast<APawn*>(Inst->GetPawn()) : Cast<APawn>(Causer);
	const ENCAnimalKind K = Kind();
	const FNCAnimalSpecies* D = Def();
	if (Attacker && D && D->Bite > 0.f && (K != ENCAnimalKind::Dog || Health > D->Health * 0.3f))
	{
		Prey = Attacker;
		State = EState::Attack;
		StateTime = 20.f;
	}
	else
	{
		Scare(Attacker ? Attacker->GetActorLocation() : GetActorLocation() - Dir * 500.f);
	}
	return Done;
}

void ANCAnimal::Die(const FVector& Impulse)
{
	if (Mode == 3)
	{
		return;
	}
	Mode = 3;
	if (Manager)
	{
		Manager->OnAnimalGone(SpawnIndex, true);
	}
	MulticastDie(Impulse);
	SetLifeSpan(45.f);
}

void ANCAnimal::MulticastDie_Implementation(FVector Impulse)
{
	Mode = 3;
	Mesh->Stop();
	if (Mesh->GetPhysicsAsset())
	{
		// ragdoll
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
		Mesh->SetAllBodiesSimulatePhysics(true);
		Mesh->SetSimulatePhysics(true);
		Mesh->WakeAllRigidBodies();
		Mesh->AddImpulse(Impulse, NAME_None, true);
	}
	else
	{
		// no physics asset: the capsule tumbles, the animal lies on its side
		Mesh->SetRelativeRotation(FRotator(0, 0, 90.f));
		Capsule->SetCollisionProfileName(TEXT("PhysicsActor"));
		Capsule->SetSimulatePhysics(true);
		Capsule->AddImpulse(Impulse, NAME_None, true);
	}
}

// ====================================================================== manager
ANCAnimalManager::ANCAnimalManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;                              // animals replicate themselves; insects are local
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANCAnimalManager::BeginPlay()
{
	Super::BeginPlay();
	Alive.SetNum(Spawns.Num());
	NotBefore.Init(0.0, Spawns.Num());
	TArray<UStaticMesh*> PoolMeshes = {ButterflyA.Get(), ButterflyB.Get(), Firefly.Get()};
	for (int32 f = 0; f < 3; ++f)
	{
		PoolMeshes.Add(Fish.IsValidIndex(f) ? Fish[f].Get() : nullptr);
	}
	for (UStaticMesh* M : PoolMeshes)
	{
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
		ISM->SetStaticMesh(M);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetCastShadow(false);
		ISM->SetupAttachment(RootComponent);
		ISM->RegisterComponent();
		InsectPools.Add(ISM);
	}
	UE_LOG(LogTemp, Log, TEXT("[NeonCoast] animals: %d species, %d spawn points, %d insect swarms"), Species.Num(), Spawns.Num(),
		Swarms.Num());
}

void ANCAnimalManager::Scare(UWorld* W, const FVector& Where, float Radius)
{
	if (!W)
	{
		return;
	}
	const float R2 = Radius * Radius * 1.5f;          // animals are jumpier than people
	for (TActorIterator<ANCAnimal> It(W); It; ++It)
	{
		if (FVector::DistSquared(It->GetActorLocation(), Where) < R2)
		{
			It->Scare(Where);
		}
	}
}

void ANCAnimalManager::OnAnimalGone(int32 Index, bool bKilled)
{
	if (Alive.IsValidIndex(Index))
	{
		Alive[Index] = nullptr;
		NotBefore[Index] = GetWorld()->GetTimeSeconds() + (bKilled ? RespawnSeconds : 5.0);
	}
}

void ANCAnimalManager::Tick(float Dt)
{
	Super::Tick(Dt);
	if (HasAuthority())
	{
		Accum += Dt;
		if (Accum > 1.f)
		{
			Accum = 0.f;
			UpdateSpawns();
		}
	}
	UpdateInsects(Dt);
}

void ANCAnimalManager::UpdateSpawns()
{
	TArray<FVector> Players;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APawn* P = It->Get() ? It->Get()->GetPawn() : nullptr)
		{
			Players.Add(P->GetActorLocation());
		}
	}
	if (Players.Num() == 0)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	int32 Count = 0;
	for (const TWeakObjectPtr<ANCAnimal>& A : Alive)
	{
		Count += A.IsValid() ? 1 : 0;
	}
	for (int32 i = 0; i < Spawns.Num(); ++i)
	{
		const FNCAnimalSpawn& S = Spawns[i];
		if (!Species.IsValidIndex(S.Species))
		{
			continue;
		}
		const ENCAnimalKind K = Species[S.Species].Kind;
		const float R = (K == ENCAnimalKind::Shark || K == ENCAnimalKind::Gull) ? WideSpawnRadius : SpawnRadius;
		float Nearest = MAX_flt;
		for (const FVector& P : Players)
		{
			Nearest = FMath::Min(Nearest, static_cast<float>(FVector::Dist2D(P, S.Location)));
		}
		ANCAnimal* A = Alive[i].Get();
		if (A)
		{
			if (Nearest > R * 1.3f && !A->IsDead())
			{
				A->Destroy();                          // nobody around: free it
				OnAnimalGone(i, false);
				--Count;
			}
			continue;
		}
		if (Nearest > R || Now < NotBefore[i] || Count >= MaxAlive)
		{
			continue;
		}
		if (Now > 5.0 && Nearest < 3500.f && K != ENCAnimalKind::Gull && K != ENCAnimalKind::Shark)
		{
			continue;                                  // do not pop up in front of someone
		}
		FVector L = S.Location;
		if (K == ENCAnimalKind::Shark)
		{
			L.Z = WaterZ - 55.f;
		}
		else if (K != ENCAnimalKind::Gull)
		{
			L.Z += FMath::Max(Species[S.Species].Radius, Species[S.Species].HalfHeight);
		}
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ANCAnimal* N = GetWorld()->SpawnActor<ANCAnimal>(ANCAnimal::StaticClass(), FTransform(FRotator(0, FMath::FRandRange(0.f, 360.f), 0), L), SP);
		if (N)
		{
			N->Init(this, S.Species, i, L);
			Alive[i] = N;
			++Count;
		}
	}
}

// ---------------------------------------------------------------------- insects
void ANCAnimalManager::UpdateInsects(float Dt)
{
	if (InsectPools.Num() < 6 || GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	InsectScan -= Dt;
	if (InsectScan <= 0.f)
	{
		InsectScan = 1.f;
		bNight = false;
		for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
		{
			bNight = It->GetLightComponent() && It->GetLightComponent()->Intensity < 2.f;
			break;
		}
		TArray<int32> Now;
		for (int32 i = 0; i < Swarms.Num(); ++i)
		{
			const bool bTime = Swarms[i].Kind == 2 || (Swarms[i].Kind == 1) == bNight;
			if (bTime && FVector::DistSquared(Swarms[i].Location, Cam) < FMath::Square(InsectRadius))
			{
				Now.Add(i);
			}
		}
		if (Now != ActiveSwarms)
		{
			ActiveSwarms = Now;
			for (UInstancedStaticMeshComponent* P : InsectPools)
			{
				P->ClearInstances();
			}
			for (int32 s : ActiveSwarms)
			{
				const int32 N = Swarms[s].Kind == 2 ? 16 : (Swarms[s].Kind == 1 ? 14 : 6);
				for (int32 k = 0; k < N; ++k)
				{
					const int32 Pool = Swarms[s].Kind == 2 ? 3 + s % 3 : (Swarms[s].Kind == 1 ? 2 : (k % 2));
					InsectPools[Pool]->AddInstance(FTransform(Swarms[s].Location), true);
				}
			}
		}
	}
	// flutter: each insect follows its own Lissajous loop; butterflies flap by squashing the wings
	const float T = GetWorld()->GetTimeSeconds();
	TArray<int32> Used = {0, 0, 0, 0, 0, 0};
	for (int32 s : ActiveSwarms)
	{
		const FNCInsectSwarm& Sw = Swarms[s];
		if (Sw.Kind == 2)
		{
			// a school: everyone circles the same way, each on its own lane
			for (int32 k = 0; k < 16; ++k)
			{
				const int32 Pool = 3 + s % 3;
				const float Ph = s * 0.7f + k * 0.39f;
				const float Rr = 250.f + 60.f * FMath::Sin(k * 1.7f);
				const float A = T * 0.35f + Ph;
				const FVector Off(FMath::Cos(A) * Rr, FMath::Sin(A) * Rr, 40.f * FMath::Sin(k * 2.3f + T * 0.8f));
				const FRotator R(0, FMath::RadiansToDegrees(A) + 90.f + 90.f, 0);      // tangent, then the -Y mesh fix
				InsectPools[Pool]->UpdateInstanceTransform(Used[Pool]++, FTransform(R, Sw.Location + Off, FVector(1.f)), true, false, true);
			}
			continue;
		}
		const bool bFly = Sw.Kind == 1;
		const int32 N = bFly ? 14 : 6;
		for (int32 k = 0; k < N; ++k)
		{
			const int32 Pool = bFly ? 2 : (k % 2);
			const float Ph = s * 1.7f + k * 2.39f;
			const float Rr = bFly ? 450.f : 250.f;
			const float Sp = bFly ? 0.25f : 0.5f;
			const FVector Off(FMath::Sin(T * Sp * 1.3f + Ph) * Rr, FMath::Cos(T * Sp * 0.9f + Ph * 1.3f) * Rr,
				FMath::Sin(T * Sp * 2.1f + Ph) * (bFly ? 90.f : 50.f) + (bFly ? 60.f : 0.f));
			const FVector Vel(FMath::Cos(T * Sp * 1.3f + Ph) * 1.3f, -FMath::Sin(T * Sp * 0.9f + Ph * 1.3f) * 0.9f, 0.f);
			FVector Scale(1.f);
			if (!bFly)
			{
				Scale.X = 0.25f + 0.75f * FMath::Abs(FMath::Sin(T * 16.f + Ph));
			}
			else
			{
				// blink
				Scale = FVector(FMath::Max(0.05f, FMath::Sin(T * 2.3f + Ph * 3.f)));
			}
			// meshes face +Y in Blender = -Y after import: yaw so the insect heads where it flies
			const FRotator R(0, Vel.Rotation().Yaw + 90.f, 0);
			InsectPools[Pool]->UpdateInstanceTransform(Used[Pool]++, FTransform(R, Sw.Location + Off, Scale), true, false, true);
		}
	}
	for (UInstancedStaticMeshComponent* P : InsectPools)
	{
		P->MarkRenderStateDirty();
	}
}
