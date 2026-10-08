#pragma warning(disable: 4456 4457 4458 4459)
#include "NCPickup.h"

#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "NCHUD.h"
#include "NCHealthComponent.h"
#include "NCPlayerState.h"
#include "NCSound.h"
#include "NCWeaponComponent.h"
#include "NCWeaponTypes.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

static UStaticMesh* NCMesh(const FString& Name)
{
	return Name.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/NeonCoast/Meshes/SM_%s.SM_%s"), *Name, *Name));
}

ANCPickup::ANCPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	Sphere->InitSphereRadius(110.f);
	Sphere->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	RootComponent = Sphere;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Sphere);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ring"));
	Ring->SetupAttachment(Sphere);
	Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ring->SetRelativeLocation(FVector(0, 0, -95.f));
}

void ANCPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANCPickup, bActive);
}

void ANCPickup::BeginPlay()
{
	Super::BeginPlay();
	FString M = Kind == 1 ? TEXT("NC2_Pickup_Health") : (Kind == 2 ? TEXT("NC2_Pickup_Armor") : ((Kind == 3 || Kind == 4) ? TEXT("NC2_Treasure") : TEXT("")));
	if (Kind == 0)
	{
		if (const FNCWeaponDef* D = NCWeapons::Find(Weapon))
		{
			M = D->Mesh;
		}
	}
	Mesh->SetStaticMesh(NCMesh(M));
	Mesh->SetWorldScale3D(FVector(Kind == 0 ? 1.6f : 1.0f));
	if (Kind == 3)
	{
		Sphere->SetSphereRadius(260.f);                              // big enough for the submarine to scoop it
	}
	Ring->SetStaticMesh(NCMesh(TEXT("NC2_Pickup_Ring")));
	if (HasAuthority())
	{
		Sphere->OnComponentBeginOverlap.AddDynamic(this, &ANCPickup::OnOverlap);
	}
}

void ANCPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float T = GetWorld()->GetTimeSeconds();
	Mesh->SetRelativeLocationAndRotation(FVector(0, 0, -20.f + 10.f * FMath::Sin(T * 2.f)), FRotator(0.f, T * 90.f, 0.f));
}

void ANCPickup::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Index, bool bSweep,
	const FHitResult& Hit)
{
	APawn* P = Cast<APawn>(Other);
	if (!bActive || !P || !P->IsPlayerControlled())
	{
		return;
	}
	UNCHealthComponent* H = UNCHealthComponent::Of(P);
	UNCWeaponComponent* W = UNCWeaponComponent::Of(P);
	bool bTaken = false;
	if (Kind == 0 && W)
	{
		const FNCWeaponDef* D = NCWeapons::Find(Weapon);
		W->Give(Weapon, D ? FMath::Max(D->MagSize * 2, D->AmmoPerBox) : 30);
		bTaken = true;
	}
	else if (Kind == 1 && H && H->Health < H->MaxHealth)
	{
		H->Heal(100.f);
		bTaken = true;
	}
	else if (Kind == 2 && H && H->Armor < H->MaxArmor)
	{
		H->AddArmor(100.f);
		bTaken = true;
	}
	else if (Kind == 4)
	{
		bTaken = true;                                                   // mission item: the mission manager takes it from here
		RespawnTime = 1e6f;
	}
	else if (Kind == 3)
	{
		// sunken treasure (reach it with the submarine): money for whoever grabs it
		if (ANCPlayerState* PS = P->GetPlayerState<ANCPlayerState>())
		{
			PS->Money += 2500;
			bTaken = true;
		}
	}
	if (bTaken)
	{
		bActive = false;
		OnRep_Active();
		GetWorldTimerManager().SetTimer(Timer, this, &ANCPickup::Reactivate, RespawnTime, false);
	}
}

void ANCPickup::Reactivate()
{
	bActive = true;
	OnRep_Active();
}

void ANCPickup::OnRep_Active()
{
	Mesh->SetVisibility(bActive);
	Ring->SetVisibility(bActive);
	if (!bActive)
	{
		ANCSoundBank::Play(this, TEXT("Chime"), GetActorLocation(), 0.8f, Kind == 3 ? 0.8f : 1.f);
	}
}

// ================================================================== shop zone
ANCShopZone::ANCShopZone()
{
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	Box->SetBoxExtent(FVector(220.f, 220.f, 150.f));
	Box->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	RootComponent = Box;
}

void ANCShopZone::BeginPlay()
{
	Super::BeginPlay();
	Box->OnComponentBeginOverlap.AddDynamic(this, &ANCShopZone::OnBegin);
	if (ShopType == 7)
	{
		// mission giver: a big glowing ring on the ground
		UStaticMeshComponent* R = NewObject<UStaticMeshComponent>(this);
		R->SetStaticMesh(NCMesh(TEXT("NC2_Pickup_Ring")));
		R->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		R->SetupAttachment(RootComponent);
		R->SetRelativeLocation(FVector(0.f, 0.f, -140.f));
		R->SetRelativeScale3D(FVector(3.f, 3.f, 1.f));
		R->RegisterComponent();
	}
	Box->OnComponentEndOverlap.AddDynamic(this, &ANCShopZone::OnEnd);
}

static ANCHUD* LocalHud(AActor* Other)
{
	const APawn* P = Cast<APawn>(Other);
	const APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	return (PC && PC->IsLocalController()) ? Cast<ANCHUD>(PC->GetHUD()) : nullptr;
}

void ANCShopZone::OnBegin(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Index, bool bSweep,
	const FHitResult& Hit)
{
	if (!bAutoOpen) return;
	if (ANCHUD* H = LocalHud(Other))
	{
		H->OpenShop(ShopType, Title);
	}
}

void ANCShopZone::OnEnd(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Index)
{
	if (ANCHUD* H = LocalHud(Other))
	{
		H->CloseShop();
	}
}
