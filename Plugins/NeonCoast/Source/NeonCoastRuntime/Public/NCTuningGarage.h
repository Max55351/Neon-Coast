#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCTuningGarage.generated.h"

class UBoxComponent;

/** 'Neon Coast Customs': drive a car into the box and the tuning menu opens for its driver. */
UCLASS()
class NEONCOASTRUNTIME_API ANCTuningGarage : public AActor
{
	GENERATED_BODY()
public:
	ANCTuningGarage();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tuning") TObjectPtr<UBoxComponent> Bay;

protected:
	UFUNCTION() void OnEnter(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body, bool bSweep, const FHitResult& Hit);
	UFUNCTION() void OnLeave(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 Body);
};
