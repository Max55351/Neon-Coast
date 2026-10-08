#pragma warning(disable: 4456 4457 4458 4459)
#include "NCMission.h"
#include "NCWeaponComponent.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NCHealthComponent.h"
#include "NCPickup.h"
#include "NCPlayerState.h"
#include "NCPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NCPoliceManager.h"
#include "NCPoliceOfficer.h"
#include "NCSound.h"
#include "NCTypes.h"
#include "NCVehiclePawn.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FVector ReadLoc(const TSharedPtr<FJsonObject>& O)
	{
		double X = 0, Y = 0, Z = 0;
		O->TryGetNumberField(TEXT("x"), X);
		O->TryGetNumberField(TEXT("y"), Y);
		O->TryGetNumberField(TEXT("z"), Z);
		return NCData::ToUE(X, Y, Z);
	}
}

ANCMissionManager::ANCMissionManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

ANCMissionManager* ANCMissionManager::Get(const UObject* Ctx)
{
	UWorld* W = Ctx ? Ctx->GetWorld() : nullptr;
	if (W)
	{
		for (TActorIterator<ANCMissionManager> It(W); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

void ANCMissionManager::Load()
{
	List.Reset();
	LoadFile(DataFile);
	LoadFile(TEXT("NeonCoast/Data/city_story.json"));
	LoadFile(TEXT("NeonCoast/Data/mafia_story.json"));
}

void ANCMissionManager::LoadFile(const FString& File)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), File)))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return;
	}
	for (const TSharedPtr<FJsonValue>& MV : Root->GetArrayField(TEXT("missions")))
	{
		const TSharedPtr<FJsonObject> M = MV->AsObject();
		FNCMission Mi;
		Mi.Id = FName(*M->GetStringField(TEXT("id")));
		Mi.Title = M->GetStringField(TEXT("title"));
		Mi.Reward = static_cast<int32>(M->GetNumberField(TEXT("reward")));
		Mi.Giver = ReadLoc(M->GetObjectField(TEXT("giver")));
		for (const TSharedPtr<FJsonValue>& R : M->GetArrayField(TEXT("requires")))
		{
			Mi.Requires.Add(FName(*R->AsString()));
		}
		for (const TSharedPtr<FJsonValue>& SV : M->GetArrayField(TEXT("steps")))
		{
			const TSharedPtr<FJsonObject> S = SV->AsObject();
			FNCMissionStep St;
			St.Type = S->GetStringField(TEXT("type"));
			St.Loc = ReadLoc(S);
			double V = 0;
			if (S->TryGetNumberField(TEXT("radius"), V)) St.Radius = V * 100.0;
			if (S->TryGetNumberField(TEXT("intact"), V)) St.Intact = V;
			if (S->TryGetNumberField(TEXT("seconds"), V)) St.Seconds = V;
			S->TryGetStringField(TEXT("text"), St.Text);
			S->TryGetStringField(TEXT("business"), St.Business);
			FString Veh;
			if (S->TryGetStringField(TEXT("vehicle"), Veh)) St.Vehicle = FName(*Veh);
			const TArray<TSharedPtr<FJsonValue>>* Arr;
			if (S->TryGetArrayField(TEXT("lines"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& L : *Arr)
				{
					const TArray<TSharedPtr<FJsonValue>>& P = L->AsArray();
					if (P.Num() == 2)
					{
						St.Lines.Add({P[0]->AsString(), P[1]->AsString()});
					}
				}
			}
			if (S->TryGetArrayField(TEXT("enemies"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& E : *Arr)
				{
					const TArray<TSharedPtr<FJsonValue>>& P = E->AsArray();
					if (P.Num() >= 3)
					{
						St.Enemies.Add(NCData::ToUE(P[0]->AsNumber(), P[1]->AsNumber(), P[2]->AsNumber()));
					}
				}
			}
			Mi.Steps.Add(St);
		}
		if (!List.ContainsByPredicate([&](const FNCMission& Existing) { return Existing.Id == Mi.Id; })) List.Add(Mi);
	}
	UE_LOG(LogTemp, Log, TEXT("[NeonCoast] missions: %d"), List.Num());
}

void ANCMissionManager::BeginPlay()
{
	Super::BeginPlay();
	Load();
	// one mission ring per giver location (local on every machine: the list opens on your own HUD)
	TArray<FVector> Done;
	for (const FNCMission& M : List)
	{
		if (Done.ContainsByPredicate([&M](const FVector& V) { return FVector::Dist(V, M.Giver) < 300.f; }))
		{
			continue;
		}
		Done.Add(M.Giver);
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ANCShopZone* Z = GetWorld()->SpawnActorDeferred<ANCShopZone>(ANCShopZone::StaticClass(), FTransform(M.Giver + FVector(0, 0, 100.f)));
		if (Z)
		{
			Z->ShopType = 7;
			Z->Title = TEXT("MISE - rodina Marino");
			Z->FinishSpawning(FTransform(M.Giver + FVector(0, 0, 100.f)));
			Givers.Add(Z);
		}
	}
}

bool ANCMissionManager::Available(const ANCPlayerState* PS, int32 Index) const
{
	if (!PS || !List.IsValidIndex(Index) || PS->Completed.Contains(List[Index].Id) || !PS->ActiveMission.IsNone())
	{
		return false;
	}
	for (const FName& R : List[Index].Requires)
	{
		if (!PS->Completed.Contains(R))
		{
			return false;
		}
	}
	return true;
}

void ANCMissionManager::Start(APlayerController* PC, FName Id)
{
	if (!HasAuthority()) return;
	ANCPlayerState* PS = PC ? PC->GetPlayerState<ANCPlayerState>() : nullptr;
	const int32 I = List.IndexOfByPredicate([Id](const FNCMission& M) { return M.Id == Id; });
	if (!Available(PS, I))
	{
		return;
	}
	FRun R;
	R.Mission = I;
	Runs.Add(PC, R);
	PS->ActiveMission = Id;
	PS->ClientBanner(FString::Printf(TEXT("MISE: %s"), *List[I].Title), true);
}

void ANCMissionManager::ContinueStory(APlayerController* PC)
{
	if (!HasAuthority() || !PC || !PC->GetPawn()) return;
	ANCPlayerState* PS = PC->GetPlayerState<ANCPlayerState>();
	if (!PS) return;
	if (!bLoaded) LoadHost();
	if (!PS->ActiveMission.IsNone())
	{
		PS->ClientBanner(TEXT("Uz plnis zakazku. Sleduj cil na mape (M)."), true);
		return;
	}
	const int32 Intro = List.IndexOfByPredicate([](const FNCMission& M){ return M.Id==FName(TEXT("mafia_intro")); });
	if (Available(PS, Intro)) { Start(PC,List[Intro].Id); return; }
	for (int32 I=0; I<List.Num(); ++I)
		if (List[I].Id.ToString().StartsWith(TEXT("mafia_")) && Available(PS,I)) { Start(PC,List[I].Id); return; }
	// Preserve original saves. Pick the next available original chapter, then the new epilogue.
	for (int32 I=0; I<List.Num(); ++I)
	{
		if (List[I].Id.ToString().StartsWith(TEXT("m")) && Available(PS,I))
		{
			Start(PC,List[I].Id); return;
		}
	}
	PS->ClientBanner(TEXT("Hlavni pribeh je dokonceny. Vedlejsi zakazky najdes u Marina."), true);
}

void ANCMissionManager::SkipScene(APlayerController* PC)
{
	if (!HasAuthority()) return;
	FRun* R = Runs.Find(PC);
	if (!R || !List.IsValidIndex(R->Mission) || !List[R->Mission].Steps.IsValidIndex(R->Step) || !R->bEntered) return;
	const FNCMissionStep& S = List[R->Mission].Steps[R->Step];
	if (S.Type!=TEXT("talk") && S.Type!=TEXT("cutscene")) return;
	R->DialogueIndex=S.Lines.Num();
	R->TalkLength=0.f;
	if (ANCPlayerController* C=Cast<ANCPlayerController>(PC)) C->ClientEndStoryScene();
	if (ANCPlayerState* PS=PC->GetPlayerState<ANCPlayerState>()) PS->ClientSubtitle(TEXT(""),TEXT(""),0.f);
}

void ANCMissionManager::Enter(APlayerController* PC, FRun& R, const FNCMissionStep& S)
{
	ANCPlayerState* PS = PC->GetPlayerState<ANCPlayerState>();
	APawn* P = PC->GetPawn();
	UWorld* W = GetWorld();
	PS->Objective = S.Text;
	R.VisitsAtStart = PS->BusinessVisits;
	R.RobberiesAtStart = PS->CompletedRobberies;
	PS->ObjectiveLoc = S.Loc;
	PS->bObjective = !S.Loc.IsNearlyZero() && S.Type != TEXT("talk") && S.Type != TEXT("cutscene") && S.Type != TEXT("lose_wanted") && S.Type != TEXT("timer");
	ANCPoliceManager* PM = nullptr;
	for (TActorIterator<ANCPoliceManager> It(W); It; ++It)
	{
		PM = *It;
		break;
	}
	if (S.Type == TEXT("talk") || S.Type == TEXT("cutscene"))
	{
		// Tick-driven dialogue cannot leave timer callbacks behind after failure or skip.
		R.TalkLength = 0.f;
		R.DialogueIndex = 0;
		R.NextDialogue = 0.f;
		for (const TPair<FString, FString>& L : S.Lines)
		{
			const float Len = FMath::Clamp(1.5f + L.Value.Len() * 0.06f, 2.5f, 6.f);
			R.TalkLength += Len;
		}
		if (ACharacter* Hero=Cast<ACharacter>(P)) Hero->GetCharacterMovement()->StopMovementImmediately();
		if (ANCPlayerController* C=Cast<ANCPlayerController>(PC)) C->ClientStoryScene(R.TalkLength+0.3f);
		PS->Objective.Empty();
	}
	else if (S.Type == TEXT("steal") && PM)
	{
		if (const TSubclassOf<ANCVehiclePawn>* Cls = PM->VehicleClasses.Find(S.Vehicle))
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			R.Car = W->SpawnActor<ANCVehiclePawn>(*Cls, FTransform(S.Loc + FVector(0, 0, 80.f)), SP);
		}
	}
	else if (S.Type == TEXT("kill") && PM)
	{
		R.Foes.Reset();
		const FNCOfficerLook& L = PM->GangsterLook.Mesh ? PM->GangsterLook : PM->PoliceLook;
		for (int32 i = 0; i < S.Enemies.Num(); ++i)
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			if (ANCPoliceOfficer* O = W->SpawnActor<ANCPoliceOfficer>(ANCPoliceOfficer::StaticClass(), S.Enemies[i] + FVector(0, 0, 100.f),
				FRotator::ZeroRotator, SP))
			{
				O->bGangster = true;
				O->Init(i % 3 == 2 ? 1 : 0, L.Mesh, L.Walk, L.Idle, P);
				R.Foes.Add(O);
			}
		}
		if (PS->Objective.IsEmpty())
		{
			PS->Objective = TEXT("Zlikviduj nepratele");
		}
	}
	else if (S.Type == TEXT("collect"))
	{
		ANCPickup* K = W->SpawnActorDeferred<ANCPickup>(ANCPickup::StaticClass(), FTransform(S.Loc + FVector(0, 0, 80.f)));
		if (K)
		{
			K->Kind = 4;
			K->FinishSpawning(FTransform(S.Loc + FVector(0, 0, 80.f)));
			R.Item = K;
		}
	}
	else if (S.Type == TEXT("timer"))
	{
		R.Deadline = W->GetTimeSeconds() + S.Seconds;
	}
	else if (S.Type == TEXT("lose_wanted") && PS->Wanted == 0)
	{
		PS->AddWanted(2);                                                 // they are onto you
	}
}

bool ANCMissionManager::Done(APlayerController* PC, FRun& R, const FNCMissionStep& S, bool& bFail)
{
	ANCPlayerState* PS = PC->GetPlayerState<ANCPlayerState>();
	APawn* P = PC->GetPawn();
	const FVector Here = P ? P->GetActorLocation() : FVector::ZeroVector;
	if (S.Type == TEXT("talk") || S.Type == TEXT("cutscene"))
	{
		return R.StepTime >= R.TalkLength + 0.3f;
	}
	if (S.Type == TEXT("goto"))
	{
		return P && FVector::Dist2D(Here, S.Loc) < S.Radius;
	}
	if (S.Type == TEXT("visit")) return PS->BusinessVisits > R.VisitsAtStart && PS->LastBusiness == FName(*S.Business);
	if (S.Type == TEXT("rob")) return PS->CompletedRobberies > R.RobberiesAtStart && PS->LastRobbedBusiness == FName(*S.Business);
	if (S.Type == TEXT("buy_weapon"))
	{
		const auto* Weapon = UNCWeaponComponent::Of(P);
		return Weapon && Weapon->Owned.Contains(S.Vehicle);
	}
	if (S.Type == TEXT("steal"))
	{
		if (!R.Car.IsValid())
		{
			bFail = true;
			return false;
		}
		return P == R.Car.Get();
	}
	if (S.Type == TEXT("deliver"))
	{
		ANCVehiclePawn* C = R.Car.Get();
		if (!C || C->Body.bWrecked)
		{
			bFail = true;
			return false;
		}
		return FVector::Dist2D(C->GetActorLocation(), S.Loc) < S.Radius && C->GetSpeedKmh() < 15.f;
	}
	if (S.Type == TEXT("lose_wanted"))
	{
		return PS->Wanted == 0;
	}
	if (S.Type == TEXT("kill"))
	{
		if (S.Enemies.Num()>0 && R.Foes.Num()==0) { bFail=true; return false; }
		for (const TWeakObjectPtr<ANCPoliceOfficer>& O : R.Foes)
		{
			if (O.IsValid() && !O->IsDead())
			{
				return false;
			}
		}
		return true;
	}
	if (S.Type == TEXT("collect"))
	{
		if (!R.Item.IsValid()) { bFail=true; return false; }
		return !R.Item->bActive;
	}
	if (S.Type == TEXT("timer")) return true; // establishes a deadline for following steps
	bFail = true; // unknown objectives must never silently award a completed mission
	return false;
}

void ANCMissionManager::Finish(APlayerController* PC, FRun& R, bool bSuccess, const FString& Why)
{
	if (ANCPlayerController* C=Cast<ANCPlayerController>(PC)) C->ClientEndStoryScene();
	ANCPlayerState* PS = PC ? PC->GetPlayerState<ANCPlayerState>() : nullptr;
	for (const TWeakObjectPtr<ANCPoliceOfficer>& O : R.Foes)
	{
		if (O.IsValid() && !O->IsDead())
		{
			O->Destroy();
		}
	}
	if (R.Item.IsValid())
	{
		R.Item->Destroy();
	}
	if (PS)
	{
		const FNCMission& M = List[R.Mission];
		PS->ActiveMission = NAME_None;
		PS->bObjective = false;
		PS->Objective.Empty();
		if (bSuccess)
		{
			PS->Earn(M.Reward);
			PS->Completed.AddUnique(M.Id);
			PS->ClientBanner(FString::Printf(TEXT("MISE SPLNENA  +$%d"), M.Reward), true);
			if (PC->IsLocalController())
			{
				SaveHost(PS);
			}
		}
		else
		{
			PS->ClientBanner(FString::Printf(TEXT("MISE SELHALA - %s"), *Why), false);
		}
	}
}

void ANCMissionManager::SaveHost(ANCPlayerState* PS)
{
	UNCSaveGame* S = Cast<UNCSaveGame>(UGameplayStatics::CreateSaveGameObject(UNCSaveGame::StaticClass()));
	S->Completed = PS->Completed;
	S->Money = PS->Money;
	S->Outfit = PS->Outfit;
	UGameplayStatics::SaveGameToSlot(S, TEXT("NeonCoast"), 0);
}

void ANCMissionManager::LoadHost()
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ANCPlayerState* PS = PC ? PC->GetPlayerState<ANCPlayerState>() : nullptr;
	if (!PS)
	{
		return;
	}
	bLoaded = true;
	if (UNCSaveGame* S = Cast<UNCSaveGame>(UGameplayStatics::LoadGameFromSlot(TEXT("NeonCoast"), 0)))
	{
		PS->Completed = S->Completed;
		PS->Money = S->Money;
		PS->Outfit = S->Outfit;
		PS->OnRep_Outfit();
	}
}

void ANCMissionManager::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!HasAuthority())
	{
		return;
	}
	if (!bLoaded)
	{
		LoadHost();
	}
	Accum += Dt;
	if (Accum < 0.25f)
	{
		return;
	}
	const float Step = Accum;
	Accum = 0.f;
	TArray<TWeakObjectPtr<APlayerController>> Ended;
	for (TPair<TWeakObjectPtr<APlayerController>, FRun>& It : Runs)
	{
		APlayerController* PC = It.Key.Get();
		FRun& R = It.Value;
		if (!PC || !List.IsValidIndex(R.Mission))
		{
			Ended.Add(It.Key);
			continue;
		}
		const FNCMission& M = List[R.Mission];
		const UNCHealthComponent* H = UNCHealthComponent::Of(PC->GetPawn());
		if (H && H->bDead)
		{
			Finish(PC, R, false, TEXT("zemrel jsi"));
			Ended.Add(It.Key);
			continue;
		}
		if (R.Deadline > 0.0 && GetWorld()->GetTimeSeconds() > R.Deadline)
		{
			Finish(PC, R, false, TEXT("vyprsel cas"));
			Ended.Add(It.Key);
			continue;
		}
		if (R.Step >= M.Steps.Num())
		{
			Finish(PC, R, true, FString());
			Ended.Add(It.Key);
			continue;
		}
		const FNCMissionStep& S = M.Steps[R.Step];
		if (!R.bEntered)
		{
			R.bEntered = true;
			R.StepTime = 0.f;
			Enter(PC, R, S);
		}
		R.StepTime += Step;
		const bool bDialogue = S.Type==TEXT("talk") || S.Type==TEXT("cutscene");
		if (bDialogue && S.Lines.IsValidIndex(R.DialogueIndex) && R.StepTime>=R.NextDialogue)
		{
			const auto& Line=S.Lines[R.DialogueIndex++];
			const float Length=FMath::Clamp(1.5f+Line.Value.Len()*0.06f,2.5f,6.f);
			PC->GetPlayerState<ANCPlayerState>()->ClientSubtitle(Line.Key,Line.Value,Length);
			R.NextDialogue+=Length;
		}
		bool bFail = false;
		if (Done(PC, R, S, bFail))
		{
			if (bDialogue) if (ANCPlayerController* C=Cast<ANCPlayerController>(PC)) C->ClientEndStoryScene();
			++R.Step;
			R.bEntered = false;
			ANCSoundBank::Play(PC->GetPawn(), TEXT("Chime"), PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector, 0.6f);
		}
		else if (bFail)
		{
			Finish(PC, R, false, TEXT("cil mise nelze dokoncit"));
			Ended.Add(It.Key);
		}
	}
	for (const TWeakObjectPtr<APlayerController>& K : Ended)
	{
		Runs.Remove(K);
	}
}
