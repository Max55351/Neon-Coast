#pragma warning(disable: 4456 4457 4458 4459)
#include "NCVehicleAnimInstance.h"

#include "Animation/AnimNodeBase.h"

#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "WheeledVehiclePawn.h"
#include "NCVehiclePawn.h"

void FNCVehicleAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	Wheels.Reset();
	AWheeledVehiclePawn* Pawn = Cast<AWheeledVehiclePawn>(InAnimInstance->GetOwningActor());
	UChaosWheeledVehicleMovementComponent* Move = Pawn ? Cast<UChaosWheeledVehicleMovementComponent>(Pawn->GetVehicleMovementComponent()) : nullptr;
	if (!Move)
	{
		return;
	}
	// doors / lids: smooth towards the replicated open state
	if (const ANCVehiclePawn* NC = Cast<ANCVehiclePawn>(Pawn))
	{
		for (int32 p = 0; p < 6; ++p)
		{
			const float Target = (NC->Body.OpenMask & (1 << p)) ? 1.f : 0.f;
			OpenFrac[p] = FMath::FInterpTo(OpenFrac[p], Target, DeltaSeconds, 5.f);
		}
		SteerWheelDeg = 0.f;
		if (Move->Wheels.Num() > 0 && Move->Wheels[0])
		{
			SteerWheelDeg = Move->Wheels[0]->GetSteerAngle() * 11.f;
		}
		bHasBody = true;
	}
	for (int32 i = 0; i < Move->Wheels.Num() && i < Move->WheelSetups.Num(); ++i)
	{
		if (UChaosVehicleWheel* W = Move->Wheels[i])
		{
			FWheel D;
			D.Bone = Move->WheelSetups[i].BoneName;
			D.SpinDeg = W->GetRotationAngle();
			D.SteerDeg = W->GetSteerAngle();
			D.SuspensionCm = W->GetSuspensionOffset();
			Wheels.Add(D);
		}
	}
}

bool FNCVehicleAnimProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();
	const FBoneContainer& BC = Output.Pose.GetBoneContainer();
	const FReferenceSkeleton& Ref = BC.GetReferenceSkeleton();
	for (const FWheel& W : Wheels)
	{
		const int32 MeshIndex = Ref.FindBoneIndex(W.Bone);
		if (MeshIndex == INDEX_NONE)
		{
			continue;
		}
		const FCompactPoseBoneIndex CI = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (!CI.IsValid())
		{
			continue;
		}
		// component-space reference of the parent (usually the root bone)
		FTransform ParentCS = FTransform::Identity;
		for (int32 P = Ref.GetParentIndex(MeshIndex); P != INDEX_NONE; P = Ref.GetParentIndex(P))
		{
			ParentCS = ParentCS * Ref.GetRefBonePose()[P];
		}
		FTransform& Local = Output.Pose[CI];
		FTransform CS = Local * ParentCS;
		const FQuat Offset = FRotator(W.SpinDeg, W.SteerDeg, 0.f).Quaternion();
		CS.SetRotation(Offset * CS.GetRotation());
		CS.AddToTranslation(FVector(0.f, 0.f, W.SuspensionCm));
		Local = CS.GetRelativeTransform(ParentCS);
	}
	if (!bHasBody)
	{
		return true;
	}
	// doors swing about the vertical axis at the hinge, lids about the lateral axis
	static const float Angles[6] = {65.f, -65.f, 65.f, -65.f, 42.f, -60.f};
	for (int32 p = 0; p < 6; ++p)
	{
		if (OpenFrac[p] < 0.001f)
		{
			continue;
		}
		const int32 MeshIndex = Ref.FindBoneIndex(ANCVehiclePawn::PartBones[p]);
		if (MeshIndex == INDEX_NONE)
		{
			continue;
		}
		const FCompactPoseBoneIndex CI = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (!CI.IsValid())
		{
			continue;
		}
		FTransform ParentCS = FTransform::Identity;
		for (int32 P = Ref.GetParentIndex(MeshIndex); P != INDEX_NONE; P = Ref.GetParentIndex(P))
		{
			ParentCS = ParentCS * Ref.GetRefBonePose()[P];
		}
		FTransform& Local = Output.Pose[CI];
		FTransform CS = Local * ParentCS;
		const float A = Angles[p] * OpenFrac[p];
		const FQuat Q = p < 4 ? FRotator(0.f, A, 0.f).Quaternion() : FRotator(A, 0.f, 0.f).Quaternion();
		CS.SetRotation(Q * CS.GetRotation());
		Local = CS.GetRelativeTransform(ParentCS);
	}
	const int32 SW = Ref.FindBoneIndex(TEXT("steering_wheel"));
	if (SW != INDEX_NONE)
	{
		const FCompactPoseBoneIndex CI = BC.MakeCompactPoseIndex(FMeshPoseBoneIndex(SW));
		if (CI.IsValid())
		{
			FTransform& Local = Output.Pose[CI];
			Local.SetRotation(Local.GetRotation() * FQuat(FVector::XAxisVector, FMath::DegreesToRadians(-SteerWheelDeg)));
		}
	}
	return true;
}
