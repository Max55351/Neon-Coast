#pragma once

#include "CoreMinimal.h"
#include "NCTypes.generated.h"

class UStaticMesh;
class USkeletalMesh;
class UAnimSequence;
class ANCVehiclePawn;

UENUM(BlueprintType)
enum class ENCDrivetrain : uint8
{
	FWD,
	RWD,
	AWD_4x4,
	AWD_4x4_LowRange
};

/** Handling spec of one vehicle model (values come from Data/vehicles.json). */
USTRUCT(BlueprintType)
struct FNCVehicleSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle") ENCDrivetrain Drivetrain = ENCDrivetrain::FWD;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle") float MassKg = 1450.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle") float MaxTorqueNm = 320.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle") float MaxRPM = 6200.f;
	/** metres of wheel travel (split between raise and drop) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle") float SuspensionTravelM = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle") float MaxSteerDeg = 38.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle") float TopSpeedKmh = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle") float WheelRadiusM = 0.33f;
	/** true for SUVs / pickups: better grip on dirt, sand and grass */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vehicle") bool bOffroad = false;
};

/** One kind of traffic car: the static mesh used while it is just traffic and the
 *  drivable pawn class that replaces it when a player takes the car. */
USTRUCT(BlueprintType)
struct FNCTrafficType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") TObjectPtr<UStaticMesh> Mesh = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") TSubclassOf<ANCVehiclePawn> DrivableClass;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float Weight = 1.f;
	/** vehicle length in cm (spacing) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float Length = 480.f;
};

/** One pedestrian kind: animated skeletal mesh near the camera, static walk pose far away. */
USTRUCT(BlueprintType)
struct FNCPedestrianType
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") TObjectPtr<USkeletalMesh> SkeletalMesh = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") TObjectPtr<UAnimSequence> WalkAnim = nullptr;
	/** Played while fleeing (falls back to a fast WalkAnim). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") TObjectPtr<UAnimSequence> RunAnim = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crowd") TObjectPtr<UStaticMesh> FarMesh = nullptr;
};

/** Polyline in Unreal space (cm) with cumulative distances. */
struct FNCPath
{
	TArray<FVector> Points;
	TArray<float> Dist;
	float Length = 0.f;
	float Speed = 0.f;      // cm/s

	void Build();
	/** Location + unit direction at distance S along the path. */
	void Eval(float S, FVector& OutLoc, FVector& OutDir) const;
};

namespace NCData
{
	/** Blender metres (X east, Y north, Z up) -> Unreal centimetres (Y flipped). */
	inline FVector ToUE(double X, double Y, double Z) { return FVector(X * 100.0, -Y * 100.0, Z * 100.0); }

	/** Loads Data/traffic.json from <Project>/Content/<RelPath>. Returns false if missing. */
	NEONCOASTRUNTIME_API bool LoadPaths(const FString& RelPath, const FString& Key, TArray<FNCPath>& Out, float DefaultSpeed);
}
