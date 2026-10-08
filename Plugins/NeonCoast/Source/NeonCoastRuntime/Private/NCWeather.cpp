#pragma warning(disable: 4456 4457 4458 4459)
#include "NCWeather.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Math/RotationMatrix.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "NCCraft.h"
#include "NCHUD.h"
#include "NCSound.h"
#include "NCWater.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	TArray<TWeakObjectPtr<ANCWeatherManager>> GWeather;

	struct FLook
	{
		float Sun, Fog, Rain, Wind, Waves;
		const TCHAR* Name;
	};
	const FLook& LookOf(ENCWeather W)
	{
		static const FLook L[5] = {
			{1.00f, 1.0f, 0.0f, 0.4f, 1.0f, TEXT("Jasno")},
			{0.45f, 1.6f, 0.0f, 0.8f, 1.3f, TEXT("Zatazeno")},
			{0.25f, 2.5f, 0.6f, 1.0f, 1.6f, TEXT("Dest")},
			{0.10f, 3.0f, 1.0f, 1.9f, 2.6f, TEXT("Bourka")},
			{0.40f, 12.0f, 0.0f, 0.2f, 0.6f, TEXT("Mlha")},
		};
		return L[FMath::Clamp(static_cast<int32>(W), 0, 4)];
	}
}

ANCWeatherManager::ANCWeatherManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANCWeatherManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANCWeatherManager, Weather);
}

ANCWeatherManager* ANCWeatherManager::Get(const UWorld* W)
{
	for (int32 i = GWeather.Num() - 1; i >= 0; --i)
	{
		ANCWeatherManager* M = GWeather[i].Get();
		if (!M)
		{
			GWeather.RemoveAt(i);
		}
		else if (M->GetWorld() == W)
		{
			return M;
		}
	}
	return nullptr;
}

void ANCWeatherManager::BeginPlay()
{
	Super::BeginPlay();
	GWeather.AddUnique(this);
	NextChange = FMath::FRandRange(MinDuration, MaxDuration);
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// rain: thin streaks in a 25 m cylinder around the camera
	Drops = NewObject<UInstancedStaticMeshComponent>(this);
	Drops->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	if (RainMaterial)
	{
		Drops->SetMaterial(0, RainMaterial);
	}
	Drops->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Drops->SetCastShadow(false);
	Drops->SetMobility(EComponentMobility::Movable);
	Drops->SetupAttachment(RootComponent);
	Drops->RegisterComponent();
	for (int32 i = 0; i < RainDrops; ++i)
	{
		DropPos.Add(FVector(FMath::FRandRange(-2500.f, 2500.f), FMath::FRandRange(-2500.f, 2500.f), FMath::FRandRange(-600.f, 1400.f)));
		Drops->AddInstance(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector), true);
	}
	RainSound = ANCSoundBank::Loop(RootComponent, TEXT("Rain"), 0.f);
	WindSound = ANCSoundBank::Loop(RootComponent, TEXT("Wind"), 0.f);
	SeaSound = ANCSoundBank::Loop(RootComponent, TEXT("Waves"), 0.f);
	CitySound = ANCSoundBank::Loop(RootComponent, TEXT("City"), 0.f);
	for (UAudioComponent* A : {RainSound.Get(), WindSound.Get(), SeaSound.Get(), CitySound.Get()})
	{
		if (A)
		{
			A->bAllowSpatialization = false;                           // ambience: everywhere around you
		}
	}
}

void ANCWeatherManager::EndPlay(const EEndPlayReason::Type Reason)
{
	GWeather.Remove(this);
	Super::EndPlay(Reason);
}

void ANCWeatherManager::SetWeather(ENCWeather NewWeather)
{
	if (!HasAuthority())
	{
		return;
	}
	Weather = NewWeather;
	NextChange = FMath::FRandRange(MinDuration, MaxDuration);
	OnRep_Weather();
}

void ANCWeatherManager::OnRep_Weather()
{
	// tell the local player
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (ANCHUD* H = Cast<ANCHUD>(PC->GetHUD()))
		{
			H->ShowMessage(FString::Printf(TEXT("Pocasi: %s"), LookOf(Weather).Name), 4.f);
		}
	}
}

void ANCWeatherManager::Tick(float Dt)
{
	Super::Tick(Dt);
	const FLook& L = LookOf(Weather);
	if (HasAuthority())
	{
		if (bAutoChange)
		{
			NextChange -= Dt;
			if (NextChange <= 0.f)
			{
				// mostly fine weather, sometimes rain, rarely a storm or fog
				const float R = FMath::FRand();
				SetWeather(R < 0.4f ? ENCWeather::Clear : (R < 0.62f ? ENCWeather::Cloudy : (R < 0.82f ? ENCWeather::Rain
					: (R < 0.92f ? ENCWeather::Storm : ENCWeather::Fog))));
			}
		}
		// the sea and the wind follow the weather
		if (ANCWaterManager* WM = ANCWaterManager::Get(GetWorld()))
		{
			if (!FMath::IsNearlyEqual(WM->WaveScale, L.Waves, 0.01f))
			{
				WM->SetWaveScale(FMath::FInterpConstantTo(WM->WaveScale, L.Waves, Dt, 0.05f));
			}
		}
		if (ANCCraftManager* CM = ANCCraftManager::Get(GetWorld()))
		{
			CM->WindStrength = FMath::FInterpConstantTo(CM->WindStrength, L.Wind, Dt, 0.05f);
		}
		if (Weather == ENCWeather::Storm)
		{
			NextStrike -= Dt;
			if (NextStrike <= 0.f)
			{
				NextStrike = FMath::FRandRange(6.f, 18.f);
				const FVector2D Off = FVector2D(FMath::VRand()).GetSafeNormal() * FMath::FRandRange(2000.f, 60000.f);
				MulticastLightning(FVector(Off.X, Off.Y, 0.f), FMath::Clamp(Off.Size() / 34300.f, 0.2f, 3.f));
			}
		}
	}
	// every machine: blend towards the look (a front takes ~20 s to roll in)
	Sun = FMath::FInterpTo(Sun, L.Sun, Dt, 0.15f);
	Fog = FMath::FInterpTo(Fog, L.Fog, Dt, 0.15f);
	Rain = FMath::FInterpTo(Rain, L.Rain, Dt, 0.2f);
	Wind = FMath::FInterpTo(Wind, L.Wind, Dt, 0.2f);
	Flash = FMath::Max(0.f, Flash - Dt * 6.f);
	if (GetNetMode() != NM_DedicatedServer)
	{
		ApplyLighting(Dt);
		UpdateRain(Dt);
		UpdateAmbience(Dt);
	}
}

void ANCWeatherManager::ApplyLighting(float Dt)
{
	// scale whatever the time of day set (it may change the base at any moment: detect and rebase)
	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		ULightComponent* C = It->GetLightComponent();
		if (!C)
		{
			continue;
		}
		if (LastLux < 0.f || !FMath::IsNearlyEqual(C->Intensity, LastLux, 0.001f))
		{
			BaseLux = C->Intensity;                                     // time of day changed it: new base
		}
		LastLux = BaseLux * Sun + Flash * 25.f;
		C->SetIntensity(LastLux);
		break;
	}
	for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
	{
		USkyLightComponent* C = It->GetLightComponent();
		if (!C)
		{
			continue;
		}
		if (LastSky < 0.f || !FMath::IsNearlyEqual(C->Intensity, LastSky, 0.001f))
		{
			BaseSky = C->Intensity;
		}
		LastSky = BaseSky * (0.45f + 0.55f * Sun) + Flash * 2.f;
		C->SetIntensity(LastSky);
		break;
	}
	for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
	{
		UExponentialHeightFogComponent* C = It->GetComponent();
		if (!C)
		{
			continue;
		}
		if (LastFog < 0.f || !FMath::IsNearlyEqual(C->FogDensity, LastFog, 0.00001f))
		{
			BaseFog = C->FogDensity;
		}
		LastFog = BaseFog * Fog;
		C->SetFogDensity(LastFog);
		break;
	}
}

void ANCWeatherManager::UpdateRain(float Dt)
{
	if (!Drops)
	{
		return;
	}
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	const int32 Active = FMath::RoundToInt(DropPos.Num() * Rain);
	const FVector Fall = FVector(250.f * Wind, 120.f * Wind, -1100.f);                // wind slants the rain
	const FRotator Slant = FRotationMatrix::MakeFromZ(-Fall).Rotator();
	const bool bUnderWater = Cam.Z < ANCWaterManager::SurfaceZ(GetWorld(), Cam);
	for (int32 i = 0; i < DropPos.Num(); ++i)
	{
		FVector& P = DropPos[i];
		P += Fall * Dt;
		if (P.Z < -600.f)
		{
			P = FVector(FMath::FRandRange(-2500.f, 2500.f), FMath::FRandRange(-2500.f, 2500.f), 1400.f);
		}
		const bool bOn = i < Active && !bUnderWater;
		// relative to the camera: the cylinder travels with you
		Drops->UpdateInstanceTransform(i, FTransform(Slant, Cam + P, bOn ? FVector(0.006f, 0.006f, 0.45f) : FVector::ZeroVector),
			true, false, true);
	}
	Drops->MarkRenderStateDirty();
}

void ANCWeatherManager::UpdateAmbience(float Dt)
{
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector Cam = PC->PlayerCameraManager->GetCameraLocation();
	const float Water = ANCWaterManager::SurfaceZ(GetWorld(), Cam);
	const float High = FMath::Clamp((Cam.Z - Water) / 8000.f, 0.f, 1.f);
	// how much sea is around: sample a ring of points 60 m out
	int32 Sea = 0;
	FCollisionObjectQueryParams O;
	O.AddObjectTypesToQuery(ECC_WorldStatic);
	for (int32 k = 0; k < 6; ++k)
	{
		const FVector P = Cam + FRotator(0.f, k * 60.f, 0.f).Vector() * 6000.f;
		FHitResult H;
		if (!GetWorld()->LineTraceSingleByObjectType(H, FVector(P.X, P.Y, Cam.Z + 2000.f), FVector(P.X, P.Y, Water - 50.f), O))
		{
			++Sea;
		}
	}
	const float SeaK = Sea / 6.f;
	auto Blend = [Dt](UAudioComponent* A, float V) {
		if (A)
		{
			A->SetVolumeMultiplier(FMath::FInterpTo(A->VolumeMultiplier, V, Dt, 1.5f));
		}
	};
	Blend(RainSound, Rain * 0.9f);
	Blend(WindSound, FMath::Clamp(Wind * 0.35f + High * 0.5f, 0.f, 1.f));
	Blend(SeaSound, SeaK * (1.f - High) * (0.5f + 0.25f * Wind));
	Blend(CitySound, (1.f - SeaK) * (1.f - High) * 0.35f);
}

void ANCWeatherManager::MulticastLightning_Implementation(FVector_NetQuantize Where, float ThunderDelay)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	Flash = 1.f;
	const float Loud = FMath::Clamp(1.2f - ThunderDelay * 0.3f, 0.3f, 1.2f);
	GetWorldTimerManager().SetTimer(ThunderTimer, [this, Loud]() {
		ANCSoundBank::Play2D(this, TEXT("Thunder"), Loud, FMath::FRandRange(0.8f, 1.1f));
	}, ThunderDelay, false);
}
