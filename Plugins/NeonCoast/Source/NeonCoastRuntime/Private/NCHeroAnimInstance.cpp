#include "NCHeroAnimInstance.h"
#include "NCWardrobe.h"
#include "NCPlayerMove.h"
#include "NCWeaponComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UNCHeroAnimInstance::NativeInitializeAnimation()
{
 Super::NativeInitializeAnimation();
 const TCHAR* Paths[] = {
 TEXT("/Game/Animation/Locomotion/MM_Idle.MM_Idle"),
 TEXT("/Game/Animation/Locomotion/MM_Walk_InPlace.MM_Walk_InPlace"),
 TEXT("/Game/Animation/Locomotion/MM_Run_Fwd.MM_Run_Fwd"),
 TEXT("/Game/Animation/Locomotion/MM_Jump.MM_Jump"),
 TEXT("/Game/Animation/Locomotion/MM_Fall_Loop.MM_Fall_Loop"),
 TEXT("/Game/Characters/Mannequins/Anims/Pistol/MF_Pistol_Idle_ADS.MF_Pistol_Idle_ADS"),
 TEXT("/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS.MF_Rifle_Idle_ADS"),
 TEXT("/Game/Characters/Mannequins/Anims/Rifle/MM_Rifle_Reload.MM_Rifle_Reload") };
 Clips.Reset();
 for (const TCHAR* Path : Paths) Clips.Add(LoadObject<UAnimSequence>(nullptr, Path));
}

void FNCHeroAnimProxy::PreUpdate(UAnimInstance* In, float Dt)
{
 FAnimInstanceProxy::PreUpdate(In, Dt);
 auto* Anim = Cast<UNCHeroAnimInstance>(In);
 auto* Mesh = In->GetSkelMeshComponent();
 AActor* Owner = In->GetOwningActor();
 ACharacter* C = Cast<ACharacter>(Owner);
 if (!C && Owner) C = Cast<ACharacter>(Owner->GetOwner()); // MetaHuman actor belongs to hero.
 if (!Anim || !C || !Mesh || !Mesh->GetSkeletalMeshAsset()) return;
 const auto* Outfit = C->FindComponentByClass<UNCOutfitComponent>();
 const auto* Move = UNCPlayerMove::Of(C);
 const auto* Weapon = UNCWeaponComponent::Of(C);
 const bool Swim = Move && Move->bSwimming;
 const bool Falling = C->GetCharacterMovement()->IsFalling();
 const float Speed = C->GetVelocity().Size2D();
 if (WasFalling && !Falling && !Swim) LandingTime = .22f;
 LandingTime = FMath::Max(0.f, LandingTime - Dt);
 const bool Aim = Weapon && Weapon->IsAiming() && Weapon->CurrentDef().Kind != ENCWeaponKind::Melee;
 Anim->Motion = Swim ? (Move->bUnderWater ? ENCHeroMotion::Dive : ENCHeroMotion::Swim)
 : Falling ? (C->GetVelocity().Z > 30.f ? ENCHeroMotion::Jump : ENCHeroMotion::Fall)
 : LandingTime > 0.f ? ENCHeroMotion::Land
 : Weapon && Weapon->bReloading ? ENCHeroMotion::Reload
 : Aim ? ENCHeroMotion::Aim : C->bIsCrouched ? ENCHeroMotion::Crouch
 : Speed < 20.f ? ENCHeroMotion::Idle : Speed < 600.f ? ENCHeroMotion::Walk : ENCHeroMotion::Run;
 WasFalling = Falling;
 if (Previous != Anim->Motion) StateTime = 0.f;
 StateTime += Dt;
 Time += Dt;
 UAnimSequence* Base = Anim->Clips.IsValidIndex(0) ? Anim->Clips[0].Get() : nullptr;
 if (Outfit && Outfit->Look.Idle) Base = Outfit->Look.Idle;
 if (Speed > 20.f && !Swim && !Falling)
 {
  const int32 Index = Speed > 600.f ? 2 : 1;
  Base = Anim->Clips.IsValidIndex(Index) ? Anim->Clips[Index].Get() : Base;
  if (Outfit && (Index == 2 ? Outfit->Look.Run : Outfit->Look.Walk)) Base = Index == 2 ? Outfit->Look.Run.Get() : Outfit->Look.Walk.Get();
 }
 if (Falling && !Swim) Base = Anim->Clips.IsValidIndex(4) ? Anim->Clips[Anim->Motion == ENCHeroMotion::Jump ? 3 : 4].Get() : Base;
 UAnimSequence* Upper = nullptr;
 if (!Swim && (Aim || Anim->Motion == ENCHeroMotion::Reload) && Anim->Clips.Num() >= 8)
  Upper = Anim->Clips[Anim->Motion == ENCHeroMotion::Reload ? 7 : Weapon->CurrentDef().Slot == 2 ? 5 : 6];

 const auto& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
 TArray<FTransform> Next = Ref.GetRefBonePose();
 Anim->MatchedBones = 0;
 auto Sample = [&](UAnimSequence* Clip, bool UpperOnly)
 {
  if (!Clip || !Clip->GetSkeleton()) return;
  const auto& Source = Clip->GetSkeleton()->GetReferenceSkeleton();
  const float Rate = !UpperOnly && !Falling && Speed > 20.f ? FMath::Clamp(Speed / (Speed > 600.f ? 500.f : 180.f), .7f, 2.f) : 1.f;
  const bool Loop = !Falling && Anim->Motion != ENCHeroMotion::Reload;
  const double Position = Loop ? FMath::Fmod(Time * Rate, FMath::Max(.01f, Clip->GetPlayLength())) : FMath::Min(StateTime, Clip->GetPlayLength());
  FAnimExtractContext Context(Position, false, {}, Loop);
  for (int32 i = 1; i < Next.Num(); ++i)
  {
   const FString Name = Ref.GetBoneName(i).ToString();
   if (UpperOnly && !(Name.StartsWith(TEXT("spine")) || Name.StartsWith(TEXT("clavicle")) || Name.Contains(TEXT("arm")) || Name.StartsWith(TEXT("hand")) || Name.Contains(TEXT("index")) || Name.Contains(TEXT("thumb")) || Name.Contains(TEXT("middle")) || Name.Contains(TEXT("ring")) || Name.Contains(TEXT("pinky")))) continue;
   const int32 S = Source.FindBoneIndex(Ref.GetBoneName(i));
   if (S == INDEX_NONE) continue;
   FTransform Sampled;
   Clip->GetBoneTransform(Sampled, FSkeletonPoseBoneIndex(S), Context, false);
   // Keep target bone lengths and reference orientation; never play an incompatible
   // sequence directly on a MetaHuman or resize its skeleton.
   const FQuat Delta = Source.GetRefBonePose()[S].GetRotation().Inverse() * Sampled.GetRotation();
   Next[i].SetRotation((Next[i].GetRotation() * Delta).GetNormalized());
   ++Anim->MatchedBones;
  }
 };
 Sample(Base, false);
 Sample(Upper, true);
 auto Rotate = [&](const TCHAR* Bone, const FVector& Axis, float Degrees)
 {
  const int32 i = Ref.FindBoneIndex(Bone);
  if (i != INDEX_NONE) Next[i].SetRotation((Next[i].GetRotation() * FQuat(Axis, FMath::DegreesToRadians(Degrees))).GetNormalized());
 };
 if (Swim)
 {
  Next = Ref.GetRefBonePose();
  const bool Prone = Speed > 30.f || Move->bUnderWater;
  const float Stroke = FMath::Sin(Time * (Speed > 300.f ? 6.f : 4.f));
  // Pelvis axes differ between outfits. Pitch the anatomical torso up vector
  // toward the real travel direction in component space, then into parent space.
  TArray<FTransform> ReferenceComponent = Ref.GetRefBonePose();
  for (int32 i=1;i<ReferenceComponent.Num();++i)
  {
   const int32 Parent=Ref.GetParentIndex(i);
   if (Parent!=INDEX_NONE) ReferenceComponent[i]=ReferenceComponent[i]*ReferenceComponent[Parent];
  }
  const int32 Pelvis=Ref.FindBoneIndex(TEXT("pelvis"));
  const int32 Head=Ref.FindBoneIndex(TEXT("head"));
  if (Pelvis!=INDEX_NONE && Head!=INDEX_NONE)
  {
   const FVector Up=(ReferenceComponent[Head].GetLocation()-ReferenceComponent[Pelvis].GetLocation()).GetSafeNormal();
   const FVector Travel=Speed>30.f ? C->GetVelocity().GetSafeNormal2D() : C->GetActorForwardVector();
   const FVector Forward=Mesh->GetComponentTransform().InverseTransformVectorNoScale(Travel).GetSafeNormal();
   const FQuat Pitch=FQuat::Slerp(FQuat::Identity,FQuat::FindBetweenNormals(Up,Forward),Prone?.87f:.11f);
   const int32 Parent=Ref.GetParentIndex(Pelvis);
   const FQuat ParentRotation=Parent!=INDEX_NONE ? ReferenceComponent[Parent].GetRotation() : FQuat::Identity;
   const FQuat ParentDelta=ParentRotation.Inverse()*Pitch*ParentRotation;
   Next[Pelvis].SetRotation((ParentDelta*Next[Pelvis].GetRotation()).GetNormalized());
  }
  Rotate(TEXT("upperarm_l"), FVector::YAxisVector, -45.f + Stroke * 55.f);
  Rotate(TEXT("upperarm_r"), FVector::YAxisVector, 45.f - Stroke * 55.f);
  Rotate(TEXT("lowerarm_l"), FVector::ZAxisVector, -35.f - Stroke * 20.f);
  Rotate(TEXT("lowerarm_r"), FVector::ZAxisVector, 35.f + Stroke * 20.f);
  Rotate(TEXT("thigh_l"), FVector::YAxisVector, Stroke * 22.f);
  Rotate(TEXT("thigh_r"), FVector::YAxisVector, -Stroke * 22.f);
  Rotate(TEXT("calf_l"), FVector::YAxisVector, 15.f + Stroke * 12.f);
  Rotate(TEXT("calf_r"), FVector::YAxisVector, 15.f - Stroke * 12.f);
 }
 if (Aim && !Swim) Rotate(TEXT("spine_03"), FVector::XAxisVector, FMath::Clamp(C->GetBaseAimRotation().Pitch, -60.f, 60.f) * .65f);
 if (C->bIsCrouched && !Swim && !Falling)
 {
  const int32 Pelvis = Ref.FindBoneIndex(TEXT("pelvis"));
  if (Pelvis != INDEX_NONE) Next[Pelvis].AddToTranslation(FVector(0,0,-22.f));
  Rotate(TEXT("thigh_l"), FVector::YAxisVector, -35.f);
  Rotate(TEXT("thigh_r"), FVector::YAxisVector, -35.f);
  Rotate(TEXT("calf_l"), FVector::YAxisVector, 60.f);
  Rotate(TEXT("calf_r"), FVector::YAxisVector, 60.f);
 }
 if (Anim->Motion == ENCHeroMotion::Land) Rotate(TEXT("spine_01"), FVector::XAxisVector, LandingTime * 35.f);
 if (Pose.Num() != Next.Num()) Pose = Next;
 else for (int32 i = 0; i < Next.Num(); ++i) Pose[i].Blend(Pose[i], Next[i], FMath::Clamp(Dt * 12.f, 0.f, 1.f));
 Previous = Anim->Motion;
}

bool FNCHeroAnimProxy::Evaluate(FPoseContext& Output)
{
 Output.ResetToRefPose();
 const auto& BC = Output.Pose.GetBoneContainer();
 for (FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
 {
  const int32 Index = BC.MakeMeshPoseIndex(Bone).GetInt();
  if (Pose.IsValidIndex(Index)) Output.Pose[Bone] = Pose[Index];
 }
 return true;
}
