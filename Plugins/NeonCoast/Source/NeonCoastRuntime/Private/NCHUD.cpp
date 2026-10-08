#pragma warning(disable: 4456 4457 4458 4459)
#include "NCHUD.h"
#include "NCBusinessManager.h"

#include "ChaosWheeledVehicleMovementComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameUserSettings.h"
#include "NCPlayerState.h"
#include "NCPlayerController.h"
#include "NCVehiclePawn.h"
#include "NCCraft.h"
#include "NCWardrobe.h"
#include "NCMission.h"
#include "NCPlayerMove.h"
#include "EngineUtils.h"
#include "NCCrowdManager.h"
#include "Components/InputComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "CanvasItem.h"
#include "GameFramework/Character.h"
#include "NCHealthComponent.h"
#include "NCWeaponComponent.h"
#include "NCTuningGarage.h"
#include "NCPickup.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	enum ERowId { RRepair, RPaint, RSpoiler, RFront, RSkirts, ROffroad, RRims, RNeon, REngine, RTurbo, RTires, RSusp, RTint, RCount };
}

TArray<ANCHUD::FRowDef> ANCHUD::Rows() const
{
	TArray<FRowDef> R;
	R.SetNum(RCount);
	const FNCTuningCatalog* C = Car.IsValid() ? &Car->TuningCatalog : nullptr;
	R[RRepair] = {TEXT("Oprava + nove pneu"), Car.IsValid() && Car->NeedsRepair() ? 1 : 0};
	R[RPaint] = {TEXT("Lak"), NCTune::PaintColors().Num()};
	R[RSpoiler] = {TEXT("Zadni spoiler"), C ? C->Spoilers.Num() : 0};
	R[RFront] = {TEXT("Predni spoiler"), C ? C->Fronts.Num() : 0};
	R[RSkirts] = {TEXT("Prahy"), C ? C->Skirts.Num() : 0};
	R[ROffroad] = {TEXT("Offroad vybava"), C && C->Offroad.Num() ? (1 << C->Offroad.Num()) - 1 : 0};
	R[RRims] = {TEXT("Disky"), C ? C->Rims.Num() : 0};
	R[RNeon] = {TEXT("Neon podsviceni"), C && C->NeonStrip ? NCTune::NeonColors().Num() : 0};
	R[REngine] = {TEXT("Motor"), 3};
	R[RTurbo] = {TEXT("Turbo"), 1};
	R[RTires] = {TEXT("Pneumatiky"), 3};
	R[RSusp] = {TEXT("Podvozek"), 2};
	R[RTint] = {TEXT("Tonovani skel"), 2};
	return R;
}

int32 ANCHUD::GetValue(int32 R) const
{
	switch (R)
	{
	case RRepair: return RepairSel;
	case RPaint:
		return Edit.Paint.A <= 0.f ? 0 : NCTune::PaintColors().IndexOfByPredicate([&](const FLinearColor& C) { return C.Equals(FLinearColor(Edit.Paint.R, Edit.Paint.G, Edit.Paint.B, 1.f)); }) + 1;
	case RSpoiler: return Edit.Spoiler;
	case RFront: return Edit.Front;
	case RSkirts: return Edit.Skirts;
	case ROffroad: return Edit.OffroadMask;
	case RRims: return Edit.Rims;
	case RNeon: return Edit.Neon;
	case REngine: return Edit.Engine;
	case RTurbo: return Edit.Turbo;
	case RTires: return Edit.Tires;
	case RSusp: return Edit.Suspension;
	case RTint: return Edit.Tint;
	default: return 0;
	}
}

void ANCHUD::SetValue(int32 R, int32 V)
{
	switch (R)
	{
	case RRepair: RepairSel = V; break;
	case RPaint:
		if (V == 0)
		{
			Edit.Paint = FLinearColor(0, 0, 0, 0);
		}
		else
		{
			const FLinearColor C = NCTune::PaintColors()[V - 1];
			Edit.Paint = FLinearColor(C.R, C.G, C.B, 1.f);
		}
		break;
	case RSpoiler: Edit.Spoiler = V; break;
	case RFront: Edit.Front = V; break;
	case RSkirts: Edit.Skirts = V; break;
	case ROffroad: Edit.OffroadMask = V; break;
	case RRims: Edit.Rims = V; break;
	case RNeon: Edit.Neon = V; break;
	case REngine: Edit.Engine = V; break;
	case RTurbo: Edit.Turbo = V; break;
	case RTires: Edit.Tires = V; break;
	case RSusp: Edit.Suspension = V; break;
	case RTint: Edit.Tint = V; break;
	default: break;
	}
}

FString ANCHUD::ValueText(int32 R) const
{
	const int32 V = GetValue(R);
	switch (R)
	{
	case RRepair: return V == 0 ? TEXT("ne") : FString::Printf(TEXT("ano  $%d"), Car.IsValid() ? Car->RepairCost() : 0);
	case RPaint: return V == 0 ? TEXT("tovarni") : FString::Printf(TEXT("barva %d"), V);
	case ROffroad:
		return V == 0 ? TEXT("zadna") : FString::Printf(TEXT("%s%s%s"), (V & 1) ? TEXT("bullbar ") : TEXT(""),
			(V & 2) ? TEXT("rampa svetel ") : TEXT(""), (V & 4) ? TEXT("snorchl") : TEXT(""));
	case RNeon: return V == 0 ? TEXT("vypnuto") : FString::Printf(TEXT("barva %d"), V);
	case RTurbo: return V ? TEXT("ano") : TEXT("ne");
	case RSusp: return V == 0 ? TEXT("serie") : (V == 1 ? TEXT("sport") : TEXT("zavodni"));
	case RTint: return V == 0 ? TEXT("zadne") : (V == 1 ? TEXT("lehke") : TEXT("tmave"));
	case REngine:
	case RTires: return V == 0 ? TEXT("serie") : FString::Printf(TEXT("stupen %d"), V);
	default: return V == 0 ? TEXT("serie / zadny") : FString::Printf(TEXT("typ %d"), V);
	}
}

void ANCHUD::OpenTuning(ANCVehiclePawn* InCar)
{
	if (!InCar)
	{
		return;
	}
	Car = InCar;
	Edit = InCar->Tuning;
	RepairSel = InCar->NeedsRepair() ? 1 : 0;       // a damaged car gets the repair pre-selected
	Row = RepairSel ? RRepair : RPaint;
}

void ANCHUD::CloseTuning(bool bKeepPreview)
{
	if (Car.IsValid() && !bKeepPreview)
	{
		Car->PreviewTuning(Car->Tuning);    // revert to what was bought
	}
	Car = nullptr;
}

void ANCHUD::MenuMove(int32 DRow, int32 DValue)
{
	if (!Car.IsValid())
	{
		return;
	}
	const TArray<FRowDef> R = Rows();
	for (int32 k = 0; k < RCount && DRow != 0; ++k)
	{
		Row = (Row + DRow + RCount) % RCount;
		if (R[Row].Max > 0)
		{
			break;
		}
	}
	if (DValue != 0 && R[Row].Max > 0)
	{
		const int32 N = R[Row].Max + 1;
		SetValue(Row, (GetValue(Row) + DValue + N) % N);
		Car->PreviewTuning(Edit);
	}
}

void ANCHUD::MenuAccept()
{
	if (Car.IsValid())
	{
		if (RepairSel && Car->NeedsRepair())
		{
			Car->ServerRepairPaid(Car->RepairCost());
		}
		Car->ServerBuyTuning(Edit);
		CloseTuning(true);
	}
}

static const TCHAR* GCheats[] = {
	TEXT("NEONCASH"), TEXT("FIXMYRIDE"), TEXT("MAXTUNE"), TEXT("SPAWNSUV"), TEXT("SPAWNSPORT"), TEXT("SPAWNPICKUP"),
	TEXT("SPAWNPOLICE"), TEXT("SPAWNTAXI"), TEXT("BOOMTOWN"), TEXT("CARJUMP"), TEXT("MOONJUMP"), TEXT("SLOWMO"),
	TEXT("NOCOPS"), TEXT("NIGHTCITY"), TEXT("SUNNYDAY"), TEXT("PINKTRAFFIC"), TEXT("NEONARMS"), TEXT("FULLHEALTH"),
	TEXT("HEATUP"), TEXT("COOLDOWN"), TEXT("CLEARSKY"), TEXT("CLOUDYDAY"), TEXT("RAINYDAY"), TEXT("STORMYNIGHT"),
	TEXT("FOGGYDAY")};

void ANCHUD::BeginPlay()
{
	Super::BeginPlay();
	PrimaryActorTick.bTickEvenWhenPaused = true;
	InitMap();
	if (APlayerController* PC = GetOwningPlayerController())
	{
		EnableInput(PC);
		if (InputComponent)
		{
			FInputKeyBinding& B = InputComponent->BindKey(EKeys::AnyKey, IE_Pressed, this, &ANCHUD::OnAnyKey);
			B.bConsumeInput = false;
			B.bExecuteWhenPaused = true;
		}
	}
}

bool ANCHUD::HandleMultiplayerKey(const FKey& Key)
{
	APlayerController* PC = GetOwningPlayerController();
	if (Key == EKeys::F6)
	{
		bMpOpen = !bMpOpen;
		bMpTypingIp = false;
		return true;
	}
	if (!bMpOpen || !PC)
	{
		return false;
	}
	if (!bMpTypingIp)
	{
		if (Key == EKeys::H)
		{
			bMpOpen = false;
			// reload the current map as a listen server: friends join with the host's IP
			if (ANCPlayerController* NPC = Cast<ANCPlayerController>(PC))
			{
				NPC->HostGame();
			}
			else
			{
				UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this, true)), true,
					TEXT("listen"));
			}
		}
		else if (Key == EKeys::J)
		{
			bMpTypingIp = true;
			IpBuffer.Reset();
		}
		else if (Key == EKeys::Escape || Key == EKeys::BackSpace)
		{
			bMpOpen = false;
		}
		return true;
	}
	static const FKey Digits[] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
	static const FKey Pad[] = {EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree,
		EKeys::NumPadFour, EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine};
	for (int32 D = 0; D < 10; ++D)
	{
		if (Key == Digits[D] || Key == Pad[D])
		{
			IpBuffer.AppendInt(D);
			return true;
		}
	}
	if ((Key == EKeys::Period || Key == EKeys::Decimal) && IpBuffer.Len() < 21)
	{
		IpBuffer.AppendChar(TEXT('.'));
	}
	else if (Key == EKeys::Semicolon)
	{
		IpBuffer.AppendChar(TEXT(':'));          // host:port (Shift+; on most layouts)
	}
	else if (Key == EKeys::BackSpace)
	{
		if (IpBuffer.IsEmpty())
		{
			bMpTypingIp = false;
		}
		else
		{
			IpBuffer.LeftChopInline(1);
		}
	}
	else if (Key == EKeys::Enter && !IpBuffer.IsEmpty())
	{
		bMpOpen = bMpTypingIp = false;
		ShowMessage(FString::Printf(TEXT("PRIPOJUJI SE K %s ..."), *IpBuffer), 6.f);
		PC->ClientTravel(IpBuffer, TRAVEL_Absolute);
	}
	else if (Key == EKeys::Escape)
	{
		bMpOpen = bMpTypingIp = false;
	}
	return true;
}

void ANCHUD::DrawMultiplayer()
{
	const float X = 40.f, Y = 140.f;
	DrawRect(FLinearColor(0.02f, 0.f, 0.06f, 0.78f), X - 16.f, Y - 14.f, 520.f, bMpTypingIp ? 150.f : 170.f);
	UFont* F = GEngine->GetMediumFont();
	const FLinearColor Pink(1.f, 0.25f, 0.7f), White(1.f, 1.f, 1.f), Grey(0.7f, 0.7f, 0.75f);
	DrawText(TEXT("MULTIPLAYER"), Pink, X, Y, GEngine->GetLargeFont(), 1.2f);
	const ENetMode NM = GetNetMode();
	const TCHAR* State = NM == NM_ListenServer ? TEXT("Hostujes hru - kamaradi zadaji tvou IP")
		: NM == NM_Client ? TEXT("Pripojeno k hostiteli") : TEXT("Hrajes sam");
	DrawText(State, Grey, X, Y + 34.f, F, 1.f);
	if (bMpTypingIp)
	{
		DrawText(FString::Printf(TEXT("IP hostitele: %s_"), *IpBuffer), White, X, Y + 66.f, F, 1.2f);
		DrawText(TEXT("[Enter] pripojit   [Backspace] smazat   [Esc] zpet"), Grey, X, Y + 100.f, F, 1.f);
		return;
	}
	DrawText(TEXT("[H] Hostovat hru (port 7777 UDP)"), White, X, Y + 66.f, F, 1.1f);
	DrawText(TEXT("[J] Pripojit se ke kamaradovi"), White, X, Y + 94.f, F, 1.1f);
	DrawText(TEXT("[F6] Zavrit"), Grey, X, Y + 126.f, F, 1.f);
}

void ANCHUD::OnAnyKey(FKey Key)
{
	if (ANCPlayerController* C=Cast<ANCPlayerController>(GetOwningPlayerController()))
	{
		if (C->IsStoryScene())
		{
			if (Key==EKeys::Escape || Key==EKeys::Gamepad_FaceButton_Right) C->ServerSkipStoryScene();
			return;
		}
		if (!IsMainMenu() && !IsShopOpen() && Key==EKeys::F8) { C->ServerContinueStory(); return; }
	}
		if (IsMainMenu())
	{
		MenuKey(Key);
		return;
	}
	if (Key==EKeys::Escape || Key==EKeys::Gamepad_Special_Right)
	{
		if (bMapOpen) { ToggleMap(); return; }
		if (IsShopOpen()) { CloseShop(); return; }
		if (IsTuningOpen()) { CloseTuning(false); return; }
		if (bMpOpen) { bMpOpen=false; bMpTypingIp=false; return; }
		if (ANCPlayerController* C=Cast<ANCPlayerController>(GetOwningPlayerController()))
		{
			MenuPage=0; MenuSel=0; bMenuIp=false; MenuStart=-1;
			C->OpenPauseMenu();
		}
		return;
	}
	if (IsShopOpen() && (Key == EKeys::Up || Key == EKeys::Down || Key == EKeys::Enter || Key == EKeys::BackSpace
		|| Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_FaceButton_Bottom
		|| Key == EKeys::Gamepad_FaceButton_Right))
	{
		ShopKey(Key);
		return;
	}
	if (HandleMultiplayerKey(Key))
	{
		return;
	}
	const FString N = Key.GetFName().ToString();
	if (Key == EKeys::M) { ToggleMap(); return; }
	if (bMapOpen)
	{
		if (Key == EKeys::Escape) ToggleMap();
		if (Key == EKeys::MouseScrollUp || Key == EKeys::Add || Key == EKeys::Equals) MapZoom = FMath::Clamp(MapZoom * 1.4f, 1.f, 16.f);
		if (Key == EKeys::MouseScrollDown || Key == EKeys::Subtract || Key == EKeys::Hyphen) MapZoom = FMath::Clamp(MapZoom / 1.4f, 1.f, 16.f);
		const float Step = MapWorldSize / MapZoom * 0.12f;
		if (Key == EKeys::I) MapCenter.Y -= Step;
		if (Key == EKeys::K) MapCenter.Y += Step;
		if (Key == EKeys::J) MapCenter.X -= Step;
		if (Key == EKeys::L) MapCenter.X += Step;
		if (Key == EKeys::Home && GetOwningPlayerController()->GetPawn()) MapCenter = GetOwningPlayerController()->GetPawn()->GetActorLocation();
		const TCHAR* Search = Key == EKeys::One ? TEXT("SERVIS") : (Key == EKeys::Two ? TEXT("FUEL") : (Key == EKeys::Three ? TEXT("ARMS") : (Key == EKeys::Four ? TEXT("POLICIE") : (Key == EKeys::Five ? TEXT("NEMOCNICE") : nullptr))));
		if (Search)
		{
			float Best = TNumericLimits<float>::Max();
			const FVector Player = GetOwningPlayerController()->GetPawn()->GetActorLocation();
			for (const FMapPOI& POI : MapPOIs)
				if (POI.Label.Contains(Search) && FVector::DistSquared2D(Player,POI.Location) < Best)
				{ Best = FVector::DistSquared2D(Player,POI.Location); MapCenter = POI.Location; MapZoom = 6.f; }
		}
		return;
	}
	if (N.Len() != 1 || !FChar::IsAlpha(N[0]))
	{
		return;
	}
	Typed = (Typed + N.ToUpper()).Right(16);
	for (const TCHAR* C : GCheats)
	{
		if (Typed.EndsWith(C))
		{
			if (ANCPlayerState* PS = GetOwningPlayerController()->GetPlayerState<ANCPlayerState>())
			{
				PS->ServerCheat(FName(C));
			}
			ShowMessage(TEXT("CHEAT AKTIVOVAN"));
			Typed.Reset();
			return;
		}
	}
}

void ANCHUD::ShowMessage(const FString& Text, float Seconds)
{
	Message = Text;
	MessageUntil = GetWorld()->GetTimeSeconds() + Seconds;
}

void ANCHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Message.IsEmpty() && GetWorld()->GetTimeSeconds() < MessageUntil && Canvas)
	{
		float W = 0.f, H = 0.f;
		GetTextSize(Message, W, H, GEngine->GetLargeFont(), 2.f);
		DrawText(Message, FLinearColor(1.f, 0.85f, 0.2f), (Canvas->ClipX - W) * 0.5f, 90.f, GEngine->GetLargeFont(), 2.f);
	}
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !Canvas)
	{
		return;
	}
	if (IsMainMenu())
	{
		DrawMainMenu();
		return;
	}
	if (const ANCPlayerController* C=Cast<ANCPlayerController>(PC); C && C->IsStoryScene())
	{
		const float Bar=Canvas->ClipY*0.12f;
		DrawRect(FLinearColor::Black,0,0,Canvas->ClipX,Bar);
		DrawRect(FLinearColor::Black,0,Canvas->ClipY-Bar,Canvas->ClipX,Bar);
		DrawText(TEXT("ESC - preskocit scenu"),FLinearColor(0.7f,0.7f,0.7f),25,25,GEngine->GetSmallFont());
		if (const ANCPlayerState* PS=PC->GetPlayerState<ANCPlayerState>(); PS && GetWorld()->GetTimeSeconds()<PS->SubUntil)
		{
			DrawText(PS->SubSpeaker,FLinearColor(1.f,0.75f,0.25f),Canvas->ClipX*0.1f,Canvas->ClipY-Bar+8,GEngine->GetMediumFont());
			DrawText(PS->SubText,FLinearColor::White,Canvas->ClipX*0.1f,Canvas->ClipY-Bar+35,GEngine->GetSmallFont());
		}
		return;
	}
	if (const ANCPlayerState* PS=PC->GetPlayerState<ANCPlayerState>(); PS && PS->ActiveMission.IsNone())
		DrawText(TEXT("F8 - zahajit / pokracovat v mafianskem pribehu"),FLinearColor(1.f,0.85f,0.5f),25,Canvas->ClipY-45,GEngine->GetSmallFont());
	DrawMap();
	if (bMapOpen) return;
	if (ANCVehiclePawn* V = Cast<ANCVehiclePawn>(PC->GetPawn()))
	{
		DrawDriving(V);
	}
	if (ANCCraftPawn* B = Cast<ANCCraftPawn>(PC->GetPawn()))
	{
		DrawCraft(B);
	}
	if (const UNCPlayerMove* MV = UNCPlayerMove::Of(PC->GetPawn()))
	{
		if (MV->bSwimming && MV->Oxygen < MV->MaxOxygen - 0.1f)
		{
			// oxygen bar while diving
			const float X = Canvas->ClipX * 0.5f - 150.f, Y = Canvas->ClipY - 90.f;
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), X, Y, 300.f, 14.f);
			DrawRect(MV->Oxygen < 8.f ? FLinearColor(1.f, 0.2f, 0.1f) : FLinearColor(0.3f, 0.8f, 1.f), X, Y, 300.f * MV->Oxygen / MV->MaxOxygen, 14.f);
			DrawText(TEXT("kyslik"), FLinearColor::White, X + 310.f, Y - 2.f, GEngine->GetSmallFont(), 1.f);
		}
		else if (MV->bInCover)
		{
			DrawText(TEXT("KRYT  (C = opustit)"), FLinearColor(0.8f, 0.9f, 1.f), Canvas->ClipX * 0.5f - 80.f, Canvas->ClipY - 90.f,
				GEngine->GetMediumFont(), 1.f);
		}
	}
	{
		APawn* Hero = PC->GetPawn();
	if (const ANCVehiclePawn* V = Cast<ANCVehiclePawn>(Hero))
		{
			Hero = V->Driver ? V->Driver.Get() : nullptr;
		}
		else if (const ANCCraftPawn* B = Cast<ANCCraftPawn>(Hero))
		{
			Hero = B->Driver ? B->Driver.Get() : nullptr;
		}
		DrawStatus(Hero, PC->GetPlayerState<ANCPlayerState>());
		if (IsShopOpen())
		{
			DrawShop(Hero);
		}
	}
	for (TActorIterator<ANCCrowdManager> It(GetWorld()); It; ++It)
	{
		FVector BL;
		FString BT;
		if (It->GetBark(BL, BT))
		{
			const FVector S = Project(BL);
			if (S.Z > 0.f)
			{
				float W = 0.f, Hh = 0.f;
				GetTextSize(BT, W, Hh, GEngine->GetMediumFont(), 1.1f);
				DrawRect(FLinearColor(1.f, 1.f, 1.f, 0.85f), S.X - W * 0.5f - 8.f, S.Y - 4.f, W + 16.f, Hh + 8.f);
				DrawText(BT, FLinearColor(0.05f, 0.05f, 0.08f), S.X - W * 0.5f, S.Y, GEngine->GetMediumFont(), 1.1f);
			}
		}
		break;
	}
	if (const ANCPlayerState* PS = PC->GetPlayerState<ANCPlayerState>())
	{
		if (!PS->Objective.IsEmpty())
		{
			// mission objective top-left with the distance to it
			FString T = PS->Objective;
			if (PS->bObjective && PC->GetPawn())
			{
				T += FString::Printf(TEXT("   (%d m)"), FMath::RoundToInt(FVector::Dist2D(PC->GetPawn()->GetActorLocation(), PS->ObjectiveLoc) / 100.f));
			}
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), 30.f, 30.f, 620.f, 44.f);
			DrawText(T, FLinearColor(1.f, 0.85f, 0.3f), 44.f, 38.f, GEngine->GetMediumFont(), 1.3f);
			if (PS->bObjective && PC->PlayerCameraManager)
			{
				// arrow at the top of the screen pointing where to go
				const FVector To = PS->ObjectiveLoc - PC->PlayerCameraManager->GetCameraLocation();
				const float Rel = FMath::FindDeltaAngleDegrees(PC->PlayerCameraManager->GetCameraRotation().Yaw, To.Rotation().Yaw);
				const float AX = Canvas->ClipX * 0.5f + FMath::Clamp(Rel / 90.f, -1.f, 1.f) * Canvas->ClipX * 0.35f;
				DrawText(FMath::Abs(Rel) > 90.f ? TEXT("v") : TEXT("^"), FLinearColor(1.f, 0.85f, 0.2f), AX, 90.f, GEngine->GetLargeFont(), 2.f);
			}
		}
		if (GetWorld()->GetTimeSeconds() < PS->SubUntil)
		{
			const FString S = PS->SubSpeaker + TEXT(": ") + PS->SubText;
			float W = 0.f, Hh = 0.f;
			GetTextSize(S, W, Hh, GEngine->GetMediumFont(), 1.3f);
			DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.55f), (Canvas->ClipX - W) * 0.5f - 16.f, Canvas->ClipY - 150.f, W + 32.f, Hh + 16.f);
			DrawText(PS->SubSpeaker + TEXT(":"), FLinearColor(1.f, 0.6f, 0.2f), (Canvas->ClipX - W) * 0.5f, Canvas->ClipY - 142.f,
				GEngine->GetMediumFont(), 1.3f);
			float SW = 0.f, SH = 0.f;
			GetTextSize(PS->SubSpeaker + TEXT(": "), SW, SH, GEngine->GetMediumFont(), 1.3f);
			DrawText(PS->SubText, FLinearColor::White, (Canvas->ClipX - W) * 0.5f + SW, Canvas->ClipY - 142.f, GEngine->GetMediumFont(), 1.3f);
		}
		DrawText(FString::Printf(TEXT("$ %d"), PS->Money), FLinearColor(0.3f, 1.f, 0.5f), Canvas->ClipX - 220.f, 40.f,
			GEngine->GetLargeFont(), 1.4f);
	}
	if (Car.IsValid())
	{
		DrawMenu();
	}
	if (bMpOpen)
	{
		DrawMultiplayer();
	}
	else if (GetWorld()->GetTimeSeconds() < 12.0)
	{
		DrawText(TEXT("F6 = multiplayer"), FLinearColor(0.8f, 0.8f, 0.85f), 40.f, Canvas->ClipY - 60.f,
			GEngine->GetMediumFont(), 1.f);
	}
}

void ANCHUD::InitMap()
{
	NCData::LoadPaths(TEXT("NeonCoast/Data/traffic.json"), TEXT("lanes"), MapRoads, 1400.f);
	FBox Bounds(ForceInit);
	for (const FNCPath& Road : MapRoads) for (const FVector& P : Road.Points) Bounds += P;
	if (Bounds.IsValid) { MapCenter = Bounds.GetCenter(); MapWorldSize = FMath::Max(Bounds.GetSize().X, Bounds.GetSize().Y) * 1.1f; }
	MapPOIs.Reset();
	if (const auto* City = ANCBusinessManager::Get(this)) for (const auto& Place : City->Places()) MapPOIs.Add({Place.Entrance,Place.Title,FLinearColor(.3f,.9f,1.f)});
	for (TActorIterator<ANCTuningGarage> It(GetWorld()); It; ++It) MapPOIs.Add({It->GetActorLocation(), TEXT("SERVIS / CUSTOMS"), FLinearColor(1,0.65f,0.1f)});
	for (TActorIterator<ANCShopZone> It(GetWorld()); It; ++It) MapPOIs.Add({It->GetActorLocation(), It->Title, FLinearColor(0.4f,1,0.5f)});
	for (TActorIterator<ANCWardrobe> It(GetWorld()); It; ++It) MapPOIs.Add({It->GetActorLocation(), TEXT("OBLECENI"), FLinearColor(1,0.35f,0.8f)});
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (FFileHelper::LoadFileToString(Text,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("NeonCoast/Data/placements.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root) && Root.IsValid())
	{
		const TSharedPtr<FJsonObject>* Placements = nullptr;
		if (Root->TryGetObjectField(TEXT("placements"),Placements))
		{
			const TCHAR* Keys[] = {TEXT("NC2_GasStation"),TEXT("NC2_Hospital"),TEXT("NC2_PoliceStation")};
			const TCHAR* Labels[] = {TEXT("BENZINKA"),TEXT("NEMOCNICE"),TEXT("POLICIE")};
			for (int32 k = 0; k < 3; ++k)
			{
				const TArray<TSharedPtr<FJsonValue>>* Places = nullptr;
				if (!(*Placements)->TryGetArrayField(Keys[k],Places)) continue;
				for (const auto& Place : *Places)
				{
					const auto& Values = Place->AsArray();
					if (Values.Num() >= 3) MapPOIs.Add({NCData::ToUE(Values[0]->AsNumber(),Values[1]->AsNumber(),Values[2]->AsNumber()),Labels[k],FLinearColor(0.3f,0.85f,1)});
				}
			}
		}
	}
}

void ANCHUD::ToggleMap()
{
	bMapOpen = !bMapOpen;
	if (bMapOpen) { InitMap(); MapZoom = 1.f; }
}

void ANCHUD::DrawMap()
{
	APlayerController* PC = GetOwningPlayerController();
	if (!PC || !PC->GetPawn() || !Canvas) return;
	const float Size = bMapOpen ? FMath::Min(Canvas->ClipY - 120.f, Canvas->ClipX * 0.67f) : 200.f;
	const float X = bMapOpen ? 40.f : 25.f;
	const float Y = bMapOpen ? 70.f : Canvas->ClipY - Size - 100.f;
	const FVector Center = bMapOpen ? MapCenter : PC->GetPawn()->GetActorLocation();
	const float WorldSize = bMapOpen ? MapWorldSize / MapZoom : 20000.f;
	auto ProjectMap = [&](const FVector& P) { return FVector2D(X + Size * (0.5f + (P.X - Center.X) / WorldSize), Y + Size * (0.5f + (P.Y - Center.Y) / WorldSize)); };
	auto Inside = [&](const FVector2D& P) { return P.X >= X && P.Y >= Y && P.X <= X + Size && P.Y <= Y + Size; };
	DrawRect(FLinearColor(0.02f,0.06f,0.09f,0.97f), X-5,Y-5,Size+10,Size+10);
	for (const FNCPath& Road : MapRoads)
	{
		for (int32 i = 1; i < Road.Points.Num(); i += 4)
		{
			const FVector2D A = ProjectMap(Road.Points[i-1]), B = ProjectMap(Road.Points[FMath::Min(i+3,Road.Points.Num()-1)]);
			if (Inside(A) && Inside(B)) DrawLine(A.X,A.Y,B.X,B.Y,FLinearColor(0.4f,0.55f,0.62f),bMapOpen ? 2.f : 1.f);
		}
	}
	for (const FMapPOI& POI : MapPOIs)
	{
		const FVector2D P = ProjectMap(POI.Location);
		if (!Inside(P)) continue;
		DrawRect(POI.Color,P.X-4,P.Y-4,8,8);
		if (bMapOpen && MapZoom >= 4.f) DrawText(POI.Label,POI.Color,P.X+7,P.Y,GEngine->GetSmallFont(),0.9f);
		else if (POI.Label.Contains(TEXT("SERVIS"))) DrawText(TEXT("S"),POI.Color,P.X+5,P.Y-5,GEngine->GetSmallFont(),1.f);
	}
	const FVector2D Player = ProjectMap(PC->GetPawn()->GetActorLocation());
	if (Inside(Player)) DrawText(TEXT("^"),FLinearColor::White,Player.X-5,Player.Y-8,GEngine->GetLargeFont(),0.8f);
	if (bMapOpen)
	{
		DrawText(TEXT("MAPA   M / Esc zavrit   +/- nebo kolecko priblizit"),FLinearColor::White,40,25,GEngine->GetMediumFont(),1.f);
		DrawText(TEXT("1 servis  2 benzinka  3 zbrane  4 policie  5 nemocnice"),FLinearColor(1,0.8f,0.3f),40,48,GEngine->GetSmallFont(),1.f);
		DrawText(TEXT("I J K L posun   Home = moje poloha"),FLinearColor::White,40,Y+Size+15,GEngine->GetSmallFont(),1.f);
		float RowY = 90.f;
		TArray<FMapPOI> Sorted = MapPOIs;
		const FVector Position = PC->GetPawn()->GetActorLocation();
		Sorted.Sort([&](const FMapPOI& A,const FMapPOI& B) { return FVector::DistSquared2D(Position,A.Location) < FVector::DistSquared2D(Position,B.Location); });
		TSet<FString> Seen;
		for (const FMapPOI& POI : Sorted)
		{
			if (Seen.Contains(POI.Label)) continue;
			Seen.Add(POI.Label);
			if (RowY > Canvas->ClipY-60) break;
			DrawText(FString::Printf(TEXT("%s  %d m"),*POI.Label,FMath::RoundToInt(FVector::Dist2D(PC->GetPawn()->GetActorLocation(),POI.Location)/100.f)),POI.Color,X+Size+30,RowY,GEngine->GetSmallFont(),1.f);
			RowY += 22.f;
		}
	}
	else DrawText(TEXT("M = velka mapa"),FLinearColor::White,X,Y+Size+8,GEngine->GetSmallFont(),0.9f);
}

void ANCHUD::DrawCraft(ANCCraftPawn* B)
{
	const float X = Canvas->ClipX - 300.f;
	const float Y = Canvas->ClipY - 170.f;
	const FNCCraftType* T = B->Type();
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), X - 20.f, Y - 20.f, 280.f, 150.f);
	DrawText(FString::Printf(TEXT("%3.0f km/h"), B->SpeedKmh()), FLinearColor(0.2f, 0.9f, 1.f), X, Y, GEngine->GetLargeFont(), 2.0f);
	FString Info = FString::Printf(TEXT("%.0f uzlu   trup %d %%"), B->SpeedKmh() / 1.852f,
		T ? FMath::RoundToInt(100.f * FMath::Max(0.f, B->Health) / FMath::Max(1.f, T->Health)) : 100);
	DrawText(Info, FLinearColor(0.8f, 0.9f, 1.f), X, Y + 58.f, GEngine->GetMediumFont(), 1.1f);
	if (T && T->Kind == ENCCraftKind::Sub)
	{
		DrawText(FString::Printf(TEXT("hloubka %d m"), FMath::RoundToInt(B->Altitude / 100.f)), FLinearColor(0.4f, 0.9f, 1.f), X, Y + 88.f,
			GEngine->GetMediumFont(), 1.f);
		DrawText(TEXT("W/S plyn  A/D otacet  Ctrl ponorit  Mezernik vynorit  F vystoupit"), FLinearColor(0.7f, 0.7f, 0.75f), X - 250.f,
			Y + 116.f, GEngine->GetSmallFont(), 1.f);
	}
	else if (T && T->Kind == ENCCraftKind::Tank)
	{
		DrawText(TEXT("W/S jizda  A/D otaceni  mys = vez  LMB = kanon"), FLinearColor(0.7f, 0.7f, 0.75f), X - 150.f, Y + 88.f,
			GEngine->GetSmallFont(), 1.f);
		// gun sight
		DrawText(TEXT("+"), FLinearColor(1.f, 0.3f, 0.2f), Canvas->ClipX * 0.5f - 8.f, Canvas->ClipY * 0.45f, GEngine->GetLargeFont(), 1.5f);
	}
	else if (B->IsAir())
	{
		const bool bHeli = T && T->Kind == ENCCraftKind::Heli;
		DrawText(FString::Printf(TEXT("vyska %d m   %s %d %%"), FMath::RoundToInt(B->Altitude / 100.f), bHeli ? TEXT("rotor") : TEXT("plyn"),
			FMath::RoundToInt(B->Power * 100.f)), FLinearColor(1.f, 0.9f, 0.4f), X, Y + 88.f, GEngine->GetMediumFont(), 1.f);
		DrawText(bHeli ? TEXT("Mezernik/Ctrl nahoru/dolu  W/S dopredu/dozadu  A/D otacet  Q/E do boku")
			: TEXT("W/S plyn  Mezernik/Ctrl nos nahoru/dolu  A/D naklon  Q/E smerovka"),
			FLinearColor(0.7f, 0.7f, 0.75f), X - 330.f, Y + 116.f, GEngine->GetSmallFont(), 1.f);
		if (T && T->Kind == ENCCraftKind::Plane && B->SpeedKmh() < T->StallSpeed * 0.036f * 1.05f && B->Altitude > 300.f)
		{
			DrawText(TEXT("PRETAZENI!"), FLinearColor(1.f, 0.15f, 0.1f), X, Y - 50.f, GEngine->GetLargeFont(), 1.4f);
		}
	}
	else if (T && T->Kind == ENCCraftKind::Sail)
	{
		// wind relative to the bow + how well the sail draws
		const float A = FMath::Abs(B->WindAngle);
		const TCHAR* Point = A < 35.f ? TEXT("proti vetru!") : (A < 70.f ? TEXT("ostre") : (A < 120.f ? TEXT("bocni vitr") : (A < 155.f ? TEXT("zadobok") : TEXT("po vetru"))));
		DrawText(FString::Printf(TEXT("Vitr %s  %s  plachta %d %%"), B->WindAngle > 0.f ? TEXT("zprava") : TEXT("zleva"), Point,
			FMath::RoundToInt(B->SailEfficiency * 100.f)), FLinearColor(1.f, 0.9f, 0.4f), X - 120.f, Y + 88.f, GEngine->GetMediumFont(), 1.f);
	}
	else
	{
		DrawText(TEXT("W/S plyn  A/D kormidlo  Shift turbo  F vystoupit"), FLinearColor(0.7f, 0.7f, 0.75f), X - 160.f, Y + 88.f,
			GEngine->GetSmallFont(), 1.f);
	}
}

void ANCHUD::DrawDriving(ANCVehiclePawn* V)
{
	const float X = Canvas->ClipX - 300.f;
	const float Y = Canvas->ClipY - 170.f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.45f), X - 20.f, Y - 20.f, 280.f, 140.f);
	DrawText(FString::Printf(TEXT("%3.0f km/h"), V->GetSpeedKmh()), FLinearColor(1.f, 0.3f, 0.8f), X, Y,
		GEngine->GetLargeFont(), 2.0f);
	// fuel gauge
	DrawRect(FLinearColor(0.2f, 0.2f, 0.2f, 0.8f), X, Y + 100.f, 200.f, 8.f);
	DrawRect(V->Fuel < 15.f ? FLinearColor(1.f, 0.2f, 0.1f) : FLinearColor(1.f, 0.8f, 0.2f), X, Y + 100.f, 2.f * V->Fuel, 8.f);
	DrawText(V->Fuel <= 0.f ? TEXT("PRAZDNA NADRZ - benzinka NEON FUEL") : TEXT("palivo"), FLinearColor(0.8f, 0.8f, 0.8f), X + 205.f,
		Y + 94.f, GEngine->GetSmallFont(), 1.f);
	int32 Gear = 0;
	if (const UChaosWheeledVehicleMovementComponent* M = Cast<UChaosWheeledVehicleMovementComponent>(V->GetVehicleMovementComponent()))
	{
		Gear = M->GetCurrentGear();
	}
	FString Info = FString::Printf(TEXT("Stupen %s"), Gear < 0 ? TEXT("R") : (Gear == 0 ? TEXT("N") : *FString::FromInt(Gear)));
	if (V->Spec.Drivetrain == ENCDrivetrain::AWD_4x4 || V->Spec.Drivetrain == ENCDrivetrain::AWD_4x4_LowRange)
	{
		Info += TEXT("   4x4");
		if (V->bLowRange) Info += TEXT(" LOW");
		if (V->bDiffLock) Info += TEXT(" LOCK");
	}
	DrawText(Info, FLinearColor(0.2f, 0.9f, 1.f), X, Y + 60.f, GEngine->GetMediumFont(), 1.2f);
	// tyre gauges (fl fr / rl rr): green -> red with wear, dark = burst
	for (int32 k = 0; k < 4 && k < V->TyreWear.Num(); ++k)
	{
		const float W = V->TyreWear[k] / 255.f;
		const bool bFlat = (V->Body.FlatMask & (1 << k)) != 0;
		const FLinearColor C = bFlat ? FLinearColor(0.15f, 0.15f, 0.15f, 0.9f)
			: FLinearColor::LerpUsingHSV(FLinearColor(0.2f, 1.f, 0.35f), FLinearColor(1.f, 0.15f, 0.1f), W);
		DrawRect(C, X + 200.f + (k % 2) * 18.f, Y + 62.f + (k / 2) * 30.f, 12.f, 24.f);
	}
	// live drift score
	if (V->DriftPoints > 1.f)
	{
		const FString D = FString::Printf(TEXT("DRIFT  %d   x%d"), FMath::RoundToInt(V->DriftPoints), V->DriftCombo);
		float TW = 0.f, TH = 0.f;
		GetTextSize(D, TW, TH, GEngine->GetLargeFont(), 1.8f);
		GlowText(D, (Canvas->ClipX - TW) * 0.5f, 150.f, 1.8f, FLinearColor(1.f, 1.f, 1.f), FLinearColor(1.f, 0.2f, 0.65f), 1.f);
		DrawText(FString::Printf(TEXT("%2.0f deg"), FMath::Abs(V->DriftAngle)), FLinearColor(0.2f, 0.9f, 1.f),
			(Canvas->ClipX - 60.f) * 0.5f, 150.f + TH + 4.f, GEngine->GetMediumFont(), 1.1f);
	}
}

void ANCHUD::DrawMenu()
{
	const TArray<FRowDef> R = Rows();
	const float X = 60.f;
	float Y = 120.f;
	DrawRect(FLinearColor(0.02f, 0.f, 0.05f, 0.75f), X - 20.f, Y - 70.f, 560.f, 60.f + 38.f * RCount + 90.f);
	DrawText(TEXT("NEON COAST CUSTOMS"), FLinearColor(1.f, 0.2f, 0.75f), X, Y - 55.f, GEngine->GetLargeFont(), 1.6f);
	for (int32 i = 0; i < RCount; ++i)
	{
		if (R[i].Max <= 0)
		{
			continue;
		}
		const bool bSel = i == Row;
		const FLinearColor C = bSel ? FLinearColor(0.2f, 1.f, 1.f) : FLinearColor(0.85f, 0.85f, 0.9f);
		DrawText((bSel ? TEXT("> ") : TEXT("  ")) + R[i].Label, C, X, Y, GEngine->GetMediumFont(), 1.2f);
		DrawText(TEXT("< ") + ValueText(i) + TEXT(" >"), C, X + 280.f, Y, GEngine->GetMediumFont(), 1.2f);
		Y += 38.f;
	}
	const int32 Price = Car.IsValid() ? NCTune::Price(Car->Tuning, Edit) : 0;
	DrawText(FString::Printf(TEXT("Cena: $ %d      Enter = koupit    Backspace = odejit"), Price),
		FLinearColor(1.f, 0.85f, 0.3f), X, Y + 20.f, GEngine->GetMediumFont(), 1.1f);
}

// ====================================================================== main menu
bool ANCHUD::IsMainMenu() const
{
	const ANCPlayerController* PC = Cast<ANCPlayerController>(GetOwningPlayerController());
	return PC && (PC->IsInMenu() || PC->IsPauseMenu());
}

TArray<FString> ANCHUD::MenuItems() const
{
	switch (MenuPage)
	{
	case 1: return {TEXT("HOSTOVAT HRU"), TEXT("PRIPOJIT SE KE KAMARADOVI"), TEXT("ZPET")};
	case 2: return {TEXT("DEN"), TEXT("ZLATA HODINKA"), TEXT("NEONOVA NOC"), TEXT("ZPET")};
	case 3: return {TEXT("ZPET")};
	case 4:
	{
		const UGameUserSettings* S=GEngine ? GEngine->GetGameUserSettings() : nullptr;
		if (!S) return {TEXT("ZPET")};
		float Normalized=0,Scale=100,Min=0,Max=100;
		S->GetResolutionScaleInformationEx(Normalized,Scale,Min,Max);
		const TCHAR* Quality[]={TEXT("NIZKA"),TEXT("STREDNI"),TEXT("VYSOKA"),TEXT("EPICKA")};
		// Textures stay high independently; use shadow quality as the preset indicator.
		const int32 Q=S->GetShadowQuality();
		return {FString(TEXT("KVALITA: "))+(Q>=0 && Q<4 ? Quality[Q] : TEXT("VLASTNI")),
			FString::Printf(TEXT("ROZLISENI VYKRESLOVANI: %.0f %%"),Scale),
			FString::Printf(TEXT("LIMIT FPS: %s"),S->GetFrameRateLimit()>0 ? *FString::Printf(TEXT("%.0f"),S->GetFrameRateLimit()) : TEXT("BEZ LIMITU")),
			FString(TEXT("V-SYNC: "))+(S->IsVSyncEnabled() ? TEXT("ZAPNUTO") : TEXT("VYPNUTO")),TEXT("ZPET")};
	}
	case 5: return {TEXT("NE, ZPET"),TEXT("ANO, VYPNOUT HRU")};
	default:
		if (const ANCPlayerController* PC=Cast<ANCPlayerController>(GetOwningPlayerController()); PC && PC->IsPauseMenu())
			return {TEXT("POKRACOVAT"),TEXT("NASTAVENI"),TEXT("OVLADANI"),TEXT("VYPNOUT HRU")};
		return {TEXT("HRAT"), TEXT("MULTIPLAYER"), TEXT("DEN / NOC"), TEXT("OVLADANI"), TEXT("NASTAVENI"), TEXT("KONEC")};
	}
}

void ANCHUD::MenuActivate()
{
	ANCPlayerController* PC = Cast<ANCPlayerController>(GetOwningPlayerController());
	if (!PC)
	{
		return;
	}
	const FString Item = MenuItems().IsValidIndex(MenuSel) ? MenuItems()[MenuSel] : FString();
	if (Item == TEXT("ZPET"))
	{
		MenuSel = 0;
		MenuPage = 0;
		bMenuIp = false;
		return;
	}
	switch (MenuPage)
	{
	case 0:
		if (PC->IsPauseMenu())
		{
			if (MenuSel==0) PC->ResumeGame();
			else { MenuPage=MenuSel==1 ? 4 : MenuSel==2 ? 3 : 5; MenuSel=0; }
			break;
		}
		if (MenuSel == 0) { PC->StartGame(); }
		else if (MenuSel == 1) { MenuPage = 1; MenuSel = 0; }
		else if (MenuSel == 2) { MenuPage = 2; MenuSel = PC->GetTimeOfDay(); }
		else if (MenuSel == 3) { MenuPage = 3; MenuSel = 0; }
		else if (MenuSel==4) { MenuPage=4; MenuSel=0; }
		else { MenuPage=5; MenuSel=0; }
		break;
	case 1:
		if (MenuSel == 0)
		{
			ShowMessage(TEXT("SPOUSTIM SERVER ..."), 5.f);
			PC->HostGame();
		}
		else
		{
			bMenuIp = true;
			IpBuffer.Reset();
		}
		break;
	case 2:
		PC->SetTimeOfDay(MenuSel);
		break;
	case 4:
		if (UGameUserSettings* S=GEngine->GetGameUserSettings())
		{
			if (MenuSel==0) { S->SetOverallScalabilityLevel((FMath::Max(0,S->GetShadowQuality())+1)%4); S->SetTextureQuality(3); }
			else if (MenuSel==1)
			{
				float N=0,Scale=100,Min=0,Max=100; S->GetResolutionScaleInformationEx(N,Scale,Min,Max);
				const float Next=Scale<59 ? 60 : Scale<69 ? 70 : Scale<84 ? 85 : Scale<99 ? 100 : 50;
				S->SetResolutionScaleValueEx(FMath::Clamp(Next,Min,Max));
			}
			else if (MenuSel==2)
			{
				const float F=S->GetFrameRateLimit(); S->SetFrameRateLimit(F<1 ? 30 : F<31 ? 60 : F<61 ? 90 : F<91 ? 120 : 0);
			}
			else if (MenuSel==3) S->SetVSyncEnabled(!S->IsVSyncEnabled());
			S->ApplySettings(false);
			// Older launchers used console-priority overrides. Update those in this session too.
			if (MenuSel==0)
			{
				const TCHAR* Groups[]={TEXT("ViewDistance"),TEXT("AntiAliasing"),TEXT("Shadow"),TEXT("GlobalIllumination"),TEXT("Reflection"),TEXT("PostProcess"),TEXT("Effects"),TEXT("Foliage"),TEXT("Shading"),TEXT("Landscape")};
				for (const TCHAR* Group:Groups) PC->ConsoleCommand(FString::Printf(TEXT("sg.%sQuality %d"),Group,S->GetShadowQuality()),false);
				PC->ConsoleCommand(TEXT("sg.TextureQuality 3"),false);
			}
			if (MenuSel==1)
			{
				float N=0,Scale=100,Min=0,Max=100; S->GetResolutionScaleInformationEx(N,Scale,Min,Max);
				PC->ConsoleCommand(FString::Printf(TEXT("sg.ResolutionQuality %.0f"),Scale),false);
				PC->ConsoleCommand(FString::Printf(TEXT("r.ScreenPercentage %.0f"),Scale),false);
			}
			if (MenuSel==2) PC->ConsoleCommand(FString::Printf(TEXT("t.MaxFPS %.0f"),S->GetFrameRateLimit()),false);
			if (MenuSel==3) PC->ConsoleCommand(FString::Printf(TEXT("r.VSync %d"),S->IsVSyncEnabled()?1:0),false);
			S->SaveSettings();
		}
		break;
	case 5:
		if (MenuSel==1) PC->QuitGame(); else { MenuPage=0; MenuSel=0; }
		break;
	default:
		break;
	}
}

void ANCHUD::MenuKey(const FKey& Key)
{
	if (bMenuIp)
	{
		// reuse the F6 panel's IP editor
		bMpOpen = true;
		bMpTypingIp = true;
		if (Key == EKeys::Enter && !IpBuffer.IsEmpty())
		{
			if (ANCPlayerController* PC = Cast<ANCPlayerController>(GetOwningPlayerController()))
			{
				ShowMessage(FString::Printf(TEXT("PRIPOJUJI SE K %s ..."), *IpBuffer), 6.f);
				PC->JoinGame(IpBuffer);
			}
		}
		else if (Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || (Key == EKeys::BackSpace && IpBuffer.IsEmpty()))
		{
			bMenuIp = false;
		}
		else
		{
			HandleMultiplayerKey(Key);
		}
		bMpOpen = false;
		bMpTypingIp = false;
		return;
	}
	const int32 N = MenuItems().Num();
	if (Key == EKeys::Up || Key == EKeys::W || Key == EKeys::Gamepad_DPad_Up || Key == EKeys::Gamepad_LeftStick_Up)
	{
		MenuSel = (MenuSel + N - 1) % N;
	}
	else if (Key == EKeys::Down || Key == EKeys::S || Key == EKeys::Gamepad_DPad_Down || Key == EKeys::Gamepad_LeftStick_Down)
	{
		MenuSel = (MenuSel + 1) % N;
	}
	else if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom
		|| Key == EKeys::Gamepad_Special_Right)
	{
		MenuActivate();
	}
	else if (Key == EKeys::Escape || Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)
	{
		if (MenuPage!=0) { MenuSel=0; MenuPage=0; }
		else if (ANCPlayerController* PC=Cast<ANCPlayerController>(GetOwningPlayerController()); PC && PC->IsPauseMenu()) PC->ResumeGame();
	}
}

void ANCHUD::NotifyHitBoxClick(FName BoxName)
{
	Super::NotifyHitBoxClick(BoxName);
	const FString N = BoxName.ToString();
	if (IsMainMenu() && N.StartsWith(TEXT("NCMenu_")))
	{
		MenuSel = FCString::Atoi(*N.RightChop(7));
		if (!bMenuIp)
		{
			MenuActivate();
		}
	}
}

void ANCHUD::NotifyHitBoxBeginCursorOver(FName BoxName)
{
	Super::NotifyHitBoxBeginCursorOver(BoxName);
	const FString N = BoxName.ToString();
	if (IsMainMenu() && N.StartsWith(TEXT("NCMenu_")) && !bMenuIp)
	{
		MenuSel = FCString::Atoi(*N.RightChop(7));
	}
}

void ANCHUD::GlowText(const FString& T, float X, float Y, float Scale, const FLinearColor& Core, const FLinearColor& Glow, float Alpha)
{
	UFont* F = GEngine->GetLargeFont();
	const float R = 3.5f * Scale;
	for (int32 k = 0; k < 12; ++k)
	{
		const float A = 2.f * PI * k / 12.f;
		FLinearColor G = Glow;
		G.A = 0.10f * Alpha;
		DrawText(T, G, X + FMath::Cos(A) * R, Y + FMath::Sin(A) * R, F, Scale);
	}
	FLinearColor G2 = Glow;
	G2.A = 0.35f * Alpha;
	DrawText(T, G2, X + 1.5f, Y + 1.5f, F, Scale);
	FLinearColor C = Core;
	C.A = Alpha;
	DrawText(T, C, X, Y, F, Scale);
}

void ANCHUD::DrawMainMenu()
{
	const float W = Canvas->ClipX, H = Canvas->ClipY, K = H / 1080.f;
	const double Now = GetWorld()->GetRealTimeSeconds();
	if (MenuStart < 0.0)
	{
		MenuStart = Now;
	}
	const float Fade = FMath::Clamp(static_cast<float>((Now - MenuStart) / 2.0), 0.f, 1.f);
	const float Pulse = 0.85f + 0.15f * FMath::Sin(static_cast<float>(Now) * 2.2f);

	// cinematic letterbox + dark gradient behind the menu column
	const float Bar = H * 0.075f;
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.92f), 0.f, 0.f, W, Bar);
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.92f), 0.f, H - Bar, W, Bar);
	const int32 Strips = 32;
	const float PanelW = W * 0.46f;
	for (int32 i = 0; i < Strips; ++i)
	{
		const float T = static_cast<float>(i) / Strips;
		DrawRect(FLinearColor(0.03f, 0.f, 0.07f, 0.78f * FMath::Pow(1.f - T, 1.6f)), T * PanelW, Bar, PanelW / Strips + 1.f, H - 2.f * Bar);
	}

	// logo
	const float X0 = 90.f * K;
	const FLinearColor Pink(1.f, 0.18f, 0.62f), PinkCore(1.f, 0.72f, 0.9f), Cyan(0.1f, 0.85f, 1.f), CyanCore(0.75f, 0.97f, 1.f);
	GlowText(TEXT("NEON"), X0, H * 0.17f, 3.4f * K, PinkCore, Pink, Fade * Pulse);
	GlowText(TEXT("COAST"), X0 + 6.f * K, H * 0.17f + 78.f * K, 3.4f * K, CyanCore, Cyan, Fade);
	DrawRect(FLinearColor(1.f, 0.2f, 0.65f, 0.8f * Fade), X0, H * 0.17f + 170.f * K, 340.f * K, 3.f * K);
	UFont* Med = GEngine->GetMediumFont();
	DrawText(TEXT("OTEVRENY SVET  -  MAFIE  -  MULTIPLAYER"), FLinearColor(0.85f, 0.85f, 0.95f, 0.8f * Fade), X0,
		H * 0.17f + 184.f * K, Med, 1.1f * K);

	// page title + items
	const TArray<FString> Items = MenuItems();
	float Y = H * 0.47f;
	static const TCHAR* Titles[] = {TEXT(""), TEXT("MULTIPLAYER"), TEXT("DEN / NOC"), TEXT("OVLADANI"),TEXT("NASTAVENI"),TEXT("OPRAVDU VYPNOUT?")};
	if (MenuPage > 0)
	{
		DrawText(Titles[MenuPage], FLinearColor(0.1f, 0.85f, 1.f, Fade), X0, Y - 56.f * K, GEngine->GetLargeFont(), 1.3f * K);
	}
	const ANCPlayerController* PC = Cast<ANCPlayerController>(GetOwningPlayerController());
	if (PC && PC->IsPauseMenu() && MenuPage==0)
		DrawText(GetNetMode()==NM_Standalone ? TEXT("PAUZA") : TEXT("MENU - ONLINE HRA POKRACUJE"),FLinearColor::White,X0,Y-56.f*K,GEngine->GetMediumFont(),K);
	if (MenuPage == 3)
	{
		static const TCHAR* Lines[] = {
			TEXT("Pohyb  WASD / leva packa        Kamera  mys / prava packa"),
			TEXT("Nastoupit / vystoupit / spolujezdec  F / Y"),
			TEXT("Plyn / brzda  W S / RT LT        Rucni brzda  mezernik / B"),
			TEXT("Pohled z kabiny  V        Dvere  O        Postavit auto  R"),
			TEXT("Redukce 4x4  L        Uzaverka  K"),
			TEXT("Tuning  zajed do Neon Coast Customs, sipky + Enter"),
			TEXT("Multiplayer ve hre  F6        Cheaty  napis kod (README)"),
			TEXT("Pauza / nastaveni  Esc        Mapa  M        Pribeh  F8"),
		};
		float LY = Y;
		for (const TCHAR* L : Lines)
		{
			DrawText(L, FLinearColor(0.92f, 0.92f, 0.98f, Fade), X0, LY, Med, 1.15f * K);
			LY += 34.f * K;
		}
		Y = LY + 24.f * K;
	}
	if (bMenuIp)
	{
		DrawText(TEXT("IP adresa hostitele:"), FLinearColor(0.8f, 0.8f, 0.9f, Fade), X0, Y, Med, 1.2f * K);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X0 - 8.f * K, Y + 36.f * K, 420.f * K, 50.f * K);
		const bool bCaret = FMath::Frac(Now * 1.6) < 0.5;
		DrawText(IpBuffer + (bCaret ? TEXT("_") : TEXT("")), FLinearColor(1.f, 1.f, 1.f, Fade), X0, Y + 42.f * K,
			GEngine->GetLargeFont(), 1.3f * K);
		DrawText(TEXT("Enter = pripojit     Esc = zpet"), FLinearColor(0.7f, 0.7f, 0.8f, Fade), X0, Y + 100.f * K, Med, 1.f * K);
	}
	else
	{
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			const bool bSel = i == MenuSel;
			FString Label = Items[i];
			if (MenuPage == 2 && PC && i == PC->GetTimeOfDay())
			{
				Label += TEXT("   *");
			}
			const float IX = X0 + (bSel ? 18.f * K : 0.f);
			if (bSel)
			{
				DrawRect(FLinearColor(1.f, 0.2f, 0.65f, Fade), X0 - 26.f * K, Y + 6.f * K, 7.f * K, 40.f * K);
				DrawRect(FLinearColor(1.f, 0.2f, 0.65f, 0.12f * Fade), X0 - 26.f * K, Y + 2.f * K, 470.f * K, 48.f * K);
				GlowText(Label, IX, Y, 1.75f * K, FLinearColor(1.f, 1.f, 1.f), Pink, Fade);
			}
			else
			{
				DrawText(Label, FLinearColor(0.85f, 0.85f, 0.92f, 0.72f * Fade), IX, Y, GEngine->GetLargeFont(), 1.75f * K);
			}
			AddHitBox(FVector2D(X0 - 30.f * K, Y), FVector2D(480.f * K, 52.f * K), FName(*FString::Printf(TEXT("NCMenu_%d"), i)), true, 0);
			Y += 56.f * K;
		}
	}

	// footer
	DrawText(TEXT("Sipky / mys = vyber      Enter / A = potvrdit      Esc / B = zpet"),
		FLinearColor(0.75f, 0.75f, 0.85f, 0.8f * Fade), X0, H - Bar - 40.f * K, Med, 1.f * K);
	DrawText(TEXT("v0.4 prototyp"), FLinearColor(0.6f, 0.6f, 0.7f, 0.7f * Fade), W - 180.f * K, H - Bar - 40.f * K, Med, 1.f * K);
	// fade in from black
	if (Fade < 1.f)
	{
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 1.f - Fade), 0.f, 0.f, W, H);
	}
}

// ====================================================================== on-foot status, wanted stars, shop
void ANCHUD::DrawStars(float X, float Y, int32 Level, bool bSeen)
{
	const float T = GetWorld()->GetRealTimeSeconds();
	const bool bBlinkOff = Level > 0 && !bSeen && FMath::Frac(T * 1.6f) > 0.55f;
	for (int32 s = 0; s < 5; ++s)
	{
		const bool bOn = s < Level;
		FLinearColor C = bOn ? (bSeen ? FLinearColor(1.f, 0.82f, 0.15f) : FLinearColor(0.55f, 0.7f, 1.f)) : FLinearColor(0.f, 0.f, 0.f, 0.35f);
		if (bOn && bBlinkOff)
		{
			C.A = 0.25f;
		}
		const FVector2D Ctr(X + s * 38.f, Y);
		TArray<FCanvasUVTri> Tris;
		for (int32 k = 0; k < 10; ++k)
		{
			const float A0 = -PI / 2 + k * PI / 5.f, A1 = -PI / 2 + (k + 1) * PI / 5.f;
			const float R0 = (k % 2) ? 7.f : 17.f, R1 = (k % 2) ? 17.f : 7.f;
			FCanvasUVTri Tri;
			Tri.V0_Pos = Ctr;
			Tri.V1_Pos = Ctr + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * R0;
			Tri.V2_Pos = Ctr + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * R1;
			Tri.V0_Color = Tri.V1_Color = Tri.V2_Color = C;
			Tris.Add(Tri);
		}
		Canvas->K2_DrawTriangle(nullptr, Tris);
	}
}

void ANCHUD::DrawStatus(APawn* Hero, ANCPlayerState* PS)
{
	if (const auto* City = ANCBusinessManager::Get(this))
	{
		const FString Hint = City->Hint(Hero);
		if (!Hint.IsEmpty()) DrawText(Hint,FLinearColor(.5f,1.f,1.f),40.f,Canvas->ClipY-145.f,nullptr,1.f);
	}
	const float W = Canvas->ClipX, H = Canvas->ClipY;
	const float Now = GetWorld()->GetTimeSeconds();
	if (PS)
	{
		DrawStars(W - 205.f, 105.f, PS->Wanted, PS->bWantedSeen);
		if (PS->Wanted > 0)
			DrawText(PS->bWantedSeen ? TEXT("PRONASLEDOVANI") : TEXT("PATRANI - ZUSTAN MIMO DOHLED"),
				PS->bWantedSeen ? FLinearColor(1.f,.5f,.15f) : FLinearColor(.55f,.75f,1.f), W - 310.f, 130.f, nullptr, .85f);
	}
	const UNCHealthComponent* HC = UNCHealthComponent::Of(Hero);
	if (HC)
	{
		const float X = 40.f, Y = H - 70.f, BW = 280.f;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.5f), X - 4.f, Y - 26.f, BW + 8.f, 44.f);
		if (HC->Armor > 0.f)
		{
			DrawRect(FLinearColor(0.2f, 0.55f, 1.f, 0.9f), X, Y - 20.f, BW * HC->Armor / HC->MaxArmor, 10.f);
		}
		const float Frac = HC->Health / FMath::Max(1.f, HC->MaxHealth);
		const bool bHurt = Now - HC->LastHurtTime < 0.35f;
		DrawRect(FLinearColor(0.15f, 0.15f, 0.15f, 0.8f), X, Y - 4.f, BW, 14.f);
		DrawRect(bHurt ? FLinearColor(1.f, 1.f, 1.f) : (Frac < 0.3f ? FLinearColor(1.f, 0.15f, 0.1f) : FLinearColor(0.25f, 0.95f, 0.35f)),
			X, Y - 4.f, BW * Frac, 14.f);
		if (bHurt)
		{
			DrawRect(FLinearColor(0.8f, 0.f, 0.f, 0.18f), 0.f, 0.f, W, H);         // damage flash
		}
	}
	const UNCWeaponComponent* WC = UNCWeaponComponent::Of(Hero);
	const APlayerController* PC = GetOwningPlayerController();
	const bool bOnFoot = PC && PC->GetPawn() == Hero;
	if (WC && Hero)
	{
		const FNCWeaponDef& D = WC->CurrentDef();
		FString Txt = D.Display;
		if (D.MagSize > 0)
		{
			Txt += WC->bReloading ? TEXT("   prebijim...") : FString::Printf(TEXT("   %d / %d"), WC->CurrentClip(), WC->CurrentReserve());
		}
		DrawText(Txt, FLinearColor(1.f, 1.f, 1.f), W - 340.f, 140.f, GEngine->GetMediumFont(), 1.3f);
		// weapon strip for a moment after switching
		if (WC->Current != LastWeaponIndex)
		{
			LastWeaponIndex = WC->Current;
			WeaponStripUntil = Now + 1.6f;
		}
		if (Now < WeaponStripUntil)
		{
			float SX = W * 0.5f - WC->Owned.Num() * 60.f;
			for (int32 i = 0; i < WC->Owned.Num(); ++i)
			{
				const FNCWeaponDef* Wd = NCWeapons::Find(WC->Owned[i]);
				const bool bCur = i == WC->Current;
				DrawRect(bCur ? FLinearColor(1.f, 0.2f, 0.65f, 0.75f) : FLinearColor(0.f, 0.f, 0.f, 0.55f), SX, 60.f, 114.f, 34.f);
				DrawText(Wd ? Wd->Display : FString(), FLinearColor::White, SX + 6.f, 66.f, GEngine->GetSmallFont(), 1.1f);
				SX += 120.f;
			}
		}
		// crosshair (on foot, guns only)
		if (bOnFoot && D.Kind != ENCWeaponKind::Melee)
		{
			const float CX = W * 0.5f, CY = H * 0.5f;
			const bool bScope = WC->IsAiming() && D.Id == TEXT("Sniper");
			if (bScope)
			{
				const float R = H * 0.42f;
				DrawRect(FLinearColor::Black, 0.f, 0.f, CX - R, H);
				DrawRect(FLinearColor::Black, CX + R, 0.f, W - CX - R, H);
				DrawRect(FLinearColor::Black, CX - R, 0.f, 2.f * R, CY - R);
				DrawRect(FLinearColor::Black, CX - R, CY + R, 2.f * R, H - CY - R);
				DrawLine(CX - R, CY, CX + R, CY, FLinearColor::Black, 1.5f);
				DrawLine(CX, CY - R, CX, CY + R, FLinearColor::Black, 1.5f);
			}
			else
			{
				const float Gap = WC->IsAiming() ? 5.f : 11.f;
				const FLinearColor C(1.f, 1.f, 1.f, 0.9f);
				DrawRect(C, CX - 1.f, CY - 1.f, 2.f, 2.f);
				DrawLine(CX - Gap - 8.f, CY, CX - Gap, CY, C, 1.5f);
				DrawLine(CX + Gap, CY, CX + Gap + 8.f, CY, C, 1.5f);
				DrawLine(CX, CY - Gap - 8.f, CX, CY - Gap, C, 1.5f);
				DrawLine(CX, CY + Gap, CX, CY + Gap + 8.f, C, 1.5f);
			}
			if (Now - WC->LastHitMarker < 0.25f)
			{
				const FLinearColor R(1.f, 0.2f, 0.2f);
				DrawLine(CX - 14.f, CY - 14.f, CX - 6.f, CY - 6.f, R, 2.f);
				DrawLine(CX + 14.f, CY - 14.f, CX + 6.f, CY - 6.f, R, 2.f);
				DrawLine(CX - 14.f, CY + 14.f, CX - 6.f, CY + 6.f, R, 2.f);
				DrawLine(CX + 14.f, CY + 14.f, CX + 6.f, CY + 6.f, R, 2.f);
			}
		}
	}
	// WASTED / BUSTED
	if (const ANCPlayerController* NPC = Cast<ANCPlayerController>(PC))
	{
		const float T = GetWorld()->GetRealTimeSeconds() - NPC->DeathScreenStart;
		if (T >= 0.f && T < 5.f)
		{
			DrawRect(FLinearColor(0.f, 0.f, 0.f, FMath::Min(0.55f, T * 0.4f)), 0.f, 0.f, W, H);
			const FString Big = NPC->bDeathBusted ? TEXT("BUSTED") : TEXT("WASTED");
			float TW = 0.f, TH = 0.f;
			GetTextSize(Big, TW, TH, GEngine->GetLargeFont(), 5.f);
			const FLinearColor C = NPC->bDeathBusted ? FLinearColor(0.35f, 0.6f, 1.f) : FLinearColor(0.85f, 0.08f, 0.08f);
			GlowText(Big, (W - TW) * 0.5f, H * 0.42f, 5.f, C, C, FMath::Clamp(T * 1.5f, 0.f, 1.f));
			const FString Sub = NPC->bDeathBusted ? TEXT("Pokuta $1000, zbrane zabaveny") : TEXT("Nemocnice $500");
			GetTextSize(Sub, TW, TH, GEngine->GetMediumFont(), 1.4f);
			DrawText(Sub, FLinearColor(0.9f, 0.9f, 0.9f), (W - TW) * 0.5f, H * 0.42f + 120.f, GEngine->GetMediumFont(), 1.4f);
		}
	}
}

void ANCHUD::OpenShop(int32 Type, const FString& Title)
{
	if (const APlayerController* PC = GetOwningPlayerController())
	{
		ShopWhere = PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector;
	}
	ShopType = Type;
	ShopTitle = Title;
	ShopRow = 0;
}

void ANCHUD::CloseShop()
{
	ShopType = -1;
}

static TArray<const FNCWeaponDef*> ShopWeapons()
{
	TArray<const FNCWeaponDef*> Out;
	for (const FNCWeaponDef& D : NCWeapons::All())
	{
		if (D.Id != TEXT("Fists"))
		{
			Out.Add(&D);
		}
	}
	return Out;
}

// ---- clothes (2), slots (3), roulette (4), blackjack (5), bar (6): rows + what Enter does
void ANCHUD::ExRows(TArray<FString>& L, TArray<FString>& R) const
{
	const APlayerController* PC = GetOwningPlayerController();
	const ANCPlayerState* PS = PC ? PC->GetPlayerState<ANCPlayerState>() : nullptr;
	static const int32 Bets[3] = {10, 50, 200};
	switch (ShopType)
	{
	case 2:
		L.Add(TEXT("Puvodni obleceni"));
		R.Add(PS && PS->Outfit == -1 ? TEXT("mas na sobe") : TEXT("zdarma"));
		if (const ANCWardrobe* W = ANCWardrobe::Get(this))
		{
			for (int32 i = 0; i < W->Outfits.Num(); ++i)
			{
				L.Add(W->Outfits[i].Name);
				R.Add(PS && PS->Outfit == i ? FString(TEXT("mas na sobe")) : FString::Printf(TEXT("$%d"), W->Outfits[i].Price));
			}
		}
		break;
	case 3:
		for (int32 B : Bets)
		{
			L.Add(FString::Printf(TEXT("Zatocit za $%d"), B));
			R.Add(TEXT("3 stejne x10, 777 x50, 2 stejne x2"));
		}
		break;
	case 4:
		for (const TCHAR* T : {TEXT("Cervena"), TEXT("Cerna"), TEXT("Suda"), TEXT("Licha"), TEXT("Cislo 7"), TEXT("Cislo 17"), TEXT("Cislo 0")})
		{
			L.Add(FString::Printf(TEXT("%s  $100"), T));
			R.Add(FString(T).StartsWith(TEXT("Cislo")) ? TEXT("x36") : TEXT("x2"));
		}
		break;
	case 5:
		if (PS && PS->BJState == 1)
		{
			L.Add(TEXT("Dalsi karta (hit)"));
			R.Add(TEXT(""));
			L.Add(TEXT("Staci (stand)"));
			R.Add(TEXT(""));
		}
		else
		{
			L.Add(TEXT("Rozdat za $100"));
			R.Add(TEXT("vyhra x2, blackjack x2.5"));
			L.Add(TEXT("Rozdat za $500"));
			R.Add(TEXT(""));
		}
		break;
	case 7:
		if (const ANCMissionManager* MM = ANCMissionManager::Get(this))
		{
			for (int32 i = 0; i < MM->Missions().Num(); ++i)
			{
				const bool bDone = PS && PS->Completed.Contains(MM->Missions()[i].Id);
				if (bDone || MM->Available(PS, i))
				{
					L.Add(MM->Missions()[i].Title);
					R.Add(bDone ? FString(TEXT("splneno")) : FString::Printf(TEXT("odmena $%d"), MM->Missions()[i].Reward));
				}
			}
			if (L.Num() == 0)
			{
				L.Add(PS && !PS->ActiveMission.IsNone() ? TEXT("Uz plnis misi") : TEXT("Ted pro tebe nic nemam"));
				R.Add(TEXT(""));
			}
		}
		break;
	case 8:
		L.Add(TEXT("Natankovat plnou"));
		R.Add(TEXT("$2 za procento"));
		break;
	case 9:
		for (const auto& F : UNCWeaponComponent::FoodMenu())
		{
			L.Add(F.Name);
			R.Add(FString::Printf(TEXT("+%d zdravi  $%d"), FMath::RoundToInt(F.Heal), F.Price));
		}
		L.Add(TEXT("PREPADNOUT POKLADNU"));
		R.Add(TEXT("mir nabitou zbrani, 6 sekund, 2 hvezdy"));
		break;
	case 10:
		for (const TCHAR* T : {TEXT("Cerne vlasy"), TEXT("Blond"), TEXT("Zrzave"), TEXT("Neonove ruzove"), TEXT("Neonove modre"), TEXT("Stribrne")})
		{
			L.Add(T);
			R.Add(TEXT("$100 (s oblecenim z Neon Threads)"));
		}
		break;
	case 11:
		L.Add(TEXT("Opravit a prestrikat"));
		R.Add(TEXT("$500, setrese nevidici policii"));
		break;
	case 12:
		for (const TCHAR* T : {TEXT("Vlozit $1000"), TEXT("Vlozit vse"), TEXT("Vybrat $1000"), TEXT("Vybrat vse")})
		{
			L.Add(T);
			R.Add(PS ? FString::Printf(TEXT("na uctu $%d"), PS->BankMoney) : FString());
		}
		break;
	case 6:
		L.Add(TEXT("Pivo"));
		R.Add(TEXT("$10"));
		L.Add(TEXT("Koktejl Neon Sunset"));
		R.Add(TEXT("$25"));
		L.Add(TEXT("Panak tequily"));
		R.Add(TEXT("$15"));
		break;
	default:
		break;
	}
}

void ANCHUD::ExEnter(int32 Row)
{
	const APlayerController* PC = GetOwningPlayerController();
	ANCPlayerState* PS = PC ? PC->GetPlayerState<ANCPlayerState>() : nullptr;
	if (!PS)
	{
		return;
	}
	static const int32 Bets[3] = {10, 50, 200};
	switch (ShopType)
	{
	case 2: PS->ServerBuyOutfit(Row - 1); break;
	case 3: PS->ServerCasino(0, 0, Bets[FMath::Clamp(Row, 0, 2)]); break;
	case 4: PS->ServerCasino(1, Row < 4 ? Row : (Row == 4 ? 11 : (Row == 5 ? 21 : 4)), 100); break;
	case 5:
		if (PS->BJState == 1)
		{
			PS->ServerCasino(Row == 0 ? 3 : 4, 0, 0);
		}
		else
		{
			PS->ServerCasino(2, 0, Row == 0 ? 100 : 500);
		}
		break;
	case 6: PS->ServerCasino(5, Row, 0); break;
	case 8: PS->ServerService(0, 0, ShopWhere); break;
	case 9:
		if (Row < UNCWeaponComponent::FoodMenu().Num())
		{
			if (UNCWeaponComponent* WC = UNCWeaponComponent::Of(PC->GetPawn()))
			{
				WC->ServerBuyFood(Row);
			}
		}
		else
		{
			PS->ServerService(1, 0, ShopWhere);
		}
		break;
	case 10: PS->ServerService(2, Row, ShopWhere); break;
	case 11: PS->ServerService(3, 0, ShopWhere); break;
	case 12: PS->ServerService(Row < 2 ? 4 : 5, Row % 2 == 0 ? 1000 : 0, ShopWhere); break;
	case 7:
		if (const ANCMissionManager* MM = ANCMissionManager::Get(this))
		{
			int32 k = 0;
			for (int32 i = 0; i < MM->Missions().Num(); ++i)
			{
				const bool bDone = PS->Completed.Contains(MM->Missions()[i].Id);
				if (bDone || MM->Available(PS, i))
				{
					if (k++ == Row && !bDone)
					{
						PS->ServerStartMission(MM->Missions()[i].Id);
						CloseShop();
					}
				}
			}
		}
		break;
	default: break;
	}
}

void ANCHUD::ShopKey(const FKey& Key)
{
	if (ShopType >= 2)
	{
		TArray<FString> L, R;
		ExRows(L, R);
		const int32 N = FMath::Max(1, L.Num());
		if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up) ShopRow = (ShopRow + N - 1) % N;
		else if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down) ShopRow = (ShopRow + 1) % N;
		else if (Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right) CloseShop();
		else if (Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom) ExEnter(ShopRow % N);
		return;
	}
	const APlayerController* PC = GetOwningPlayerController();
	APawn* Hero = PC ? PC->GetPawn() : nullptr;
	UNCWeaponComponent* WC = UNCWeaponComponent::Of(Hero);
	const int32 N = ShopType == 0 ? ShopWeapons().Num() : UNCWeaponComponent::FoodMenu().Num();
	if (Key == EKeys::Up || Key == EKeys::Gamepad_DPad_Up)
	{
		ShopRow = (ShopRow + N - 1) % N;
	}
	else if (Key == EKeys::Down || Key == EKeys::Gamepad_DPad_Down)
	{
		ShopRow = (ShopRow + 1) % N;
	}
	else if (Key == EKeys::BackSpace || Key == EKeys::Gamepad_FaceButton_Right)
	{
		CloseShop();
	}
	else if (WC && (Key == EKeys::Enter || Key == EKeys::Gamepad_FaceButton_Bottom))
	{
		if (ShopType == 0)
		{
			const FNCWeaponDef* D = ShopWeapons()[ShopRow];
			WC->ServerBuy(D->Id, WC->Owned.Contains(D->Id));
		}
		else
		{
			WC->ServerBuyFood(ShopRow);
		}
	}
}

void ANCHUD::DrawShop(APawn* Hero)
{
	if (ShopType >= 2)
	{
		TArray<FString> L, R;
		ExRows(L, R);
		const float X = 60.f;
		float Y = 150.f;
		DrawRect(FLinearColor(0.02f, 0.f, 0.05f, 0.8f), X - 20.f, Y - 80.f, 720.f, 200.f + 36.f * L.Num());
		DrawText(ShopTitle, FLinearColor(1.f, 0.75f, 0.2f), X, Y - 65.f, GEngine->GetLargeFont(), 1.6f);
		for (int32 i = 0; i < L.Num(); ++i)
		{
			const bool bSel = i == ShopRow % FMath::Max(1, L.Num());
			const FLinearColor C = bSel ? FLinearColor(0.2f, 1.f, 1.f) : FLinearColor(0.85f, 0.85f, 0.9f);
			DrawText((bSel ? TEXT("> ") : TEXT("  ")) + L[i], C, X, Y, GEngine->GetMediumFont(), 1.2f);
			DrawText(R[i], C, X + 340.f, Y, GEngine->GetMediumFont(), 1.1f);
			Y += 36.f;
		}
		const APlayerController* PC = GetOwningPlayerController();
		const ANCPlayerState* PS = PC ? PC->GetPlayerState<ANCPlayerState>() : nullptr;
		if (PS && ShopType == 5 && PS->BJState > 0)
		{
			auto Hand = [](const TArray<uint8>& H) {
				FString S;
				static const TCHAR* Names[14] = {TEXT("?"), TEXT("A"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7"),
					TEXT("8"), TEXT("9"), TEXT("10"), TEXT("J"), TEXT("Q"), TEXT("K")};
				for (uint8 C : H) S += FString(Names[FMath::Min<int32>(C, 13)]) + TEXT(" ");
				return S;
			};
			DrawText(FString::Printf(TEXT("Ty: %s (%d)"), *Hand(PS->BJPlayer), ANCPlayerState::HandValue(PS->BJPlayer)),
				FLinearColor(1.f, 1.f, 1.f), X, Y + 10.f, GEngine->GetMediumFont(), 1.2f);
			DrawText(FString::Printf(TEXT("Dealer: %s (%d)"), *Hand(PS->BJDealer), ANCPlayerState::HandValue(PS->BJDealer)),
				FLinearColor(1.f, 0.7f, 0.7f), X, Y + 40.f, GEngine->GetMediumFont(), 1.2f);
			Y += 60.f;
		}
		if (PS && GetWorld()->GetTimeSeconds() - PS->CasinoTime < 6.0)
		{
			DrawText(PS->CasinoText, PS->CasinoWin > 0 ? FLinearColor(0.3f, 1.f, 0.4f) : FLinearColor(1.f, 0.8f, 0.4f), X, Y + 20.f,
				GEngine->GetLargeFont(), 1.2f);
		}
		DrawText(TEXT("Sipky = vyber   Enter = potvrdit   Backspace = zavrit"), FLinearColor(0.7f, 0.7f, 0.8f), X, Y + 70.f,
			GEngine->GetSmallFont(), 1.2f);
		return;
	}
	const UNCWeaponComponent* WC = UNCWeaponComponent::Of(Hero);
	const float X = 60.f;
	float Y = 150.f;
	const int32 N = ShopType == 0 ? ShopWeapons().Num() : UNCWeaponComponent::FoodMenu().Num();
	DrawRect(FLinearColor(0.02f, 0.f, 0.05f, 0.8f), X - 20.f, Y - 80.f, 600.f, 110.f + 36.f * N);
	DrawText(ShopTitle, FLinearColor(1.f, 0.75f, 0.2f), X, Y - 65.f, GEngine->GetLargeFont(), 1.6f);
	for (int32 i = 0; i < N; ++i)
	{
		const bool bSel = i == ShopRow;
		const FLinearColor C = bSel ? FLinearColor(0.2f, 1.f, 1.f) : FLinearColor(0.85f, 0.85f, 0.9f);
		FString L, R;
		if (ShopType == 0)
		{
			const FNCWeaponDef* D = ShopWeapons()[i];
			const bool bHave = WC && WC->Owned.Contains(D->Id);
			L = D->Display;
			if (bHave && D->Kind == ENCWeaponKind::Melee)
			{
				R = TEXT("mas");
			}
			else if (bHave || D->Price == 0)
			{
				R = FString::Printf(TEXT("naboje +%d   $%d"), D->AmmoPerBox, D->AmmoPrice + (bHave ? 0 : D->Price));
			}
			else
			{
				R = FString::Printf(TEXT("koupit   $%d"), D->Price + D->AmmoPrice);
			}
		}
		else
		{
			const auto& F = UNCWeaponComponent::FoodMenu()[i];
			L = F.Name;
			R = FString::Printf(TEXT("+%d zdravi   $%d"), FMath::RoundToInt(F.Heal), F.Price);
		}
		DrawText((bSel ? TEXT("> ") : TEXT("  ")) + L, C, X, Y, GEngine->GetMediumFont(), 1.2f);
		DrawText(R, C, X + 300.f, Y, GEngine->GetMediumFont(), 1.2f);
		Y += 36.f;
	}
	DrawText(TEXT("Sipky = vyber   Enter = koupit   Backspace = zavrit"), FLinearColor(0.7f, 0.7f, 0.8f), X, Y + 10.f,
		GEngine->GetSmallFont(), 1.2f);
}
