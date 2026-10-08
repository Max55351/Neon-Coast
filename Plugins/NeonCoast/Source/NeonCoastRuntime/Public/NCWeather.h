#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NCWeather.generated.h"

class UAudioComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;

UENUM(BlueprintType)
enum class ENCWeather : uint8
{
	Clear,
	Cloudy,
	Rain,
	Storm,
	Fog
};

/**
 * Weather that changes by itself every few minutes (server decides, everyone sees the same):
 *   clear, cloudy (dimmer sun), rain, storm (heavy rain, lightning + thunder, big waves, strong
 *   wind), fog. Drives the sun / sky light / height fog, the rain streaks around the camera,
 *   the sea state (ANCWaterManager::WaveScale), the wind for sailboats and the ambient sound
 *   (rain, wind, sea, city). Cheats: CLEARSKY, CLOUDYDAY, RAINYDAY, STORMYNIGHT, FOGGYDAY.
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCWeatherManager : public AActor
{
	GENERATED_BODY()
public:
	ANCWeatherManager();

	UPROPERTY(ReplicatedUsing = OnRep_Weather, BlueprintReadOnly, Category = "Weather") ENCWeather Weather = ENCWeather::Clear;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather") bool bAutoChange = true;
	/** Seconds between changes (random in range). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather") float MinDuration = 240.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather") float MaxDuration = 480.f;
	/** Translucent streak material for rain drops. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather") TObjectPtr<UMaterialInterface> RainMaterial = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather") int32 RainDrops = 900;

	static ANCWeatherManager* Get(const UWorld* World);
	/** Server: switch now (cheats, missions). */
	UFUNCTION(BlueprintCallable, Category = "Weather") void SetWeather(ENCWeather NewWeather);
	/** Current rain 0..1 (wet roads, HUD). */
	float RainAmount() const { return Rain; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION() void OnRep_Weather();
	UFUNCTION(NetMulticast, Unreliable) void MulticastLightning(FVector_NetQuantize Where, float ThunderDelay);

	float NextChange = 300.f;
	float NextStrike = 10.f;
	// smoothed state (every machine blends towards the target of the current weather)
	float Sun = 1.f, Fog = 1.f, Rain = 0.f, Wind = 0.3f;
	float Flash = 0.f;
	float LastLux = -1.f, BaseLux = -1.f;
	float LastFog = -1.f, BaseFog = -1.f;
	float LastSky = -1.f, BaseSky = -1.f;
	void ApplyLighting(float Dt);
	void UpdateRain(float Dt);
	void UpdateAmbience(float Dt);

	UPROPERTY(Transient) TObjectPtr<UInstancedStaticMeshComponent> Drops;
	TArray<FVector> DropPos;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> RainSound;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> WindSound;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> SeaSound;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> CitySound;
	FTimerHandle ThunderTimer;
};
