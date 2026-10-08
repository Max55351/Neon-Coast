#pragma warning(disable: 4456 4457 4458 4459)
#include "NCWardrobe.h"
#include "NCHeroAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"

ANCWardrobe::ANCWardrobe()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ANCWardrobe* ANCWardrobe::Get(const UObject* Ctx)
{
	UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	if (!W)
	{
		return nullptr;
	}
	for (TActorIterator<ANCWardrobe> It(W); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

FLinearColor ANCWardrobe::HairColour(int32 I)
{
	static const FLinearColor Cols[6] = {FLinearColor(0.02f, 0.015f, 0.01f), FLinearColor(0.65f, 0.5f, 0.25f), FLinearColor(0.35f, 0.08f, 0.02f),
		FLinearColor(0.9f, 0.1f, 0.5f), FLinearColor(0.05f, 0.5f, 0.9f), FLinearColor(0.8f, 0.8f, 0.8f)};
	return Cols[FMath::Clamp(I, 0, 5)];
}

void ANCWardrobe::Apply(APawn* Pawn, int32 Index, int32 Hair)
{
	ACharacter* C = Cast<ACharacter>(Pawn);
	ANCWardrobe* W = Get(Pawn);
	if (!C || !W || !C->GetMesh())
	{
		return;
	}
	UNCOutfitComponent* O = C->FindComponentByClass<UNCOutfitComponent>();
	if (!W->Outfits.IsValidIndex(Index) && W->Outfits.Num() > 0)
	{
		// no outfit bought yet: the hero wears the casual street look instead of the grey mannequin
		Index = 0;
		for (int32 i = 0; i < W->Outfits.Num(); ++i)
		{
			const FNCOutfit& Candidate = W->Outfits[i];
			const FString N = Candidate.Mesh ? Candidate.Mesh->GetName() : Candidate.Name;
			if (N.Contains(TEXT("Taro")) || (Candidate.ActorClass && Candidate.ActorClass->GetName().Contains(TEXT("Taro"))))
			{
				Index = i;
				break;
			}
		}
	}
	if (!W->Outfits.IsValidIndex(Index))
	{
		// default look: the mannequin again
		C->GetMesh()->SetVisibility(true, true);
		if (O)
		{
			O->DestroyComponent();
		}
		return;
	}
	if (!O)
	{
		O = NewObject<UNCOutfitComponent>(C, TEXT("NCOutfit"));
		O->RegisterComponent();
	}
	O->Wear(W->Outfits[Index], Hair);
	// the mannequin keeps animating (weapon sockets, sockets for the camera) but is not drawn
	C->GetMesh()->SetVisibility(!(O->Body && O->Body->GetSkeletalMeshAsset()), false);
	C->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
}

UNCOutfitComponent::UNCOutfitComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UNCOutfitComponent::Wear(const FNCOutfit& Outfit, int32 Hair)
{
	ACharacter* C = Cast<ACharacter>(GetOwner());
	if (!C)
	{
		return;
	}
	Look = Outfit;
	if (Outfit.ActorClass)
	{
		// MetaHuman: a whole character actor (body + face + grooms) rides on the hero capsule
		if (Body && !Extra)
		{
			Body->DestroyComponent();
		}
		Body = nullptr;
		if (Extra)
		{
			Extra->Destroy();
			Extra = nullptr;
		}
		FActorSpawnParameters SP;
		SP.Owner = C;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Extra = GetWorld()->SpawnActor<AActor>(Outfit.ActorClass, C->GetActorTransform(), SP);
		if (Extra)
		{
			Extra->AttachToComponent(C->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			Extra->SetActorRelativeLocation(FVector(0.f, 0.f, -C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
			// MetaHuman bodies and these animation clips face +Y. Align their
			// forward axis with the character capsule's +X instead of running sideways.
			Extra->SetActorRelativeRotation(FRotator(0.f, -90.f, 0.f));
			TArray<UPrimitiveComponent*> Prims;
			Extra->GetComponents(Prims);
			for (UPrimitiveComponent* Pr : Prims)
			{
				Pr->SetCollisionEnabled(ECollisionEnabled::NoCollision);   // the hero capsule is the collision
				Pr->SetCanEverAffectNavigation(false);
			}
			TArray<USkeletalMeshComponent*> Skels;
			Extra->GetComponents(Skels);
			for (USkeletalMeshComponent* S : Skels)
			{
				if (S->GetName().Equals(TEXT("Body"), ESearchCase::IgnoreCase))
				{
					Body = S;
					break;
				}
			}
			if (Body)
			{
				Body->SetAnimInstanceClass(UNCHeroAnimInstance::StaticClass());
				Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			}
			else UE_LOG(LogTemp,Warning,TEXT("[NCOutfit] Missing Body component in %s; keeping base hero visible."),*Extra->GetName());
		}
		Playing = -1;
		return;
	}
	if (Extra)
	{
		Extra->Destroy();
		Extra = nullptr;
		Body = nullptr;
	}
	if (!Body)
	{
		Body = NewObject<USkeletalMeshComponent>(C, TEXT("NCOutfitBody"));
		Body->SetupAttachment(C->GetRootComponent());
		// same feet height as the mannequin; MakeHuman meshes already face +X
		Body->SetRelativeLocation(FVector(0.f, 0.f, C->GetMesh()->GetRelativeLocation().Z));
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->RegisterComponent();
	}
	Body->SetSkeletalMeshAsset(Outfit.Mesh);
	Body->EmptyOverrideMaterials(); // do not carry previous outfit slots / hair tint onto another mesh
	Body->SetAnimInstanceClass(UNCHeroAnimInstance::StaticClass());
	Body->SetVisibility(true);
	if (Hair >= 0)
	{
		// barber: tint every hair / eyebrow material slot
		const TArray<FName> Slots = Body->GetMaterialSlotNames();
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			const FString S = Slots[i].ToString();
			if (S.Contains(TEXT("hair"), ESearchCase::IgnoreCase) || S.Contains(TEXT("brow"), ESearchCase::IgnoreCase))
			{
				if (UMaterialInstanceDynamic* M = Body->CreateDynamicMaterialInstance(i))
				{
					M->SetVectorParameterValue(TEXT("BaseColor"), ANCWardrobe::HairColour(Hair));
				}
			}
		}
	}
	Playing = -1;
}

void UNCOutfitComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(Dt, TickType, ThisTickFunction);
	const APawn* P = Cast<APawn>(GetOwner());
	if (!Body || !P)
	{
		return;
	}
	if (Extra)
	{
		Extra->SetActorHiddenInGame(P->IsHidden());
	}
	else
	{
		Body->SetVisibility(!P->IsHidden());                               // hidden while sitting in a car
	}
	const float Speed = P->GetVelocity().Size2D();
	// The native state machine also handles aim, jump, landing, swimming and reload.
	if (Cast<UNCHeroAnimInstance>(Body->GetAnimInstance())) return;
	const int32 Want = Speed < 20.f ? 0 : (Speed < 330.f ? 1 : 2);
	UAnimSequence* A = Want == 0 ? Look.Idle.Get() : (Want == 1 ? Look.Walk.Get() : (Look.Run ? Look.Run.Get() : Look.Walk.Get()));
	if (Want != Playing && A)
	{
		Playing = Want;
		Body->PlayAnimation(A, true);
	}
	if (Want == 1)
	{
		Body->SetPlayRate(FMath::Clamp(Speed / 130.f, 0.6f, 2.5f));
	}
	else if (Want == 2)
	{
		Body->SetPlayRate(FMath::Clamp(Speed / 500.f, 0.7f, 1.6f));
	}
}

void UNCOutfitComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (Extra)
	{
		Extra->Destroy();
		Extra = nullptr;
	}
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}
