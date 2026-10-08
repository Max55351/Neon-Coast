#pragma once

#include "CoreMinimal.h"
#include "ChaosVehicleWheel.h"
#include "NCWheels.generated.h"

/** Steered front wheel. Radius / suspension / friction are overridden per vehicle at runtime. */
UCLASS()
class NEONCOASTRUNTIME_API UNCWheelFront : public UChaosVehicleWheel
{
	GENERATED_BODY()
public:
	UNCWheelFront();
};

/** Rear wheel with handbrake. */
UCLASS()
class NEONCOASTRUNTIME_API UNCWheelRear : public UChaosVehicleWheel
{
	GENERATED_BODY()
public:
	UNCWheelRear();
};
