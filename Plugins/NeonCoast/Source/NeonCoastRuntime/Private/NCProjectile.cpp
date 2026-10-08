#pragma warning(disable: 4456 4457 4458 4459)
#include "NCProjectile.h"

#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "NCCrowdManager.h"
#include "NCHealthComponent.h"
#include "NCWeaponComponent.h"
#include "Net/UnrealNetwork.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "TimerManager.h"

static UStaticMesh* LoadNCMesh(const TCHAR* Name)
{
	return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/NeonCoast/Meshes/SM_%s.SM_%s"), Name, Name));
}

// ================================================================== projectile
ANCProjectile::ANCProjectile()
{
	bReplicates = true;
	SetReplicateMovement(true);
	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(8.f);
	Sphere->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Sphere->SetNotifyRigidBodyCollision(true);
	RootComponent = Sphere;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Sphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Move = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Move"));
	Move->UpdatedComponent = Sphere;
	Move->bRotationFollowsVelocity = true;
	InitialLifeSpan = 12.f;
}

void ANCProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANCProjectile, Kind);
}

ANCProjectile* ANCProjectile::Launch(UWorld* W, uint8 Kind, const FVector& From, const FVector& Velocity, float Damage,
	float Radius, AActor* Owner, APawn* Instigator)
{
	FActorSpawnParameters P;
	P.Owner = Owner;
	P.Instigator = Instigator;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	P.bDeferConstruction = true;
	ANCProjectile* Pr = W->SpawnActor<ANCProjectile>(ANCProjectile::StaticClass(), FTransform(Velocity.Rotation(), From), P);
	if (!Pr)
	{
		return nullptr;
	}
	Pr->Kind = Kind;
	Pr->Damage = Damage;
	Pr->Radius = Radius;
	Pr->Move->InitialSpeed = Velocity.Size();
	Pr->Move->MaxSpeed = Velocity.Size() * 1.2f;
	Pr->Move->Velocity = Velocity;
	Pr->Move->ProjectileGravityScale = Kind == 0 ? 0.f : 1.f;
	Pr->Move->bShouldBounce = Kind == 1;
	Pr->Move->Bounciness = 0.35f;
	Pr->Move->Friction = 0.4f;
	Pr->Move->bRotationFollowsVelocity = Kind == 0;
	if (Owner)
	{
		Pr->Sphere->IgnoreActorWhenMoving(Owner, true);
	}
	UGameplayStatics::FinishSpawningActor(Pr, FTransform(Velocity.Rotation(), From));
	return Pr;
}

void ANCProjectile::BeginPlay()
{
	Super::BeginPlay();
	static const TCHAR* Meshes[3] = {TEXT("NC2_Projectile_Rocket"), TEXT("NC2_Weapon_Grenade"), TEXT("NC2_Weapon_Molotov")};
	if (UStaticMesh* SM = LoadNCMesh(Meshes[FMath::Min<int32>(Kind, 2)]))
	{
		Mesh->SetStaticMesh(SM);
		Mesh->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));      // Blender +Y (-Y in UE) -> +X
	}
	if (Kind == 0)
	{
		if (UParticleSystem* Smoke = LoadObject<UParticleSystem>(nullptr, TEXT("/Game/StarterContent/Particles/P_Smoke.P_Smoke")))
		{
			UGameplayStatics::SpawnEmitterAttached(Smoke, Mesh, NAME_None, FVector(-60.f, 0, 0), FRotator::ZeroRotator,
				FVector(0.5f));
		}
		UPointLightComponent* L = NewObject<UPointLightComponent>(this);
		L->SetupAttachment(Mesh);
		L->SetRelativeLocation(FVector(-70.f, 0, 0));
		L->SetIntensity(30000.f);
		L->SetLightColor(FLinearColor(1.f, 0.6f, 0.2f));
		L->SetAttenuationRadius(600.f);
		L->SetCastShadows(false);
		L->RegisterComponent();
	}
	if (HasAuthority())
	{
		Sphere->OnComponentHit.AddDynamic(this, &ANCProjectile::OnHit);
		if (Kind == 1)
		{
			GetWorldTimerManager().SetTimer(Fuse, this, &ANCProjectile::Detonate, 3.0f, false);
		}
	}
}

void ANCProjectile::OnHit(UPrimitiveComponent* HitComp, AActor* Other, UPrimitiveComponent* OtherComp, FVector Impulse,
	const FHitResult& Hit)
{
	if (Kind != 1)                                   // grenades bounce and wait for the fuse
	{
		Detonate();
	}
}

void ANCProjectile::Detonate()
{
	if (bDone || !HasAuthority())
	{
		return;
	}
	bDone = true;
	AController* Inst = GetInstigator() ? GetInstigator()->GetController() : nullptr;
	UNCWeaponComponent::Explode(GetWorld(), GetActorLocation(), Radius, Damage, this, Inst, Kind == 2);
	Destroy();
}

// ================================================================== tracer
ANCTracer::ANCTracer()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	InitialLifeSpan = 0.07f;
}

void ANCTracer::Setup(const FVector& From, const FVector& To, bool bFlash)
{
	// A raw static UObject pointer outlives UE garbage collection, not its asset.
	// Reload/resolve each shot; the beam's reflected properties keep assets alive.
	UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/NeonCoast/Materials/M_NC_Tracer.M_NC_Tracer"));
	const FVector D = To - From;
	const float L = D.Size();
	if (Cyl && L > 10.f)
	{
		Beam = NewObject<UStaticMeshComponent>(this);
		Beam->SetStaticMesh(Cyl);
		Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Beam->SetCastShadow(false);
		Beam->SetupAttachment(RootComponent);
		Beam->RegisterComponent();
		// the engine cylinder is 100 cm tall along Z around its centre, 100 cm across
		Beam->SetWorldLocationAndRotation((From + To) * 0.5f, FRotationMatrix::MakeFromZ(D / L).Rotator());
		Beam->SetWorldScale3D(FVector(0.018f, 0.018f, L / 100.f));
		if (Mat)
		{
			Beam->SetMaterial(0, Mat);
		}
	}
	if (bFlash)
	{
		Flash = NewObject<UPointLightComponent>(this);
		Flash->SetupAttachment(RootComponent);
		Flash->SetRelativeLocation(FVector::ZeroVector);                  // the tracer is spawned at the muzzle
		Flash->SetIntensity(40000.f);
		Flash->SetLightColor(FLinearColor(1.f, 0.75f, 0.35f));
		Flash->SetAttenuationRadius(500.f);
		Flash->SetCastShadows(false);
		Flash->RegisterComponent();                                      // configure first, register last
	}
}

// ================================================================== fire area
ANCFireArea::ANCFireArea()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	InitialLifeSpan = 12.f;
}

void ANCFireArea::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANCFireArea, Radius);
}

void ANCFireArea::BeginPlay()
{
	Super::BeginPlay();
	if (UParticleSystem* Fire = LoadObject<UParticleSystem>(nullptr, TEXT("/Game/StarterContent/Particles/P_Fire.P_Fire")))
	{
		for (int32 k = 0; k < 5; ++k)
		{
			const float A = k * 2.f * PI / 5.f;
			const FVector O = k == 0 ? FVector::ZeroVector : FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * Radius * 0.55f;
			UGameplayStatics::SpawnEmitterAttached(Fire, RootComponent, NAME_None, O, FRotator::ZeroRotator, FVector(1.2f));
		}
	}
	UPointLightComponent* L = NewObject<UPointLightComponent>(this);
	L->SetupAttachment(RootComponent);
	L->SetRelativeLocation(FVector(0, 0, 120.f));
	L->SetIntensity(60000.f);
	L->SetLightColor(FLinearColor(1.f, 0.5f, 0.15f));
	L->SetAttenuationRadius(Radius * 3.f);
	L->SetCastShadows(false);
	L->RegisterComponent();
}

void ANCFireArea::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	const FVector C = GetActorLocation();
	TArray<AActor*> Hit;
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		if (FVector::Dist(It->GetActorLocation(), C) < Radius + 50.f)
		{
			if (UNCHealthComponent* H = UNCHealthComponent::Of(*It))
			{
				H->Hurt(DamagePerSecond * DeltaSeconds, Starter, FVector::ZeroVector);
			}
		}
	}
	for (TActorIterator<ANCCrowdManager> It(GetWorld()); It; ++It)
	{
		for (int32 P : It->PedsInRadius(C, Radius))
		{
			It->KillPed(P, FVector(0, 0, 150.f));
		}
	}
}
