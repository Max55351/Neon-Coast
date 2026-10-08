#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "NCTuning.h"
#include "NCTypes.h"
#include "NCHUD.generated.h"

class ANCVehiclePawn;

/** Driving HUD (speed, gear, 4x4, money) + the Neon Coast Customs tuning menu. */
UCLASS()
class NEONCOASTRUNTIME_API ANCHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
	virtual void BeginPlay() override;

	/** Big centred message (e.g. "CHEAT AKTIVOVAN"). */
	UFUNCTION(BlueprintCallable, Category = "NeonCoast") void ShowMessage(const FString& Text, float Seconds = 3.f);

	virtual void NotifyHitBoxClick(FName BoxName) override;
	virtual void NotifyHitBoxBeginCursorOver(FName BoxName) override;

	void OpenTuning(ANCVehiclePawn* Car);
	void CloseTuning(bool bKeepPreview);
	bool IsTuningOpen() const { return Car.IsValid(); }
	void MenuMove(int32 DRow, int32 DValue);
	void MenuAccept();

	/** Shop counters (ANCShopZone): 0 guns, 1 food. */
	void OpenShop(int32 Type, const FString& Title);
	void CloseShop();
	bool IsShopOpen() const { return ShopType >= 0; }
	void RefreshCityMap() { InitMap(); }

protected:
	friend class ANCGameMode;
	struct FMapPOI { FVector Location; FString Label; FLinearColor Color; };
	TArray<FNCPath> MapRoads;
	TArray<FMapPOI> MapPOIs;
	bool bMapOpen = false;
	float MapZoom = 1.f;
	FVector MapCenter = FVector::ZeroVector;
	float MapWorldSize = 200000.f;
	void ToggleMap();
	void InitMap();
	void DrawMap();
	TWeakObjectPtr<ANCVehiclePawn> Car;
	FNCTuning Edit;
	int32 Row = 0;
	int32 RepairSel = 0;

	struct FRowDef
	{
		FString Label;
		int32 Max = 0;
	};
	TArray<FRowDef> Rows() const;
	int32 GetValue(int32 R) const;
	void SetValue(int32 R, int32 V);
	FString ValueText(int32 R) const;
	void DrawDriving(ANCVehiclePawn* V);
	void DrawCraft(class ANCCraftPawn* B);
	void DrawStatus(class APawn* Hero, class ANCPlayerState* PS);
	void DrawStars(float X, float Y, int32 Level, bool bSeen);
	void DrawShop(class APawn* Hero);
	void ExRows(TArray<FString>& L, TArray<FString>& R) const;
	void ExEnter(int32 Row);
	void ShopKey(const FKey& Key);
	int32 ShopType = -1;
	FVector ShopWhere = FVector::ZeroVector;
	int32 ShopRow = 0;
	FString ShopTitle;
	int32 LastWeaponIndex = -1;
	float WeaponStripUntil = 0.f;
	void OnAnyKey(FKey Key);
	FString Typed;
	/** F6 multiplayer panel: H = host (listen server), J = join (type the host's IP, Enter). */
	bool bMpOpen = false;
	bool bMpTypingIp = false;
	FString IpBuffer;
	bool HandleMultiplayerKey(const FKey& Key);
	void DrawMultiplayer();
	FString Message;
	double MessageUntil = 0.0;

	/** Main menu (drawn while ANCPlayerController::IsInMenu): 0 main, 1 multiplayer, 2 graphics, 3 controls. */
	int32 MenuPage = 0;
	int32 MenuSel = 0;
	bool bMenuIp = false;
	double MenuStart = -1.0;
	TArray<FString> MenuItems() const;
	bool IsMainMenu() const;
	void MenuKey(const FKey& Key);
	void MenuActivate();
	void DrawMainMenu();
	void GlowText(const FString& T, float X, float Y, float Scale, const FLinearColor& Core, const FLinearColor& Glow, float Alpha);
	void DrawMenu();
};
