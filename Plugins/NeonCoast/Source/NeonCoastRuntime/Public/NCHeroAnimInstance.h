#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "NCHeroAnimInstance.generated.h"

class UAnimSequence;
enum class ENCHeroMotion : uint8 { Idle, Walk, Run, Jump, Fall, Land, Aim, Swim, Dive, Crouch, Reload };

/** Snapshot on the game thread; animation workers only consume owned transforms. */
struct FNCHeroAnimProxy : public FAnimInstanceProxy
{
 FNCHeroAnimProxy() = default;
 explicit FNCHeroAnimProxy(UAnimInstance* In) : FAnimInstanceProxy(In) {}
 virtual void PreUpdate(UAnimInstance* In, float Dt) override;
 virtual bool Evaluate(FPoseContext& Output) override;
 TArray<FTransform> Pose;
 float Time = 0.f, StateTime = 0.f, LandingTime = 0.f;
 ENCHeroMotion Previous = ENCHeroMotion::Idle;
 bool WasFalling = false;
};

UCLASS(Transient, NotBlueprintable)
class NEONCOASTRUNTIME_API UNCHeroAnimInstance : public UAnimInstance
{
 GENERATED_BODY()
public:
 virtual void NativeInitializeAnimation() override;
 ENCHeroMotion Motion = ENCHeroMotion::Idle;
 int32 MatchedBones = 0;
 UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> Clips;
protected:
 virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FNCHeroAnimProxy(this); }
 virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* P) override { delete P; }
};
