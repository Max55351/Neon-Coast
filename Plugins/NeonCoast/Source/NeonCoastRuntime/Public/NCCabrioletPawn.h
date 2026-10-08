#pragma once

#include "CoreMinimal.h"
#include "NCVehiclePawn.h"
#include "NCCabrioletPawn.generated.h"

/** Open-cabin, modular convertible using the existing Chaos chassis and materials. */
UCLASS(Blueprintable)
class NEONCOASTRUNTIME_API ANCCabrioletPawn : public ANCVehiclePawn
{
	GENERATED_BODY()
public:
	ANCCabrioletPawn(const FObjectInitializer& Initializer);
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Roof") bool bRoofOpen = true;
	UFUNCTION(Server, Reliable) void ServerToggleRoof();
	bool IsRoofClosed() const { return !bRoofOpen && RoofFraction < 0.02f; }
protected:
	virtual void OnReset(const FInputActionValue& Value) override;
	UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Roof;
	UPROPERTY() TArray<TObjectPtr<class UStaticMeshComponent>> VisualWheels;
	float RoofFraction = 1.f;
	class UStaticMeshComponent* MakePart(const TCHAR* Name, class UStaticMesh* Shape, const FVector& Location, const FVector& Size, const TCHAR* Material);
	void UpdateRoof();
};
