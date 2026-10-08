#include "NCCabrioletPawn.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "ChaosVehicleWheel.h"
#include "Net/UnrealNetwork.h"

UStaticMeshComponent* ANCCabrioletPawn::MakePart(const TCHAR* Name, UStaticMesh* Shape, const FVector& Location, const FVector& Size, const TCHAR* Material)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(GetMesh());
	Part->SetStaticMesh(Shape);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeScale3D(Size / 100.f);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); // authoritative Chaos chassis supplies collision
	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, Material)) Part->SetMaterial(0, Mat);
	return Part;
}

ANCCabrioletPawn::ANCCabrioletPawn(const FObjectInitializer& Initializer) : Super(Initializer)
{
	VehicleMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/NeonCoast/Vehicles/SK_NC2_Car_Sport_D.SK_NC2_Car_Sport_D"));
	Spec.Drivetrain = ENCDrivetrain::RWD;
	Spec.MassKg = 1400.f;
	Spec.MaxTorqueNm = 420.f;
	Spec.TopSpeedKmh = 220.f;
	Spec.WheelRadiusM = 0.34f;
	Spec.SuspensionTravelM = 0.16f;
	Seats = {FVector(-20.f, -40.f, 65.f), FVector(-20.f, 40.f, 65.f)};
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	const TCHAR* Paint = TEXT("/Game/NeonCoast/Materials/Instances/MI_NC2_Car_Cyan.MI_NC2_Car_Cyan");
	const TCHAR* Rubber = TEXT("/Game/NeonCoast/Materials/Instances/MI_NC2_Rubber.MI_NC2_Rubber");
	const TCHAR* Cloth = TEXT("/Game/NeonCoast/Materials/Instances/MI_NC2_Cloth_Black.MI_NC2_Cloth_Black");
	MakePart(TEXT("LowerBody"), Cube, FVector(0,0,53), FVector(430,184,38), Paint);
	MakePart(TEXT("Bonnet"), Cube, FVector(158,0,80), FVector(115,184,18), Paint);
	MakePart(TEXT("RearDeck"), Cube, FVector(-164,0,88), FVector(100,184,18), Paint);
	MakePart(TEXT("DoorLeft"), Cube, FVector(-12,-87,91), FVector(190,12,48), Paint);
	MakePart(TEXT("DoorRight"), Cube, FVector(-12,87,91), FVector(190,12,48), Paint);
	MakePart(TEXT("SeatLeft"), Cube, FVector(-20,-40,80), FVector(68,55,12), Cloth);
	MakePart(TEXT("SeatRight"), Cube, FVector(-20,40,80), FVector(68,55,12), Cloth);
	MakePart(TEXT("SeatBackLeft"), Cube, FVector(-52,-40,107), FVector(12,55,55), Cloth);
	MakePart(TEXT("SeatBackRight"), Cube, FVector(-52,40,107), FVector(12,55,55), Cloth);
	UStaticMeshComponent* Glass = MakePart(TEXT("Windscreen"), Cube, FVector(81,0,125), FVector(5,158,62), TEXT("/Game/NeonCoast/Materials/Instances/MI_NC2_Glass_CarClear.MI_NC2_Glass_CarClear"));
	Glass->SetRelativeRotation(FRotator(-18,0,0));
	for (int32 Side = 0; Side < 2; ++Side)
	{
		MakePart(Side == 0 ? TEXT("FrontLightLeft") : TEXT("FrontLightRight"), Cube, FVector(217, Side == 0 ? -64 : 64, 71), FVector(6,32,12), TEXT("/Game/NeonCoast/Materials/Instances/MI_NC2_Light_Head.MI_NC2_Light_Head"));
		MakePart(Side == 0 ? TEXT("RearLightLeft") : TEXT("RearLightRight"), Cube, FVector(-217, Side == 0 ? -64 : 64, 76), FVector(6,32,12), TEXT("/Game/NeonCoast/Materials/Instances/MI_NC2_Light_Tail.MI_NC2_Light_Tail"));
	}
	const FVector WheelPositions[] = {FVector(138,-85,34), FVector(138,85,34), FVector(-130,-85,34), FVector(-130,85,34)};
	const TCHAR* WheelNames[] = {TEXT("VisualFL"),TEXT("VisualFR"),TEXT("VisualRL"),TEXT("VisualRR")};
	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMeshComponent* Wheel = MakePart(WheelNames[i], Cylinder, WheelPositions[i], FVector(68,68,24), Rubber);
		Wheel->SetRelativeRotation(FRotator(0,0,90));
		VisualWheels.Add(Wheel);
	}
	Roof = MakePart(TEXT("RetractableRoof"), Cube, FVector(-15,0,160), FVector(190,180,10), Cloth);
	UpdateRoof();
}

void ANCCabrioletPawn::BeginPlay()
{
	Super::BeginPlay();
	// Keep the validated skeletal chassis for physics, but render an actually open
	// cabin instead of the source sports car's inseparable, permanently closed roof.
	GetMesh()->SetVisibility(false, false);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	RoofFraction = bRoofOpen ? 1.f : 0.f;
	UpdateRoof();
}

void ANCCabrioletPawn::UpdateRoof()
{
	if (!Roof) return;
	const float Blend = FMath::InterpEaseInOut(0.f, 1.f, RoofFraction, 2.f);
	Roof->SetRelativeLocation(FMath::Lerp(FVector(-15,0,160), FVector(-150,0,110), Blend));
	Roof->SetRelativeRotation(FRotator(110.f * Blend,0,0));
	Roof->SetRelativeScale3D(FMath::Lerp(FVector(1.9f,1.8f,0.1f), FVector(0.3f,1.65f,0.08f), Blend));
}

void ANCCabrioletPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	RoofFraction = FMath::FInterpConstantTo(RoofFraction, bRoofOpen ? 1.f : 0.f, DeltaTime, 0.25f);
	UpdateRoof();
	if (auto* Movement = Move())
	{
		for (int32 i = 0; i < VisualWheels.Num() && i < Movement->Wheels.Num(); ++i)
		{
			const UChaosVehicleWheel* Wheel = Movement->Wheels[i];
			if (!Wheel) continue;
			const FQuat Rotation = FRotator(0,Wheel->GetSteerAngle(),0).Quaternion() * FQuat(FVector::YAxisVector,FMath::DegreesToRadians(Wheel->GetRotationAngle())) * FRotator(0,0,90).Quaternion();
			VisualWheels[i]->SetRelativeRotation(Rotation);
		}
	}
}

void ANCCabrioletPawn::OnReset(const FInputActionValue& Value)
{
	if (FMath::Abs(GetActorRotation().Roll) > 45.f) Super::OnReset(Value);
	else ServerToggleRoof();
}

void ANCCabrioletPawn::ServerToggleRoof_Implementation()
{
	if (!Driver || Body.bWrecked || GetSpeedKmh() > 10.f) return;
	bRoofOpen = !bRoofOpen;
}

void ANCCabrioletPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANCCabrioletPawn, bRoofOpen);
}
