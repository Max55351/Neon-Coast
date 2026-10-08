#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCWater.generated.h"

class UMaterialInterface;
class UMaterialParameterCollection;
class UStaticMesh;
class UStaticMeshComponent;
class UPostProcessComponent;

/**
 * The sea: one wave function used by the ocean material (world position offset) and by every
 * boat, so hulls ride exactly the waves you see. Time is the server clock, so all players see
 * the same swell.
 *
 * Height(x, y, t) = WaterZ + Scale * Mask(x, y) * sum_i A_i sin(k_i (D_i . p) - w_i t + phi_i)
 *   4 deep-water waves (w = sqrt(g k)); Mask = open sea 1, bays less, harbours and the wetland
 *   almost flat (Data/water.json, same grid as the T_NC_WaveMask texture).
 * Keep the constants in sync with the HLSL in import_neoncoast.py (WAVE_HLSL).
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCWaterManager : public AActor
{
	GENERATED_BODY()
public:
	ANCWaterManager();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water") float WaterZ = 0.f;
	/** Weather: 0.4 calm, 1 normal, 2.5 storm (replicated so everyone has the same sea). */
	UPROPERTY(ReplicatedUsing = OnRep_Scale, EditAnywhere, BlueprintReadWrite, Category = "Water") float WaveScale = 1.f;
	/** Camera-following dense ocean mesh with the wave material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water") TObjectPtr<UStaticMesh> PatchMesh = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water") TObjectPtr<UMaterialInterface> PatchMaterial = nullptr;
	/** MPC_NC_Water: scalars WaveTime, WaveScale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water") TObjectPtr<UMaterialParameterCollection> Params = nullptr;
	/** Flat far ocean placed by the importer (hidden once the patch runs). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water") TObjectPtr<AActor> FlatOcean = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water") FString DataFile = TEXT("NeonCoast/Data/water.json");

	/** World Z of the sea surface at P (WaterZ when there is no manager). */
	static float SurfaceZ(const UWorld* World, const FVector& P);
	/** Surface Z and normal at P. */
	static void Sample(const UWorld* World, const FVector& P, float& OutZ, FVector& OutNormal);
	/** Server-synced clock the waves run on. */
	static double WaveTime(const UWorld* World);
	static ANCWaterManager* Get(const UWorld* World);

	/** Server: change the sea state (weather). */
	UFUNCTION(BlueprintCallable, Category = "Water") void SetWaveScale(float NewScale);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnRep_Scale();
	float Mask(double X, double Y) const;
	void Eval(double X, double Y, double T, float& OutH, float& OutDX, float& OutDY) const;

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Patch;
	/** Blue-green tint, vignette and haze while the camera is below the surface. */
	UPROPERTY(Transient) TObjectPtr<UPostProcessComponent> Underwater;
	TArray<uint8> Amp;
	int32 NX = 0, NY = 0;
	FVector2D Origin = FVector2D::ZeroVector;
	FVector2D Size = FVector2D(1.f, 1.f);
};
