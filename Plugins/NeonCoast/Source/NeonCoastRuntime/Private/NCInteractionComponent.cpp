#pragma warning(disable: 4456 4457 4458 4459)
#include "NCInteractionComponent.h"
#include "NCBusinessManager.h"
#include "NCPoliceManager.h"
#include "NCPoliceOfficer.h"

#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "NCTrafficManager.h"
#include "NCVehiclePawn.h"
#include "NCCraft.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"

static float BoardingDistance(const ANCVehiclePawn* Vehicle,const FVector& Point)
{
	const auto* Mesh=Vehicle->GetMesh();
	const FBox Box=Mesh->CalcBounds(FTransform::Identity).GetBox();
	const FVector Local=Mesh->GetComponentTransform().InverseTransformPosition(Point);
	return FVector::Dist(Point,Mesh->GetComponentTransform().TransformPosition(Box.GetClosestPointTo(Local)));
}

UNCInteractionComponent::UNCInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UNCInteractionComponent::BeginPlay()
{
	Super::BeginPlay();
	TryBindInput();
}

void UNCInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || BoundInput.Get() != Pawn->InputComponent) bBound = false;
	if (Pawn && Pawn->IsLocallyControlled() && !Pawn->IsHidden() && !Pawn->GetAttachParentActor())
	{
		const auto* PC=Cast<APlayerController>(Pawn->GetController());
		if (PC && PC->GetLocalPlayer()) if (auto* Sub=PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			if (Mapping && !Sub->HasMappingContext(Mapping)) Sub->AddMappingContext(Mapping,5);
	}
	if (!bBound)
	{
		RestoreAfterExit();
		TryBindInput();     // input component exists only once the pawn is possessed locally
	}
}

void UNCInteractionComponent::RestoreAfterExit()
{
	auto* Pawn=Cast<APawn>(GetOwner());
	auto* PC=Pawn?Cast<APlayerController>(Pawn->GetController()):nullptr;
	if (!PC || !PC->IsLocalController() || Pawn->IsHidden() || Pawn->GetAttachParentActor()) return;
	for (TActorIterator<ANCVehiclePawn> It(GetWorld());It;++It) It->RemoveLocalControls(PC);
	for (TActorIterator<ANCCraftPawn> It(GetWorld());It;++It) It->RemoveLocalControls(PC);
	PC->ResetIgnoreInputFlags(); TryBindInput();
}

void UNCInteractionComponent::TryBindInput()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled() || !Pawn->InputComponent)
	{
		return;
	}
	const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC)
	{
		return;
	}
	if (!InteractAction)
	{
		InteractAction = NewObject<UInputAction>(this, TEXT("IA_NC_Interact"));
		InteractAction->ValueType = EInputActionValueType::Boolean;
		Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_NC_OnFoot"));
		// Vehicle interaction is routed by the persistent player controller.
		BusinessAction = NewObject<UInputAction>(this, TEXT("IA_NC_Business"));
		BusinessAction->ValueType = EInputActionValueType::Boolean;
		Mapping->MapKey(BusinessAction,EKeys::E);
		Mapping->MapKey(BusinessAction,EKeys::Gamepad_DPad_Left);
	}
	if (UEnhancedInputLocalPlayerSubsystem* Sub = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
	{
		Sub->AddMappingContext(Mapping, 5);
	}
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(Pawn->InputComponent))
	{
		if (bBound && BoundInput.Get()==Pawn->InputComponent) return;
		EIC->BindAction(InteractAction, ETriggerEvent::Started, this, &UNCInteractionComponent::OnInteract);
		EIC->BindAction(BusinessAction, ETriggerEvent::Started, this, &UNCInteractionComponent::OnBusiness);
		bBound = true;
		BoundInput = Pawn->InputComponent;
	}
}

void UNCInteractionComponent::OnInteract(const FInputActionValue&)
{
	UE_LOG(LogTemp, Display, TEXT("[NCInteract] F pressed"));
	Interact();
}

void UNCInteractionComponent::Interact()
{
	ServerInteract();
}

void UNCInteractionComponent::OnBusiness(const FInputActionValue&) { ServerUseBusiness(); }
void UNCInteractionComponent::ServerUseBusiness_Implementation()
{
	if (auto* City = ANCBusinessManager::Get(this)) City->Use(Cast<APawn>(GetOwner()));
}

void UNCInteractionComponent::ServerInteract_Implementation()
{
	APawn* Me = Cast<APawn>(GetOwner());
	if (!Me)
	{
		return;
	}
	// riding as a passenger -> get out
	if (ANCVehiclePawn* Ride = Cast<ANCVehiclePawn>(Me->GetAttachParentActor()))
	{
		Ride->ExitPassenger(Me);
		return;
	}
	if (ANCCraftPawn* Boat = Cast<ANCCraftPawn>(Me->GetAttachParentActor()))
	{
		Boat->ExitPassenger(Me);
		return;
	}
	const FVector P = Me->GetActorLocation();
	// boats / jet skis: closest hull within reach (measured to the hull, they are long)
	{
		ANCCraftPawn* BestCraft = nullptr;
		float BestD = 350.f;
		for (TActorIterator<ANCCraftPawn> It(GetWorld()); It; ++It)
		{
			if (It->bWrecked || !It->Box)
			{
				continue;
			}
			FVector Closest;
			// Box distance can return -1 while physics state is unavailable.
			// Its oriented bounds still provide a reliable boarding distance.
			const FVector Local=It->Box->GetComponentTransform().InverseTransformPosition(P);
			const FVector Ext=It->Box->GetUnscaledBoxExtent();
			Closest=It->Box->GetComponentTransform().TransformPosition(FVector(FMath::Clamp(Local.X,-Ext.X,Ext.X),FMath::Clamp(Local.Y,-Ext.Y,Ext.Y),FMath::Clamp(Local.Z,-Ext.Z,Ext.Z)));
			const float D = FVector::Dist(P,Closest);
			if (D >= 0.f && D < BestD)
			{
				BestD = D;
				BestCraft = *It;
			}
		}
		if (BestCraft)
		{
			if (!BestCraft->Driver)
			{
				if (BestCraft->EnterCraft(Me)) return;
			}
			if (BestCraft->Driver != Me && BestCraft->EnterAsPassenger(Me))
			{
				return;
			}
		}
	}
	// 0) a car with a driver next to us -> hop in as passenger (multiplayer)
	for (TActorIterator<ANCVehiclePawn> It(GetWorld()); It; ++It)
	{
		if (BoardingDistance(*It,P) < EnterRadius && It->Driver && It->Driver != Me && !It->Body.bWrecked)
		{
			if (It->EnterAsPassenger(Me))
			{
				return;
			}
		}
	}
	// 1) nearest free drivable car
	ANCVehiclePawn* Best = nullptr;
	float BestD = EnterRadius;
	for (TActorIterator<ANCVehiclePawn> It(GetWorld()); It; ++It)
	{
		const float D = BoardingDistance(*It,P);
				// parked police cars (crew out on foot) can be stolen too
		if (D < BestD && !It->Body.bWrecked && !It->Driver && (!It->GetController() || Cast<ANCPoliceDriver>(It->GetController())))
		{
			Best = *It;
			BestD = D;
		}
	}
		// 2) otherwise take a car from passing traffic (GTA style)
	bool bCarjack = false;
	if (!Best)
	{
		for (TActorIterator<ANCTrafficManager> It(GetWorld()); It && !Best; ++It)
		{
			Best = It->TakeVehicleNear(P, CarjackRadius);
			bCarjack = Best != nullptr;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[NCInteract] at %s parked=%s carjack=%d"), *P.ToCompactString(), Best ? *Best->GetName() : TEXT("none"), bCarjack ? 1 : 0);
	if (Best)
	{
		AController* Crim = Me ? Me->GetController() : nullptr;
		if (ANCPoliceDriver* Cop = Cast<ANCPoliceDriver>(Best->GetController()))
		{
			Cop->UnPossess();
			Cop->Destroy();
			Best->SetSiren(false);
			ANCPoliceManager::ReportCrime(GetWorld(), Crim, 2, P, false, true);       // stealing a squad car
		}
		else if (bCarjack)
		{
			ANCPoliceManager::ReportCrime(GetWorld(), Crim, 1, P, true, false, true);  // only if a cop sees it
		}
		Best->EnterVehicle(Me);
	}
}
