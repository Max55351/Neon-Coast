#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "NCVehicleAnimInstance.generated.h"

/** Native wheel animation (spin, steer, suspension) - no Animation Blueprint needed. */
struct FNCVehicleAnimProxy : public FAnimInstanceProxy
{
	FNCVehicleAnimProxy() = default;
	explicit FNCVehicleAnimProxy(UAnimInstance* In) : FAnimInstanceProxy(In) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

	struct FWheel
	{
		FName Bone;
		float SpinDeg = 0.f;
		float SteerDeg = 0.f;
		float SuspensionCm = 0.f;
	};
	TArray<FWheel> Wheels;
	float OpenFrac[6] = {0, 0, 0, 0, 0, 0};
	float SteerWheelDeg = 0.f;
	bool bHasBody = false;
};

UCLASS(Transient, NotBlueprintable)
class NEONCOASTRUNTIME_API UNCVehicleAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FNCVehicleAnimProxy(this); }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override { delete InProxy; }
};
