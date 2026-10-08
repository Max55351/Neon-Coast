#pragma once

#include "CoreMinimal.h"
#include "NCTuning.generated.h"

class UStaticMesh;

/** Replicated tuning state of one car. Index 0 = stock / none, n = catalogue entry n-1. */
USTRUCT(BlueprintType)
struct FNCTuning
{
	GENERATED_BODY()

	/** Alpha 0 = factory paint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") FLinearColor Paint = FLinearColor(0, 0, 0, 0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") uint8 Spoiler = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") uint8 Front = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") uint8 Skirts = 0;
	/** Bitmask of off-road kit parts (bull bar, light bar, snorkel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") uint8 OffroadMask = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") uint8 Rims = 0;
	/** 0 = off, 1.. = colour index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") uint8 Neon = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") uint8 Engine = 0;   // 0..3
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") uint8 Turbo = 0;    // 0..1
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") uint8 Tires = 0;    // 0..3
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") uint8 Suspension = 0; // 0..2 (sport / race)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") uint8 Tint = 0;     // 0..2

	bool operator==(const FNCTuning& O) const
	{
		return Paint == O.Paint && Spoiler == O.Spoiler && Front == O.Front && Skirts == O.Skirts && OffroadMask == O.OffroadMask &&
			Rims == O.Rims && Neon == O.Neon && Engine == O.Engine && Turbo == O.Turbo && Tires == O.Tires &&
			Suspension == O.Suspension && Tint == O.Tint;
	}
};

/** Parts available for one car model (filled by the import script). */
USTRUCT(BlueprintType)
struct FNCTuningCatalog
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") TArray<TObjectPtr<UStaticMesh>> Spoilers;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") TArray<TObjectPtr<UStaticMesh>> Fronts;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") TArray<TObjectPtr<UStaticMesh>> Skirts;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") TArray<TObjectPtr<UStaticMesh>> Offroad;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") TArray<TObjectPtr<UStaticMesh>> Rims;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") TObjectPtr<UStaticMesh> NeonStrip;
	/** Rim meshes are unit radius with the face towards +X; fix the orientation here if needed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tuning") FRotator RimRotation = FRotator::ZeroRotator;
};

namespace NCTune
{
	/** Paint and neon colour palettes used by the garage menu. */
	NEONCOASTRUNTIME_API const TArray<FLinearColor>& PaintColors();
	NEONCOASTRUNTIME_API const TArray<FLinearColor>& NeonColors();
	/** Price in $ of going from A to B. */
	NEONCOASTRUNTIME_API int32 Price(const FNCTuning& A, const FNCTuning& B);
}
