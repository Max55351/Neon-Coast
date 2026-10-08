#pragma warning(disable: 4456 4457 4458 4459)
#include "NCHealthComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Net/UnrealNetwork.h"

UNCHealthComponent::UNCHealthComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UNCHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AActor* O = GetOwner())
	{
		O->OnTakeAnyDamage.AddDynamic(this, &UNCHealthComponent::OnAnyDamage);
	}
}

void UNCHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UNCHealthComponent, Health);
	DOREPLIFETIME(UNCHealthComponent, Armor);
	DOREPLIFETIME(UNCHealthComponent, bDead);
}

void UNCHealthComponent::OnAnyDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType,
	AController* InstigatedBy, AActor* DamageCauser)
{
	FVector Imp = FVector::ZeroVector;
	if (DamageCauser && DamagedActor)
	{
		Imp = (DamagedActor->GetActorLocation() - DamageCauser->GetActorLocation()).GetSafeNormal() * FMath::Min(Damage * 60.f, 30000.f);
	}
	Hurt(Damage, InstigatedBy, Imp);
}

void UNCHealthComponent::Hurt(float Amount, AController* Instigator, const FVector& Impulse)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bDead || Amount <= 0.f)
	{
		return;
	}
	const float Soak = FMath::Min(Armor, Amount * 0.66f);
	Armor -= Soak;
	Health = FMath::Max(0.f, Health - (Amount - Soak));
	MulticastHurt(Health);
	if (Health <= 0.f)
	{
		PendingImpulse = Impulse;
		bDead = true;
		OnRep_Dead();
		OnDeath.Broadcast(GetOwner(), Instigator);
	}
}

void UNCHealthComponent::MulticastHurt_Implementation(float NewHealth)
{
	LastHurtTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}

void UNCHealthComponent::Heal(float Amount)
{
	if (GetOwner() && GetOwner()->HasAuthority() && !bDead)
	{
		Health = FMath::Min(MaxHealth, Health + Amount);
	}
}

void UNCHealthComponent::AddArmor(float Amount)
{
	if (GetOwner() && GetOwner()->HasAuthority() && !bDead)
	{
		Armor = FMath::Min(MaxArmor, Armor + Amount);
	}
}

void UNCHealthComponent::Revive()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		bDead = false;
		Health = MaxHealth;
		Armor = 0.f;
	}
}

void UNCHealthComponent::OnRep_Dead()
{
	if (bDead)
	{
		Ragdoll(PendingImpulse);
	}
}

void UNCHealthComponent::Ragdoll(const FVector& Impulse)
{
	ACharacter* C = Cast<ACharacter>(GetOwner());
	if (!C)
	{
		return;
	}
	if (UCharacterMovementComponent* M = C->GetCharacterMovement())
	{
		M->DisableMovement();
		M->StopMovementImmediately();
	}
	C->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	USkeletalMeshComponent* Mesh = C->GetMesh();
	if (Mesh && Mesh->GetPhysicsAsset())
	{
		Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
		Mesh->SetAllBodiesSimulatePhysics(true);
		Mesh->SetSimulatePhysics(true);
		Mesh->WakeAllRigidBodies();
		if (!Impulse.IsNearlyZero())
		{
			Mesh->AddImpulse(Impulse, NAME_None, true);
		}
	}
	C->DisableInput(nullptr);
}
