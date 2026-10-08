#pragma warning(disable: 4456 4457 4458 4459)
#include "NCCrowdManager.h"

#include "Animation/AnimSequence.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Misc/Crc.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NCAnimal.h"
#include "NCSound.h"
#include "NCWeaponComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"

static void RestorePedMaterials(UMeshComponent* Mesh)
{
	if (!Mesh) return;
	const TArray<FName> Names = Mesh->GetMaterialSlotNames();
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		UMaterialInterface* Existing = Mesh->GetMaterial(i);
		if (Existing && !Existing->GetPathName().Contains(TEXT("WorldGridMaterial"))) continue;
		FString Name = Names[i].ToString();
		if (Name.StartsWith(TEXT("M_NC"))) Name = TEXT("MI_") + Name.RightChop(2);
		if (!Name.StartsWith(TEXT("MI_NC"))) continue;
		const FString Path = TEXT("/Game/NeonCoast/Materials/Instances/") + Name + TEXT(".") + Name;
		if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Path)) Mesh->SetMaterial(i, Material);
	}
}

ANCCrowdManager::ANCCrowdManager()
{
	PrimaryActorTick.bCanEverTick = true;
	// deterministic walking: every machine simulates its own copy; only deaths are multicast
	bReplicates = true;
	bAlwaysRelevant = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ANCCrowdManager::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickInterval(1.f / 30.f);
	if (!NCData::LoadPaths(DataFile, TEXT("sidewalks"), Paths, WalkSpeed) || Types.Num() == 0)
	{
		SetActorTickEnabled(false);
		return;
	}
	for (const FNCPedestrianType& T : Types)
	{
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
		ISM->SetStaticMesh(T.FarMesh);
		RestorePedMaterials(ISM);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetupAttachment(RootComponent);
		ISM->RegisterComponent();
		Pools.Add(ISM);
	}
	for (int32 i = 0; i < MaxAnimated; ++i)
	{
		USkeletalMeshComponent* S = NewObject<USkeletalMeshComponent>(this);
		S->SetupAttachment(RootComponent);
		S->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		S->SetVisibility(false);
		S->RegisterComponent();
		Slots.Add(S);
		SlotOwner.Add(INDEX_NONE);
	}
	FRandomStream R(Seed);
	for (int32 p = 0; p < Paths.Num(); ++p)
	{
		const int32 N = FMath::Max(1, FMath::RoundToInt(Paths[p].Length / 10000.f * Density));
		for (int32 k = 0; k < N; ++k)
		{
			FPed P;
			P.Path = p;
			P.Offset = R.FRandRange(0.f, Paths[p].Length * 2.f);
			P.Speed = WalkSpeed * R.FRandRange(0.8f, 1.15f) * (R.FRand() < 0.5f ? 1.f : -1.f);
			P.Type = R.RandRange(0, Types.Num() - 1);
			P.Run = RunSpeed * R.FRandRange(0.85f, 1.15f);
			Evaluate(P, 0.0);
			P.Instance = Pools[P.Type]->AddInstance(FTransform(P.Dir.Rotation() + FRotator(0, StaticYawOffset, 0), P.Loc), true);
			Peds.Add(P);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[NeonCoast] crowd: %d paths, %d pedestrians"), Paths.Num(), Peds.Num());
}

void ANCCrowdManager::Place(const FNCPath& Path, double U, bool bReverse, FVector& OutLoc, FVector& OutDir) const
{
	// ping-pong along the sidewalk: U in [0, 2L) unfolds "there and back again"
	const double L = FMath::Max(1.0, static_cast<double>(Path.Length));
	double S = FMath::Fmod(U, 2.0 * L);
	if (S < 0.0)
	{
		S += 2.0 * L;
	}
	bool bBack = S > L;
	if (bBack)
	{
		S = 2.0 * L - S;
	}
	if (bReverse)
	{
		S = L - S;
		bBack = !bBack;
	}
	Path.Eval(static_cast<float>(S), OutLoc, OutDir);
	OutDir.Z = 0.f;
	OutDir = OutDir.GetSafeNormal();
	if (bBack)
	{
		OutDir = -OutDir;
	}
}

bool ANCCrowdManager::AddInteriorResidents(FName Place,const FBox& Bounds,const FVector& Counter)
{
	if (PopulatedInteriors.Contains(Place)) return true;
	if (Types.IsEmpty() || Pools.Num()!=Types.Num()) return false;
	FRandomStream Random(FCrc::StrCrc32(*Place.ToString())^Seed);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(NCInteriorResidents),true,this);
	FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	TArray<FVector> Starts;
	const FVector Center=Bounds.GetCenter(),Extent=Bounds.GetExtent();
	for (int32 Seat=0;Seat<3;++Seat)
	{
		for (int32 Try=0;Try<24;++Try)
		{
			const float X=FMath::Max(0.f,Extent.X-160.f),Y=FMath::Max(0.f,Extent.Y-160.f);
			FVector A(Center.X+Random.FRandRange(-X,X),Center.Y+Random.FRandRange(-Y,Y),Counter.Z-96.f);
			if (Seat==0 && Try==0) { A=Counter; A.Z-=96.f; A.X+=110.f; }
			bool TooClose=false; for (const FVector& Existing:Starts) TooClose|=FVector::DistSquared2D(A,Existing)<FMath::Square(180.f);
			if (TooClose) continue;
			FVector B=A+FVector(Random.FRandRange(-240.f,240.f),Random.FRandRange(-240.f,240.f),0);
			B.X=FMath::Clamp(B.X,Center.X-X,Center.X+X); B.Y=FMath::Clamp(B.Y,Center.Y-Y,Center.Y+Y);
			if (Seat==0) B=A+FVector(1.f,0,0);
			if (Seat!=0 && FVector::Dist2D(A,B)<120.f) continue;
			FHitResult Block;
			if (GetWorld()->SweepSingleByObjectType(Block,A+FVector(0,0,90),B+FVector(0,0,90),FQuat::Identity,Objects,FCollisionShape::MakeCapsule(35.f,82.f),Query)) continue;
			FNCPath Path; Path.Points={A,B}; Path.Build();
			FPed Ped; Ped.Path=Paths.Add(MoveTemp(Path)); Ped.Type=Random.RandRange(0,Types.Num()-1);
			Ped.Speed=Seat==0?0.f:Random.FRandRange(65.f,90.f); Ped.Run=180.f; Ped.bInterior=true;
			Evaluate(Ped,NowT());
			Ped.Instance=Pools[Ped.Type]->AddInstance(FTransform(Ped.Dir.Rotation()+FRotator(0,StaticYawOffset,0),Ped.Loc),true);
			auto* Body=NewObject<UCapsuleComponent>(this); Body->SetupAttachment(RootComponent); Body->InitCapsuleSize(35.f,85.f);
			Body->SetCollisionObjectType(ECC_Pawn); Body->SetCollisionResponseToAllChannels(ECR_Ignore);
			Body->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block); Body->SetCollisionResponseToChannel(ECC_Vehicle,ECR_Block);
			Body->SetCollisionEnabled(ECollisionEnabled::NoCollision); Body->RegisterComponent();
			Ped.InteriorBody=Body; InteriorBodies.Add(Body); Starts.Add(A); Peds.Add(Ped); break;
		}
	}
	PopulatedInteriors.Add(Place);
	return true;
}

double ANCCrowdManager::UAt(FPed& P, double T, int32& OutSign) const
{
	OutSign = 1;
	if (P.FleeEnd > 0.0)
	{
		if (T >= P.FleeEnd)
		{
			// the run is over: walk on from where it ended, in the direction it went
			// (the same spot seen from the other half of the ping-pong is 2L - U)
			double Ue = P.FleeU0 + P.FleeSign * P.Run * (P.FleeEnd - P.FleeStart);
			if (P.FleeSign < 0)
			{
				Ue = 2.0 * Paths[P.Path].Length - Ue;
			}
			P.Offset = Ue - P.FleeEnd * FMath::Abs(P.Speed);
			P.FleeEnd = 0.0;
		}
		else if (T >= P.FleeStart)
		{
			OutSign = P.FleeSign;
			return P.FleeU0 + P.FleeSign * P.Run * (T - P.FleeStart);
		}
	}
	return P.Offset + T * FMath::Abs(P.Speed);
}

void ANCCrowdManager::Evaluate(FPed& P, double T) const
{
	if (P.bInterior && FMath::Abs(P.Speed)<1.f) { Paths[P.Path].Eval(0.f,P.Loc,P.Dir); return; }
	int32 Sign = 1;
	const double U = UAt(P, T, Sign);
	Place(Paths[P.Path], U, P.Speed < 0.f, P.Loc, P.Dir);
	if (Sign < 0)
	{
		P.Dir = -P.Dir;
	}
}

void ANCCrowdManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TArray<bool> DirtyPools;
	DirtyPools.Init(false, Pools.Num());
	for (int32 i = FallingBodies.Num() - 1; i >= 0; --i)
	{
		auto& Fall = FallingBodies[i];
		if (!Fall.Mesh.IsValid()) { FallingBodies.RemoveAtSwap(i); continue; }
		Fall.Age += DeltaSeconds;
		const float Alpha = FMath::Clamp(Fall.Age / 0.6f, 0.f, 1.f);
		Fall.Mesh->SetWorldLocationAndRotation(FMath::Lerp(Fall.Start, Fall.End, Alpha), FRotator(0, Fall.Yaw, 90.f * Alpha));
		if (Alpha >= 1.f) FallingBodies.RemoveAtSwap(i);
	}
	const AGameStateBase* GS = GetWorld()->GetGameState();
	const double T = GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	FVector Cam = FVector::ZeroVector;
	bool bHasCam = false;
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			Cam = PC->PlayerCameraManager->GetCameraLocation();
			bHasCam = true;
		}
	}
	const float Far2 = FarRadius * FarRadius;
	const float Near2 = NearRadius * NearRadius;
	ReassignTimer -= DeltaSeconds;
	const bool bReassign = ReassignTimer <= 0.f;
	if (bReassign)
	{
		ReassignTimer = 0.5f;
	}
	for (int32 i = 0; i < Peds.Num(); ++i)
	{
		FPed& P = Peds[i];
		if (P.bDead)
		{
			continue;
		}
		const float D2 = bHasCam ? FVector::DistSquared(Cam, P.Loc) : 0.f;
		if (D2 > Far2 && P.Slot == INDEX_NONE)
		{
			continue;
		}
		Evaluate(P, T);
		if (auto* Body=P.InteriorBody.Get())
		{
			Body->SetWorldLocation(P.Loc+FVector(0,0,90));
			Body->SetCollisionEnabled(D2<FMath::Square(3000.f)?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
		}
		// hand an animated skeletal slot to pedestrians close to the camera
		if (bReassign)
		{
			if (D2 < Near2 && P.Slot == INDEX_NONE)
			{
				const int32 Free = SlotOwner.IndexOfByKey(INDEX_NONE);
				if (Free != INDEX_NONE && Types[P.Type].SkeletalMesh)
				{
					SlotOwner[Free] = i;
					P.Slot = Free;
					USkeletalMeshComponent* S = Slots[Free];
					S->SetSkeletalMeshAsset(Types[P.Type].SkeletalMesh);
					S->EmptyOverrideMaterials();
					RestorePedMaterials(S);
						P.bRunAnim = !Fleeing(P, T);          // forces the animation pick below
						S->SetVisibility(true);
				}
			}
			else if (D2 > Near2 * 1.2f && P.Slot != INDEX_NONE)
			{
				Slots[P.Slot]->SetVisibility(false);
				SlotOwner[P.Slot] = INDEX_NONE;
				P.Slot = INDEX_NONE;
			}
		}
		const FRotator Face = P.Dir.Rotation();
		if (P.Slot != INDEX_NONE && P.bRunAnim != Fleeing(P, T))
		{
			// walk <-> run
			P.bRunAnim = Fleeing(P, T);
			const FNCPedestrianType& Ty = Types[P.Type];
			USkeletalMeshComponent* S = Slots[P.Slot];
			if (P.bRunAnim && Ty.RunAnim)
			{
				S->PlayAnimation(Ty.RunAnim, true);
				S->SetPlayRate(P.Run / RunSpeed);
			}
			else if (Ty.WalkAnim)
			{
				S->PlayAnimation(Ty.WalkAnim, true);
				S->SetPlayRate((P.bRunAnim ? P.Run : FMath::Abs(P.Speed)) / WalkSpeed);
			}
		}
		if (P.Slot != INDEX_NONE)
		{
			Slots[P.Slot]->SetWorldLocationAndRotation(P.Loc, Face);
			Pools[P.Type]->UpdateInstanceTransform(P.Instance, FTransform(FQuat::Identity, P.Loc, FVector::ZeroVector), true, false, true);
		}
		else
		{
			Pools[P.Type]->UpdateInstanceTransform(P.Instance, FTransform(Face + FRotator(0, StaticYawOffset, 0), P.Loc), true, false, true);
		}
		DirtyPools[P.Type] = true;
	}
	for (int32 i = 0; i < Pools.Num(); ++i)
	{
		if (DirtyPools[i]) Pools[i]->MarkRenderStateDirty();
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateBarks(DeltaSeconds, T);
	}
}

bool ANCCrowdManager::HasWitness(const FVector& Where, AActor* Ignore) const
{
	int32 Checks = 0;
	FCollisionQueryParams Query(TEXT("PedCrimeWitness"), false, this);
	if (Ignore) Query.AddIgnoredActor(Ignore);
	TArray<int32> Nearby;
	for (int32 i=0;i<Peds.Num();++i) if (!Peds[i].bDead && FVector::DistSquared(Peds[i].Loc,Where)<FMath::Square(3500.f)) Nearby.Add(i);
	Nearby.Sort([&](int32 A,int32 B) { return FVector::DistSquared(Peds[A].Loc,Where)<FVector::DistSquared(Peds[B].Loc,Where); });
	for (int32 Index:Nearby)
	{
		const FPed& Ped=Peds[Index];
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit, Ped.Loc + FVector(0,0,150), Where + FVector(0,0,60), ECC_Visibility, Query)) return true;
		if (++Checks >= 8) return false; // bounded sight queries during rapid fire
	}
	return false;
}

void ANCCrowdManager::UpdateBarks(float Dt, double T)
{
	NextBark -= Dt;
	if (NextBark > 0.f)
	{
		return;
	}
	NextBark = FMath::FRandRange(4.f, 9.f);
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const APawn* Me = PC ? PC->GetPawn() : nullptr;
	if (!Me)
	{
		return;
	}
	int32 Best = INDEX_NONE;
	float BestD = 900.f;
	for (int32 i = 0; i < Peds.Num(); ++i)
	{
		if (!Peds[i].bDead && Peds[i].Slot != INDEX_NONE)
		{
			const float D = FVector::Dist(Peds[i].Loc, Me->GetActorLocation());
			if (D < BestD)
			{
				BestD = D;
				Best = i;
			}
		}
	}
	if (Best == INDEX_NONE)
	{
		return;
	}
	static const TCHAR* Calm[] = {TEXT("Hezky den!"), TEXT("Nevidel jste mi psa?"), TEXT("To horko je neuveritelny."),
		TEXT("Pozor, kam slapes!"), TEXT("Neon Coast, mesto snu..."), TEXT("Slysel jsi o tom kasinu?"), TEXT("Uz zase zavreli plaz."),
		TEXT("Moje mama by te milovala."), TEXT("Nemas drobny?"), TEXT("Dneska je v Club Vice party!"), TEXT("Taxi! ...ah, zase nic."),
		TEXT("Hej, ja te znam! Nebo ne?")};
	static const TCHAR* Scared[] = {TEXT("POMOC!"), TEXT("Nestrilej!"), TEXT("Volejte policii!"), TEXT("Utikejte!"), TEXT("Mami!")};
	static const TCHAR* Armed[] = {TEXT("Schovej tu zbran, clovece!"), TEXT("Ty jsi blazen?!"), TEXT("Nic jsem nevidel..."),
		TEXT("Zavolam policajty!")};
	static const TCHAR* Interior[] = {TEXT("Dobry den, s cim vam pomuzu?"),TEXT("Hned se vam budu venovat."),TEXT("Dneska je tu plno."),TEXT("Jsem dalsi na rade?"),TEXT("Mate tohle i v cerne?"),TEXT("Nechame si to zabalit.")};
	const bool bScared = Fleeing(Peds[Best], T);
	const UNCWeaponComponent* WC = UNCWeaponComponent::Of(Me);
	const bool bArmed = WC && WC->CurrentDef().Kind != ENCWeaponKind::Melee;
	if (bScared)
	{
		BarkText = Scared[FMath::RandRange(0, UE_ARRAY_COUNT(Scared) - 1)];
	}
	else if (bArmed)
	{
		BarkText = Armed[FMath::RandRange(0, UE_ARRAY_COUNT(Armed) - 1)];
	}
	else
	{
		BarkText = Peds[Best].bInterior ? Interior[FMath::RandRange(0,UE_ARRAY_COUNT(Interior)-1)] : Calm[FMath::RandRange(0, UE_ARRAY_COUNT(Calm) - 1)];
	}
	BarkPed = Best;
	BarkUntil = GetWorld()->GetTimeSeconds() + 3.0;
	static const TCHAR* Voices[3] = {TEXT("Voice_A"), TEXT("Voice_B"), TEXT("Voice_C")};
	ANCSoundBank::Play(this, Voices[Peds[Best].Type % 3], Peds[Best].Loc + FVector(0, 0, 160.f), bScared ? 1.f : 0.6f,
		bScared ? 1.3f : FMath::FRandRange(0.9f, 1.1f));
}

bool ANCCrowdManager::GetBark(FVector& OutLoc, FString& OutText) const
{
	if (!Peds.IsValidIndex(BarkPed) || Peds[BarkPed].bDead || GetWorld()->GetTimeSeconds() > BarkUntil)
	{
		return false;
	}
	OutLoc = Peds[BarkPed].Loc + FVector(0, 0, 205.f);
	OutText = BarkText;
	return true;
}

// ------------------------------------------------------------------ hits
double ANCCrowdManager::NowT() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

bool ANCCrowdManager::TraceRay(const FVector& A, const FVector& B, int32& OutPed, float& OutDist) const
{
	const FVector D = B - A;
	const float L = D.Size();
	OutPed = INDEX_NONE;
	OutDist = L;
	if (L < 1.f)
	{
		return false;
	}
	const FVector U = D / L;
	const double T = NowT();
	FBox Box(ForceInit);
	Box += A;
	Box += B;
	Box = Box.ExpandBy(200.f);
	for (int32 i = 0; i < Peds.Num(); ++i)
	{
		if (Peds[i].bDead || !Box.IsInsideXY(Peds[i].Loc))
		{
			continue;
		}
		FPed P = Peds[i];
		Evaluate(P, T);
		// body = vertical capsule 0..180 cm above the walk path, radius 40 cm
		const FVector C = P.Loc + FVector(0, 0, 90.f);
		const float S = FMath::Clamp(static_cast<float>(FVector::DotProduct(C - A, U)), 0.f, L);
		const FVector Off = C - (A + U * S);
		if (FMath::Abs(Off.Z) < 95.f && Off.Size2D() < 40.f && S < OutDist)
		{
			OutDist = S;
			OutPed = i;
		}
	}
	return OutPed != INDEX_NONE;
}

TArray<int32> ANCCrowdManager::PedsInRadius(const FVector& C, float R) const
{
	TArray<int32> Out;
	const double T = NowT();
	for (int32 i = 0; i < Peds.Num(); ++i)
	{
		if (Peds[i].bDead || FVector::DistSquared2D(Peds[i].Loc, C) > FMath::Square(R + 3000.f))
		{
			continue;
		}
		FPed P = Peds[i];
		Evaluate(P, T);
		if (FVector::Dist(P.Loc + FVector(0, 0, 90.f), C) < R)
		{
			Out.Add(i);
		}
	}
	return Out;
}

void ANCCrowdManager::KillPed(int32 Index, const FVector& Impulse)
{
	if (HasAuthority() && Peds.IsValidIndex(Index) && !Peds[Index].bDead)
	{
		const FVector Where = PedLocation(Index);
		MulticastKillPed(Index, Impulse);
		PanicAt(GetWorld(), Where, 3500.f);
	}
}

// ------------------------------------------------------------------ panic
void ANCCrowdManager::PanicAt(UWorld* W, const FVector& Where, float Radius)
{
	if (!W || W->GetNetMode() == NM_Client)
	{
		return;
	}
	ANCAnimalManager::Scare(W, Where, Radius);
	for (TActorIterator<ANCCrowdManager> It(W); It; ++It)
	{
		ANCCrowdManager* CM = *It;
		const double T = CM->NowT();
		// throttle: one panic per ~12 m and 2.5 s is plenty (automatic fire calls this per bullet)
		CM->RecentPanics.RemoveAll([T](const FVector4& E) { return T - E.W > 2.5; });
		for (const FVector4& E : CM->RecentPanics)
		{
			if (FVector::DistSquared(FVector(E.X, E.Y, E.Z), Where) < FMath::Square(1200.f))
			{
				return;
			}
		}
		CM->RecentPanics.Add(FVector4(Where.X, Where.Y, Where.Z, T));
		CM->MulticastPanic(Where, Radius, T);
	}
}

void ANCCrowdManager::MulticastPanic_Implementation(FVector Where, float Radius, double T0)
{
	const float R2 = Radius * Radius;
	for (int32 i = 0; i < Peds.Num(); ++i)
	{
		FPed& P = Peds[i];
		if (P.bDead)
		{
			continue;
		}
		// exact position at T0 (every machine computes the same thing, however late the RPC arrives)
		int32 Sign = 1;
		const double U0 = UAt(P, T0, Sign);
		FVector Loc, Dir;
		Place(Paths[P.Path], U0, P.Speed < 0.f, Loc, Dir);
		if (FVector::DistSquared(Loc, Where) > R2)
		{
			continue;
		}
		// run whichever way along the sidewalk takes us away from the danger
		FVector A, B, Tmp;
		Place(Paths[P.Path], U0 + 150.0, P.Speed < 0.f, A, Tmp);
		Place(Paths[P.Path], U0 - 150.0, P.Speed < 0.f, B, Tmp);
		P.FleeSign = FVector::DistSquared2D(A, Where) >= FVector::DistSquared2D(B, Where) ? 1 : -1;
		P.FleeU0 = U0;
		P.FleeStart = T0;
		// a little variety (deterministic per pedestrian)
		P.FleeEnd = T0 + FleeSeconds * (0.75 + 0.5 * FMath::Frac(FMath::Sin(static_cast<double>(i) * 12.9898) * 43758.5453));
	}
}

bool ANCCrowdManager::IsPanicking(int32 Index) const
{
	return Peds.IsValidIndex(Index) && Fleeing(Peds[Index], NowT());
}

void ANCCrowdManager::MulticastKillPed_Implementation(int32 Index, FVector Impulse)
{
	if (!Peds.IsValidIndex(Index) || Peds[Index].bDead)
	{
		return;
	}
	FPed& P = Peds[Index];
	Evaluate(P, NowT());
	P.bDead = true;
	if (auto* Body=P.InteriorBody.Get()) Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (P.Slot != INDEX_NONE)
	{
		Slots[P.Slot]->SetVisibility(false);
		SlotOwner[P.Slot] = INDEX_NONE;
		P.Slot = INDEX_NONE;
	}
	Pools[P.Type]->UpdateInstanceTransform(P.Instance, FTransform(FQuat::Identity, P.Loc, FVector::ZeroVector), true, true, true);
	USkeletalMesh* SK = Types[P.Type].SkeletalMesh;
	if (!SK)
	{
		return;
	}
	USkeletalMeshComponent* B = NewObject<USkeletalMeshComponent>(this);
	B->SetSkeletalMeshAsset(SK);
	RestorePedMaterials(B);
	B->SetWorldLocationAndRotation(P.Loc, P.Dir.Rotation());
	B->RegisterComponent();
	if (B->GetPhysicsAsset())
	{
		B->SetCollisionProfileName(TEXT("Ragdoll"));
		B->SetSimulatePhysics(true);
		B->AddImpulse(Impulse, NAME_None, true);
	}
	else
	{
		// Imported crowd meshes may have no ragdoll asset. Still visibly fall,
		// lie on the actual pavement and remain as a corpse instead of standing.
		FHitResult Ground;
		FCollisionQueryParams Query(NAME_None, false, this);
		FVector End = P.Loc + Impulse.GetSafeNormal2D() * 65.f;
		if (GetWorld()->LineTraceSingleByChannel(Ground, End + FVector(0,0,200), End - FVector(0,0,1000), ECC_WorldStatic, Query)) End.Z = Ground.ImpactPoint.Z;
		End.Z += 25.f;
		FallingBodies.Add({B, P.Loc, End, static_cast<float>(P.Dir.Rotation().Yaw), 0.f});
	}
	Bodies.Add(B);
	if (Bodies.Num() > MaxBodies)
	{
		if (Bodies[0])
		{
			Bodies[0]->DestroyComponent();
		}
		Bodies.RemoveAt(0);
	}
}
