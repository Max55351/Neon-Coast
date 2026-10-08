#pragma warning(disable: 4456 4457 4458 4459)
#include "NCPoliceManager.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "NCCrowdManager.h"
#include "NCHealthComponent.h"
#include "NCPlayerState.h"
#include "NCPoliceOfficer.h"
#include "NCVehiclePawn.h"
#include "NCCraft.h"

ANCPoliceManager::ANCPoliceManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;                     // pure server logic; the units it spawns replicate
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANCPoliceManager::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickInterval(.5f);
	if (HasAuthority())
	{
		NCData::LoadPaths(DataFile, TEXT("lanes"), Lanes, 1000.f);
		NCData::LoadPaths(DataFile,TEXT("sidewalks"),PatrolWalkways,130.f);
	}
}

static bool InPoly(const TArray<FVector2D>& Poly, const FVector& P)
{
	bool bIn = false;
	for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
	{
		if (((Poly[i].Y > P.Y) != (Poly[j].Y > P.Y))
			&& (P.X < (Poly[j].X - Poly[i].X) * (P.Y - Poly[i].Y) / (Poly[j].Y - Poly[i].Y + 1e-6) + Poly[i].X))
		{
			bIn = !bIn;
		}
	}
	return bIn;
}

void ANCPoliceManager::ReportCrime(UWorld* W, AController* Criminal, int32 Stars, const FVector& Where, bool bNeedWitness,
	bool bMinimum, bool bPoliceOnly)
{
	if (!W || !Criminal || W->GetNetMode() == NM_Client)
	{
		return;
	}
	ANCPlayerState* PS = Criminal->GetPlayerState<ANCPlayerState>();
	if (!PS || !Criminal->IsPlayerController())
	{
		return;
	}
	bool bSeen = !bNeedWitness;
	ANCPoliceManager* PM = nullptr;
	for (TActorIterator<ANCPoliceManager> It(W); It; ++It)
	{
		PM = *It;
		break;
	}
	if (PM && PS->Wanted > 0 && Stars <= 1 && !bMinimum)
		if (const auto* Timers = PM->ResponseTimers.Find(Cast<APlayerController>(Criminal)); Timers && W->GetTimeSeconds() - Timers->Crime < .25) return;
	if (!bSeen && PM)
	{
		auto Witness = [&](const AActor* Observer, float Radius)
		{
			if (!IsValid(Observer) || FVector::DistSquared(Observer->GetActorLocation(), Where) > FMath::Square(Radius)) return false;
			FHitResult Hit;
			FCollisionQueryParams Query(TEXT("CrimeWitness"), false, Observer);
			Query.AddIgnoredActor(Criminal->GetPawn());
			return !W->LineTraceSingleByChannel(Hit, Observer->GetActorLocation()+FVector(0,0,100), Where+FVector(0,0,60), ECC_Visibility, Query);
		};
		for (const ANCVehiclePawn* C : PM->Cars)
		{
			if (Witness(C, 6000.f)) { bSeen = true; break; }
		}
		for (const ANCPoliceOfficer* O : PM->Officers)
		{
			if (IsValid(O) && !O->IsDead() && Witness(O, 5000.f)) { bSeen = true; break; }
		}
	}
	if (!bSeen && !bPoliceOnly)
	{
		// pedestrians who saw it call the cops
		for (TActorIterator<ANCCrowdManager> It(W); It && !bSeen; ++It)
		{
			bSeen = It->HasWitness(Where, Criminal->GetPawn());
		}
	}
	if (bSeen)
	{
		// Repeated bullets refresh the report, but cannot create five stars instantly.
		if (PM)
		{
			auto& Timers = PM->ResponseTimers.FindOrAdd(Cast<APlayerController>(Criminal));
			const double Now = W->GetTimeSeconds();
			if (!bMinimum && Now - Timers.Crime < (Stars <= 1 ? 3.0 : 1.5))
			{
				PS->UnseenTime = 0.f; PS->LastSeenLoc = Criminal->GetPawn() ? Criminal->GetPawn()->GetActorLocation() : Where;
				return;
			}
			Timers.Crime = Now;
		}
		// a shot fired when already wanted does not stack stars every bullet
		if (!bMinimum && PS->Wanted >= Stars && Stars <= 1)
		{
			PS->AddWanted(PS->Wanted, true);
			return;
		}
		PS->AddWanted(Stars, bMinimum);
	}
}

bool ANCPoliceManager::CanSee(const AActor* From, const APawn* Target) const
{
	FHitResult H;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(NCPoliceLos), false, From);
	Q.AddIgnoredActor(Target);
	if (const AActor* Parent = Target->GetAttachParentActor())
	{
		Q.AddIgnoredActor(Parent);
	}
	return !GetWorld()->LineTraceSingleByChannel(H, From->GetActorLocation() + FVector(0, 0, 150.f),
		Target->GetActorLocation() + FVector(0, 0, 60.f), ECC_Visibility, Q);
}

void ANCPoliceManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	Accum += DeltaSeconds;
	if (Accum < 0.5f)
	{
		return;
	}
	const float Dt = Accum;
	Accum = 0.f;
	for (auto It = ResponseTimers.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
	Cars.RemoveAll([](const ANCVehiclePawn* C) { return !IsValid(C); });
	Officers.RemoveAll([](const ANCPoliceOfficer* O) { return !IsValid(O); });
	Helis.RemoveAll([](const ANCCraftPawn* H) { return !IsValid(H); });
	UpdateAmbientPatrols();
	for (ANCCraftPawn* H : Helis)
	{
		// suspect lost / caught / dead / below 2 stars: the helicopter goes home
		const AController* HC = H->ChaseController.Get();
		const ANCPlayerState* TPS = HC ? HC->GetPlayerState<ANCPlayerState>() : nullptr;
		const UNCHealthComponent* THC = UNCHealthComponent::Of(HC ? HC->GetPawn() : nullptr);
		if (!TPS || TPS->Wanted < 3 || (THC && THC->bDead))
		{
			H->ChaseTarget = nullptr;
			H->ChaseController = nullptr;
			H->bPoliceSearching = false;
		}
	}
	TArray<APawn*> Suspects;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		UpdatePlayer(PC, Dt);
		const ANCPlayerState* PS = PC->GetPlayerState<ANCPlayerState>();
		if (PS && PS->Wanted > 0 && PC->GetPawn())
		{
			Suspects.Add(PC->GetPawn());
		}
		// the base garrison appears when somebody comes near the military base
		if (!bGuardsSpawned && BaseGuards.Num() > 0 && PC->GetPawn()
			&& FVector::Dist(PC->GetPawn()->GetActorLocation(), BaseGuards[0].GetLocation()) < 60000.f)
		{
			bGuardsSpawned = true;
			for (const FTransform& G : BaseGuards)
			{
				if (ANCPoliceOfficer* O = SpawnOfficer(G.GetLocation(), G.Rotator(), 2, nullptr))
				{
					O->bGuard = true;
				}
			}
		}
	}
	Cleanup(Suspects);
}

void ANCPoliceManager::UpdatePlayer(APlayerController* PC, float Dt)
{
	ANCPlayerState* PS = PC->GetPlayerState<ANCPlayerState>();
	APawn* P = PC->GetPawn();
	const UNCHealthComponent* HC = UNCHealthComponent::Of(P);
	if (!PS || !P || (HC && HC->bDead))
	{
		return;
	}
	const FVector Loc = P->GetActorLocation();
	for (const FNCZone& Z : Zones)
	{
		if (Z.Poly.Num() >= 3 && InPoly(Z.Poly, Loc))
		{
			PS->AddWanted(Z.Stars, true);
		}
	}
	const int32 W = PS->Wanted;
	auto& Timers = ResponseTimers.FindOrAdd(PC);
	const double Time = GetWorld()->GetTimeSeconds();
	if (W <= 0)
	{
		for (ANCVehiclePawn* C : Cars)
			if (auto* D = Cast<ANCPoliceDriver>(C->GetController()); D && D->PursuedController.Get() == PC) { D->Target = nullptr; D->bSearching = false; }
		for (ANCPoliceOfficer* O : Officers)
			if (!O->bGuard && O->PursuedController.Get() == PC) { O->Target = nullptr; O->bSearching = false; }
		return;
	}
	// does any unit see the suspect?
	bool bSeen = false;
	for (const ANCVehiclePawn* C : Cars)
	{
		if (C->Body.bWrecked) continue;
		const float D = FVector::DistSquared(C->GetActorLocation(), Loc);
		bSeen |= D < FMath::Square(6000.f) && CanSee(C, P);
		if (bSeen)
		{
			break;
		}
	}
	for (int32 i = 0; i < Officers.Num() && !bSeen; ++i)
	{
		const ANCPoliceOfficer* O = Officers[i];
		if (O->IsDead())
		{
			continue;
		}
		const float D = FVector::DistSquared(O->GetActorLocation(), Loc);
		bSeen |= D < FMath::Square(5000.f) && CanSee(O, P);
	}
	for (const ANCCraftPawn* H : Helis)
	{
		if (!bSeen && IsValid(H) && !H->bWrecked && FVector::Dist(H->GetActorLocation(), Loc) < 12000.f && CanSee(H, P))
		{
			bSeen = true;                                              // the helicopter keeps its searchlight on you
		}
	}
	if (bSeen)
	{
		PS->bWantedSeen = true;
		PS->LastSeenLoc = Loc;
		PS->UnseenTime = 0.f;
	}
	else
	{
		PS->bWantedSeen = false;
		PS->UnseenTime += Dt;
		const float Search = 2500.f + 1000.f * W;
		if ((PS->UnseenTime > 12.f + 6.f * W && FVector::DistSquared(Loc, PS->LastSeenLoc) > FMath::Square(Search)) || PS->UnseenTime > 30.f + 10.f * W)
		{
			// Search pressure fades one star at a time, not five at once.
			PS->Wanted = FMath::Max(0, W - 1);
			PS->UnseenTime = 0.f;
			if (PS->Wanted == 0) { PS->ClearWanted(); return; }
		}
	}
	// response: patrol cars chase the last known position; soldiers at 5 stars
	static const int32 WantCars[6] = {0, 1, 2, 3, 4, 4};
	int32 Have = 0;
	for (ANCVehiclePawn* C : Cars)
	{
		if (ANCPoliceDriver* D = Cast<ANCPoliceDriver>(C->GetController()); D && D->PursuedController.Get() == PC && !C->Body.bWrecked)
		{
			Have += 1;
			D->Target = bSeen ? P : nullptr;
			D->bSearching = !bSeen && !C->Tags.Contains(TEXT("NCDismounted"));
			D->SearchLocation = PS->LastSeenLoc;
			if (C->Tags.Contains(TEXT("NCDismounted"))) D->Target = nullptr;
		}
	}
	if ((bSeen || (Have == 0 && PS->UnseenTime < 3.f)) && Have < WantCars[FMath::Clamp(PS->Wanted,0,5)] && Cars.Num() < 12 && Time >= Timers.Car)
	{
		Timers.Car = Time + 6.f;
		SpawnCarNear(P, W);
	}
	// 3+ stars: a helicopter with a searchlight and a gunner
	bool bHasHeli = false;
	for (ANCCraftPawn* H : Helis)
	{
		if (IsValid(H) && !H->bWrecked && H->ChaseController.Get() == PC)
		{
			H->ChaseTarget = bSeen ? P : nullptr;
			H->bPoliceSearching = !bSeen;
			H->PoliceSearchLocation = PS->LastSeenLoc;
			bHasHeli = true;
		}
	}
	if (W >= 3 && bSeen && !bHasHeli && Helis.Num() < 2 && Time >= Timers.Heli)
	{
		Timers.Heli = Time + 20.f;
		if (ANCCraftManager* CM = ANCCraftManager::Get(GetWorld()))
		{
			if (ANCCraftPawn* H = CM->SpawnPoliceHeli(P))
			{
				H->ChaseController = PC;
				Helis.Add(H);
			}
		}
	}
	if (W >= 5 && bSeen && Officers.Num() < 16)
	{
		int32 Soldiers = 0;
		for (const ANCPoliceOfficer* O : Officers)
		{
			Soldiers += (O->Rank == 2 && !O->bGuard && !O->IsDead()) ? 1 : 0;
		}
		if (Soldiers < 4 && Lanes.Num() > 0)
		{
			const FNCPath& L = Lanes[FMath::RandRange(0, Lanes.Num() - 1)];
			FVector At, Dir;
			L.Eval(FMath::FRandRange(0.f, L.Length), At, Dir);
			if (FVector::Dist2D(At, Loc) > 8000.f && FVector::Dist2D(At, Loc) < 20000.f)
			{
				SpawnOfficer(At, Dir.Rotation(), 2, P);
			}
		}
	}
	if (bSeen) DismountNear(P);
	for (ANCPoliceOfficer* O : Officers)
	{
		if (!O->bGuard && O->PursuedController.Get() == PC && !O->IsDead())
		{
			O->Target = bSeen ? P : nullptr;
			O->bSearching = !bSeen;
			O->SearchLocation = PS->LastSeenLoc;
		}
	}
}

bool ANCPoliceManager::SpawnCarNear(APawn* Target, int32 Stars)
{
	if (!PoliceCarClass)
	{
		return false;
	}
	const auto* State = Target->GetPlayerState<ANCPlayerState>();
	const FVector Loc = State && !State->bWantedSeen ? State->LastSeenLoc : Target->GetActorLocation();
	FVector Best = FVector::ZeroVector, BestDir = FVector::ForwardVector;
	bool bFound = false;
	for (int32 Try = 0; Try < 240 && Lanes.Num() > 0 && !bFound; ++Try)
	{
		const FNCPath& L = Lanes[FMath::RandRange(0, Lanes.Num() - 1)];
		FVector At, Dir;
		L.Eval(FMath::FRandRange(0.f, L.Length), At, Dir);
		const float D = FVector::Dist2D(At, Loc);
		if (D > 12000.f && D < 30000.f && FMath::Abs(At.Z - Loc.Z) < 2000.f)
		{
			Best = At;
			BestDir = Dir;
			bFound = true;
		}
	}
	if (!bFound)
	{
		// off the road network: drop in behind the suspect
		Best = Loc - Target->GetActorForwardVector() * 9000.f;
		FHitResult H;
		if (GetWorld()->LineTraceSingleByChannel(H, Best + FVector(0, 0, 5000.f), Best - FVector(0, 0, 5000.f), ECC_Visibility))
		{
			Best = H.ImpactPoint;
		}
		BestDir = (Loc - Best).GetSafeNormal2D();
	}
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ANCVehiclePawn* V = GetWorld()->SpawnActor<ANCVehiclePawn>(PoliceCarClass, Best + FVector(0, 0, 90.f),
		FRotator(0.f, BestDir.Rotation().Yaw, 0.f), SP);
	if (!V)
	{
		return false;
	}
	V->SetSiren(true);
	ANCPoliceDriver* D = GetWorld()->SpawnActor<ANCPoliceDriver>(ANCPoliceDriver::StaticClass(), V->GetActorTransform(), SP);
	if (D)
	{
		D->Possess(V);
		D->Target = Target;
		D->PursuedController = Target->GetController();
		if (const auto* State = Target->GetPlayerState<ANCPlayerState>(); State && !State->bWantedSeen)
		{
			D->Target = nullptr; D->bSearching = true; D->SearchLocation = State->LastSeenLoc;
		}
	}
	V->Tags.Add(Stars >= 4 ? TEXT("NCSwat") : TEXT("NCPolice"));
	Cars.Add(V);
	return true;
}

ANCPoliceOfficer* ANCPoliceManager::SpawnOfficer(const FVector& Where, const FRotator& Rot, int32 Rank, APawn* Target)
{
	const FNCOfficerLook& L = Rank == 2 ? SoldierLook : (Rank == 1 ? SwatLook : PoliceLook);
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ANCPoliceOfficer* O = GetWorld()->SpawnActor<ANCPoliceOfficer>(ANCPoliceOfficer::StaticClass(), Where + FVector(0, 0, 100.f),
		FRotator(0.f, Rot.Yaw, 0.f), SP);
	if (O)
	{
		O->Init(Rank, L.Mesh ? L.Mesh.Get() : PoliceLook.Mesh.Get(), L.Walk ? L.Walk.Get() : PoliceLook.Walk.Get(),
			L.Idle ? L.Idle.Get() : PoliceLook.Idle.Get(), Target);
		Officers.Add(O);
	}
	return O;
}

void ANCPoliceManager::DismountNear(APawn* Target)
{
	const float TSpeed = Target->GetVelocity().Size();
	for (ANCVehiclePawn* C : Cars)
	{
		const auto* Pursuer = Cast<ANCPoliceDriver>(C->GetController());
		if (!Pursuer || Pursuer->PursuedController.Get() != Target->GetController() || Officers.Num() >= 16 || C->Tags.Contains(TEXT("NCDismounted")))
		{
			continue;
		}
		const float D = FVector::Dist(C->GetActorLocation(), Target->GetActorLocation());
		if (D < 2500.f && (TSpeed < 800.f || C->GetSpeedKmh() < 8.f) && C->GetSpeedKmh() < 25.f)
		{
			C->Tags.Add(TEXT("NCDismounted"));
			if (ANCPoliceDriver* Dr = Cast<ANCPoliceDriver>(C->GetController()))
			{
				Dr->Target = nullptr;              // park; the crew continues on foot
			}
			const int32 Rank = C->Tags.Contains(TEXT("NCSwat")) ? 1 : 0;
			const FVector R = C->GetActorRightVector();
			for (int32 s = -1; s <= 1; s += 2)
			{
				SpawnOfficer(C->GetActorLocation() + R * s * 240.f, C->GetActorRotation(), Rank, Target);
			}
		}
	}
}

void ANCPoliceManager::UpdateAmbientPatrols()
{
	TArray<APawn*> Players;
	for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It) if (auto* PC=It->Get(); PC && PC->GetPawn()) Players.Add(PC->GetPawn());
	int32 Count=0;
	for (ANCPoliceOfficer* Officer:Officers)
	{
		if (!IsValid(Officer) || !Officer->bAmbientPatrol || Officer->IsDead()) continue;
		bool Near=false;
		for (APawn* Player:Players)
		{
			const float Distance=FVector::DistSquared(Officer->GetActorLocation(),Player->GetActorLocation());
			Near|=Distance<FMath::Square(18000.f);
			const auto* PS=Player->GetPlayerState<ANCPlayerState>();
			if (PS && PS->Wanted>0 && Distance<FMath::Square(5000.f) && CanSee(Officer,Player))
			{ Officer->PursuedController=Player->GetController(); Officer->Target=Player; Officer->bSearching=false; }
		}
		if (!Near && !Officer->Target && !Officer->bSearching) { Officer->Destroy(); continue; }
		++Count;
	}
	const double Time=GetWorld()->GetTimeSeconds();
	if (Count>=4 || Players.IsEmpty() || PatrolWalkways.IsEmpty() || !PoliceLook.Mesh || Time<NextAmbientPatrol) return;
	NextAmbientPatrol=Time+15.f;
	for (int32 Try=0;Try<120;++Try)
	{
		const FNCPath& Path=PatrolWalkways[FMath::RandRange(0,PatrolWalkways.Num()-1)];
		if (Path.Length<1500.f) continue;
		const float S=FMath::FRandRange(0.f,Path.Length-1000.f);
		FVector At,Direction,End,Unused; Path.Eval(S,At,Direction); Path.Eval(FMath::Min(S+2500.f,Path.Length),End,Unused);
		bool Suitable=false;
		for (APawn* Player:Players)
		{
			const float Distance=FVector::Dist2D(At,Player->GetActorLocation());
			Suitable|=Distance>4500.f && Distance<12000.f && FMath::Abs(At.Z-Player->GetActorLocation().Z)<400.f
				&& FVector::DotProduct((At-Player->GetActorLocation()).GetSafeNormal2D(),Player->GetActorForwardVector())<.3f;
		}
		if (!Suitable) continue;
		if (auto* Officer=SpawnOfficer(At,Direction.Rotation(),0,nullptr))
		{
			Officer->bAmbientPatrol=true;
			const float Finish=FMath::Min(S+2500.f,Path.Length);
			for (float Sample=S;Sample<Finish;Sample+=300.f) { FVector Point,Dir; Path.Eval(Sample,Point,Dir); Officer->PatrolPoints.Add(Point); }
			Officer->PatrolPoints.Add(End);
			for (int32 i=Officer->PatrolPoints.Num()-2;i>0;--i) { const FVector Back=Officer->PatrolPoints[i]; Officer->PatrolPoints.Add(Back); }
			Officer->LastPatrolPosition=Officer->GetActorLocation();
		}
		break;
	}
}

void ANCPoliceManager::Cleanup(const TArray<APawn*>& Suspects)
{
	// Dispose of wrecked police cars away from every player even during a chase.
	// Otherwise old wrecks keep occupying the dispatch budget indefinitely.
	for (ANCVehiclePawn* C : Cars)
	{
		if (!IsValid(C) || !C->Body.bWrecked || C->Driver) continue;
		bool NearPlayer = false;
		for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			if (const auto* PC = It->Get(); PC && PC->GetPawn())
				NearPlayer |= FVector::DistSquared(C->GetActorLocation(), PC->GetPawn()->GetActorLocation()) < FMath::Square(10000.f);
		if (!NearPlayer)
		{
			if (AController* Controller = C->GetController()) { Controller->UnPossess(); Controller->Destroy(); }
			C->Destroy();
		}
	}
	// nobody wanted any more: units drive off / vanish once out of sight (a few seconds of calm first)
	if (Suspects.Num() > 0)
	{
		CalmTime = 0.f;
		return;
	}
	CalmTime += 0.5f;
	if (CalmTime < 12.f)
	{
		return;
	}
	FVector Cam = FVector::ZeroVector;
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->GetPawn())
		{
			Cam = PC->GetPawn()->GetActorLocation();
		}
	}
	for (ANCVehiclePawn* C : Cars)
	{
		if (C && !C->Driver && FVector::Dist(C->GetActorLocation(), Cam) > 8000.f)
		{
			if (AController* Ctl = C->GetController())
			{
				Ctl->UnPossess();
				Ctl->Destroy();
			}
			C->Destroy();
		}
	}
	for (ANCPoliceOfficer* O : Officers)
	{
		if (O && !O->bGuard && !O->bAmbientPatrol && FVector::Dist(O->GetActorLocation(), Cam) > 6000.f)
		{
			O->Destroy();
		}
		else if (O && !O->bGuard)
		{
			O->Target = nullptr;
		}
	}
}
