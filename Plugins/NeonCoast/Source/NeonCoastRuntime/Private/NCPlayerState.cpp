#pragma warning(disable: 4456 4457 4458 4459)
#include "NCPlayerState.h"
#include "NCBusinessManager.h"
#include "NCWeather.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "NCWardrobe.h"
#include "NCMission.h"
#include "NCSound.h"
#include "NCVehiclePawn.h"
#include "NCWeaponComponent.h"
#include "NCPoliceManager.h"
#include "NCCrowdManager.h"
#include "NCHUD.h"
#include "GameFramework/PlayerController.h"
#include "NCHealthComponent.h"

#include "Net/UnrealNetwork.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "NCTrafficManager.h"
#include "NCVehiclePawn.h"
#include "NCHealthComponent.h"
#include "NCWeaponComponent.h"

bool ANCPlayerState::Spend(int32 Amount)
{
	if (!HasAuthority() || Amount > Money)
	{
		return false;
	}
	Money -= Amount;
	return true;
}

void ANCPlayerState::Earn(int32 Amount)
{
	if (HasAuthority())
	{
		Money += Amount;
	}
}

void ANCPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANCPlayerState, Money);
	DOREPLIFETIME(ANCPlayerState, bNeverWanted);
	DOREPLIFETIME(ANCPlayerState, Wanted);
	DOREPLIFETIME(ANCPlayerState, Outfit);
	DOREPLIFETIME(ANCPlayerState, BankMoney);
	DOREPLIFETIME(ANCPlayerState, HairColor);
	DOREPLIFETIME(ANCPlayerState, Completed);
	DOREPLIFETIME(ANCPlayerState, ActiveMission);
	DOREPLIFETIME_CONDITION(ANCPlayerState, Objective, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ANCPlayerState, ObjectiveLoc, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ANCPlayerState, bObjective, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ANCPlayerState, BJPlayer, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ANCPlayerState, BJDealer, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ANCPlayerState, BJState, COND_OwnerOnly);
	DOREPLIFETIME(ANCPlayerState, bWantedSeen);
	DOREPLIFETIME_CONDITION(ANCPlayerState, BusinessVisits, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ANCPlayerState, CompletedRobberies, COND_OwnerOnly);
}

static TSubclassOf<ANCVehiclePawn> FindVehicleClass(UWorld* W, const TCHAR* Key)
{
	for (TActorIterator<ANCTrafficManager> It(W); It; ++It)
	{
		for (const FNCTrafficType& T : It->Types)
		{
			if (T.DrivableClass && T.DrivableClass->GetName().Contains(Key))
			{
				return T.DrivableClass;
			}
		}
	}
	return nullptr;
}

void ANCPlayerState::ServerCheat_Implementation(FName Code)
{
	APawn* P = GetPawn();
	UWorld* W = GetWorld();
	if (!P || !W)
	{
		return;
	}
	ANCVehiclePawn* Car = Cast<ANCVehiclePawn>(P);
	const FString C = Code.ToString();
	if (C == TEXT("NEONCASH"))
	{
		Money += 250000;
	}
	else if (C == TEXT("FIXMYRIDE") && Car)
	{
		Car->RepairAll();
	}
	else if (C == TEXT("MAXTUNE") && Car)
	{
		FNCTuning T = Car->Tuning;
		T.Engine = 3;
		T.Turbo = 1;
		T.Tires = 3;
		T.Suspension = 2;
		T.Spoiler = Car->TuningCatalog.Spoilers.Num();
		T.Front = Car->TuningCatalog.Fronts.Num();
		T.Skirts = Car->TuningCatalog.Skirts.Num();
		T.OffroadMask = (1 << Car->TuningCatalog.Offroad.Num()) - 1;
		T.Rims = Car->TuningCatalog.Rims.Num();
		T.Neon = Car->TuningCatalog.NeonStrip ? 1 : 0;
		Car->SetTuningFree(T);
	}
	else if (C.StartsWith(TEXT("SPAWN")))
	{
		const TCHAR* Key = C == TEXT("SPAWNSUV") ? TEXT("SUV") : C == TEXT("SPAWNSPORT") ? TEXT("Sport")
			: C == TEXT("SPAWNPICKUP") ? TEXT("Pickup") : C == TEXT("SPAWNPOLICE") ? TEXT("Police") : TEXT("Taxi");
		if (TSubclassOf<ANCVehiclePawn> Cls = FindVehicleClass(W, Key))
		{
			const FVector Loc = P->GetActorLocation() + P->GetActorForwardVector() * 700.f + FVector(0, 0, 150.f);
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			W->SpawnActor<ANCVehiclePawn>(Cls, Loc, FRotator(0.f, P->GetActorRotation().Yaw + 90.f, 0.f), SP);
		}
	}
	else if (C == TEXT("BOOMTOWN"))
	{
		for (TActorIterator<ANCVehiclePawn> It(W); It; ++It)
		{
			if (*It != Car && FVector::Dist(It->GetActorLocation(), P->GetActorLocation()) < 8000.f)
			{
				It->BlowUp();
			}
		}
	}
	else if (C == TEXT("CARJUMP") && Car)
	{
		Car->GetMesh()->AddImpulse(FVector(0, 0, 1200.f) + Car->GetActorForwardVector() * 600.f, NAME_None, true);
	}
	else if (C == TEXT("MOONJUMP"))
	{
		AWorldSettings* WS = W->GetWorldSettings();
		WS->WorldGravityZ = WS->WorldGravityZ < -500.f ? -240.f : -980.f;
		WS->GlobalGravityZ = WS->WorldGravityZ;
		WS->bGlobalGravitySet = true;
	}
	else if (C == TEXT("SLOWMO"))
	{
		AWorldSettings* WS = W->GetWorldSettings();
		WS->SetTimeDilation(WS->TimeDilation < 0.9f ? 1.f : 0.45f);
	}
		else if (C == TEXT("NOCOPS"))
	{
		bNeverWanted = !bNeverWanted;
		if (bNeverWanted)
		{
			ClearWanted();
		}
	}
	else if (C == TEXT("NEONARMS"))
	{
		APawn* Hero = Car ? Car->Driver.Get() : P;
		if (UNCWeaponComponent* W = UNCWeaponComponent::Of(Hero))
		{
			W->GiveAll();
		}
	}
	else if (C == TEXT("FULLHEALTH"))
	{
		APawn* Hero = Car ? Car->Driver.Get() : P;
		if (UNCHealthComponent* H = UNCHealthComponent::Of(Hero))
		{
			H->Heal(1000.f);
			H->AddArmor(1000.f);
		}
	}
	else if (C == TEXT("HEATUP"))
	{
		AddWanted(2);
	}
	else if (C == TEXT("COOLDOWN"))
	{
		ClearWanted();
	}
	else if (C == TEXT("NIGHTCITY") || C == TEXT("SUNNYDAY") || C == TEXT("PINKTRAFFIC"))
	{
		MulticastWorldCheat(Code);
	}
	else if (ANCWeatherManager* WM = ANCWeatherManager::Get(W))
	{
		if (C == TEXT("CLEARSKY")) WM->SetWeather(ENCWeather::Clear);
		else if (C == TEXT("CLOUDYDAY")) WM->SetWeather(ENCWeather::Cloudy);
		else if (C == TEXT("RAINYDAY")) WM->SetWeather(ENCWeather::Rain);
		else if (C == TEXT("STORMYNIGHT")) WM->SetWeather(ENCWeather::Storm);
		else if (C == TEXT("FOGGYDAY")) WM->SetWeather(ENCWeather::Fog);
	}
}

void ANCPlayerState::MulticastWorldCheat_Implementation(FName Code)
{
	UWorld* W = GetWorld();
	const FString C = Code.ToString();
	if (C == TEXT("NIGHTCITY") || C == TEXT("SUNNYDAY"))
	{
		ApplyTimeOfDay(W, C == TEXT("NIGHTCITY") ? 2 : 0);
	}
	else if (C == TEXT("PINKTRAFFIC"))
	{
		for (TActorIterator<ANCTrafficManager> It(W); It; ++It)
		{
			It->PaintAll(FLinearColor(0.85f, 0.15f, 0.45f));
		}
	}
}

void ANCPlayerState::ApplyTimeOfDay(UWorld* W, int32 Preset)
{
	if (!W)
	{
		return;
	}
	struct FPreset
	{
		float Pitch, Yaw, Lux;
		FLinearColor Sun;
		float Sky, Fog;
		FLinearColor FogColor;
	};
	static const FPreset P[3] = {
		{-48.f, -35.f, 11.f, FLinearColor(1.f, 0.95f, 0.88f), 1.f, 0.012f, FLinearColor(0.55f, 0.7f, 0.9f)},
		{-9.f, -70.f, 9.f, FLinearColor(1.f, 0.68f, 0.42f), 0.8f, 0.02f, FLinearColor(1.f, 0.62f, 0.45f)},
		{-35.f, 40.f, 0.25f, FLinearColor(0.55f, 0.65f, 1.f), 0.25f, 0.03f, FLinearColor(0.35f, 0.15f, 0.55f)},
	};
	const FPreset& S = P[FMath::Clamp(Preset, 0, 2)];
	// office windows light up at dusk / night (facade material reads MPC_NC_World.Night)
	if (UMaterialParameterCollection* MPC = LoadObject<UMaterialParameterCollection>(nullptr,
		TEXT("/Game/NeonCoast/Materials/MPC_NC_World.MPC_NC_World")))
	{
		UKismetMaterialLibrary::SetScalarParameterValue(W, MPC, TEXT("Night"), Preset == 2 ? 1.f : (Preset == 1 ? 0.35f : 0.f));
	}
	for (TActorIterator<ADirectionalLight> It(W); It; ++It)
	{
		It->SetActorRotation(FRotator(S.Pitch, S.Yaw, 0.f));
		It->GetLightComponent()->SetIntensity(S.Lux);
		It->GetLightComponent()->SetLightColor(S.Sun);
	}
	for (TActorIterator<ASkyLight> It(W); It; ++It)
	{
		It->GetLightComponent()->SetIntensity(S.Sky);
		It->GetLightComponent()->RecaptureSky();
	}
	for (TActorIterator<AExponentialHeightFog> It(W); It; ++It)
	{
		It->GetComponent()->SetFogDensity(S.Fog);
		It->GetComponent()->SetFogInscatteringColor(S.FogColor);
	}
}

void ANCPlayerState::AddWanted(int32 Stars, bool bMinimum)
{
	if (!HasAuthority() || bNeverWanted || Stars <= 0)
	{
		return;
	}
	const int32 Before = Wanted;
	Wanted = FMath::Clamp(bMinimum ? FMath::Max(Wanted, Stars) : Wanted + Stars, 0, 5);
	if (Wanted != Before || bMinimum)
	{
		bWantedSeen = true;
		UnseenTime = 0.f;
		if (const APawn* P = GetPawn())
		{
			LastSeenLoc = P->GetActorLocation();
		}
	}
}

void ANCPlayerState::ClearWanted()
{
	if (HasAuthority())
	{
		Wanted = 0;
		bWantedSeen = false;
		UnseenTime = 0.f;
	}
}

// ------------------------------------------------------------------ wardrobe
void ANCPlayerState::ServerBuyOutfit_Implementation(int32 Index)
{
	const ANCWardrobe* W = ANCWardrobe::Get(this);
	if (Index == -1 || (W && W->Outfits.IsValidIndex(Index) && Spend(W->Outfits[Index].Price)))
	{
		Outfit = Index;
		OnRep_Outfit();
	}
}

void ANCPlayerState::OnRep_Outfit()
{
	ANCWardrobe::Apply(GetPawn(), Outfit, HairColor);
}

// ------------------------------------------------------------------ casino
int32 ANCPlayerState::HandValue(const TArray<uint8>& Cards)
{
	int32 V = 0, Aces = 0;
	for (uint8 C : Cards)
	{
		V += C == 1 ? 11 : FMath::Min<int32>(C, 10);
		Aces += C == 1;
	}
	while (V > 21 && Aces-- > 0)
	{
		V -= 10;
	}
	return V;
}

int32 ANCPlayerState::PayOut(int32 Amount)
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now - WinWindow > 600.0)
	{
		WinWindow = Now;
		WonInWindow = 0;
	}
	const int32 Paid = FMath::Clamp(Amount, 0, FMath::Max(0, 5000 - WonInWindow) + 0);
	WonInWindow += Paid;
	Money += Paid;
	return Paid;
}

void ANCPlayerState::FinishBlackjack()
{
	while (HandValue(BJDealer) < 17)
	{
		BJDealer.Add(static_cast<uint8>(FMath::RandRange(1, 13)));
	}
	const int32 P = HandValue(BJPlayer), D = HandValue(BJDealer);
	BJState = 2;
	FString T;
	int32 Win = 0;
	if (P > 21)
	{
		T = FString::Printf(TEXT("Pres 21 (%d) - prohra"), P);
	}
	else if (D > 21 || P > D)
	{
		const bool bBJ = P == 21 && BJPlayer.Num() == 2;
		Win = PayOut(bBJ ? BJBet * 5 / 2 : BJBet * 2);
		T = FString::Printf(TEXT("%s %d proti %d - vyhra $%d"), bBJ ? TEXT("BLACKJACK!") : TEXT("Vyhra:"), P, D, Win);
	}
	else if (P == D)
	{
		Money += BJBet;
		T = FString::Printf(TEXT("Remiza %d - sazka zpet"), P);
	}
	else
	{
		T = FString::Printf(TEXT("Dealer ma %d, ty %d - prohra"), D, P);
	}
	ClientCasinoResult(T, Win);
}

void ANCPlayerState::ServerCasino_Implementation(int32 Game, int32 Option, int32 Bet)
{
	Bet = FMath::Clamp(Bet, 0, 5000);
	if (Game == 0)
	{
		// slots: 7 BAR TRESEN CITRON DIAMANT
		if (!Spend(Bet))
		{
			ClientCasinoResult(TEXT("Nemas dost penez"), 0);
			return;
		}
		static const TCHAR* Sym[5] = {TEXT("7"), TEXT("BAR"), TEXT("TRESEN"), TEXT("CITRON"), TEXT("DIAMANT")};
		const int32 A = FMath::RandRange(0, 4), B = FMath::RandRange(0, 4), C = FMath::RandRange(0, 4);
		int32 Mult = 0;
		if (A == B && B == C)
		{
			Mult = A == 0 ? 50 : 10;
		}
		else if (A == B || B == C || A == C)
		{
			Mult = 2;
		}
		const int32 Win = Mult ? PayOut(Bet * Mult) : 0;
		ClientCasinoResult(FString::Printf(TEXT("[ %s | %s | %s ]  %s"), Sym[A], Sym[B], Sym[C],
			Win ? *FString::Printf(TEXT("VYHRA $%d"), Win) : TEXT("nic")), Win);
	}
	else if (Game == 1)
	{
		// roulette: Option 0 red, 1 black, 2 even, 3 odd, 4..40 = number 0..36
		if (!Spend(Bet))
		{
			ClientCasinoResult(TEXT("Nemas dost penez"), 0);
			return;
		}
		static const int32 Reds[18] = {1, 3, 5, 7, 9, 12, 14, 16, 18, 19, 21, 23, 25, 27, 30, 32, 34, 36};
		const int32 N = FMath::RandRange(0, 36);
		bool bRed = false;
		for (int32 R : Reds)
		{
			bRed |= R == N;
		}
		bool bHit = false;
		int32 Mult = 2;
		switch (Option)
		{
		case 0: bHit = N != 0 && bRed; break;
		case 1: bHit = N != 0 && !bRed; break;
		case 2: bHit = N != 0 && N % 2 == 0; break;
		case 3: bHit = N % 2 == 1; break;
		default: bHit = N == Option - 4; Mult = 36; break;
		}
		const int32 Win = bHit ? PayOut(Bet * Mult) : 0;
		ClientCasinoResult(FString::Printf(TEXT("Padlo %d %s  %s"), N, N == 0 ? TEXT("(zelena)") : (bRed ? TEXT("(cervena)") : TEXT("(cerna)")),
			Win ? *FString::Printf(TEXT("VYHRA $%d"), Win) : TEXT("prohra")), Win);
	}
	else if (Game == 2)
	{
		if (BJState == 1 || !Spend(Bet))
		{
			return;
		}
		BJBet = Bet;
		BJPlayer = {static_cast<uint8>(FMath::RandRange(1, 13)), static_cast<uint8>(FMath::RandRange(1, 13))};
		BJDealer = {static_cast<uint8>(FMath::RandRange(1, 13))};
		BJState = 1;
		if (HandValue(BJPlayer) == 21)
		{
			FinishBlackjack();
		}
	}
	else if (Game == 3 && BJState == 1)
	{
		BJPlayer.Add(static_cast<uint8>(FMath::RandRange(1, 13)));
		if (HandValue(BJPlayer) >= 21)
		{
			FinishBlackjack();
		}
	}
	else if (Game == 4 && BJState == 1)
	{
		FinishBlackjack();
	}
	else if (Game == 5)
	{
		// bar: 0 beer $10, 1 cocktail $25, 2 shot $15 - a little health, a lot of fun
		static const int32 Price[3] = {10, 25, 15};
		const int32 I = FMath::Clamp(Option, 0, 2);
		if (Spend(Price[I]))
		{
			if (UNCHealthComponent* H = UNCHealthComponent::Of(GetPawn()))
			{
				H->Heal(10.f);
			}
			ClientCasinoResult(I == 0 ? TEXT("Na zdravi!") : (I == 1 ? TEXT("Neon Sunset - mnam") : TEXT("Tequila! Svet se toci...")), -1 - I);
		}
	}
}

void ANCPlayerState::ClientCasinoResult_Implementation(const FString& Text, int32 Win)
{
	CasinoText = Text;
	CasinoWin = Win;
	CasinoTime = GetWorld()->GetTimeSeconds();
}

// ------------------------------------------------------------------ missions
void ANCPlayerState::ServerStartMission_Implementation(FName Id)
{
	if (ANCMissionManager* M = ANCMissionManager::Get(this))
	{
		M->Start(Cast<APlayerController>(GetOwner()), Id);
	}
}

void ANCPlayerState::ClientSubtitle_Implementation(const FString& Speaker, const FString& Text, float Seconds)
{
	SubSpeaker = Speaker;
	SubText = Text;
	SubUntil = GetWorld()->GetTimeSeconds() + Seconds;
	// sim-speak: each character has a voice pitch (from the name)
	static const TCHAR* Voices[3] = {TEXT("Voice_A"), TEXT("Voice_B"), TEXT("Voice_C")};
	const uint32 H = GetTypeHash(Speaker);
	ANCSoundBank::Play2D(this, Voices[H % 3], 0.8f, 0.85f + (H % 7) * 0.05f);
}

void ANCPlayerState::ClientBanner_Implementation(const FString& Text, bool bGood)
{
	if (const APlayerController* PC = Cast<APlayerController>(GetOwner()))
	{
		if (ANCHUD* H = Cast<ANCHUD>(PC->GetHUD()))
		{
			H->ShowMessage(Text, 5.f);
		}
	}
}

// ------------------------------------------------------------------ services
void ANCPlayerState::ClientOpenShop_Implementation(int32 Type, const FString& Title)
{
	if (auto* PC = Cast<APlayerController>(GetOwner())) if (auto* HUD = Cast<ANCHUD>(PC->GetHUD())) HUD->OpenShop(Type,Title);
}
void ANCPlayerState::ClientCloseShop_Implementation()
{
	if (auto* PC = Cast<APlayerController>(GetOwner())) if (auto* HUD = Cast<ANCHUD>(PC->GetHUD())) HUD->CloseShop();
}
void ANCPlayerState::ServerService_Implementation(int32 Op, int32 Option, FVector_NetQuantize Where)
{
	APawn* P = GetPawn();
	ANCVehiclePawn* Car = Cast<ANCVehiclePawn>(P);
	switch (Op)
	{
	case 0:                                                               // NEON FUEL: $2 per %
	{
		if (!Car)
		{
			ClientCasinoResult(TEXT("Nejdriv prijed autem k pumpe"), 0);
			return;
		}
		const int32 Cost = FMath::CeilToInt((100.f - Car->Fuel) * 2.f);
		if (Cost <= 0 || Spend(Cost))
		{
			Car->Fuel = 100.f;
			ClientCasinoResult(FString::Printf(TEXT("Plna nadrz za $%d"), Cost), 0);
		}
		break;
	}
	case 1:                                                               // hold-up: gun out, grab the till
	{
		if (auto* City = ANCBusinessManager::Get(this)) City->StartRobbery(Cast<APlayerController>(GetOwner()),Where);
		break;
	}
	case 2:                                                               // NEON CUTS: hair colour $100
		if (Spend(100))
		{
			HairColor = Option;
			OnRep_Outfit();
			ClientCasinoResult(TEXT("Novy styl! Slusi ti to."), 0);
		}
		break;
	case 3:                                                               // Pay'n'Spray $500: repaired, and unseen cops lose you
		if (!Car)
		{
			ClientCasinoResult(TEXT("Prijed autem do garaze"), 0);
			return;
		}
		if (Spend(500))
		{
			Car->RepairAll();
			Car->Fuel = FMath::Max(Car->Fuel, 25.f);
			const bool bLost = Wanted > 0 && !bWantedSeen;
			if (bLost)
			{
				ClearWanted();
			}
			ClientCasinoResult(bLost ? TEXT("Opraveno a prestrikano - policie te nepoznava!") : TEXT("Opraveno."), 0);
		}
		break;
	case 4:                                                               // bank: deposit (Option $, 0 = all)
	{
		const int32 A = Option <= 0 ? Money : FMath::Min(Option, Money);
		Money -= A;
		BankMoney += A;
		ClientCasinoResult(FString::Printf(TEXT("Ulozeno $%d. Na uctu: $%d"), A, BankMoney), 0);
		break;
	}
	case 5:
	{
		const int32 A = Option <= 0 ? BankMoney : FMath::Min(Option, BankMoney);
		BankMoney -= A;
		Money += A;
		ClientCasinoResult(FString::Printf(TEXT("Vybrano $%d. Na uctu: $%d"), A, BankMoney), 0);
		break;
	}
	default:
		break;
	}
}
