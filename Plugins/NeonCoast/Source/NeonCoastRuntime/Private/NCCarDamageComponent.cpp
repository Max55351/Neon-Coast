#pragma warning(disable: 4456 4457 4458 4459)
#include "NCCarDamageComponent.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "WheeledVehiclePawn.h"
#include "NCVehiclePawn.h"

static const FName Morphs[5] = {TEXT("Damage_Front"), TEXT("Damage_Rear"), TEXT("Damage_Left"), TEXT("Damage_Right"), TEXT("Damage_Roof")};

UNCCarDamageComponent::UNCCarDamageComponent()
{
	SetIsReplicatedByDefault(true);
	Damage.Init(0.f, 5);
}

void UNCCarDamageComponent::BeginPlay()
{
	Super::BeginPlay();
	if (Damage.Num() != 5)
	{
		Damage.Init(0.f, 5);
	}
	if (USkeletalMeshComponent* Mesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>())
	{
		Mesh->SetNotifyRigidBodyCollision(true);
		if (GetOwner()->HasAuthority())
		{
			Mesh->OnComponentHit.AddDynamic(this, &UNCCarDamageComponent::OnHit);
		}
	}
	ApplyMorphs();
}

void UNCCarDamageComponent::OnHit(UPrimitiveComponent* HitComp, AActor* Other, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	const float Now = GetWorld()->GetTimeSeconds();
	const float Imp = NormalImpulse.Size();
	if (Imp < MinImpulse || Now - LastHitTime < 0.1f)
	{
		return;
	}
	LastHitTime = Now;
	AddDamageAt(Hit.ImpactPoint, (Imp - MinImpulse) * Sensitivity);
}

void UNCCarDamageComponent::AddDamageAt(const FVector& WorldPoint, float Amount)
{
	USkeletalMeshComponent* Mesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
	if (!Mesh || Amount <= 0.f)
	{
		return;
	}
	// vehicle local space: +X front, +Y right, +Z up
	const FVector L = Mesh->GetComponentTransform().InverseTransformPosition(WorldPoint);
	const FBoxSphereBounds B = Mesh->CalcBounds(FTransform::Identity);
	const FVector Ext = B.BoxExtent;
	const FVector Rel(L.X / FMath::Max(Ext.X, 1.0), L.Y / FMath::Max(Ext.Y, 1.0), (L.Z - B.Origin.Z) / FMath::Max(Ext.Z, 1.0));
	float W[5] = {0, 0, 0, 0, 0};
	W[0] = FMath::Max(0.f, (float)Rel.X);
	W[1] = FMath::Max(0.f, (float)-Rel.X);
	W[2] = FMath::Max(0.f, (float)-Rel.Y);
	W[3] = FMath::Max(0.f, (float)Rel.Y);
	W[4] = FMath::Max(0.f, (float)Rel.Z - 0.4f) * 2.f;
	const float Sum = W[0] + W[1] + W[2] + W[3] + W[4] + KINDA_SMALL_NUMBER;
	for (int32 i = 0; i < 5; ++i)
	{
		Damage[i] = FMath::Clamp(Damage[i] + Amount * W[i] / Sum, 0.f, 1.f);
	}
	ApplyMorphs();
	if (ANCVehiclePawn* V = Cast<ANCVehiclePawn>(GetOwner()))
	{
		if (GetOwner()->HasAuthority())
		{
			V->OnBodyImpact(L, WorldPoint, Amount);
		}
	}
	if (Damage[0] >= EngineFailAt)
	{
		if (AWheeledVehiclePawn* V = Cast<AWheeledVehiclePawn>(GetOwner()))
		{
			if (UChaosWheeledVehicleMovementComponent* M = Cast<UChaosWheeledVehicleMovementComponent>(V->GetVehicleMovementComponent()))
			{
				M->SetThrottleInput(0.f);
				M->SetMaxEngineTorque(0.f);
			}
		}
	}
}

void UNCCarDamageComponent::Repair()
{
	Damage.Init(0.f, 5);
	ApplyMorphs();
}

float UNCCarDamageComponent::GetTotalDamage() const
{
	float S = 0.f;
	for (float D : Damage)
	{
		S += D;
	}
	return S / 5.f;
}

void UNCCarDamageComponent::OnRep_Damage()
{
	ApplyMorphs();
}

void UNCCarDamageComponent::ApplyMorphs()
{
	if (USkeletalMeshComponent* Mesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>())
	{
		for (int32 i = 0; i < 5 && i < Damage.Num(); ++i)
		{
			Mesh->SetMorphTarget(Morphs[i], Damage[i]);
		}
	}
}

void UNCCarDamageComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UNCCarDamageComponent, Damage);
}
