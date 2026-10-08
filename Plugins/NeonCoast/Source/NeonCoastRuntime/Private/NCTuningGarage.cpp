#pragma warning(disable: 4456 4457 4458 4459)
#include "NCTuningGarage.h"

#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "NCHUD.h"
#include "NCVehiclePawn.h"

ANCTuningGarage::ANCTuningGarage()
{
	Bay = CreateDefaultSubobject<UBoxComponent>(TEXT("Bay"));
	Bay->SetBoxExtent(FVector(600.f, 450.f, 250.f));
	Bay->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	RootComponent = Bay;
	Bay->OnComponentBeginOverlap.AddDynamic(this, &ANCTuningGarage::OnEnter);
	Bay->OnComponentEndOverlap.AddDynamic(this, &ANCTuningGarage::OnLeave);
}

static ANCHUD* LocalHud(AActor* Other)
{
	ANCVehiclePawn* V = Cast<ANCVehiclePawn>(Other);
	APlayerController* PC = V ? Cast<APlayerController>(V->GetController()) : nullptr;
	return (PC && PC->IsLocalController()) ? Cast<ANCHUD>(PC->GetHUD()) : nullptr;
}

void ANCTuningGarage::OnEnter(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (ANCHUD* H = LocalHud(Other))
	{
		H->OpenTuning(Cast<ANCVehiclePawn>(Other));
	}
}

void ANCTuningGarage::OnLeave(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32)
{
	if (ANCHUD* H = LocalHud(Other))
	{
		H->CloseTuning(false);
	}
}
