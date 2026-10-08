#pragma warning(disable: 4456 4457 4458 4459)
#include "NCWheels.h"

static void NCWheelDefaults(UChaosVehicleWheel* W)
{
	W->WheelRadius = 34.f;
	W->WheelWidth = 24.f;
	W->FrictionForceMultiplier = 3.0f;
	W->CorneringStiffness = 1000.f;
	W->SideSlipModifier = 1.f;
	W->SlipThreshold = 20.f;
	W->SkidThreshold = 20.f;
	W->SuspensionMaxRaise = 8.f;
	W->SuspensionMaxDrop = 10.f;
	W->SuspensionDampingRatio = 0.5f;
	W->SpringRate = 250.f;
	W->SpringPreload = 50.f;
	W->SuspensionSmoothing = 6;
	W->MaxBrakeTorque = 4500.f;
	W->SweepShape = ESweepShape::Raycast;
	W->SweepType = ESweepType::ComplexSweep;
}

UNCWheelFront::UNCWheelFront()
{
	NCWheelDefaults(this);
	AxleType = EAxleType::Front;
	bAffectedBySteering = true;
	MaxSteerAngle = 38.f;
	bAffectedByHandbrake = false;
	bAffectedByEngine = true;
}

UNCWheelRear::UNCWheelRear()
{
	NCWheelDefaults(this);
	AxleType = EAxleType::Rear;
	bAffectedBySteering = false;
	MaxSteerAngle = 0.f;
	bAffectedByHandbrake = true;
	MaxHandBrakeTorque = 6000.f;
	bAffectedByEngine = true;
}
