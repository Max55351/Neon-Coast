#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "NCMenuCamera.generated.h"

/**
 * Main-menu flyover: loops a smooth Catmull-Rom flight through key shots over the map
 * (points + look-at targets + seconds per segment, filled by import_neoncoast.py from
 * Data/placements.json -> meta.menu_path). Banks gently into turns.
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCMenuCamera : public ACameraActor
{
	GENERATED_BODY()
public:
	ANCMenuCamera();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category = "NeonCoast") TArray<FVector> PathPoints;
	UPROPERTY(EditAnywhere, Category = "NeonCoast") TArray<FVector> LookPoints;
	UPROPERTY(EditAnywhere, Category = "NeonCoast") TArray<float> SegmentSeconds;
	/** Global speed multiplier of the flight. */
	UPROPERTY(EditAnywhere, Category = "NeonCoast") float Speed = 1.f;

	/** Pose on the loop at time T (seconds, wraps). */
	void Evaluate(double T, FVector& OutLoc, FVector& OutLook) const;

protected:
	double Time = 0.0;
	float Roll = 0.f;
	float LastYaw = 0.f;
	bool bHasYaw = false;
	void EnsurePath();
};
