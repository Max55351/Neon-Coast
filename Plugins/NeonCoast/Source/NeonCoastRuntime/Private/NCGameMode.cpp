#pragma warning(disable: 4456 4457 4458 4459)
#include "NCGameMode.h"
#include "NCBusinessManager.h"
#include "Camera/CameraComponent.h"
#include "NCCabrioletPawn.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "ChaosVehicleWheel.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/BoxElem.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NCWardrobe.h"
#include "NCHeroAnimInstance.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "NCTrafficManager.h"
#include "Components/BoxComponent.h"

#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NCHUD.h"
#include "NCHealthComponent.h"
#include "NCInteractionComponent.h"
#include "NCPlayerController.h"
#include "NCPlayerState.h"
#include "NCVehiclePawn.h"
#include "NCPlayerMove.h"
#include "NCPoliceManager.h"
#include "NCVehiclePawn.h"
#include "NCWeaponComponent.h"
#include "NCCrowdManager.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ANCGameMode::ANCGameMode()
{
	static ConstructorHelpers::FClassFinder<APawn> ThirdPerson(TEXT("/Game/ThirdPerson/Blueprints/BP_ThirdPersonCharacter"));
	if (ThirdPerson.Succeeded())
	{
		DefaultPawnClass = ThirdPerson.Class;
	}
	else
	{
		DefaultPawnClass = ACharacter::StaticClass();
	}
	PlayerStateClass = ANCPlayerState::StaticClass();
	HUDClass = ANCHUD::StaticClass();
	PlayerControllerClass = ANCPlayerController::StaticClass();
}

template <class T>
static T* AddRuntimeComponent(APawn* Pawn, const TCHAR* Name)
{
	if (T* Existing = Pawn->FindComponentByClass<T>())
	{
		return Existing;
	}
	T* C = NewObject<T>(Pawn, Name);
	C->RegisterComponent();
	Pawn->AddInstanceComponent(C);
	return C;
}

void ANCGameMode::FinishRestartPlayer(AController* NewPlayer, const FRotator& StartRotation)
{
	Super::FinishRestartPlayer(NewPlayer, StartRotation);
	APawn* Pawn = NewPlayer ? NewPlayer->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}
	AddRuntimeComponent<UNCInteractionComponent>(Pawn, TEXT("NCInteraction"));
	UNCHealthComponent* H = AddRuntimeComponent<UNCHealthComponent>(Pawn, TEXT("NCHealth"));
	AddRuntimeComponent<UNCWeaponComponent>(Pawn, TEXT("NCWeapons"));
	AddRuntimeComponent<UNCPlayerMove>(Pawn, TEXT("NCMove"));
	H->OnDeath.AddUniqueDynamic(this, &ANCGameMode::OnPawnDied);
	if (!ParkedCabriolet.IsValid() && !FParse::Param(FCommandLine::Get(), TEXT("NCSelfTest")))
	{
		GetWorldTimerManager().SetTimer(CabrioletTimer, this, &ANCGameMode::SpawnCabriolet, 2.f, true);
	}
	Pending.Remove(Cast<APlayerController>(NewPlayer));
	if (ANCPlayerState* PS = NewPlayer->GetPlayerState<ANCPlayerState>())
	{
		PS->OnRep_Outfit();                                              // the bought outfit survives WASTED / BUSTED
	}
}

void ANCGameMode::SpawnCabriolet()
{
	if (ParkedCabriolet.IsValid()) { GetWorldTimerManager().ClearTimer(CabrioletTimer); return; }
	APawn* Hero = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Cast<ACharacter>(Hero)) return;
	FCollisionQueryParams Query(NAME_None, false, Hero);
	for (int32 Candidate = 0; Candidate < 8; ++Candidate)
	{
		const FVector Offset = FRotator(0.f, Candidate * 45.f, 0.f).Vector() * 850.f;
		const FVector Position = Hero->GetActorLocation() + Offset;
		FHitResult Ground;
		if (!GetWorld()->LineTraceSingleByChannel(Ground, Position + FVector(0,0,600.f), Position - FVector(0,0,1800.f), ECC_Pawn, Query) || Ground.ImpactNormal.Z < 0.9f) continue;
		const FVector Location = Ground.ImpactPoint + FVector(0,0,120.f);
		if (GetWorld()->OverlapBlockingTestByChannel(Location + FVector(0,0,60.f), FQuat::Identity, ECC_Vehicle, FCollisionShape::MakeBox(FVector(240,110,80)), Query)) continue;
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
		ParkedCabriolet = GetWorld()->SpawnActor<ANCCabrioletPawn>(Location, FRotator::ZeroRotator, Parameters);
		if (ParkedCabriolet.IsValid())
		{
			GetWorldTimerManager().ClearTimer(CabrioletTimer);
			UE_LOG(LogTemp, Log, TEXT("[NeonCoast] parked cyan cabriolet at %s; R toggles roof below 10 km/h"), *Location.ToCompactString());
			return;
		}
	}
}

void ANCGameMode::OnPawnDied(AActor* Victim, AController* Killer)
{
	APawn* P = Cast<APawn>(Victim);
	APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	if (!PC)
	{
		// died while sitting in a car: the car owns the controller
		for (TActorIterator<ANCVehiclePawn> It(GetWorld()); It && !PC; ++It)
		{
			if (It->Driver == P)
			{
				PC = Cast<APlayerController>(It->GetController());
			}
		}
	}
	if (!PC || Pending.Contains(PC))
	{
		return;
	}
	Pending.Add(PC);
	if (ANCPlayerController* NPC = Cast<ANCPlayerController>(PC))
	{
		NPC->ClientDeathScreen(false);
	}
	FTimerHandle T;
	GetWorldTimerManager().SetTimer(T, FTimerDelegate::CreateWeakLambda(this, [this, PC]() { Respawn(PC, false); }), 5.f, false);
}

void ANCGameMode::Busted(APlayerController* PC)
{
	if (!PC || Pending.Contains(PC))
	{
		return;
	}
	Pending.Add(PC);
	if (ACharacter* C = Cast<ACharacter>(PC->GetPawn()))
	{
		C->GetCharacterMovement()->DisableMovement();
	}
	if (ANCPlayerController* NPC = Cast<ANCPlayerController>(PC))
	{
		NPC->ClientDeathScreen(true);
	}
	FTimerHandle T;
	GetWorldTimerManager().SetTimer(T, FTimerDelegate::CreateWeakLambda(this, [this, PC]() { Respawn(PC, true); }), 4.f, false);
}

void ANCGameMode::Respawn(APlayerController* PC, bool bBusted)
{
	if (!IsValid(PC))
	{
		return;
	}
	if (ANCPlayerState* PS = PC->GetPlayerState<ANCPlayerState>())
	{
		PS->ClearWanted();
		PS->Money = FMath::Max(0, PS->Money - (bBusted ? BustedFine : HospitalBill));
	}
	TArray<FName> KeepWeapons;
	TArray<int32> KeepClip, KeepReserve;
	APawn* Old = PC->GetPawn();
	if (ANCVehiclePawn* V = Cast<ANCVehiclePawn>(Old))
	{
		Old = V->Driver;
	}
	if (const UNCWeaponComponent* W = UNCWeaponComponent::Of(Old))
	{
		if (!bBusted)
		{
			KeepWeapons = W->Owned;              // GTA: you keep your guns after the hospital
			KeepClip = W->Clip;
			KeepReserve = W->Reserve;
		}
	}
	PC->UnPossess();
	if (Old)
	{
		Old->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Old->SetLifeSpan(20.f);
	}
	FTransform Spawn = FTransform::Identity;
	bool bHave = false;
	for (TActorIterator<ANCPoliceManager> It(GetWorld()); It; ++It)
	{
		Spawn = bBusted ? It->PoliceSpawn : It->HospitalSpawn;
		bHave = !Spawn.GetLocation().IsNearlyZero();
		break;
	}
	if (bHave)
	{
		RestartPlayerAtTransform(PC, Spawn);
	}
	else
	{
		RestartPlayer(PC);
	}
	if (UNCWeaponComponent* W = UNCWeaponComponent::Of(PC->GetPawn()))
	{
		if (KeepWeapons.Num() > 0)
		{
			W->Owned = KeepWeapons;
			W->Clip = KeepClip;
			W->Reserve = KeepReserve;
			W->Current = 0;
		}
	}
	Pending.Remove(PC);
}

// ---------------------------------------------------------------------- headless self test
void ANCGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (!ANCBusinessManager::Get(this)) GetWorld()->SpawnActor<ANCBusinessManager>();
	if (FParse::Param(FCommandLine::Get(), TEXT("NCSelfTest")))
	{
		GetWorldTimerManager().SetTimer(SelfTestTimer, this, &ANCGameMode::SelfTestTick, 0.5f, true, 12.f);
	}
}

void ANCGameMode::SelfTestTick()
{
	UWorld* W = GetWorld();
	APlayerController* PC = UGameplayStatics::GetPlayerController(W, 0);
	APawn* Hero = STHero.Get();
	if (!Hero && PC && STState == 0)
	{
		Hero = PC->GetPawn();
		STHero = Hero;
	}
	ACharacter* Ch = Cast<ACharacter>(Hero);
	ANCVehiclePawn* Car = STCar.Get();
	++STTick;
	auto Log = [this](const FString& S) { if (S.StartsWith(TEXT("FAIL"))) ++STFailures; UE_LOG(LogTemp, Display, TEXT("[NCSELFTEST] %s"), *S); };
	auto Diag = [&](ANCVehiclePawn* V) -> FString
	{
		FString Out;
		if (V && V->Move())
		{
			for (int32 i = 0; i < V->Move()->Wheels.Num(); ++i)
			{
				UChaosVehicleWheel* Wh = V->Move()->Wheels[i];
				Out += FString::Printf(TEXT(" w%d[air=%d off=%.1f r=%.1f]"), i, Wh ? Wh->IsInAir() : -1, Wh ? Wh->GetSuspensionOffset() : 0.f, Wh ? Wh->WheelRadius : 0.f);
			}
			FBodyInstance* BI = V->GetMesh()->GetBodyInstance();
			Out += FString::Printf(TEXT(" mass=%.0f sim=%d angvel=%.1f"), V->GetMesh()->GetMass(), BI ? BI->bSimulatePhysics : -1, V->GetMesh()->GetPhysicsAngularVelocityInDegrees().Size());
		}
		return Out;
	};	switch (STState)
	{
	case 0:
	{
		if (!Hero)
		{
			Log(TEXT("waiting for the hero"));
			return;
		}
		// Exercise gameplay, not the title menu's locked input and cinematic camera.
		if (ANCPlayerController* Controller = Cast<ANCPlayerController>(PC)) Controller->StartGame();
		bool SignalPhasesOK = ANCTrafficManager::SignalState(false, 0.0) == 2 &&
			ANCTrafficManager::SignalState(false, 20.0) == 1 &&
			ANCTrafficManager::SignalState(false, 23.0) == 0 &&
			ANCTrafficManager::SignalState(true, 25.0) == 2;
		for (int32 Phase = 0; Phase < 100; ++Phase)
			SignalPhasesOK &= !(ANCTrafficManager::SignalState(false, Phase * 0.5) == 2 && ANCTrafficManager::SignalState(true, Phase * 0.5) == 2);
		Log(SignalPhasesOK ? TEXT("PASS opposing signal phases never both green") : TEXT("FAIL signal phases"));
		UNCOutfitComponent* O = Hero->FindComponentByClass<UNCOutfitComponent>();
		Log(FString::Printf(TEXT("hero=%s at %s outfitComp=%d extra=%s mannequinVisible=%d"), *Hero->GetName(), *Hero->GetActorLocation().ToCompactString(),
			O ? 1 : 0, (O && O->Extra) ? *O->Extra->GetName() : TEXT("none"), (Ch && Ch->GetMesh()) ? Ch->GetMesh()->IsVisible() : -1));
		if (O && O->Extra)
		{
			TArray<USkeletalMeshComponent*> Sk;
			O->Extra->GetComponents(Sk);
			for (USkeletalMeshComponent* S : Sk)
			{
				Log(FString::Printf(TEXT("  metahuman part %s mesh=%s visible=%d hidden=%d"), *S->GetName(), S->GetSkeletalMeshAsset() ? *S->GetSkeletalMeshAsset()->GetName() : TEXT("none"), S->IsVisible(), S->bHiddenInGame));
			}
		}
		UClass* Cls = LoadClass<ANCVehiclePawn>(nullptr, TEXT("/Game/NeonCoast/Vehicles/BP_NC2_Car_Sedan_A.BP_NC2_Car_Sedan_A_C"));
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		FVector Loc = Hero->GetActorLocation() + Hero->GetActorForwardVector() * 700.f + FVector(0.f, 0.f, 250.f);
		{
			FHitResult Hit;
			FCollisionQueryParams QP(NAME_None, false, Hero);
			const FVector Top = Loc + FVector(0.f, 0.f, 4000.f);
			if (W->LineTraceSingleByChannel(Hit, Top, Loc - FVector(0.f, 0.f, 4000.f), ECC_Visibility, QP))
			{
				Log(FString::Printf(TEXT("ground under spawn: z=%.1f actor=%s comp=%s"), Hit.ImpactPoint.Z, Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("none"), Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("none")));
				Loc = Hit.ImpactPoint + FVector(0.f, 0.f, 120.f);
			}
		}
		Car = Cls ? W->SpawnActor<ANCVehiclePawn>(Cls, Loc, FRotator(0.f, Hero->GetActorRotation().Yaw + 180.f, 0.f), SP) : nullptr;
		// A controlled flat surface separates vehicle-physics regressions from
		// slopes/props around the player's spawn. Exists only in -NCSelfTest.
		AActor* Platform = W->SpawnActor<AActor>();
		UBoxComponent* PlatformBox = NewObject<UBoxComponent>(Platform);
		Platform->SetRootComponent(PlatformBox);
		PlatformBox->SetBoxExtent(FVector(12000.f, 3000.f, 30.f));
		PlatformBox->SetCollisionProfileName(TEXT("BlockAll"));
		PlatformBox->RegisterComponent();
		const FVector PlatformLocation = Hero->GetActorLocation() + FVector(0, 0, 2000.f);
		Platform->SetActorLocation(PlatformLocation);
		if (Car)
		{
			Car->SetActorLocationAndRotation(PlatformLocation + FVector(0, 0, 150.f), FRotator::ZeroRotator, false, nullptr, ETeleportType::TeleportPhysics);
			Car->GetMesh()->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Car->GetMesh()->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
		STCar = Car;
		for (TActorIterator<ANCTrafficManager> It(W); It; ++It)
		{
			ANCTrafficManager* Traffic = *It;
			float BestDistance = 8000.f;
			for (int32 i = 0; i < Traffic->Cars.Num(); ++i)
			{
				const auto& TrafficCar = Traffic->Cars[i];
				const float Distance = FVector::Dist(Hero->GetActorLocation(), TrafficCar.Last.GetLocation());
				if (!TrafficCar.bRemoved && Distance < BestDistance)
				{
					BestDistance = Distance;
					STTrafficCar = i;
					STTraffic = Traffic;
				}
			}
			if (STTrafficCar != INDEX_NONE)
			{
				auto& TrafficCar = Traffic->Cars[STTrafficCar];
				FNCPath TestLane;
				TestLane.Points = {PlatformLocation + FVector(0, 1800.f, 30.f), PlatformLocation + FVector(20000.f, 1800.f, 30.f)};
				TestLane.Speed = 500.f;
				TestLane.Build();
				TrafficCar.Lane = Traffic->Lanes.Add(TestLane);
				Traffic->LaneCars.Add(TArray<int32>{STTrafficCar});
				TrafficCar.Offset = 0.f;
				TrafficCar.Speed = 500.f;
				TrafficCar.Last = Traffic->CarTransform(TrafficCar, 0.0);
				STTrafficStart = TrafficCar.Offset;
				FVector Point, Direction;
				const auto& Lane = Traffic->Lanes[TrafficCar.Lane];
				Lane.Eval(FMath::Fmod(TrafficCar.Offset + 1000.f, Lane.Length), Point, Direction);
				AActor* Blocker = W->SpawnActor<AActor>();
				UBoxComponent* Box = NewObject<UBoxComponent>(Blocker);
				Blocker->SetRootComponent(Box);
				Box->SetBoxExtent(FVector(70.f, 180.f, 100.f));
				Box->SetCollisionProfileName(TEXT("BlockAll"));
				Box->RegisterComponent();
				Blocker->SetActorLocationAndRotation(Point + FVector(0, 0, 100.f), Direction.Rotation());
				STObstacle = Blocker;
				Log(TEXT("traffic obstacle test started"));
				break;
			}
		}
		Log(FString::Printf(TEXT("spawned car=%s at %s class=%s"), Car ? *Car->GetName() : TEXT("FAILED"), *Loc.ToCompactString(), Cls ? *Cls->GetName() : TEXT("null")));
		STState = 1;
		STTick = 0;
		break;
	}
	case 1:
		if (STTick == 8 && Car)
		{
			if (ANCTrafficManager* Traffic = STTraffic.Get())
			{
				const auto& Tested = Traffic->Cars[STTrafficCar];
				const float Distance = FMath::Fmod(Tested.Offset - STTrafficStart + Traffic->Lanes[Tested.Lane].Length, Traffic->Lanes[Tested.Lane].Length);
				Log(FString::Printf(TEXT("%s traffic obstacle distance=%.1f speed=%.1f"), Distance < 850.f && Tested.Speed < 100.f ? TEXT("PASS") : TEXT("FAIL"), Distance, Tested.Speed));
				if (STObstacle.IsValid()) STObstacle->Destroy();
				FHitResult Contact;
				const FVector Center = Tested.Last.GetLocation() + FVector(0, 0, 90.f);
				FCollisionQueryParams Query(NAME_None, false, Car);
				const bool Blocked = W->SweepSingleByChannel(Contact, Center - FVector(800.f, 0, 0), Center + FVector(800.f, 0, 0), FQuat::Identity,
					ECC_Vehicle, FCollisionShape::MakeBox(FVector(80.f, 80.f, 50.f)), Query);
				Log(Blocked && Contact.GetActor() == Traffic ? TEXT("PASS traffic physically blocks vehicle") : TEXT("FAIL traffic vehicle collision"));
			}
			USkeletalMeshComponent* M = Car->GetMesh();
			bool MaterialsOK = M->GetNumMaterials() > 0;
			for (int32 i = 0; i < M->GetNumMaterials(); ++i)
			{
				Log(FString::Printf(TEXT("material slot %d = %s"), i, M->GetMaterial(i) ? *M->GetMaterial(i)->GetPathName() : TEXT("NULL")));
				MaterialsOK &= M->GetMaterial(i) && !M->GetMaterial(i)->GetPathName().Contains(TEXT("WorldGridMaterial"));
			}
			Log(MaterialsOK ? TEXT("PASS drivable car materials assigned") : TEXT("FAIL missing car materials"));
			Log(FString::Printf(TEXT("car collision profile=%s objType=%d collEnabled=%d gravity=%d simulating=%d loc=%s"), *M->GetCollisionProfileName().ToString(), (int32)M->GetCollisionObjectType(),
				(int32)M->GetCollisionEnabled(), M->IsGravityEnabled(), M->IsSimulatingPhysics(), *Car->GetActorLocation().ToCompactString()));
			const ECollisionChannel Chans[] = {ECC_WorldStatic, ECC_WorldDynamic, ECC_Pawn, ECC_PhysicsBody, ECC_Vehicle, ECC_Visibility, ECC_Camera};
			const TCHAR* Names[] = {TEXT("WorldStatic"), TEXT("WorldDynamic"), TEXT("Pawn"), TEXT("PhysicsBody"), TEXT("Vehicle"), TEXT("Visibility"), TEXT("Camera")};
			for (int32 k = 0; k < 7; ++k)
			{
				TArray<FHitResult> Hits;
				FCollisionQueryParams QP(NAME_None, true, Car);
				W->LineTraceMultiByChannel(Hits, Car->GetActorLocation() + FVector(0, 0, 60), Car->GetActorLocation() - FVector(0, 0, 1800), Chans[k], QP);
				FString S;
				for (const FHitResult& H : Hits)
				{
					S += FString::Printf(TEXT(" [%s/%s z=%.0f blk=%d]"), H.GetActor() ? *H.GetActor()->GetName() : TEXT("?"), H.GetComponent() ? *H.GetComponent()->GetName() : TEXT("?"), H.ImpactPoint.Z, H.bBlockingHit);
				}
				Log(FString::Printf(TEXT("  downward trace %s:%s"), Names[k], S.IsEmpty() ? TEXT(" nothing") : *S));
			}
			TArray<USkeletalMeshComponent*> Sk;
			Car->GetComponents(Sk);
			for (int32 i = 0; i < 4; ++i)
			{
				static const FName WB[4] = {TEXT("wheel_fl"), TEXT("wheel_fr"), TEXT("wheel_rl"), TEXT("wheel_rr")};
				Log(FString::Printf(TEXT("  bone %s world=%s"), *WB[i].ToString(), *M->GetSocketLocation(WB[i]).ToCompactString()));
			}
			Log(FString::Printf(TEXT("  bounds origin=%s extent=%s"), *M->Bounds.Origin.ToCompactString(), *M->Bounds.BoxExtent.ToCompactString()));
			if (UPhysicsAsset* PA = M->GetPhysicsAsset())
			{
				for (USkeletalBodySetup* BS : PA->SkeletalBodySetups)
				{
					for (const FKBoxElem& B : BS->AggGeom.BoxElems)
					{
						Log(FString::Printf(TEXT("  PA body bone=%s box X=%.1f Y=%.1f Z=%.1f center=%s"), *BS->BoneName.ToString(), B.X, B.Y, B.Z, *B.Center.ToCompactString()));
					}
					Log(FString::Printf(TEXT("  PA body bone=%s boxes=%d convex=%d sphyl=%d spheres=%d"), *BS->BoneName.ToString(), BS->AggGeom.BoxElems.Num(), BS->AggGeom.ConvexElems.Num(), BS->AggGeom.SphylElems.Num(), BS->AggGeom.SphereElems.Num()));
				}
			}
			if (USkeletalMesh* SKM = M->GetSkeletalMeshAsset())
			{
				const FBoxSphereBounds IB = SKM->GetImportedBounds();
				const FBoxSphereBounds MB = SKM->GetBounds();
				Log(FString::Printf(TEXT("  mesh imported bounds origin=%s extent=%s | bounds origin=%s extent=%s"), *IB.Origin.ToCompactString(), *IB.BoxExtent.ToCompactString(), *MB.Origin.ToCompactString(), *MB.BoxExtent.ToCompactString()));
			}
			if (FBodyInstance* BI = M->GetBodyInstance())
			{
				const FBox BB = BI->GetBodyBounds();
				Log(FString::Printf(TEXT("  body bounds min=%s max=%s"), *BB.Min.ToCompactString(), *BB.Max.ToCompactString()));
			}
		}		if (Car)
		{
			Log(FString::Printf(TEXT("gravity t=%d car z=%.1f vz=%.1f hero z=%.1f%s"), STTick, Car->GetActorLocation().Z, Car->GetVelocity().Z, Hero ? Hero->GetActorLocation().Z : 0.f, *Diag(Car)));
		}
		if (STTick >= 9)
		{
			STState = 2;
		}
		break;
	case 2:
		if (Car && Hero)
		{
			Car->EnterVehicle(Hero);
			Log(FString::Printf(TEXT("entered: driver=%s controllerPawn=%s heroHidden=%d attachedTo=%s"), Car->Driver ? *Car->Driver->GetName() : TEXT("none"),
				PC->GetPawn() ? *PC->GetPawn()->GetName() : TEXT("none"), Hero->IsHidden(), Hero->GetAttachParentActor() ? *Hero->GetAttachParentActor()->GetName() : TEXT("none")));
		}
		STState = 3;
		STTick = 0;
		break;
	case 3:
		if (STTick == 6 && Car)
		{
			const FVector Offset = Car->Camera->GetComponentLocation() - Car->GetActorLocation();
			Log(FString::Printf(TEXT("%s chase camera offset=%s active=%d"), Offset.Z > 80.f && Offset.Size2D() > 250.f && Car->Camera->IsActive() ? TEXT("PASS") : TEXT("FAIL"), *Offset.ToCompactString(), Car->Camera->IsActive()));
		}
		if (STTick == 6 && PC && FParse::Param(FCommandLine::Get(), TEXT("NCVisualTest"))) PC->ConsoleCommand(TEXT("HighResShot 1"), false);
		if (Car && Car->Move())
		{
			Car->Move()->SetThrottleInput(1.f);
			Car->Move()->SetHandbrakeInput(false);
			Log(FString::Printf(TEXT("driving t=%d speed=%.1f km/h loc=%s onGround=%d rpm=%.0f gear=%d"), STTick, Car->GetVelocity().Size() * 0.036f, *Car->GetActorLocation().ToCompactString(),
				Car->Move()->IsMovingOnGround(), Car->Move()->GetEngineRotationSpeed(), Car->Move()->GetCurrentGear()));
			Log(TEXT("  wheels:") + Diag(Car) + FString::Printf(TEXT(" vel=%s"), *Car->GetVelocity().ToCompactString()));
		}
		if (STTick >= 12)
		{
			Log(Car && Car->GetVelocity().Size2D() > 100.f ? TEXT("PASS car drives under throttle") : TEXT("FAIL car has no traction"));
			STState = 4;
		}
		break;
	case 4:
		if (Car)
		{
			Car->Move()->SetThrottleInput(0.f);
			Car->ServerExit();
			Log(FString::Printf(TEXT("exited: controllerPawn=%s heroHidden=%d heroLoc=%s carLoc=%s"), PC->GetPawn() ? *PC->GetPawn()->GetName() : TEXT("none"), Hero ? Hero->IsHidden() : -1,
				Hero ? *Hero->GetActorLocation().ToCompactString() : TEXT("?"), *Car->GetActorLocation().ToCompactString()));
		}
		STState = 5;
		if (Hero) STWalkStart = Hero->GetActorLocation();
		if (UNCPlayerMove* Movement = UNCPlayerMove::Of(Hero)) Movement->SelfTestMove = FVector2D(0.f, 1.f);
		if (PC && PC->GetLocalPlayer() && Car)
		{
			UEnhancedInputLocalPlayerSubsystem* Sub = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
			Log(Sub && !Sub->HasMappingContext(Car->Mapping) ? TEXT("PASS vehicle input mapping removed") : TEXT("FAIL vehicle mapping remains"));
		}
		STTick = 0;
		break;
	case 5:
	{
		if (STTick == 1)
		{
			const FVector Spots[3] = {FVector(-11090.f, -6954.f, 0.f), FVector(-11310.f, -6954.f, 0.f), FVector(-11310.f, -7337.f, 0.f)};
			const ECollisionChannel Chans[4] = {ECC_Pawn, ECC_WorldStatic, ECC_Visibility, ECC_Vehicle};
			const TCHAR* CN[4] = {TEXT("Pawn"), TEXT("WorldStatic"), TEXT("Visibility"), TEXT("Vehicle")};
			for (const FVector& Sp : Spots)
			{
				for (int32 k = 0; k < 4; ++k)
				{
					FHitResult H;
					const bool bHit = W->LineTraceSingleByChannel(H, FVector(Sp.X, Sp.Y, 1500.f), FVector(Sp.X, Sp.Y, -800.f), Chans[k]);
					Log(FString::Printf(TEXT("terrain probe (%.0f,%.0f) %s: %s z=%.1f actor=%s"), Sp.X, Sp.Y, CN[k], bHit ? TEXT("HIT") : TEXT("MISS"), bHit ? H.ImpactPoint.Z : 0.f, (bHit && H.GetActor()) ? *H.GetActor()->GetName() : TEXT("-")));
				}
			}
		}
		if (STTick == 2 && Hero)
		{
			const FVector HL = Hero->GetActorLocation();
			TArray<FHitResult> Hits;
			FCollisionQueryParams QP(NAME_None, true, Hero);
			W->LineTraceMultiByChannel(Hits, FVector(HL.X, HL.Y, 1500.f), FVector(HL.X, HL.Y, -800.f), ECC_Pawn, QP);
			FString S;
			for (const FHitResult& H : Hits)
			{
				S += FString::Printf(TEXT(" [%s/%s z=%.0f blk=%d]"), H.GetActor() ? *H.GetActor()->GetName() : TEXT("?"), H.GetComponent() ? *H.GetComponent()->GetName() : TEXT("?"), H.ImpactPoint.Z, H.bBlockingHit);
			}
			Log(FString::Printf(TEXT("under the hero (%.0f,%.0f z=%.0f):%s"), HL.X, HL.Y, HL.Z, S.IsEmpty() ? TEXT(" NOTHING") : *S));
		}
		APawn* Now = PC ? PC->GetPawn() : nullptr;
		ACharacter* NC = Cast<ACharacter>(Now);
		Log(FString::Printf(TEXT("after exit t=%d pawn=%s z=%.1f vz=%.1f mode=%d onGround=%d hidden=%d"), STTick, Now ? *Now->GetName() : TEXT("none"), Now ? Now->GetActorLocation().Z : 0.f,
			Now ? Now->GetVelocity().Z : 0.f, NC ? (int32)NC->GetCharacterMovement()->MovementMode.GetValue() : -1, NC ? NC->GetCharacterMovement()->IsMovingOnGround() : -1, Now ? Now->IsHidden() : -1));
		if (STTick >= 8)
		{
			const float WalkDistance = Hero ? FVector::Dist2D(STWalkStart, Hero->GetActorLocation()) : 0.f;
			Log(FString::Printf(TEXT("%s walk after exit distance=%.1f cm"), WalkDistance > 100.f ? TEXT("PASS") : TEXT("FAIL"), WalkDistance));
			if (UNCPlayerMove* Movement = UNCPlayerMove::Of(Hero)) Movement->SelfTestMove = FVector2D::ZeroVector;
			STState = 6;
			STTick = 0;
		}
		break;
	}
	case 6:
	{
		UNCWeaponComponent* Wp = Hero ? Hero->FindComponentByClass<UNCWeaponComponent>() : nullptr;
		Log(FString::Printf(TEXT("weapons comp=%d owned=%d"), Wp ? 1 : 0, Wp ? Wp->Owned.Num() : -1));
		// Exercise the real bullet-to-traffic conversion path, including SUV meshes
		if (ANCTrafficManager* Traffic = STTraffic.Get())
		{
			const FVector Point = Traffic->Cars[STTrafficCar].Last.GetLocation()+FVector(0,0,80);
			bool Flesh = false;
			UNCWeaponComponent::FireBullet(Hero,PC,Point+FVector(600,0,0),FVector(-1,0,0),1000,25,Flesh);
			Log(Traffic->Cars[STTrafficCar].bRemoved ? TEXT("PASS bullet converts physical traffic shell to damaged car") : TEXT("FAIL traffic bullet conversion"));
		}
		// loaded while rendering, which the old four untargeted shots did not cover.
		for (TActorIterator<ANCTrafficManager> It(W); It; ++It)
		{
		TSet<int32> TestedTypes;
			for (int32 i = 0; i < It->Cars.Num(); ++i)
			{
				const auto& TrafficCar = It->Cars[i];
				if (TrafficCar.bRemoved || TestedTypes.Contains(TrafficCar.Type)) continue;
				TestedTypes.Add(TrafficCar.Type);
				if (ANCVehiclePawn* Target = It->TakeVehicle(i))
				{
					Target->SetActorLocation(Hero->GetActorLocation()+FVector(1200,TestedTypes.Num()*600,300),false,nullptr,ETeleportType::TeleportPhysics);
					const FVector From = Target->GetActorLocation() + FVector(500,0,90);
					bool Flesh = false;
					for (int32 Shot = 0; Shot < 8; ++Shot)
					{
						const FVector Hit = UNCWeaponComponent::FireBullet(Hero,PC,From,FVector(-1,0,0),800,25,Flesh);
						UNCWeaponComponent::ShotFX(W,From,Hit,Flesh);
					}
					Log(FString::Printf(TEXT("%s bullet damage on vehicle type %d total=%.0f"),Target->ShotDamage >= 150.f ? TEXT("PASS") : TEXT("FAIL"),TrafficCar.Type,Target->ShotDamage));
				}
			}
		}
		if (Wp && Hero)
		{
			FVector Loc;
			FRotator Rot;
			PC->GetPlayerViewPoint(Loc, Rot);
			for (int32 i = 0; i < 4; ++i)
			{
				Wp->ServerFire(Loc, Rot.Vector(), false, i);
			}
			Log(TEXT("fired 4 shots without crashing"));
		}
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ANCCabrioletPawn* Cabriolet = Hero ? W->SpawnActor<ANCCabrioletPawn>(Hero->GetActorLocation() + FVector(400,800,150), FRotator::ZeroRotator, Parameters) : nullptr;
		for (TActorIterator<ANCCrowdManager> It(W); It; ++It)
		{
			for (int32 i = 0; i < It->Peds.Num(); ++i)
			{
				if (It->Peds[i].bDead) continue;
				FNCPath Path;
				Path.Points = {Hero->GetActorLocation()+FVector(350,600,-90),Hero->GetActorLocation()+FVector(550,600,-90)};
				Path.Build();
				It->Peds[i].Path = It->Paths.Add(Path);
				It->Peds[i].Offset = 0;
				It->Peds[i].Speed = 0;
				It->KillPed(i,FVector(300,0,150));
				if (!It->Bodies.IsEmpty()) STPedBody = It->Bodies.Last();
				bool MaterialsOK = STPedBody.IsValid() && STPedBody->GetNumMaterials() > 0;
				if (STPedBody.IsValid()) for (int32 Slot = 0; Slot < STPedBody->GetNumMaterials(); ++Slot)
					MaterialsOK &= STPedBody->GetMaterial(Slot) && !STPedBody->GetMaterial(Slot)->GetPathName().Contains(TEXT("WorldGridMaterial"));
				Log(MaterialsOK ? TEXT("PASS pedestrian materials assigned") : TEXT("FAIL pedestrian materials"));
				break;
			}
			break;
		}
		STCar = Cabriolet;
		if (Cabriolet && Hero)
		{
			Cabriolet->EnterVehicle(Hero);
			Cabriolet->ServerToggleRoof();
			Log(!Cabriolet->bRoofOpen ? TEXT("PASS cabriolet R roof command") : TEXT("FAIL cabriolet roof command"));
		}
		else Log(TEXT("FAIL cabriolet spawn"));
		STState = 7;
		STTick = 0;
		break;
	}
	case 7:
		if (STTick == 4 && Hero)
		{
			CollectGarbage(RF_NoFlags);
			UNCWeaponComponent::ShotFX(W,Hero->GetActorLocation(),Hero->GetActorLocation()+FVector(500,0,100),false);
			Log(TEXT("PASS shot effects recreated after garbage collection"));
		}
		if (STTick == 9 && PC && FParse::Param(FCommandLine::Get(), TEXT("NCVisualTest"))) PC->ConsoleCommand(TEXT("HighResShot 1"), false);
		if (STTick >= 10)
		{
			ANCCabrioletPawn* Cabriolet = Cast<ANCCabrioletPawn>(STCar.Get());
			Log(STPedBody.IsValid() && (STPedBody->IsSimulatingPhysics() || FMath::Abs(STPedBody->GetComponentRotation().Roll) > 80.f)
				? TEXT("PASS pedestrian falls and remains down") : TEXT("FAIL pedestrian remains standing"));
			Log(Cabriolet && Cabriolet->IsRoofClosed() ? TEXT("PASS cabriolet roof animation finished") : TEXT("FAIL cabriolet roof animation"));
			if (Cabriolet) Cabriolet->ServerExit();
			Log(PC && PC->GetPawn() == Hero && Ch && Ch->GetCharacterMovement()->IsActive() && !PC->IsMoveInputIgnored()
				? TEXT("PASS cabriolet exit restores character control") : TEXT("FAIL cabriolet exit control"));
			if (UNCPlayerMove* Movement = UNCPlayerMove::Of(Hero)) Movement->SelfTestMove = FVector2D(0.f,1.f);
			STWalkStart = Hero ? Hero->GetActorLocation() : FVector::ZeroVector;
			if (ANCHUD* HUD = PC ? Cast<ANCHUD>(PC->GetHUD()) : nullptr)
			{
				HUD->ToggleMap();
				Log(HUD->MapRoads.Num() > 0 && HUD->MapPOIs.Num() > 0 ? TEXT("PASS map roads and place markers") : TEXT("FAIL map data"));
			}
			STState = 8;
			STTick = 0;
		}
		break;
	default:
		if (STTick == 2 && PC && FParse::Param(FCommandLine::Get(),TEXT("NCVisualTest"))) PC->ConsoleCommand(TEXT("HighResShot 1"),false);
		if (STTick == 4)
		{
			const float WalkDistance = Hero ? FVector::Dist2D(STWalkStart,Hero->GetActorLocation()) : 0.f;
			Log(WalkDistance > 100.f ? TEXT("PASS walk after switching between two cars") : TEXT("FAIL walk after second exit"));
			if (UNCPlayerMove* Movement = UNCPlayerMove::Of(Hero)) Movement->SelfTestMove = FVector2D::ZeroVector;
			if (!FParse::Param(FCommandLine::Get(), TEXT("NCAnimationTest")))
			{
				Log(TEXT("DONE"));
				FPlatformMisc::RequestExitWithStatus(false, STFailures ? 1 : 0);
			}
		}
		if (STTick >= 5 && FParse::Param(FCommandLine::Get(), TEXT("NCAnimationTest")) && Ch)
		{
			auto* Outfit = Ch->FindComponentByClass<UNCOutfitComponent>();
			auto* Anim = Outfit && Outfit->Body ? Cast<UNCHeroAnimInstance>(Outfit->Body->GetAnimInstance()) : nullptr;
			auto* Movement = UNCPlayerMove::Of(Ch);
			auto* Weapon = UNCWeaponComponent::Of(Ch);
			if (STTick == 5)
			{
				Log(Anim && Anim->Clips.Num() == 8 && !Anim->Clips.Contains(nullptr) ? TEXT("PASS hero motion clips loaded") : TEXT("FAIL hero motion clips missing"));
				Log(Anim && Anim->MatchedBones >= 15 ? TEXT("PASS outfit skeleton motion mapping") : TEXT("FAIL outfit motion bone mapping"));
				if (auto* HUD = Cast<ANCHUD>(PC->GetHUD())) HUD->bMapOpen = false;
				if (Weapon) { Weapon->GiveAll(); Weapon->Current = Weapon->IndexOf(TEXT("Pistol")); Weapon->bAiming = true; Weapon->UpdateHeldMesh(); }
			}
			if (STTick == 7)
			{
				Log(Anim && Anim->Motion == ENCHeroMotion::Aim ? TEXT("PASS aim motion state") : TEXT("FAIL aim motion state"));
				if (PC && FParse::Param(FCommandLine::Get(),TEXT("NCVisualTest"))) PC->ConsoleCommand(TEXT("HighResShot 1"),false);
				if (Weapon) Weapon->bAiming = false;
				Ch->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
				Ch->GetCharacterMovement()->Velocity.Z = 650.f;
			}
			if (STTick == 8) Log(Anim && Anim->Motion == ENCHeroMotion::Jump ? TEXT("PASS jump motion state") : TEXT("FAIL jump motion state"));
			if (STTick == 9) Log(Anim && Anim->Motion == ENCHeroMotion::Fall ? TEXT("PASS fall motion state") : TEXT("FAIL fall motion state"));
			if (STTick == 11 && Movement)
			{
				// Isolate animation input from water depth for this state-machine test.
				Movement->SetComponentTickEnabled(false); Movement->bSwimming = true;
				Ch->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
				Ch->GetCharacterMovement()->Velocity = FVector(180,0,0);
			}
			if (STTick == 13)
			{
				Log(Anim && Anim->Motion == ENCHeroMotion::Swim ? TEXT("PASS swim motion state") : TEXT("FAIL swim motion state"));
				if (PC && FParse::Param(FCommandLine::Get(),TEXT("NCVisualTest"))) PC->ConsoleCommand(TEXT("HighResShot 1"),false);
				if (Movement) Movement->bUnderWater = true;
			}
			if (STTick == 15)
			{
				Log(Anim && Anim->Motion == ENCHeroMotion::Dive ? TEXT("PASS dive motion state") : TEXT("FAIL dive motion state"));
				if (Movement) { Movement->bSwimming = false; Movement->bUnderWater = false; Movement->SetComponentTickEnabled(true); }
				Ch->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
				Log(TEXT("DONE")); FPlatformMisc::RequestExitWithStatus(false, STFailures ? 1 : 0);
			}
		}
		break;
	}
}
