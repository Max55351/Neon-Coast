#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputActionValue.h"
#include "NCPlayerMove.generated.h"

class UInputAction;
class UInputMappingContext;

/**
 * On-foot extras for the hero (added by ANCGameMode):
 *   V       first / third person
 *   Shift   sprint (also swims faster)
 *   Ctrl    crouch on land, dive in water
 *   Space   jump (template) / swim up
 *   C       take cover behind the wall in front (crouched, slow), C again to leave
 * Swimming: in deep water the hero floats with the head out (movement mode Flying, so looking
 * down + W dives); under water the oxygen runs out in 40 s, then it hurts.
 */
UCLASS(ClassGroup = (NeonCoast), meta = (BlueprintSpawnableComponent))
class NEONCOASTRUNTIME_API UNCPlayerMove : public UActorComponent
{
	GENERATED_BODY()
public:
	UNCPlayerMove();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move") float WalkSpeed = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move") float SprintSpeed = 850.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move") float SwimSpeed = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move") float MaxOxygen = 40.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Move") bool bSprint = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Move") bool bInCover = false;
	/** Seconds of air left (HUD bar while diving). */
	UPROPERTY(BlueprintReadOnly, Category = "Move") float Oxygen = 40.f;
	UPROPERTY(BlueprintReadOnly, Category = "Move") bool bSwimming = false;
	UPROPERTY(BlueprintReadOnly, Category = "Move") bool bUnderWater = false;
	UPROPERTY(BlueprintReadOnly, Category = "Move") bool bFirstPerson = false;

	static UNCPlayerMove* Of(const AActor* A) { return A ? A->FindComponentByClass<UNCPlayerMove>() : nullptr; }

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	friend class ANCGameMode;
	FVector2D SelfTestMove = FVector2D::ZeroVector;
	UFUNCTION(Server, Reliable) void ServerSprint(bool bOn);
	UFUNCTION(Server, Reliable) void ServerCover(bool bOn);
	void SetupInput();
	bool Mine() const;
	void OnView(const FInputActionValue& V);
	void OnSprint(const FInputActionValue& V);
	void OnSprintEnd(const FInputActionValue& V);
	void OnCrouch(const FInputActionValue& V);
	void OnCrouchEnd(const FInputActionValue& V);
	void OnCover(const FInputActionValue& V);
	void OnUp(const FInputActionValue& V);
	void OnUpEnd(const FInputActionValue& V);
	void OnMove(const FInputActionValue& V);
	void OnLook(const FInputActionValue& V);
	void ApplyView();
	void UpdateWater(float Dt);
	bool bInput = false;
	bool bDive = false;
	bool bRise = false;
	float HurtTimer = 0.f;
	float ArmLength = -1.f;
	FVector ArmOffset = FVector::ZeroVector;
	FVector ArmSocket = FVector::ZeroVector;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> Actions;
};
