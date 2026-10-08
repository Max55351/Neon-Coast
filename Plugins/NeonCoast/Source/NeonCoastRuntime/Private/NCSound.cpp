#pragma warning(disable: 4456 4457 4458 4459)
#include "NCSound.h"

#include "Components/AudioComponent.h"
#include "Components/DecalComponent.h"
#include "Components/LightComponent.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Sound/SoundBase.h"

namespace
{
	TArray<TWeakObjectPtr<ANCSoundBank>> GBanks;
}

ANCSoundBank::ANCSoundBank()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANCSoundBank::BeginPlay()
{
	Super::BeginPlay();
	GBanks.AddUnique(this);
	if (GetNetMode() != NM_DedicatedServer)
	{
		for (const FVector& P : ClubSpots)
		{
			if (USoundBase* S = Find(this, TEXT("Music_Club")))
			{
				UGameplayStatics::SpawnSoundAtLocation(this, S, P, FRotator::ZeroRotator, 1.f, 1.f, 0.f, nullptr, nullptr, false);
			}
		}
	}
}

void ANCSoundBank::Tick(float Dt)
{
	Super::Tick(Dt);
	if (ClubLights.Num() == 0 || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// 128 BPM: every beat the club lights jump to the next neon colour and flash
	const double T = GetWorld()->GetTimeSeconds();
	const double Beat = 60.0 / 128.0;
	const int32 B = static_cast<int32>(T / Beat);
	const float Ph = static_cast<float>(FMath::Fmod(T, Beat) / Beat);
	static const FLinearColor Cols[4] = {FLinearColor(1.f, 0.1f, 0.6f), FLinearColor(0.1f, 0.6f, 1.f), FLinearColor(0.6f, 0.2f, 1.f),
		FLinearColor(0.2f, 1.f, 0.5f)};
	for (int32 i = 0; i < ClubLights.Num(); ++i)
	{
		if (APointLight* L = ClubLights[i])
		{
			L->GetLightComponent()->SetLightColor(Cols[(B + i) % 4]);
			L->GetLightComponent()->SetIntensity(80.f * (0.35f + 0.65f * FMath::Exp(-Ph * 5.f)));
		}
	}
}

void ANCSoundBank::EndPlay(const EEndPlayReason::Type Reason)
{
	GBanks.Remove(this);
	Super::EndPlay(Reason);
}

ANCSoundBank* ANCSoundBank::Get(const UObject* Ctx)
{
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	if (!W)
	{
		return nullptr;
	}
	for (int32 i = GBanks.Num() - 1; i >= 0; --i)
	{
		ANCSoundBank* B = GBanks[i].Get();
		if (!B)
		{
			GBanks.RemoveAt(i);
		}
		else if (B->GetWorld() == W)
		{
			return B;
		}
	}
	return nullptr;
}

USoundBase* ANCSoundBank::Find(const UObject* Ctx, FName Id)
{
	const ANCSoundBank* B = Get(Ctx);
	const TObjectPtr<USoundBase>* S = B ? B->Sounds.Find(Id) : nullptr;
	return S ? S->Get() : nullptr;
}

void ANCSoundBank::Play(const UObject* Ctx, FName Id, const FVector& Where, float Volume, float Pitch)
{
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	if (!W || W->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const ANCSoundBank* B = Get(Ctx);
	if (USoundBase* S = Find(Ctx, Id))
	{
		UGameplayStatics::PlaySoundAtLocation(Ctx, S, Where, Volume * (B ? B->MasterVolume : 1.f), Pitch);
	}
}

void ANCSoundBank::Play2D(const UObject* Ctx, FName Id, float Volume, float Pitch)
{
	const UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	if (!W || W->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (USoundBase* S = Find(Ctx, Id))
	{
		UGameplayStatics::PlaySound2D(Ctx, S, Volume, Pitch);
	}
}

UAudioComponent* ANCSoundBank::Loop(USceneComponent* Parent, FName Id, float Volume)
{
	if (!Parent || !Parent->GetWorld() || Parent->GetWorld()->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	USoundBase* S = Find(Parent, Id);
	if (!S)
	{
		return nullptr;
	}
	UAudioComponent* A = UGameplayStatics::SpawnSoundAttached(S, Parent, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
		false, Volume, 1.f, 0.f, nullptr, nullptr, false);
	return A;
}

void ANCSoundBank::Explosion(UWorld* W, const FVector& Where, float Radius)
{
	if (!W)
	{
		return;
	}
	if (ANCSoundBank* B = Get(W))
	{
		B->MulticastExplosion(Where, Radius);
	}
	else if (W->GetNetMode() != NM_DedicatedServer)
	{
		if (UParticleSystem* FX = LoadObject<UParticleSystem>(nullptr, TEXT("/Game/StarterContent/Particles/P_Explosion.P_Explosion")))
		{
			UGameplayStatics::SpawnEmitterAtLocation(W, FX, Where, FRotator::ZeroRotator, FVector(Radius / 500.f));
		}
	}
}

void ANCSoundBank::MulticastExplosion_Implementation(FVector_NetQuantize Where, float Radius)
{
	UWorld* W = GetWorld();
	if (!W || W->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (UParticleSystem* FX = LoadObject<UParticleSystem>(nullptr, TEXT("/Game/StarterContent/Particles/P_Explosion.P_Explosion")))
	{
		UGameplayStatics::SpawnEmitterAtLocation(W, FX, Where, FRotator::ZeroRotator, FVector(Radius / 500.f));
	}
	Play(this, TEXT("Explosion"), Where, 1.f, FMath::FRandRange(0.85f, 1.1f));
	// crater: if the blast was close to the ground, scorch it (stays for 3 minutes)
	FHitResult H;
	FCollisionObjectQueryParams O;
	O.AddObjectTypesToQuery(ECC_WorldStatic);
	if (CraterDecal && W->LineTraceSingleByObjectType(H, Where + FVector(0, 0, 100.f), Where - FVector(0, 0, 400.f), O))
	{
		const float Size = FMath::Clamp(Radius * 0.45f, 120.f, 600.f);
		UGameplayStatics::SpawnDecalAtLocation(W, CraterDecal, FVector(120.f, Size, Size), H.ImpactPoint,
			(-H.ImpactNormal).Rotation() + FRotator(0.f, 0.f, FMath::FRandRange(0.f, 360.f)), 180.f);
	}
}

void ANCSoundBank::Track(UWorld* W, const FVector& Where, const FRotator& Rot, float Width)
{
	const ANCSoundBank* B = Get(W);
	if (!B || !B->TrackDecal || W->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	UGameplayStatics::SpawnDecalAtLocation(W, B->TrackDecal, FVector(60.f, 45.f, Width * 0.5f), Where,
		FRotator(-90.f, Rot.Yaw, 0.f), 60.f);
}
