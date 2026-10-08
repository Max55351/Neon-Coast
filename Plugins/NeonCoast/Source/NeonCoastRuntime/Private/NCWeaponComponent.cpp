#pragma warning(disable: 4456 4457 4458 4459)
#include "NCWeaponComponent.h"
#include "NCBusinessManager.h"
#include "NCWardrobe.h"
#include "Engine/OverlapResult.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "NCAnimal.h"
#include "NCSound.h"
#include "NCCrowdManager.h"
#include "NCHealthComponent.h"
#include "NCHUD.h"
#include "NCPlayerController.h"
#include "NCPlayerState.h"
#include "NCPoliceManager.h"
#include "NCProjectile.h"
#include "NCTrafficManager.h"
#include "NCVehiclePawn.h"
#include "Net/UnrealNetwork.h"
#include "Particles/ParticleSystem.h"
#include "TimerManager.h"

UNCWeaponComponent::UNCWeaponComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
}

void UNCWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UNCWeaponComponent, Owned);
	DOREPLIFETIME(UNCWeaponComponent, Clip);
	DOREPLIFETIME(UNCWeaponComponent, Reserve);
	DOREPLIFETIME(UNCWeaponComponent, Current);
	DOREPLIFETIME(UNCWeaponComponent, bReloading);
	DOREPLIFETIME_CONDITION(UNCWeaponComponent, bAiming, COND_SkipOwner);
}

const FNCWeaponDef& UNCWeaponComponent::CurrentDef() const
{
	const FNCWeaponDef* D = Owned.IsValidIndex(Current) ? NCWeapons::Find(Owned[Current]) : nullptr;
	return D ? *D : NCWeapons::All()[0];
}

void UNCWeaponComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->HasAuthority() && Owned.Num() == 0)
	{
		Give(TEXT("Fists"), 0);
		Give(TEXT("Pistol"), 60);           // everybody starts with a pistol and a few magazines
		Current = 0;
	}
	UpdateHeldMesh();
}

void UNCWeaponComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Held)
	{
		Held->DestroyComponent();
	}
	Super::EndPlay(Reason);
}

// ------------------------------------------------------------------ inventory
void UNCWeaponComponent::Give(FName Id, int32 Rounds)
{
	const FNCWeaponDef* D = NCWeapons::Find(Id);
	if (!D || !GetOwner()->HasAuthority())
	{
		return;
	}
	int32 i = IndexOf(Id);
	if (i == INDEX_NONE)
	{
		Owned.Add(Id);
		Clip.Add(0);
		Reserve.Add(0);
		i = Owned.Num() - 1;
		// keep the list ordered by slot so cycling feels natural
		for (int32 k = Owned.Num() - 1; k > 0; --k)
		{
			const FNCWeaponDef* A = NCWeapons::Find(Owned[k - 1]);
			const FNCWeaponDef* B = NCWeapons::Find(Owned[k]);
			if (A && B && A->Slot > B->Slot)
			{
				const FName CurId = Owned.IsValidIndex(Current) ? Owned[Current] : NAME_None;
				Owned.Swap(k - 1, k);
				Clip.Swap(k - 1, k);
				Reserve.Swap(k - 1, k);
				Current = FMath::Max(0, Owned.IndexOfByKey(CurId));
			}
		}
		i = IndexOf(Id);
	}
	if (D->MagSize > 0)
	{
		const int32 ToClip = FMath::Min(Rounds, D->MagSize - Clip[i]);
		Clip[i] += ToClip;
		Reserve[i] += Rounds - ToClip;
	}
}

void UNCWeaponComponent::GiveAll()
{
	for (const FNCWeaponDef& D : NCWeapons::All())
	{
		Give(D.Id, FMath::Max(D.MagSize * 6, D.AmmoPerBox * 3));
	}
}

void UNCWeaponComponent::Strip()
{
	if (!GetOwner()->HasAuthority())
	{
		return;
	}
	Owned.Reset();
	Clip.Reset();
	Reserve.Reset();
	Give(TEXT("Fists"), 0);
	Current = 0;
	OnRep_Current();
}

void UNCWeaponComponent::ServerBuy_Implementation(FName Id, bool bAmmoOnly)
{
	const FNCWeaponDef* D = NCWeapons::Find(Id);
	APawn* P = Cast<APawn>(GetOwner());
	ANCPlayerState* PS = P ? P->GetPlayerState<ANCPlayerState>() : nullptr;
	if (const auto* City = ANCBusinessManager::Get(this); City && !City->NearService(P,0)) return;
	if (!D || !PS)
	{
		return;
	}
	const bool bHave = IndexOf(Id) != INDEX_NONE;
	const int32 Cost = (bAmmoOnly || (bHave && D->Kind != ENCWeaponKind::Melee)) ? D->AmmoPrice : D->Price + D->AmmoPrice;
	if ((bAmmoOnly && !bHave) || (bHave && D->Kind == ENCWeaponKind::Melee) || !PS->Spend(Cost))
	{
		return;
	}
	Give(Id, D->MagSize > 0 ? D->AmmoPerBox : 0);
	Current = IndexOf(Id);
	OnRep_Current();
}

void UNCWeaponComponent::ServerSelect_Implementation(int32 Index)
{
	if (Owned.IsValidIndex(Index) && !bReloading)
	{
		Current = Index;
		OnRep_Current();
	}
}

void UNCWeaponComponent::OnRep_Current()
{
	UpdateHeldMesh();
}

void UNCWeaponComponent::UpdateHeldMesh()
{
	ACharacter* C = Cast<ACharacter>(GetOwner());
	if (!C || !C->GetMesh())
	{
		return;
	}
	const FNCWeaponDef& D = CurrentDef();
	if (!Held)
	{
		Held = NewObject<UStaticMeshComponent>(C, TEXT("NCHeldWeapon"));
		Held->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Held->SetCastShadow(true);
		Held->RegisterComponent();
	}
	UStaticMesh* SM = D.Mesh.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr,
		*FString::Printf(TEXT("/Game/NeonCoast/Meshes/SM_%s.SM_%s"), *D.Mesh, *D.Mesh));
	Held->SetStaticMesh(SM);
	Held->SetVisibility(SM != nullptr);
	FVector Loc(-9.f, 3.5f, 1.5f);
	FRotator Rot(0.f, 0.f, -90.f);
	for (TActorIterator<ANCPoliceManager> It(GetWorld()); It; ++It)
	{
		Loc = It->WeaponHandLocation;
		Rot = It->WeaponHandRotation;
		break;
	}
	USkeletalMeshComponent* HandMesh = C->GetMesh();
	if (const auto* Outfit = C->FindComponentByClass<UNCOutfitComponent>())
		if (Outfit->Body && Outfit->Body->DoesSocketExist(TEXT("hand_r"))) HandMesh = Outfit->Body;
	Held->AttachToComponent(HandMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("hand_r"));
	// Blender models: muzzle +Y -> static meshes face -Y in UE -> +90 yaw puts the muzzle along +X
	Held->SetRelativeLocationAndRotation(Loc, (Rot.Quaternion() * FRotator(0.f, 90.f, 0.f).Quaternion()).Rotator());
}

FVector UNCWeaponComponent::MuzzleLocation() const
{
	if (Held && Held->GetStaticMesh())
	{
		const FBoxSphereBounds B = Held->CalcBounds(Held->GetComponentTransform());
		const FVector Fwd = GetOwner()->GetActorForwardVector();
		return B.Origin + Fwd * B.SphereRadius * 0.8f;
	}
	return GetOwner()->GetActorLocation() + GetOwner()->GetActorForwardVector() * 50.f + FVector(0, 0, 50.f);
}

// ------------------------------------------------------------------ input (owning client)
void UNCWeaponComponent::SetupInput()
{
	APawn* P = Cast<APawn>(GetOwner());
	APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	if (!PC || !PC->IsLocalController() || !PC->InputComponent)
	{
		return;
	}
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PC->InputComponent);
	ULocalPlayer* LP = PC->GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* Sub = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!EIC || !Sub)
	{
		return;
	}
	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_NCWeapons"));
	auto Make = [this](const TCHAR* Name, std::initializer_list<FKey> Keys)
	{
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = EInputActionValueType::Boolean;
		for (const FKey& K : Keys)
		{
			Mapping->MapKey(A, K);
		}
		Actions.Add(A);
		return A;
	};
	UInputAction* Fire = Make(TEXT("IA_NCFire"), {EKeys::LeftMouseButton, EKeys::Gamepad_RightTrigger});
	UInputAction* Aim = Make(TEXT("IA_NCAim"), {EKeys::RightMouseButton, EKeys::Gamepad_LeftTrigger});
	UInputAction* Rel = Make(TEXT("IA_NCReload"), {EKeys::R, EKeys::Gamepad_FaceButton_Left});
	UInputAction* Next = Make(TEXT("IA_NCNext"), {EKeys::MouseScrollUp, EKeys::Gamepad_RightShoulder, EKeys::E});
	UInputAction* Prev = Make(TEXT("IA_NCPrev"), {EKeys::MouseScrollDown, EKeys::Gamepad_LeftShoulder, EKeys::Q});
	UInputAction* Nade = Make(TEXT("IA_NCGrenade"), {EKeys::G});
	EIC->BindAction(Fire, ETriggerEvent::Started, this, &UNCWeaponComponent::OnFirePressed);
	EIC->BindAction(Fire, ETriggerEvent::Completed, this, &UNCWeaponComponent::OnFireReleased);
	EIC->BindAction(Aim, ETriggerEvent::Started, this, &UNCWeaponComponent::OnAimPressed);
	EIC->BindAction(Aim, ETriggerEvent::Completed, this, &UNCWeaponComponent::OnAimReleased);
	EIC->BindAction(Rel, ETriggerEvent::Started, this, &UNCWeaponComponent::OnReload);
	EIC->BindAction(Next, ETriggerEvent::Started, this, &UNCWeaponComponent::OnNext);
	EIC->BindAction(Prev, ETriggerEvent::Started, this, &UNCWeaponComponent::OnPrev);
	EIC->BindAction(Nade, ETriggerEvent::Started, this, &UNCWeaponComponent::OnGrenade);
	static const FKey Digits[9] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven,
		EKeys::Eight, EKeys::Nine};
	for (int32 s = 0; s < 9; ++s)
	{
		UInputAction* A = Make(*FString::Printf(TEXT("IA_NCSlot%d"), s + 1), {Digits[s]});
		EIC->BindActionValueLambda(A, ETriggerEvent::Started, [this, s](const FInputActionValue&) { OnSlot(s + 1); });
	}
	Sub->AddMappingContext(Mapping, 2);
	bInputReady = true;
}

bool UNCWeaponComponent::CanUseInput() const
{
	const APawn* P = Cast<APawn>(GetOwner());
	const APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	if (!PC || PC->GetPawn() != P)
	{
		return false;                       // sitting in a car: the car owns the controls
	}
	if (const ANCPlayerController* NPC = Cast<ANCPlayerController>(PC))
	{
		if (NPC->IsInMenu())
		{
			return false;
		}
	}
	if (const ANCHUD* H = Cast<ANCHUD>(PC->GetHUD()))
	{
		if (H->IsShopOpen() || H->IsTuningOpen())
		{
			return false;
		}
	}
	const UNCHealthComponent* HC = UNCHealthComponent::Of(P);
	return !(HC && HC->bDead);
}

void UNCWeaponComponent::OnFirePressed(const FInputActionValue&)
{
	if (CanUseInput())
	{
		bTrigger = true;
		TryFireLocal();
	}
}

void UNCWeaponComponent::OnFireReleased(const FInputActionValue&)
{
	bTrigger = false;
}

void UNCWeaponComponent::OnAimPressed(const FInputActionValue&)
{
	if (const auto* Pawn = Cast<APawn>(GetOwner())) if (const auto* PC = Cast<APlayerController>(Pawn->GetController()))
		if (auto* HUD = Cast<ANCHUD>(PC->GetHUD()); HUD && HUD->IsShopOpen()) HUD->CloseShop();
	if (CanUseInput())
	{
		bAiming = true;
		ServerSetAim(true);
	}
}

void UNCWeaponComponent::OnAimReleased(const FInputActionValue&)
{
	bAiming = false;
	ServerSetAim(false);
}

void UNCWeaponComponent::ServerSetAim_Implementation(bool bOn)
{
	const auto* Health = UNCHealthComponent::Of(GetOwner());
	bAiming = bOn && !GetOwner()->IsHidden() && !(Health && Health->bDead);
}

void UNCWeaponComponent::OnReload(const FInputActionValue&)
{
	if (CanUseInput())
	{
		ServerReload();
	}
}

void UNCWeaponComponent::OnNext(const FInputActionValue&)
{
	if (CanUseInput())
	{
		Cycle(1);
	}
}

void UNCWeaponComponent::OnPrev(const FInputActionValue&)
{
	if (CanUseInput())
	{
		Cycle(-1);
	}
}

void UNCWeaponComponent::Cycle(int32 Dir)
{
	if (Owned.Num() > 0)
	{
		ServerSelect((Current + Dir + Owned.Num()) % Owned.Num());
	}
}

void UNCWeaponComponent::OnSlot(int32 Slot)
{
	if (!CanUseInput())
	{
		return;
	}
	// pressing the same slot again cycles through the weapons in it
	TArray<int32> In;
	for (int32 i = 0; i < Owned.Num(); ++i)
	{
		const FNCWeaponDef* D = NCWeapons::Find(Owned[i]);
		if (D && D->Slot == Slot)
		{
			In.Add(i);
		}
	}
	if (In.Num() > 0)
	{
		const int32 k = In.IndexOfByKey(Current);
		ServerSelect(In[(k + 1) % In.Num()]);
	}
}

void UNCWeaponComponent::OnGrenade(const FInputActionValue&)
{
	if (!CanUseInput())
	{
		return;
	}
	const int32 G = IndexOf(TEXT("Grenade")) != INDEX_NONE ? IndexOf(TEXT("Grenade")) : IndexOf(TEXT("Molotov"));
	if (G != INDEX_NONE && G != Current)
	{
		ServerSelect(G);
	}
}

void UNCWeaponComponent::TryFireLocal()
{
	const UWorld* W = GetWorld();
	const FNCWeaponDef& D = CurrentDef();
	if (W->GetTimeSeconds() < NextFireTime || bReloading)
	{
		return;
	}
	if (D.MagSize > 0 && CurrentClip() <= 0)
	{
		ServerReload();
		NextFireTime = W->GetTimeSeconds() + 0.3f;
		return;
	}
	NextFireTime = W->GetTimeSeconds() + D.FireInterval;
	const APawn* P = Cast<APawn>(GetOwner());
	const APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
	const FVector CamDir = PC->PlayerCameraManager->GetCameraRotation().Vector();
	// snap the character to the aim direction (hip fire, like GTA)
	if (ACharacter* C = Cast<ACharacter>(GetOwner()))
	{
		C->SetActorRotation(FRotator(0.f, CamDir.Rotation().Yaw, 0.f));
	}
	ServerFire(CamLoc, CamDir, bAiming, FMath::Rand());
	Recoil += D.RecoilDeg;
}

// ------------------------------------------------------------------ server fire
void UNCWeaponComponent::ServerFire_Implementation(FVector_NetQuantize CamLoc, FVector_NetQuantizeNormal CamDir, bool bAim, int32 Seed)
{
	APawn* P = Cast<APawn>(GetOwner());
	UWorld* W = GetWorld();
	const UNCHealthComponent* HC = UNCHealthComponent::Of(P);
	if (!P || !W || bReloading || (HC && HC->bDead))
	{
		return;
	}
	const FNCWeaponDef& D = CurrentDef();
	if (D.MagSize > 0)
	{
		if (Clip[Current] <= 0)
		{
			ServerReload_Implementation();
			return;
		}
		Clip[Current] -= 1;
	}
	AController* Inst = P->GetController();
	// what the camera looks at (ignoring ourselves), then shoot from the gun towards it
	FHitResult CH;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(NCAim), true, P);
	const FVector CamEnd = CamLoc + CamDir * D.RangeCm;
	const FVector AimPoint = W->LineTraceSingleByChannel(CH, CamLoc + CamDir * 150.f, CamEnd, ECC_Visibility, Q) ? CH.ImpactPoint : CamEnd;
	const FVector Muzzle = MuzzleLocation();
	const FVector BaseDir = (AimPoint - Muzzle).GetSafeNormal();
	FRandomStream R(Seed);
	const float Spread = D.SpreadDeg * (bAim ? 0.45f : 1.f) * (P->GetVelocity().Size() > 200.f ? 1.6f : 1.f);
	TArray<FVector_NetQuantize> Ends;
	TArray<bool> Flesh;
	switch (D.Kind)
	{
	case ENCWeaponKind::Melee:
	{
		const FVector From = P->GetActorLocation() + FVector(0, 0, 40.f);
		const FVector Fwd = P->GetActorForwardVector();
		TArray<FHitResult> Hits;
		FCollisionShape S = FCollisionShape::MakeSphere(55.f);
		W->SweepMultiByChannel(Hits, From, From + Fwd * D.RangeCm, FQuat::Identity, ECC_Pawn, S, Q);
		TSet<AActor*> Done;
		for (const FHitResult& H : Hits)
		{
			if (H.GetActor() && !Done.Contains(H.GetActor()))
			{
				Done.Add(H.GetActor());
				UGameplayStatics::ApplyPointDamage(H.GetActor(), D.Damage, Fwd, H, Inst, P, UDamageType::StaticClass());
				if (!H.GetActor()->IsA<ANCAnimal>())
				{
					ANCPoliceManager::ReportCrime(W, Inst, 1, H.ImpactPoint, true);
				}
			}
		}
		for (TActorIterator<ANCCrowdManager> It(W); It; ++It)
		{
			for (int32 Ped : It->PedsInRadius(From + Fwd * D.RangeCm * 0.7f, 90.f))
			{
				It->KillPed(Ped, Fwd * 350.f + FVector(0, 0, 150.f));
				ANCPoliceManager::ReportCrime(W, Inst, 2, From, true);
			}
		}
		return;
	}
	case ENCWeaponKind::Rocket:
		ANCProjectile::Launch(W, 0, Muzzle + BaseDir * 60.f, BaseDir * 5500.f, D.Damage, D.ExplosionRadiusCm, P, P);
		ANCPoliceManager::ReportCrime(W, Inst, 2, Muzzle, true);
		MulticastShots(Muzzle, Ends, Flesh);
		return;
	case ENCWeaponKind::Thrown:
	{
		const FVector V = (BaseDir + FVector(0, 0, 0.35f)).GetSafeNormal() * 1500.f + P->GetVelocity() * 0.5f;
		ANCProjectile::Launch(W, D.bIncendiary ? 2 : 1, P->GetActorLocation() + FVector(0, 0, 80.f) + P->GetActorForwardVector() * 40.f,
			V, D.Damage, D.ExplosionRadiusCm, P, P);
		if (Clip[Current] <= 0 && Reserve[Current] > 0)
		{
			Reserve[Current] -= 1;
			Clip[Current] = 1;
		}
		ANCPoliceManager::ReportCrime(W, Inst, 1, Muzzle, true);
		return;
	}
	default:
		for (int32 k = 0; k < FMath::Max(1, D.Pellets); ++k)
		{
			const FVector Dir = R.VRandCone(BaseDir, FMath::DegreesToRadians(Spread));
			bool bFl = false;
			Ends.Add(FireBullet(P, Inst, Muzzle, Dir, D.RangeCm, D.Damage, bFl));
			Flesh.Add(bFl);
		}
		ANCPoliceManager::ReportCrime(W, Inst, 1, Muzzle, true);
		MulticastShots(Muzzle, Ends, Flesh);
		break;
	}
}

namespace
{
	FName GunSound(FName Weapon)
	{
		const FString W = Weapon.ToString();
		if (W.StartsWith(TEXT("Pistol"))) return TEXT("Gun_Pistol");
		if (W == TEXT("SMG")) return TEXT("Gun_SMG");
		if (W == TEXT("Shotgun")) return TEXT("Gun_Shotgun");
		if (W == TEXT("Sniper")) return TEXT("Gun_Sniper");
		if (W == TEXT("Minigun")) return TEXT("Gun_Minigun");
		if (W == TEXT("RPG")) return TEXT("Gun_Rocket");
		return TEXT("Gun_Rifle");
	}
	// one bang per shot even when a shotgun draws 8 tracers
	double GLastShotSound = -1.0;
	FVector GLastShotPos = FVector::ZeroVector;
	void ShotSound(UWorld* W, FName Id, const FVector& Where)
	{
		const double T = W->GetTimeSeconds();
		if (T - GLastShotSound < 0.03 && FVector::DistSquared(Where, GLastShotPos) < 10000.f)
		{
			return;
		}
		GLastShotSound = T;
		GLastShotPos = Where;
		ANCSoundBank::Play(W, Id, Where, 1.f, FMath::FRandRange(0.95f, 1.05f));
	}
}

void UNCWeaponComponent::MulticastShots_Implementation(FVector_NetQuantize Muzzle, const TArray<FVector_NetQuantize>& Ends,
	const TArray<bool>& Flesh)
{
	if (GetWorld() && GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		ShotSound(GetWorld(), GunSound(CurrentDef().Id), Muzzle);
	}
	for (int32 i = 0; i < Ends.Num(); ++i)
	{
		ShotFX(GetWorld(), Muzzle, Ends[i], Flesh.IsValidIndex(i) && Flesh[i]);
		if (Flesh.IsValidIndex(i) && Flesh[i])
		{
			LastHitMarker = GetWorld()->GetTimeSeconds();
		}
	}
}

void UNCWeaponComponent::ServerReload_Implementation()
{
	const FNCWeaponDef& D = CurrentDef();
	if (bReloading || D.MagSize <= 0 || !Clip.IsValidIndex(Current) || Clip[Current] >= D.MagSize || Reserve[Current] <= 0)
	{
		return;
	}
	bReloading = true;
	GetWorld()->GetTimerManager().SetTimer(ReloadTimer, this, &UNCWeaponComponent::FinishReload, D.ReloadTime, false);
}

void UNCWeaponComponent::FinishReload()
{
	bReloading = false;
	const FNCWeaponDef& D = CurrentDef();
	if (Clip.IsValidIndex(Current))
	{
		const int32 N = FMath::Min(D.MagSize - Clip[Current], Reserve[Current]);
		Clip[Current] += N;
		Reserve[Current] -= N;
	}
}

// ------------------------------------------------------------------ shared helpers
FVector UNCWeaponComponent::FireBullet(AActor* Shooter, AController* Inst, const FVector& From, const FVector& Dir, float Range,
	float Damage, bool& bOutFlesh)
{
	bOutFlesh = false;
	UWorld* W = Shooter ? Shooter->GetWorld() : nullptr;
	if (!W)
	{
		return From;
	}
	// gunfire: people around run, animals scatter (throttled, fine per pellet)
	ANCCrowdManager::PanicAt(W, From, 4500.f);
	FCollisionQueryParams Q(SCENE_QUERY_STAT(NCBullet), true, Shooter);
	// Traffic shells exist for physical impacts, but bullets are resolved by
	// TraceRay below. Otherwise the shell clips the analytic hit at the same
	// distance and the shot is delivered to the manager instead of the car.
	for (TActorIterator<ANCTrafficManager> It(W); It; ++It) Q.AddIgnoredActor(*It);
	if (AActor* Parent = Shooter->GetAttachParentActor())
	{
		Q.AddIgnoredActor(Parent);               // shooting out of a car
	}
	FHitResult H;
	const FVector End = From + Dir * Range;
	const bool bHit = W->LineTraceSingleByChannel(H, From, End, ECC_Visibility, Q);
	float Best = bHit ? H.Distance : Range;
	int32 Ped = INDEX_NONE, Car = INDEX_NONE;
	float PD = Best, CD = Best;
	ANCCrowdManager* CM = nullptr;
	ANCTrafficManager* TM = nullptr;
	for (TActorIterator<ANCCrowdManager> It(W); It; ++It)
	{
		CM = *It;
		break;
	}
	for (TActorIterator<ANCTrafficManager> It(W); It; ++It)
	{
		TM = *It;
		break;
	}
	if (CM)
	{
		CM->TraceRay(From, From + Dir * Best, Ped, PD);
	}
	if (TM)
	{
		TM->TraceRay(From, From + Dir * FMath::Min(Best, Ped != INDEX_NONE ? PD : Best), Car, CD);
	}
	if (Car != INDEX_NONE && (Ped == INDEX_NONE || CD < PD))
	{
		// a traffic car: it becomes a real car and takes the hit
		if (ANCVehiclePawn* V = TM->TakeVehicle(Car))
		{
			UGameplayStatics::ApplyPointDamage(V, Damage, Dir, FHitResult(), Inst, Shooter, UDamageType::StaticClass());
		}
		return From + Dir * CD;
	}
	if (Ped != INDEX_NONE)
	{
		CM->KillPed(Ped, Dir * 250.f + FVector(0, 0, 80.f));
		ANCPoliceManager::ReportCrime(W, Inst, 2, From + Dir * PD, true);
		bOutFlesh = true;
		return From + Dir * PD;
	}
	if (bHit)
	{
		if (AActor* A = H.GetActor())
		{
			UGameplayStatics::ApplyPointDamage(A, Damage, Dir, H, Inst, Shooter, UDamageType::StaticClass());
			bOutFlesh = UNCHealthComponent::Of(A) != nullptr || A->IsA<ANCAnimal>();
		}
		if (UPrimitiveComponent* PC = H.GetComponent())
		{
			if (PC->IsSimulatingPhysics())
			{
				PC->AddImpulseAtLocation(Dir * Damage * 40.f, H.ImpactPoint);
			}
		}
		return H.ImpactPoint;
	}
	return End;
}

void UNCWeaponComponent::Explode(UWorld* W, const FVector& Where, float Radius, float Damage, AActor* Causer, AController* Inst,
	bool bIncendiary)
{
	if (!W)
	{
		return;
	}
	if (bIncendiary)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ANCFireArea* F = W->SpawnActor<ANCFireArea>(ANCFireArea::StaticClass(), FTransform(Where), P))
		{
			F->Radius = Radius;
			F->Starter = Inst;
		}
		ANCPoliceManager::ReportCrime(W, Inst, 2, Where, true);
		ANCCrowdManager::PanicAt(W, Where, 3000.f);
		return;
	}
	ANCCrowdManager::PanicAt(W, Where, Radius * 3.f + 3000.f);
	ANCSoundBank::Explosion(W, Where, Radius);                        // fireball + bang + crater on every machine
	UGameplayStatics::ApplyRadialDamageWithFalloff(W, Damage, Damage * 0.1f, Where, Radius * 0.4f, Radius, 1.f,
		UDamageType::StaticClass(), TArray<AActor*>(), Causer, Inst, ECC_Visibility);
	// push loose physics (ragdolls, debris, parked cars)
	TArray<FOverlapResult> Ov;
	W->OverlapMultiByObjectType(Ov, Where, FQuat::Identity,
		FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllDynamicObjects), FCollisionShape::MakeSphere(Radius));
	for (const FOverlapResult& O : Ov)
	{
		if (UPrimitiveComponent* C = O.GetComponent())
		{
			if (C->IsSimulatingPhysics())
			{
				C->AddRadialImpulse(Where, Radius, Damage * 6.f, ERadialImpulseFalloff::RIF_Linear, true);
			}
		}
	}
	for (TActorIterator<ANCCrowdManager> It(W); It; ++It)
	{
		for (int32 Ped : It->PedsInRadius(Where, Radius))
		{
			const FVector Away = (It->PedLocation(Ped) - Where).GetSafeNormal2D();
			It->KillPed(Ped, Away * 800.f + FVector(0, 0, 600.f));
		}
	}
	for (TActorIterator<ANCTrafficManager> It(W); It; ++It)
	{
		for (int32 Car : It->CarsInRadius(Where, Radius * 0.8f))
		{
			if (ANCVehiclePawn* V = It->TakeVehicle(Car))
			{
				UGameplayStatics::ApplyPointDamage(V, Damage, FVector::UpVector, FHitResult(), Inst, Causer, UDamageType::StaticClass());
			}
		}
	}
	ANCPoliceManager::ReportCrime(W, Inst, 2, Where, true);
}

void UNCWeaponComponent::ShotFX(UWorld* W, const FVector& Muzzle, const FVector& Hit, bool bFlesh)
{
	if (!W || W->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	ShotSound(W, TEXT("Gun_Rifle"), Muzzle);                              // police / helicopter gunner
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ANCTracer* T = W->SpawnActor<ANCTracer>(ANCTracer::StaticClass(), FTransform(Muzzle), P))
	{
		T->Setup(Muzzle, Hit, true);
	}
	UParticleSystem* Sparks = LoadObject<UParticleSystem>(nullptr, TEXT("/Game/StarterContent/Particles/P_Sparks.P_Sparks"));
	if (Sparks && !bFlesh)
	{
		UGameplayStatics::SpawnEmitterAtLocation(W, Sparks, Hit, FRotator::ZeroRotator, FVector(0.25f));
	}
}

// ------------------------------------------------------------------ tick: auto fire, aim camera
void UNCWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Held)
		if (const auto* Outfit = GetOwner()->FindComponentByClass<UNCOutfitComponent>())
			if (Outfit->Body && Outfit->Body->DoesSocketExist(TEXT("hand_r")) && Held->GetAttachParent() != Outfit->Body) UpdateHeldMesh();
	const APawn* P = Cast<APawn>(GetOwner());
	if (!P || !P->IsLocallyControlled())
	{
		return;
	}
	if (!bInputReady)
	{
		SetupInput();
	}
	if (bTrigger && CurrentDef().bAutomatic && CanUseInput())
	{
		TryFireLocal();
	}
	if (!CanUseInput())
	{
		bTrigger = false;
		if (bAiming) ServerSetAim(false);
		bAiming = false;
	}
	UpdateAimCamera(DeltaTime);
}

void UNCWeaponComponent::UpdateAimCamera(float Dt)
{
	ACharacter* C = Cast<ACharacter>(GetOwner());
	USpringArmComponent* Arm = C ? C->FindComponentByClass<USpringArmComponent>() : nullptr;
	UCameraComponent* Cam = C ? C->FindComponentByClass<UCameraComponent>() : nullptr;
	if (!Arm || !Cam)
	{
		return;
	}
	if (DefaultArm < 0.f)
	{
		DefaultArm = Arm->TargetArmLength;
		DefaultFOV = Cam->FieldOfView;
		DefaultSocketOffset = Arm->SocketOffset;
		bDefaultYaw = C->bUseControllerRotationYaw;
		bDefaultOrient = C->GetCharacterMovement()->bOrientRotationToMovement;
	}
	const FNCWeaponDef& D = CurrentDef();
	const bool bAimNow = bAiming && D.Kind != ENCWeaponKind::Melee;
	const float K = FMath::Clamp(Dt * 10.f, 0.f, 1.f);
	Arm->TargetArmLength = FMath::Lerp(Arm->TargetArmLength, bAimNow ? 130.f : DefaultArm, K);
	Arm->SocketOffset = FMath::Lerp(Arm->SocketOffset, bAimNow ? FVector(0.f, 55.f, 45.f) : DefaultSocketOffset, K);
	Cam->SetFieldOfView(FMath::Lerp(Cam->FieldOfView, bAimNow ? FMath::Min(D.AimFOV, 60.f) : DefaultFOV, K));
	C->bUseControllerRotationYaw = bAimNow ? true : bDefaultYaw;
	C->GetCharacterMovement()->bOrientRotationToMovement = bAimNow ? false : bDefaultOrient;
	// recoil kicks the view up and settles
	if (Recoil > 0.01f)
	{
		if (APlayerController* PC = Cast<APlayerController>(C->GetController()))
		{
			const float Kick = FMath::Min(Recoil, Recoil * Dt * 25.f);
			PC->AddPitchInput(-Kick);
			Recoil -= Kick;
		}
	}
}

// ------------------------------------------------------------------ 24/7 food
const TArray<UNCWeaponComponent::FFood>& UNCWeaponComponent::FoodMenu()
{
	static const TArray<FFood> M = {{TEXT("Neon Cola"), 5, 25.f}, {TEXT("Energy drink"), 9, 40.f}, {TEXT("Hotdog"), 12, 60.f},
		{TEXT("Burrito"), 20, 100.f}, {TEXT("Lekarnicka"), 150, 200.f}};
	return M;
}

void UNCWeaponComponent::ServerBuyFood_Implementation(int32 Item)
{
	APawn* P = Cast<APawn>(GetOwner());
	ANCPlayerState* PS = P ? P->GetPlayerState<ANCPlayerState>() : nullptr;
	UNCHealthComponent* H = UNCHealthComponent::Of(P);
	if (PS && H && FoodMenu().IsValidIndex(Item) && H->Health < H->MaxHealth && PS->Spend(FoodMenu()[Item].Price))
	{
		H->Heal(FoodMenu()[Item].Heal);
	}
}
