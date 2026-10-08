#pragma warning(disable: 4456 4457 4458 4459)
#include "NCTrafficManager.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "NCVehiclePawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static bool IsClosedTrafficLane(const FNCPath& Lane)
{
	return Lane.Points.Num()>2 && FVector::DistSquared(Lane.Points[0],Lane.Points.Last())<FMath::Square(500.f);
}

ANCTrafficManager::ANCTrafficManager()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

double ANCTrafficManager::Now() const
{
	const AGameStateBase* GS = GetWorld()->GetGameState();
	return GS ? GS->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

void ANCTrafficManager::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickInterval(1.f / 30.f);
	if (!NCData::LoadPaths(DataFile, TEXT("lanes"), Lanes, 1400.f) || Types.Num() == 0)
	{
		SetActorTickEnabled(false);
		return;
	}
	for (const FNCTrafficType& T : Types)
	{
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(this);
		// These imported cars contain translucent glass, unsupported by Nanite.
		ISM->bDisallowNanite = true;
		ISM->SetStaticMesh(T.Mesh);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetCastShadow(true);
		ISM->SetupAttachment(RootComponent);
		ISM->RegisterComponent();
		Pools.Add(ISM);
	}
	// deterministic fleet: same seed + same data on every machine
	FRandomStream R(Seed);
	float TotalW = 0.f;
	for (const FNCTrafficType& T : Types)
	{
		TotalW += T.Weight;
	}
	for (int32 L = 0; L < Lanes.Num(); ++L)
	{
		const FNCPath& P = Lanes[L];
		const int32 N = FMath::Max(1, FMath::RoundToInt(P.Length / 100000.f * Density));
		const float Spacing = P.Length / N;
		for (int32 k = 0; k < N; ++k)
		{
			float Pick = R.FRand() * TotalW;
			int32 Ti = 0;
			for (; Ti < Types.Num() - 1; ++Ti)
			{
				Pick -= Types[Ti].Weight;
				if (Pick <= 0.f)
				{
					break;
				}
			}
			FCar C;
			C.Lane = L;
			C.Offset = k * Spacing + R.FRandRange(0.f, Spacing * 0.3f);
			C.Type = Ti;
			C.Last = CarTransform(C, 0.0);
			C.Instance = Pools[Ti]->AddInstance(C.Last, true);
			Cars.Add(C);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[NeonCoast] traffic: %d lanes, %d cars"), Lanes.Num(), Cars.Num());
	CollisionBodies.SetNum(Cars.Num());
	LaneCars.SetNum(Lanes.Num());
	for (int32 i = 0; i < Cars.Num(); ++i) LaneCars[Cars[i].Lane].Add(i);
	SetupSignals();
}

uint8 ANCTrafficManager::SignalState(bool bInitiallyRed, double Time)
{
	// Opposing approaches share a phase: 20 s green, 3 s amber, 2 s all-red.
	const double Phase = FMath::Fmod(Time + (bInitiallyRed ? 25.0 : 0.0), 50.0);
	return Phase < 20.0 ? 2 : (Phase < 23.0 ? 1 : 0);
}

void ANCTrafficManager::SetupSignals()
{
	StopLines.SetNum(Lanes.Num());
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("NeonCoast/Data/placements.json"))) ||
		!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()) return;
	const TSharedPtr<FJsonObject>* Placements = nullptr;
	if (!Root->TryGetObjectField(TEXT("placements"), Placements)) return;
	for (bool InitiallyRed : {false, true})
	{
		const TArray<TSharedPtr<FJsonValue>>* Poles = nullptr;
		if (!(*Placements)->TryGetArrayField(InitiallyRed ? TEXT("NC2_TrafficLight_Red") : TEXT("NC2_TrafficLight_Green"), Poles)) continue;
		for (const auto& Pole : *Poles)
		{
			const auto& Values = Pole->AsArray();
			if (Values.Num() < 4) continue;
			const FVector Position = NCData::ToUE(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber());
			// Exported heads face -Y; convert Blender yaw and handedness to UE.
			const FVector Facing = FRotator(0.f, 90.f - Values[3]->AsNumber(), 0.f).Vector();
			int32 BestLane = INDEX_NONE;
			float BestDistance = 1000.f, StopDistance = 0.f;
			for (int32 LaneIndex = 0; LaneIndex < Lanes.Num(); ++LaneIndex)
			{
				const FNCPath& Lane = Lanes[LaneIndex];
				for (int32 j = 1; j < Lane.Points.Num(); ++j)
				{
					const FVector A = Lane.Points[j - 1], B = Lane.Points[j];
					const FVector Direction = (B - A).GetSafeNormal2D();
					if (FVector::DotProduct(Direction, Facing) > -0.7f) continue;
					const FVector Closest = FMath::ClosestPointOnSegment(Position, A, B);
					const float Distance = FVector::Dist2D(Position, Closest);
					if (Distance < BestDistance && FMath::Abs(Position.Z - Closest.Z) < 350.f)
					{
						BestDistance = Distance;
						BestLane = LaneIndex;
						StopDistance = FMath::Max(0.f, Lane.Dist[j - 1] + FVector::Dist(A, Closest) - 250.f);
					}
				}
			}
			if (BestLane != INDEX_NONE) StopLines[BestLane].Add({StopDistance, InitiallyRed});
		}
	}
	// Change the lamps of the existing mesh components, including instanced poles.
	// No extra actors or duplicate geometry are needed for the visual phases.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		TArray<UStaticMeshComponent*> Components;
		It->GetComponents(Components);
		for (UStaticMeshComponent* Component : Components)
		{
			if (!Component->GetStaticMesh()) continue;
			const FString MeshName = Component->GetStaticMesh()->GetName();
			if (!MeshName.Contains(TEXT("TrafficLight_"))) continue;
			const bool InitiallyRed = MeshName.Contains(TEXT("_Red"));
			const TArray<FName> Slots = Component->GetMaterialSlotNames();
			for (int32 i = 0; i < Slots.Num(); ++i)
			{
				const FString Slot = Slots[i].ToString();
				int32 Lamp = Slot.Contains(TEXT("Signal_Red")) ? 0 : (Slot.Contains(TEXT("Signal_Amber")) ? 1 : (Slot.Contains(TEXT("Signal_Green")) ? 2 : -1));
				if (Lamp < 0) continue;
				if (UMaterialInstanceDynamic* Material = Component->CreateAndSetMaterialInstanceDynamic(i))
				{
					SignalVisuals.Add({SignalMaterials.Add(Material), InitiallyRed, static_cast<uint8>(Lamp)});
				}
			}
		}
	}
	UpdateSignals(Now());
}

void ANCTrafficManager::UpdateSignals(double Time)
{
	const int32 Phase = static_cast<int32>(FMath::Fmod(Time, 50.0));
	if (Phase == LastSignalPhase) return;
	LastSignalPhase = Phase;
	const FLinearColor Colors[] = {FLinearColor(1.f, 0.01f, 0.f), FLinearColor(1.f, 0.5f, 0.f), FLinearColor(0.f, 1.f, 0.05f)};
	for (const FSignalVisual& Visual : SignalVisuals)
	{
		UMaterialInstanceDynamic* Material = SignalMaterials[Visual.Material];
		const bool Lit = Visual.Lamp == SignalState(Visual.bInitiallyRed, Time);
		Material->SetVectorParameterValue(TEXT("EmissiveColor"), Lit ? Colors[Visual.Lamp] : FLinearColor::Black);
		Material->SetScalarParameterValue(TEXT("EmissiveStrength"), Lit ? 12.f : 0.f);
	}
}

FTransform ANCTrafficManager::CarTransform(const FCar& C, double T) const
{
	const FNCPath& P = Lanes[C.Lane];
	const float S = FMath::Fmod(C.Offset, P.Length);
	FVector Loc, Dir;
	P.Eval(S, Loc, Dir);
	FRotator Rot = Dir.Rotation();
	Rot.Yaw += MeshYawOffset;
	// static meshes were exported facing -Y: pitch must follow the slope around the lateral axis
	const float Pitch = Rot.Pitch;
	Rot.Pitch = 0.f;
	Rot.Roll = -Pitch;
	return FTransform(Rot, Loc);
}

FVector ANCTrafficManager::RoadPosition(const FVector& Position) const
{
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(NCTrafficSurface),true,this);
	FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	if (GetWorld()->LineTraceSingleByObjectType(Hit,Position+FVector(0,0,150.f),Position-FVector(0,0,150.f),Objects,Query)
		&& Hit.ImpactNormal.Z>.65f)
	{
		const auto* Mesh=Cast<UStaticMeshComponent>(Hit.GetComponent());
		const FString Name=Mesh && Mesh->GetStaticMesh()?Mesh->GetStaticMesh()->GetName():FString();
		// Do not snap to parked cars, crates, roof tops or a different overpass level.
		if (Name.StartsWith(TEXT("SM_NC_Road")) || Name.StartsWith(TEXT("SM_NC_Terrain")) || Name.Contains(TEXT("Bridge")))
			return FVector(Position.X,Position.Y,Hit.ImpactPoint.Z+3.f);
	}
	return Position;
}

void ANCTrafficManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Every vehicle reasons from the same frame, independent of fleet iteration order.
	const TArray<FCar> Previous=Cars;
	TArray<bool> DirtyPools;
	DirtyPools.Init(false, Pools.Num());
	// Spatial lookup replaces the fleet-wide intersection scan for each car.
	TMap<FIntPoint, TArray<int32>> Grid;
	for (int32 i = 0; i < Cars.Num(); ++i)
		if (!Cars[i].bRemoved)
		{
			const FVector L = Cars[i].Last.GetLocation();
			Grid.FindOrAdd(FIntPoint(FMath::FloorToInt(L.X / 4000.f), FMath::FloorToInt(L.Y / 4000.f))).Add(i);
		}
	TArray<int32> NextCars;
	NextCars.Init(INDEX_NONE, Cars.Num());
	for (const TArray<int32>& OnLane : LaneCars)
	{
		TArray<int32> Ordered;
		for (int32 i : OnLane) if (Cars.IsValidIndex(i) && !Cars[i].bRemoved) Ordered.Add(i);
		Ordered.Sort([&](int32 A, int32 B) { return Cars[A].Offset < Cars[B].Offset; });
		if (Ordered.Num() > 1)
		{
			for (int32 i = 0; i+1 < Ordered.Num(); ++i) NextCars[Ordered[i]] = Ordered[i+1];
			if (IsClosedTrafficLane(Lanes[Cars[Ordered.Last()].Lane])) NextCars[Ordered.Last()]=Ordered[0];
		}
	}
	const double T = Now();
	UpdateSignals(T);
	TArray<FVector> Viewers;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			if (PC->IsLocalController() && PC->PlayerCameraManager)
			{
				Viewers.Add(PC->PlayerCameraManager->GetCameraLocation());
			}
			// Physics must follow the controlled pawn, not just the rendering camera.
			// Headless servers and camera transitions may have no valid local camera.
			if (const APawn* Pawn = PC->GetPawn()) Viewers.Add(Pawn->GetActorLocation());
		}
	}
	const float R2 = UpdateRadius * UpdateRadius;
	for (int32 CarIndex = 0; CarIndex < Cars.Num(); ++CarIndex)
	{
		FCar& C = Cars[CarIndex];
		if (C.bRemoved)
		{
			continue;
		}
		bool bNear = Viewers.Num() == 0;
		for (const FVector& V : Viewers)
		{
			if (FVector::DistSquared(V, C.Last.GetLocation()) < R2)
			{
				bNear = true;
				break;
			}
		}
		if (!bNear)
		{
			if (CollisionBodies[CarIndex]) CollisionBodies[CarIndex]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			continue;
		}
		const FNCPath& Lane = Lanes[C.Lane];
		const bool ClosedLane=IsClosedTrafficLane(Lane);
		bool bPhysical = Viewers.IsEmpty();
		for (const FVector& Viewer : Viewers) bPhysical |= FVector::DistSquared(Viewer, C.Last.GetLocation()) < FMath::Square(10000.f);
		C.UpdateAccum += DeltaSeconds;
		if (!bPhysical && C.UpdateAccum < .2f) continue;
		const float SimDt = FMath::Min(C.UpdateAccum,.25f);
		C.UpdateAccum = 0.f;
		FVector Position, Forward;
		Lane.Eval(C.Offset, Position, Forward);
		if (bPhysical) Position=RoadPosition(Position);
		float TargetSpeed = Lane.Speed;
		FVector Ahead, AheadDirection;
		Lane.Eval(FMath::Min(C.Offset+1000.f,Lane.Length),Ahead,AheadDirection);
		const float TurnAngle=FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward.GetSafeNormal2D(),AheadDirection.GetSafeNormal2D()),-1.f,1.f));
		if (TurnAngle>.03f) TargetSpeed=FMath::Min(TargetSpeed,FMath::Sqrt(300.f*1000.f/TurnAngle));
		const float HalfLength = Types[C.Type].Length * 0.5f;
		const float EndOffset=FMath::Max(0.f,Lane.Length-HalfLength-100.f);
		if (!ClosedLane) TargetSpeed=FMath::Min(TargetSpeed,FMath::Sqrt(1200.f*FMath::Max(0.f,EndOffset-C.Offset)));
		const float SafeDistance = 200.f + C.Speed * 0.8f + C.Speed * C.Speed / 1200.f;
		float AvailableDistance = SafeDistance;
		if (StopLines.IsValidIndex(C.Lane))
		{
			for (const FStopLine& Stop : StopLines[C.Lane])
			{
				const uint8 State = SignalState(Stop.bInitiallyRed, T);
				const float Distance = (ClosedLane?FMath::Fmod(Stop.Distance-C.Offset+Lane.Length,Lane.Length):Stop.Distance-C.Offset)-HalfLength;
				if (Distance < 0.f || Distance > SafeDistance) continue;
				if (State==2)
				{
					// Green is not permission to block the intersection behind a queue.
					const int32 NextIndex=NextCars[CarIndex];
					if (NextIndex==INDEX_NONE) continue;
					const FCar& Next=Previous[NextIndex];
					const float Rear=(ClosedLane?FMath::Fmod(Next.Offset-C.Offset+Lane.Length,Lane.Length):Next.Offset-C.Offset)-Types[Next.Type].Length*.5f;
					if (Rear<Distance+HalfLength || Rear>Distance+HalfLength+Types[C.Type].Length+700.f || Next.Speed>300.f) continue;
				}
				// On amber, clear the junction if stopping would require emergency braking.
				if (State == 1 && Distance < C.Speed * C.Speed / 2000.f) continue;
				AvailableDistance = FMath::Min(AvailableDistance, FMath::Max(0.f, Distance - 30.f));
				TargetSpeed = FMath::Min(TargetSpeed, FMath::Max(0.f, (Distance - 30.f) / 1.2f));
			}
		}
		// Queue behind the next car on this lane; no time-based teleporting through it.
		if (const int32 NextIndex = NextCars[CarIndex]; NextIndex != INDEX_NONE)
		{
			const FCar& Next = Previous[NextIndex];
			const float Gap = (ClosedLane?FMath::Fmod(Next.Offset-C.Offset+Lane.Length,Lane.Length):Next.Offset-C.Offset)-HalfLength-Types[Next.Type].Length*.5f;
			if (Gap < SafeDistance)
			{
				TargetSpeed = FMath::Min(TargetSpeed, FMath::Max(0.f, (Gap - 120.f) / 1.2f));
				AvailableDistance = FMath::Min(AvailableDistance, FMath::Max(0.f, Gap - 120.f));
			}
		}
		// Swept chassis volume catches pedestrians, player vehicles and solid props.
		FCollisionQueryParams Query(TEXT("TrafficBrake"), true, this);
		FHitResult Obstacle;
		if (bPhysical)
		{
			// Yield to crossing traffic from the right, and never enter an occupied
			// junction. After a prolonged four-way wait, stable fleet order breaks ties.
			TArray<int32> Neighbors;
			const FIntPoint Cell(FMath::FloorToInt(Position.X / 4000.f), FMath::FloorToInt(Position.Y / 4000.f));
			for (int32 X = -1; X <= 1; ++X)
				for (int32 Y = -1; Y <= 1; ++Y)
					if (const auto* Entries = Grid.Find(Cell + FIntPoint(X,Y))) Neighbors.Append(*Entries);
			for (int32 OtherIndex : Neighbors)
			{
				const FCar& Other = Previous[OtherIndex];
				if (Other.bRemoved || Other.Lane == C.Lane || FVector::DistSquared(Position, Other.Last.GetLocation()) > FMath::Square(4000.f)) continue;
				FVector OtherPosition, OtherDirection;
				Lanes[Other.Lane].Eval(Other.Offset, OtherPosition, OtherDirection);
				const FVector Relative = OtherPosition - Position;
				if (FMath::Abs(Relative.Z) > 180.f) continue; // overpass, not an intersection
				const float Cross = Forward.X * OtherDirection.Y - Forward.Y * OtherDirection.X;
				const float Along=FVector::DotProduct(Forward.GetSafeNormal2D(),Relative);
				if (FVector::DotProduct(Forward.GetSafeNormal2D(),OtherDirection.GetSafeNormal2D())>.7f && FMath::Abs(Cross)<.5f && FMath::Abs(Forward.X*Relative.Y-Forward.Y*Relative.X)<190.f && Along>0.f && Along<SafeDistance)
				{
					// Converging lanes must not drive side-by-side through the same space.
					const float Gap=FMath::Max(0.f,Along-HalfLength-Types[Other.Type].Length*.5f-120.f);
					AvailableDistance=FMath::Min(AvailableDistance,Gap); TargetSpeed=FMath::Min(TargetSpeed,Gap/1.2f);
				}
				if (FMath::Abs(Cross) < 0.5f) continue;
				const float OurDistance = (Relative.X * OtherDirection.Y - Relative.Y * OtherDirection.X) / Cross;
				const float TheirDistance = (Relative.X * Forward.Y - Relative.Y * Forward.X) / Cross;
				if (OurDistance < 0.f || OurDistance > SafeDistance + 400.f || TheirDistance < -300.f || TheirDistance > 2500.f) continue;
				// Tangent lines on bends/ramps are not necessarily a real road crossing.
				if (C.Offset+OurDistance>Lane.Length || Other.Offset+TheirDistance>Lanes[Other.Lane].Length) continue;
				FVector OurCross,TheirCross,Unused;
				Lane.Eval(C.Offset+OurDistance,OurCross,Unused);
				Lanes[Other.Lane].Eval(Other.Offset+TheirDistance,TheirCross,Unused);
				if (FVector::Dist2D(OurCross,TheirCross)>220.f || FMath::Abs(OurCross.Z-TheirCross.Z)>150.f) continue;
				const bool OnRight = Forward.X * Relative.Y - Forward.Y * Relative.X > 0.f;
				const bool Occupied = FMath::Abs(TheirDistance) < Types[Other.Type].Length * 0.5f + 150.f;
				bool OtherRed=false;
				for (const FStopLine& Stop:StopLines[Other.Lane])
				{
					const float ToStop=Stop.Distance-Other.Offset-Types[Other.Type].Length*.5f;
					if (ToStop>=0.f && ToStop<TheirDistance && SignalState(Stop.bInitiallyRed,T)==0) { OtherRed=true; break; }
				}
				const bool Arriving = !OtherRed && TheirDistance>=0.f && TheirDistance / FMath::Max(300.f,Other.Speed) < 3.f;
				const bool Yield = C.WaitingTime > 3.f ? OtherIndex < CarIndex : OnRight;
				if (Occupied || (Yield && Arriving))
				{
					const float Clearance = FMath::Max(0.f, OurDistance - HalfLength - 250.f);
					AvailableDistance = FMath::Min(AvailableDistance, Clearance);
					TargetSpeed = FMath::Min(TargetSpeed, Clearance / 1.2f);
				}
			}
		}
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		Objects.AddObjectTypesToQuery(ECC_Pawn);
		Objects.AddObjectTypesToQuery(ECC_Vehicle);
		Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
		bool HasObstacle = false;
		// Follow the lane height and bends instead of sweeping a straight line into ramps.
		const float LookDistance=FMath::Min(SafeDistance,Lane.Length-C.Offset);
		const int32 Sections=FMath::Clamp(FMath::CeilToInt(LookDistance/1000.f),1,8);
		FVector Start=Position;
		if (bPhysical) for (int32 Section=0;Section<Sections;++Section)
		{
			const float BeginDistance=LookDistance*Section/Sections;
			const float EndDistance=LookDistance*(Section+1)/Sections;
			FVector End,Direction; Lane.Eval(C.Offset+EndDistance,End,Direction);
			// Keep only the small local height correction, not the old car's slope delta.
			FVector Raw,RawDirection; Lane.Eval(C.Offset,Raw,RawDirection);
			End.Z+=Position.Z-Raw.Z;
			TArray<FHitResult> Obstacles;
			GetWorld()->SweepMultiByObjectType(Obstacles,Start+FVector(0,0,100.f),End+FVector(0,0,100.f),
				(End-Start).Rotation().Quaternion(),Objects,FCollisionShape::MakeBox(FVector(HalfLength,85.f,50.f)),Query);
			for (FHitResult Hit:Obstacles)
			{
				if (Hit.ImpactNormal.Z>.65f && Hit.GetComponent() && Hit.GetComponent()->GetCollisionObjectType()==ECC_WorldStatic) continue;
				Hit.Distance+=BeginDistance;
				if (!HasObstacle || Hit.Distance<Obstacle.Distance) { Obstacle=Hit; HasObstacle=true; }
			}
			if (HasObstacle) break;
			Start=End;
		}
		if (HasObstacle)
		{
			TargetSpeed = FMath::Min(TargetSpeed, FMath::Max(0.f, (Obstacle.Distance - 140.f) / 1.2f));
			AvailableDistance = FMath::Min(AvailableDistance, FMath::Max(0.f, Obstacle.Distance - 140.f));
		}
		C.Speed = FMath::FInterpConstantTo(C.Speed, TargetSpeed, SimDt, TargetSpeed < C.Speed ? 1000.f : 240.f);
		C.WaitingTime = TargetSpeed < 10.f ? C.WaitingTime + SimDt : 0.f;
		const float Step = FMath::Min(C.Speed * SimDt, AvailableDistance);
		C.Offset = ClosedLane?FMath::Fmod(C.Offset+Step,Lane.Length):FMath::Min(EndOffset,C.Offset+Step);
		if (!ClosedLane && C.Offset>=EndOffset-3.f)
		{
			// Open exported roads are not loops. Recycle only when both endpoints
			// are away from players; never teleport a visible car across the map.
			bool EndpointsClear=true;
			for (const FVector& Viewer:Viewers)
				if (FVector::DistSquared(Viewer,Position)<FMath::Square(15000.f) || FVector::DistSquared(Viewer,Lane.Points[0])<FMath::Square(15000.f)) { EndpointsClear=false; break; }
			if (EndpointsClear) { C.Offset=0.f; C.Speed=0.f; C.WaitingTime=0.f; }
		}
		C.Last = CarTransform(C, T);
		if (bPhysical) C.Last.SetLocation(RoadPosition(C.Last.GetLocation()));
		// Only nearby cars need physics bodies. Distant traffic stays instanced.
		UBoxComponent* Body = CollisionBodies[CarIndex];
		if (bPhysical && !Body)
		{
			Body = NewObject<UBoxComponent>(this);
			Body->SetupAttachment(RootComponent);
			Body->SetBoxExtent(FVector(85.f, HalfLength, 60.f));
			Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
			Body->SetCollisionObjectType(ECC_Vehicle);
			Body->SetNotifyRigidBodyCollision(true);
			Body->ComponentTags.Add(FName(*FString::FromInt(CarIndex)));
			Body->OnComponentHit.AddDynamic(this, &ANCTrafficManager::TrafficHit);
			Body->RegisterComponent();
			CollisionBodies[CarIndex] = Body;
		}
		if (Body)
		{
			Body->SetWorldLocationAndRotation(C.Last.GetLocation() + FVector(0, 0, 90.f), C.Last.Rotator());
			Body->SetCollisionEnabled(bPhysical ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		}
		Pools[C.Type]->UpdateInstanceTransform(C.Instance, C.Last, true, false, true);
		DirtyPools[C.Type] = true;
	}
	for (int32 i = 0; i < Pools.Num(); ++i)
	{
		if (DirtyPools[i]) Pools[i]->MarkRenderStateDirty();
	}
}

void ANCTrafficManager::TrafficHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	// Ramming turns the kinematic traffic shell into the same Chaos car the player drives.
	if (!HasAuthority() || !Cast<ANCVehiclePawn>(OtherActor) || !HitComponent || HitComponent->ComponentTags.IsEmpty()) return;
	const int32 Index = FCString::Atoi(*HitComponent->ComponentTags[0].ToString());
	if (!Cars.IsValidIndex(Index) || Cars[Index].bRemoved) return;
	FVector Position, Forward;
	Lanes[Cars[Index].Lane].Eval(Cars[Index].Offset, Position, Forward);
	const FVector Velocity = Forward * Cars[Index].Speed;
	if (ANCVehiclePawn* Vehicle = TakeVehicle(Index)) Vehicle->GetMesh()->SetPhysicsLinearVelocity(Velocity);
}

ANCVehiclePawn* ANCTrafficManager::TakeVehicleNear(const FVector& Location, float Radius)
{
	if (!HasAuthority())
	{
		return nullptr;
	}
	int32 Best = INDEX_NONE;
	float BestD = Radius;
	for (int32 i = 0; i < Cars.Num(); ++i)
	{
		const FCar& C = Cars[i];
		if (C.bRemoved || !Types[C.Type].DrivableClass)
		{
			continue;
		}
		const float D = FVector::Dist(C.Last.GetLocation(), Location);
		if (D < BestD)
		{
			BestD = D;
			Best = i;
		}
	}
	return Best == INDEX_NONE ? nullptr : TakeVehicle(Best);
}

ANCVehiclePawn* ANCTrafficManager::TakeVehicle(int32 Best)
{
	if (!HasAuthority() || !Cars.IsValidIndex(Best) || Cars[Best].bRemoved || !Types[Cars[Best].Type].DrivableClass)
	{
		return nullptr;
	}
	const FCar& C = Cars[Best];
	if (CollisionBodies.IsValidIndex(Best) && CollisionBodies[Best]) CollisionBodies[Best]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FTransform Spawn = C.Last;
	FRotator R = Spawn.Rotator();
	R.Yaw -= MeshYawOffset;          // drivable skeletal meshes face +X
	R.Roll = 0.f;
	Spawn.SetRotation(R.Quaternion());
	Spawn.AddToTranslation(FVector(0, 0, 60.f));
	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	ANCVehiclePawn* V = GetWorld()->SpawnActor<ANCVehiclePawn>(Types[C.Type].DrivableClass, Spawn, SP);
	if (V)
	{
		MulticastRemoveCar(Best);
	}
	return V;
}

void ANCTrafficManager::MulticastRemoveCar_Implementation(int32 CarIndex)
{
	if (Cars.IsValidIndex(CarIndex) && !Cars[CarIndex].bRemoved)
	{
		FCar& C = Cars[CarIndex];
		C.bRemoved = true;
		if (CollisionBodies.IsValidIndex(CarIndex) && CollisionBodies[CarIndex]) CollisionBodies[CarIndex]->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FTransform Hidden = C.Last;
		Hidden.SetScale3D(FVector::ZeroVector);
		Pools[C.Type]->UpdateInstanceTransform(C.Instance, Hidden, true, true, true);
	}
}

void ANCTrafficManager::PaintAll(const FLinearColor& Color)
{
	for (UInstancedStaticMeshComponent* ISM : Pools)
	{
		const TArray<FName> Slots = ISM->GetMaterialSlotNames();
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			const FString S = Slots[i].ToString();
			if (S.Contains(TEXT("Car_")) && !S.Contains(TEXT("Interior")) && !S.Contains(TEXT("Headliner")) &&
				!S.Contains(TEXT("Dash")) && !S.Contains(TEXT("Caliper")) && !S.Contains(TEXT("Disc")))
			{
				if (UMaterialInstanceDynamic* MID = ISM->CreateAndSetMaterialInstanceDynamic(i))
				{
					MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
				}
			}
		}
	}
}

bool ANCTrafficManager::TraceRay(const FVector& A, const FVector& B, int32& OutCar, float& OutDist) const
{
	const FVector D = B - A;
	const float L = D.Size();
	OutCar = INDEX_NONE;
	OutDist = L;
	if (L < 1.f)
	{
		return false;
	}
	FBox Box(ForceInit);
	Box += A;
	Box += B;
	Box = Box.ExpandBy(600.f);
	for (int32 i = 0; i < Cars.Num(); ++i)
	{
		const FCar& C = Cars[i];
		if (C.bRemoved || !Box.IsInsideXY(C.Last.GetLocation()))
		{
			continue;
		}
		// car box in the instance frame: the static mesh faces -Y (MeshYawOffset), length along Y
		const FVector Ext(95.f, Types[C.Type].Length * 0.5f, 80.f);
		const FVector LA = C.Last.InverseTransformPositionNoScale(A) - FVector(0, 0, 80.f);
		const FVector LB = C.Last.InverseTransformPositionNoScale(B) - FVector(0, 0, 80.f);
		const FVector LD = LB - LA;
		double T0 = 0.0, T1 = 1.0;
		bool bHit = true;
		for (int32 k = 0; k < 3 && bHit; ++k)
		{
			if (FMath::Abs(LD[k]) < KINDA_SMALL_NUMBER)
			{
				bHit = FMath::Abs(LA[k]) <= Ext[k];
				continue;
			}
			double Ta = (-Ext[k] - LA[k]) / LD[k];
			double Tb = (Ext[k] - LA[k]) / LD[k];
			if (Ta > Tb)
			{
				Swap(Ta, Tb);
			}
			T0 = FMath::Max(T0, Ta);
			T1 = FMath::Min(T1, Tb);
			bHit = T0 <= T1;
		}
		if (bHit && T0 * L < OutDist)
		{
			OutDist = static_cast<float>(T0 * L);
			OutCar = i;
		}
	}
	return OutCar != INDEX_NONE;
}

TArray<int32> ANCTrafficManager::CarsInRadius(const FVector& C, float R) const
{
	TArray<int32> Out;
	for (int32 i = 0; i < Cars.Num(); ++i)
	{
		if (!Cars[i].bRemoved && FVector::Dist(Cars[i].Last.GetLocation(), C) < R)
		{
			Out.Add(i);
		}
	}
	return Out;
}
