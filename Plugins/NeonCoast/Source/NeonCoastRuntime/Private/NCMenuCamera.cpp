#pragma warning(disable: 4456 4457 4458 4459)
#include "NCMenuCamera.h"

#include "Camera/CameraComponent.h"

ANCMenuCamera::ANCMenuCamera()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	GetCameraComponent()->SetFieldOfView(68.f);
	GetCameraComponent()->bConstrainAspectRatio = false;
	SetReplicates(false);
}

void ANCMenuCamera::EnsurePath()
{
	if (PathPoints.Num() >= 4 && LookPoints.Num() == PathPoints.Num())
	{
		while (SegmentSeconds.Num() < PathPoints.Num())
		{
			SegmentSeconds.Add(10.f);
		}
		return;
	}
	// fallback: slow orbit around the map centre
	PathPoints.Reset();
	LookPoints.Reset();
	SegmentSeconds.Reset();
	for (int32 i = 0; i < 8; ++i)
	{
		const float A = 2.f * PI * i / 8.f;
		PathPoints.Add(FVector(FMath::Cos(A) * 120000.f, FMath::Sin(A) * 70000.f, 30000.f));
		LookPoints.Add(FVector(0.f, 0.f, 2000.f));
		SegmentSeconds.Add(12.f);
	}
}

void ANCMenuCamera::BeginPlay()
{
	Super::BeginPlay();
	EnsurePath();
	FVector L, T;
	Evaluate(0.0, L, T);
	SetActorLocationAndRotation(L, (T - L).Rotation());
}

static FVector CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float T)
{
	const float T2 = T * T, T3 = T2 * T;
	return 0.5f * ((2.f * P1) + (-P0 + P2) * T + (2.f * P0 - 5.f * P1 + 4.f * P2 - P3) * T2 + (-P0 + 3.f * P1 - 3.f * P2 + P3) * T3);
}

void ANCMenuCamera::Evaluate(double T, FVector& OutLoc, FVector& OutLook) const
{
	const int32 N = PathPoints.Num();
	double Total = 0.0;
	for (int32 i = 0; i < N; ++i)
	{
		Total += FMath::Max(SegmentSeconds.IsValidIndex(i) ? SegmentSeconds[i] : 10.f, 0.5f);
	}
	T = FMath::Fmod(T, Total);
	if (T < 0.0)
	{
		T += Total;
	}
	int32 i = 0;
	for (; i < N; ++i)
	{
		const double D = FMath::Max(SegmentSeconds.IsValidIndex(i) ? SegmentSeconds[i] : 10.f, 0.5f);
		if (T < D)
		{
			break;
		}
		T -= D;
	}
	i = FMath::Clamp(i, 0, N - 1);
	const double D = FMath::Max(SegmentSeconds.IsValidIndex(i) ? SegmentSeconds[i] : 10.f, 0.5f);
	const float U = static_cast<float>(T / D);
	auto At = [N](const TArray<FVector>& A, int32 k) { return A[((k % N) + N) % N]; };
	OutLoc = CatmullRom(At(PathPoints, i - 1), At(PathPoints, i), At(PathPoints, i + 1), At(PathPoints, i + 2), U);
	// the look-at target eases between keys (smoothstep) so the camera settles on each subject
	const float S = U * U * (3.f - 2.f * U);
	OutLook = CatmullRom(At(LookPoints, i - 1), At(LookPoints, i), At(LookPoints, i + 1), At(LookPoints, i + 2), S);
}

void ANCMenuCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (PathPoints.Num() < 4)
	{
		return;
	}
	Time += DeltaSeconds * Speed;
	FVector L, T;
	Evaluate(Time, L, T);
	FRotator R = (T - L).Rotation();
	// bank into turns: roll follows the yaw rate, heavily smoothed
	if (bHasYaw && DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		const float YawRate = FMath::FindDeltaAngleDegrees(LastYaw, R.Yaw) / DeltaSeconds;
		Roll = FMath::FInterpTo(Roll, FMath::Clamp(-YawRate * 0.35f, -7.f, 7.f), DeltaSeconds, 1.2f);
	}
	LastYaw = R.Yaw;
	bHasYaw = true;
	R.Roll = Roll;
	SetActorLocationAndRotation(L, R);
}
