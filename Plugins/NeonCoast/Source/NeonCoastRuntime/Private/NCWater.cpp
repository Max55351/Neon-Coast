#pragma warning(disable: 4456 4457 4458 4459)
#include "NCWater.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// amplitude cm, wavelength cm, direction deg (UE XY), phase  -- same table as WAVE_HLSL
	struct FWave { float A, L, Dir, Ph; };
	const FWave GWaves[4] = {
		{38.f, 6000.f, 200.f, 0.0f},
		{22.f, 3100.f, 235.f, 1.7f},
		{12.f, 1700.f, 168.f, 4.1f},
		{6.f, 900.f, 252.f, 2.6f},
	};
	TArray<TWeakObjectPtr<ANCWaterManager>> GRegistry;
}

ANCWaterManager::ANCWaterManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;     // after the camera moved
	bReplicates = true;
	bAlwaysRelevant = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANCWaterManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANCWaterManager, WaveScale);
}

ANCWaterManager* ANCWaterManager::Get(const UWorld* W)
{
	for (int32 i = GRegistry.Num() - 1; i >= 0; --i)
	{
		ANCWaterManager* M = GRegistry[i].Get();
		if (!M)
		{
			GRegistry.RemoveAt(i);
		}
		else if (M->GetWorld() == W)
		{
			return M;
		}
	}
	return nullptr;
}

double ANCWaterManager::WaveTime(const UWorld* W)
{
	if (!W)
	{
		return 0.0;
	}
	const AGameStateBase* GS = W->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : W->GetTimeSeconds();
}

void ANCWaterManager::BeginPlay()
{
	Super::BeginPlay();
	GRegistry.Add(this);
	// The rendered ocean is not a solid floor. All craft/character water logic
	// samples the analytic surface; seabed and piers retain their collisions.
	for (TActorIterator<AActor> It(GetWorld());It;++It)
	{
		TArray<UStaticMeshComponent*> Meshes; It->GetComponents(Meshes);
		for (auto* Mesh:Meshes) if (Mesh->GetStaticMesh())
		{
			const FString Path=Mesh->GetStaticMesh()->GetPathName();
			if (Path.StartsWith(TEXT("/Game/NeonCoast/World/SM_NC_Ocean"))) Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
	// amplitude mask
	FString Text;
	const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), DataFile);
	if (FFileHelper::LoadFileToString(Text, *Path))
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Text);
		if (FJsonSerializer::Deserialize(R, Root) && Root.IsValid())
		{
			NX = static_cast<int32>(Root->GetNumberField(TEXT("nx")));
			NY = static_cast<int32>(Root->GetNumberField(TEXT("ny")));
			const TArray<TSharedPtr<FJsonValue>>& O = Root->GetArrayField(TEXT("origin"));
			const TArray<TSharedPtr<FJsonValue>>& S = Root->GetArrayField(TEXT("size"));
			if (O.Num() == 2 && S.Num() == 2)
			{
				Origin = FVector2D(O[0]->AsNumber(), O[1]->AsNumber());
				Size = FVector2D(S[0]->AsNumber(), S[1]->AsNumber());
			}
			FBase64::Decode(Root->GetStringField(TEXT("amp")), Amp);
			if (Amp.Num() != NX * NY)
			{
				Amp.Reset();
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[NeonCoast] water: wave mask %dx%d %s"), NX, NY, Amp.Num() ? TEXT("loaded") : TEXT("missing (open sea everywhere)"));
	if (PatchMesh && GetNetMode() != NM_DedicatedServer)
	{
		Patch = NewObject<UStaticMeshComponent>(this);
		Patch->SetStaticMesh(PatchMesh);
		if (PatchMaterial)
		{
			Patch->SetMaterial(0, PatchMaterial);
		}
		Patch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Patch->SetCastShadow(false);
		Patch->SetMobility(EComponentMobility::Movable);
		Patch->SetupAttachment(RootComponent);
		Patch->SetUsingAbsoluteLocation(true);
		Patch->RegisterComponent();
		if (FlatOcean)
		{
			FlatOcean->SetActorHiddenInGame(true);
		}
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		Underwater = NewObject<UPostProcessComponent>(this);
		Underwater->bUnbound = true;
		Underwater->BlendWeight = 0.f;
		FPostProcessSettings& S = Underwater->Settings;
		S.bOverride_SceneColorTint = true;
		S.SceneColorTint = FLinearColor(0.35f, 0.8f, 0.95f);
		S.bOverride_ColorSaturation = true;
		S.ColorSaturation = FVector4(0.75f, 0.75f, 0.75f, 1.f);
		S.bOverride_VignetteIntensity = true;
		S.VignetteIntensity = 0.25f;
		S.bOverride_BloomIntensity = true;
		S.BloomIntensity = 0.35f;
		// Do not put every nearby reef/deck surface outside a 4 m shallow-focus plane.
		S.bOverride_DepthOfFieldScale = true;
		S.DepthOfFieldScale = 0.f;
		Underwater->SetupAttachment(RootComponent);
		Underwater->RegisterComponent();
	}
	OnRep_Scale();
}

void ANCWaterManager::EndPlay(const EEndPlayReason::Type Reason)
{
	GRegistry.Remove(this);
	Super::EndPlay(Reason);
}

void ANCWaterManager::SetWaveScale(float NewScale)
{
	if (HasAuthority())
	{
		WaveScale = FMath::Clamp(NewScale, 0.f, 4.f);
		OnRep_Scale();
	}
}

void ANCWaterManager::OnRep_Scale()
{
	if (Params)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, Params, TEXT("WaveScale"), WaveScale);
	}
}

void ANCWaterManager::Tick(float Dt)
{
	Super::Tick(Dt);
	if (Params)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, Params, TEXT("WaveTime"), static_cast<float>(WaveTime(GetWorld())));
	}
	if (Patch)
	{
		// the dense mesh follows the camera in 2 m steps (waves are in world space, so they stay put)
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				const FVector C = PC->PlayerCameraManager->GetCameraLocation();
				Patch->SetWorldLocation(FVector(FMath::GridSnap(C.X, 200.0), FMath::GridSnap(C.Y, 200.0), WaterZ));
			}
		}
	}
	if (Underwater)
	{
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (PC->PlayerCameraManager)
			{
				const FVector C = PC->PlayerCameraManager->GetCameraLocation();
				const float Want = C.Z < SurfaceZ(GetWorld(), C) - 10.f ? 1.f : 0.f;
				Underwater->BlendWeight = FMath::FInterpTo(Underwater->BlendWeight, Want, Dt, 8.f);
			}
		}
	}
}

float ANCWaterManager::Mask(double X, double Y) const
{
	if (Amp.Num() == 0)
	{
		return 1.f;
	}
	const double U = FMath::Clamp((X - Origin.X) / Size.X * NX - 0.5, 0.0, NX - 1.001);
	const double V = FMath::Clamp((Y - Origin.Y) / Size.Y * NY - 0.5, 0.0, NY - 1.001);
	const int32 I = FMath::FloorToInt(U), J = FMath::FloorToInt(V);
	const float FU = static_cast<float>(U - I), FV = static_cast<float>(V - J);
	auto At = [this](int32 i, int32 j) { return Amp[j * NX + i] / 255.f; };
	return FMath::Lerp(FMath::Lerp(At(I, J), At(I + 1, J), FU), FMath::Lerp(At(I, J + 1), At(I + 1, J + 1), FU), FV);
}

void ANCWaterManager::Eval(double X, double Y, double T, float& OutH, float& OutDX, float& OutDY) const
{
	OutH = OutDX = OutDY = 0.f;
	const float Tf = static_cast<float>(T);                // the material gets a float too
	for (const FWave& W : GWaves)
	{
		const float K = 2.f * PI / W.L;
		const float Om = FMath::Sqrt(981.f * K);
		const float A = FMath::DegreesToRadians(W.Dir);
		const float DX = FMath::Cos(A), DY = FMath::Sin(A);
		const float Ph = static_cast<float>(K * (DX * X + DY * Y)) - Om * Tf + W.Ph;
		OutH += W.A * FMath::Sin(Ph);
		const float C = W.A * K * FMath::Cos(Ph);
		OutDX += C * DX;
		OutDY += C * DY;
	}
	const float S = WaveScale * Mask(X, Y);
	OutH *= S;
	OutDX *= S;
	OutDY *= S;
}

float ANCWaterManager::SurfaceZ(const UWorld* W, const FVector& P)
{
	const ANCWaterManager* M = Get(W);
	if (!M)
	{
		return 0.f;
	}
	float H, DX, DY;
	M->Eval(P.X, P.Y, WaveTime(W), H, DX, DY);
	return M->WaterZ + H;
}

void ANCWaterManager::Sample(const UWorld* W, const FVector& P, float& OutZ, FVector& OutNormal)
{
	const ANCWaterManager* M = Get(W);
	if (!M)
	{
		OutZ = 0.f;
		OutNormal = FVector::UpVector;
		return;
	}
	float H, DX, DY;
	M->Eval(P.X, P.Y, WaveTime(W), H, DX, DY);
	OutZ = M->WaterZ + H;
	OutNormal = FVector(-DX, -DY, 1.f).GetSafeNormal();
}
