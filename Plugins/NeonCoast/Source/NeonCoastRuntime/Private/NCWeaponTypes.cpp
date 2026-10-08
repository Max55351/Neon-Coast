#pragma warning(disable: 4456 4457 4458 4459)
#include "NCWeaponTypes.h"

namespace
{
	FNCWeaponDef Make(const TCHAR* Id, const TCHAR* Display, ENCWeaponKind Kind, int32 Slot, float Damage, float Interval,
		bool bAuto, float Range, float Spread, int32 Mag, float Reload, int32 Price, int32 AmmoPrice, int32 AmmoBox,
		const TCHAR* Mesh, float Recoil = 0.6f)
	{
		FNCWeaponDef D;
		D.Id = Id;
		D.Display = Display;
		D.Kind = Kind;
		D.Slot = Slot;
		D.Damage = Damage;
		D.FireInterval = Interval;
		D.bAutomatic = bAuto;
		D.RangeCm = Range;
		D.SpreadDeg = Spread;
		D.MagSize = Mag;
		D.ReloadTime = Reload;
		D.Price = Price;
		D.AmmoPrice = AmmoPrice;
		D.AmmoPerBox = AmmoBox;
		D.Mesh = Mesh;
		D.RecoilDeg = Recoil;
		return D;
	}
}

const TArray<FNCWeaponDef>& NCWeapons::All()
{
	static TArray<FNCWeaponDef> T;
	if (T.Num() == 0)
	{
		using K = ENCWeaponKind;
		T.Add(Make(TEXT("Fists"), TEXT("Pesti"), K::Melee, 1, 15.f, 0.5f, false, 170.f, 0.f, 0, 0.f, 0, 0, 0, TEXT("")));
		T.Add(Make(TEXT("Bat"), TEXT("Baseballka"), K::Melee, 1, 40.f, 0.75f, false, 220.f, 0.f, 0, 0.f, 150, 0, 0, TEXT("NC2_Weapon_Bat")));
		T.Add(Make(TEXT("Knife"), TEXT("Nuz"), K::Melee, 1, 45.f, 0.45f, false, 160.f, 0.f, 0, 0.f, 200, 0, 0, TEXT("NC2_Weapon_Knife")));
		T.Add(Make(TEXT("Pistol"), TEXT("Pistole"), K::Hitscan, 2, 26.f, 0.22f, false, 8000.f, 1.4f, 15, 1.3f, 600, 60, 45, TEXT("NC2_Weapon_Pistol"), 1.2f));
		T.Add(Make(TEXT("PistolGold"), TEXT("Zlata pistole"), K::Hitscan, 2, 38.f, 0.25f, false, 9000.f, 1.2f, 12, 1.3f, 7500, 90, 36, TEXT("NC2_Weapon_PistolGold"), 1.4f));
		T.Add(Make(TEXT("SMG"), TEXT("Samopal"), K::Hitscan, 3, 19.f, 0.075f, true, 6000.f, 3.0f, 30, 1.8f, 1800, 90, 90, TEXT("NC2_Weapon_SMG"), 0.5f));
		FNCWeaponDef Sg = Make(TEXT("Shotgun"), TEXT("Brokovnice"), K::Shotgun, 4, 14.f, 0.9f, false, 3000.f, 6.5f, 8, 2.6f, 1500, 80, 24, TEXT("NC2_Weapon_Shotgun"), 3.0f);
		Sg.Pellets = 8;
		T.Add(Sg);
		T.Add(Make(TEXT("Rifle"), TEXT("Utocna puska"), K::Hitscan, 5, 31.f, 0.1f, true, 12000.f, 1.8f, 30, 2.2f, 4000, 120, 90, TEXT("NC2_Weapon_Rifle"), 0.7f));
		FNCWeaponDef Sn = Make(TEXT("Sniper"), TEXT("Odstrelovacka"), K::Hitscan, 6, 160.f, 1.3f, false, 40000.f, 0.08f, 5, 3.0f, 7000, 150, 20, TEXT("NC2_Weapon_Sniper"), 4.0f);
		Sn.AimFOV = 12.f;
		T.Add(Sn);
		T.Add(Make(TEXT("LMG"), TEXT("Kulomet"), K::Hitscan, 7, 33.f, 0.085f, true, 12000.f, 3.5f, 100, 4.0f, 9000, 250, 200, TEXT("NC2_Weapon_LMG"), 0.8f));
		T.Add(Make(TEXT("Minigun"), TEXT("Minigun"), K::Hitscan, 7, 26.f, 0.03f, true, 10000.f, 4.0f, 500, 5.0f, 30000, 600, 500, TEXT("NC2_Weapon_Minigun"), 0.3f));
		FNCWeaponDef R = Make(TEXT("RPG"), TEXT("Raketomet"), K::Rocket, 8, 450.f, 1.6f, false, 30000.f, 0.5f, 1, 2.6f, 15000, 800, 3, TEXT("NC2_Weapon_RPG"), 5.0f);
		R.ExplosionRadiusCm = 850.f;
		T.Add(R);
		FNCWeaponDef G = Make(TEXT("Grenade"), TEXT("Granaty"), K::Thrown, 9, 260.f, 1.0f, false, 3000.f, 0.f, 1, 0.8f, 0, 300, 3, TEXT("NC2_Weapon_Grenade"));
		G.ExplosionRadiusCm = 650.f;
		T.Add(G);
		FNCWeaponDef M = Make(TEXT("Molotov"), TEXT("Molotovy"), K::Thrown, 9, 35.f, 1.0f, false, 2600.f, 0.f, 1, 0.8f, 0, 250, 3, TEXT("NC2_Weapon_Molotov"));
		M.ExplosionRadiusCm = 420.f;
		M.bIncendiary = true;
		T.Add(M);
	}
	return T;
}

const FNCWeaponDef* NCWeapons::Find(FName Id)
{
	return All().FindByPredicate([Id](const FNCWeaponDef& D) { return D.Id == Id; });
}
