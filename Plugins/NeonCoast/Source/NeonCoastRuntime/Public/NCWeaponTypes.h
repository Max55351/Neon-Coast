#pragma once

#include "CoreMinimal.h"
#include "NCWeaponTypes.generated.h"

UENUM(BlueprintType)
enum class ENCWeaponKind : uint8
{
	Melee,       // fists, bat, knife: short sweep in front of the character
	Hitscan,     // pistols, SMG, rifles, sniper, LMG, minigun
	Shotgun,     // several hitscan pellets per shot
	Rocket,      // RPG: ANCProjectile flying straight, explodes on impact
	Thrown       // grenade (fuse) / molotov (fire on impact)
};

/** One weapon type. The table lives in NCWeapons::All() (NCWeaponTypes.cpp). */
USTRUCT(BlueprintType)
struct FNCWeaponDef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Weapon") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") FString Display;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") ENCWeaponKind Kind = ENCWeaponKind::Hitscan;
	/** 1 melee, 2 pistols, 3 SMG, 4 shotgun, 5 rifle, 6 sniper, 7 heavy MG, 8 launcher, 9 throwables */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") int32 Slot = 2;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") float Damage = 25.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") float FireInterval = 0.25f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") bool bAutomatic = false;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") float RangeCm = 8000.f;
	/** Cone half angle in degrees (hip fire; aiming halves it). */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") float SpreadDeg = 1.5f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") int32 Pellets = 1;
	/** 0 = no magazine (melee), throwables: 1. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") int32 MagSize = 15;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") float ReloadTime = 1.4f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") float AimFOV = 60.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") float ExplosionRadiusCm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") bool bIncendiary = false;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") float RecoilDeg = 0.6f;
	/** Shop prices ($): the weapon, and one box of AmmoPerBox rounds. */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") int32 Price = 500;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") int32 AmmoPrice = 50;
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") int32 AmmoPerBox = 30;
	/** Static mesh asset name (Content/NeonCoast/Meshes/SM_<Mesh>), empty = none (fists). */
	UPROPERTY(BlueprintReadOnly, Category = "Weapon") FString Mesh;
};

namespace NCWeapons
{
	NEONCOASTRUNTIME_API const TArray<FNCWeaponDef>& All();
	NEONCOASTRUNTIME_API const FNCWeaponDef* Find(FName Id);
}
