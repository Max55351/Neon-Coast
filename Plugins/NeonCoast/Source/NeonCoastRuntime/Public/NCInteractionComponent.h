#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputActionValue.h"
#include "NCInteractionComponent.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * Added to the on-foot player character (automatically by ANCGameMode).
 * F / gamepad Y: enter the nearest parked car, or pull a car out of passing traffic.
 */
UCLASS(ClassGroup = (NeonCoast), meta = (BlueprintSpawnableComponent))
class NEONCOASTRUNTIME_API UNCInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNCInteractionComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction") float EnterRadius = 450.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction") float CarjackRadius = 700.f;

	UFUNCTION(BlueprintCallable, Category = "Interaction") void Interact();
	void RestoreAfterExit();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	UFUNCTION(Server, Reliable) void ServerInteract();
	UFUNCTION(Server, Reliable) void ServerUseBusiness();
	void OnBusiness(const FInputActionValue& V);
	void OnInteract(const FInputActionValue& V);
	void TryBindInput();

	UPROPERTY(Transient) TObjectPtr<UInputAction> InteractAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> BusinessAction;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
	bool bBound = false;
	TWeakObjectPtr<class UInputComponent> BoundInput;
};
