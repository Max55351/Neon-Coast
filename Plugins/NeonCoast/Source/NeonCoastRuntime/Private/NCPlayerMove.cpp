#pragma warning(disable: 4456 4457 4458 4459)
#include "NCPlayerMove.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "NCHealthComponent.h"
#include "NCSound.h"
#include "NCWardrobe.h"
#include "NCWater.h"
#include "Net/UnrealNetwork.h"

UNCPlayerMove::UNCPlayerMove()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UNCPlayerMove::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UNCPlayerMove, bSprint);
	DOREPLIFETIME(UNCPlayerMove, bInCover);
}

void UNCPlayerMove::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* C = Cast<ACharacter>(GetOwner()))
	{
		C->GetCharacterMovement()->NavAgentProps.bCanCrouch = true;
		C->GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
		C->GetCharacterMovement()->MaxFlySpeed = SwimSpeed;
		C->GetCharacterMovement()->MaxWalkSpeedCrouched = 220.f;
		C->bUseControllerRotationYaw = false;
		C->GetCharacterMovement()->bOrientRotationToMovement = true;
		C->GetCharacterMovement()->RotationRate = FRotator(0.f, 640.f, 0.f);
	}
	Oxygen = MaxOxygen;
}

bool UNCPlayerMove::Mine() const
{
	const APawn* P = Cast<APawn>(GetOwner());
	const APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	return PC && PC->IsLocalController() && PC->GetPawn() == P;
}

void UNCPlayerMove::SetupInput()
{
	APawn* P = Cast<APawn>(GetOwner());
	APlayerController* PC = P ? Cast<APlayerController>(P->GetController()) : nullptr;
	if (!PC || !PC->IsLocalController() || !PC->InputComponent)
	{
		return;
	}
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PC->InputComponent);
	UEnhancedInputLocalPlayerSubsystem* Sub = PC->GetLocalPlayer() ? PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!EIC || !Sub)
	{
		return;
	}
	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_NCMove"));
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
	UInputAction* View = Make(TEXT("IA_NCView"), {EKeys::V, EKeys::Gamepad_RightThumbstick});
	UInputAction* Sprint = Make(TEXT("IA_NCSprint"), {EKeys::LeftShift, EKeys::Gamepad_LeftThumbstick});
	UInputAction* Crouch = Make(TEXT("IA_NCCrouch"), {EKeys::LeftControl, EKeys::Gamepad_FaceButton_Right});
	UInputAction* Cover = Make(TEXT("IA_NCCover"), {EKeys::C, EKeys::Gamepad_DPad_Right});
	UInputAction* Up = Make(TEXT("IA_NCSwimUp"), {EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom});
	EIC->BindAction(View, ETriggerEvent::Started, this, &UNCPlayerMove::OnView);
	EIC->BindAction(Sprint, ETriggerEvent::Started, this, &UNCPlayerMove::OnSprint);
	EIC->BindAction(Sprint, ETriggerEvent::Completed, this, &UNCPlayerMove::OnSprintEnd);
	EIC->BindAction(Crouch, ETriggerEvent::Started, this, &UNCPlayerMove::OnCrouch);
	EIC->BindAction(Crouch, ETriggerEvent::Completed, this, &UNCPlayerMove::OnCrouchEnd);
	EIC->BindAction(Cover, ETriggerEvent::Started, this, &UNCPlayerMove::OnCover);
	EIC->BindAction(Up, ETriggerEvent::Started, this, &UNCPlayerMove::OnUp);
	EIC->BindAction(Up, ETriggerEvent::Completed, this, &UNCPlayerMove::OnUpEnd);
	// walk + mouse look for the hero (the 5.8 Third Person template adds these in its own
	// PlayerController, which Neon Coast replaces, so they are mapped here)
	UInputAction* MoveA = NewObject<UInputAction>(this, TEXT("IA_NCMove2D"));
	MoveA->ValueType = EInputActionValueType::Axis2D;
	Actions.Add(MoveA);
	auto MapMove = [this, MoveA](const FKey& K, bool bSwizzle, bool bNegate)
	{
		FEnhancedActionKeyMapping& M = Mapping->MapKey(MoveA, K);
		if (bSwizzle)
		{
			M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Mapping));
		}
		if (bNegate)
		{
			M.Modifiers.Add(NewObject<UInputModifierNegate>(Mapping));
		}
	};
	MapMove(EKeys::W, true, false);
	MapMove(EKeys::S, true, true);
	MapMove(EKeys::D, false, false);
	MapMove(EKeys::A, false, true);
	MapMove(EKeys::Up, true, false);
	MapMove(EKeys::Down, true, true);
	MapMove(EKeys::Right, false, false);
	MapMove(EKeys::Left, false, true);
	Mapping->MapKey(MoveA, EKeys::Gamepad_Left2D);
	UInputAction* LookA = NewObject<UInputAction>(this, TEXT("IA_NCLook2D"));
	LookA->ValueType = EInputActionValueType::Axis2D;
	Actions.Add(LookA);
	Mapping->MapKey(LookA, EKeys::Mouse2D);
	Mapping->MapKey(LookA, EKeys::Gamepad_Right2D);
	EIC->BindAction(MoveA, ETriggerEvent::Triggered, this, &UNCPlayerMove::OnMove);
	EIC->BindAction(LookA, ETriggerEvent::Triggered, this, &UNCPlayerMove::OnLook);
	Sub->AddMappingContext(Mapping, 1);
	bInput = true;
}

// ---------------------------------------------------------------------- input
void UNCPlayerMove::OnMove(const FInputActionValue& V)
{
	APawn* P = Cast<APawn>(GetOwner());
	if (!Mine() || !P || !P->GetController())
	{
		return;
	}
	const FVector2D In = V.Get<FVector2D>();
	const FRotator Ctrl = P->GetController()->GetControlRotation();
	// swimming follows the full view (look down + W dives), walking only the heading
	const FRotator Dir = bSwimming ? Ctrl : FRotator(0.f, Ctrl.Yaw, 0.f);
	P->AddMovementInput(FRotationMatrix(Dir).GetUnitAxis(EAxis::X), In.Y);
	P->AddMovementInput(FRotationMatrix(FRotator(0.f, Ctrl.Yaw, 0.f)).GetUnitAxis(EAxis::Y), In.X);
}

void UNCPlayerMove::OnLook(const FInputActionValue& V)
{
	APawn* P = Cast<APawn>(GetOwner());
	if (!Mine() || !P)
	{
		return;
	}
	const FVector2D In = V.Get<FVector2D>();
	P->AddControllerYawInput(In.X);
	P->AddControllerPitchInput(-In.Y);
}

void UNCPlayerMove::OnView(const FInputActionValue&)
{
	if (Mine())
	{
		bFirstPerson = !bFirstPerson;
		ApplyView();
	}
}

void UNCPlayerMove::ApplyView()
{
	ACharacter* C = Cast<ACharacter>(GetOwner());
	USpringArmComponent* Arm = C ? C->FindComponentByClass<USpringArmComponent>() : nullptr;
	if (!Arm)
	{
		return;
	}
	if (ArmLength < 0.f)
	{
		ArmLength = Arm->TargetArmLength;
		ArmOffset = Arm->GetRelativeLocation();
		ArmSocket = Arm->SocketOffset;
	}
	if (bFirstPerson)
	{
		// eyes: the camera sits in the head, the body turns with the view
		Arm->TargetArmLength = 0.f;
		Arm->SocketOffset = FVector::ZeroVector;
		Arm->SetRelativeLocation(FVector(18.f, 0.f, C->BaseEyeHeight + 8.f));
		Arm->bEnableCameraLag = false;
		C->bUseControllerRotationYaw = true;
		C->GetCharacterMovement()->bOrientRotationToMovement = false;
	}
	else
	{
		Arm->TargetArmLength = ArmLength;
		Arm->SocketOffset = ArmSocket;
		Arm->SetRelativeLocation(ArmOffset);
		C->bUseControllerRotationYaw = false;
		C->GetCharacterMovement()->bOrientRotationToMovement = true;
	}
	// do not render your own head in first person
	C->GetMesh()->SetOwnerNoSee(bFirstPerson);
	if (const UNCOutfitComponent* O = C->FindComponentByClass<UNCOutfitComponent>())
	{
		if (O->Body)
		{
			O->Body->SetOwnerNoSee(bFirstPerson);
		}
	}
}

void UNCPlayerMove::OnSprint(const FInputActionValue&)
{
	if (Mine())
	{
		bSprint = true;
		ServerSprint(true);
	}
}

void UNCPlayerMove::OnSprintEnd(const FInputActionValue&)
{
	if (Mine())
	{
		bSprint = false;
		ServerSprint(false);
	}
}

void UNCPlayerMove::ServerSprint_Implementation(bool bOn)
{
	bSprint = bOn;
}

void UNCPlayerMove::OnCrouch(const FInputActionValue&)
{
	ACharacter* C = Cast<ACharacter>(GetOwner());
	if (!Mine() || !C)
	{
		return;
	}
	if (bSwimming)
	{
		bDive = true;
	}
	else if (C->bIsCrouched)
	{
		C->UnCrouch();
	}
	else
	{
		C->Crouch();
	}
}

void UNCPlayerMove::OnCrouchEnd(const FInputActionValue&)
{
	bDive = false;
}

void UNCPlayerMove::OnUp(const FInputActionValue&)
{
	bRise = true;
	ACharacter* C = Cast<ACharacter>(GetOwner());
	if (Mine() && C && !bSwimming)
	{
		C->Jump();
	}
}

void UNCPlayerMove::OnUpEnd(const FInputActionValue&)
{
	bRise = false;
	if (ACharacter* C = Cast<ACharacter>(GetOwner()))
	{
		C->StopJumping();
	}
}

void UNCPlayerMove::OnCover(const FInputActionValue&)
{
	ACharacter* C = Cast<ACharacter>(GetOwner());
	if (!Mine() || !C || bSwimming)
	{
		return;
	}
	if (bInCover)
	{
		bInCover = false;
		C->UnCrouch();
		ServerCover(false);
		return;
	}
	// a wall / car / crate within 1.5 m in front of you?
	FHitResult H;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(NCCover), false, C);
	const FVector From = C->GetActorLocation();
	const FVector Fwd = C->GetActorForwardVector();
	if (GetWorld()->LineTraceSingleByChannel(H, From, From + Fwd * 150.f, ECC_Visibility, Q))
	{
		bInCover = true;
		C->SetActorRotation(FRotator(0.f, (-H.ImpactNormal).Rotation().Yaw, 0.f));
		C->SetActorLocation(H.ImpactPoint + H.ImpactNormal * (C->GetCapsuleComponent()->GetScaledCapsuleRadius() + 5.f)
			+ FVector(0, 0, From.Z - H.ImpactPoint.Z));
		C->Crouch();
		ServerCover(true);
	}
}

void UNCPlayerMove::ServerCover_Implementation(bool bOn)
{
	bInCover = bOn;
	if (ACharacter* C = Cast<ACharacter>(GetOwner()))
	{
		if (bOn)
		{
			C->Crouch();
		}
		else
		{
			C->UnCrouch();
		}
	}
}

// ---------------------------------------------------------------------- tick
void UNCPlayerMove::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(Dt, TickType, ThisTickFunction);
	if (!SelfTestMove.IsNearlyZero() && Mine()) OnMove(FInputActionValue(SelfTestMove));
	ACharacter* C = Cast<ACharacter>(GetOwner());
	if (!C)
	{
		return;
	}
	if (!bInput && Mine())
	{
		SetupInput();
	}
	if (Mine() && Mapping)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		const APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
		if (PC && PC->GetLocalPlayer())
		{
			if (auto* Sub = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (!Sub->HasMappingContext(Mapping)) Sub->AddMappingContext(Mapping, 1);
			}
		}
	}
	UCharacterMovementComponent* M = C->GetCharacterMovement();
	M->MaxWalkSpeed = bInCover ? 150.f : (bSprint ? SprintSpeed : WalkSpeed);
	M->MaxFlySpeed = bSprint ? SwimSpeed * 1.5f : SwimSpeed;
	if (bInCover && C->GetVelocity().Size2D() > 10.f)
	{
		// sliding along the cover: keep facing the wall
		FHitResult H;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(NCCover2), false, C);
		if (!GetWorld()->LineTraceSingleByChannel(H, C->GetActorLocation(), C->GetActorLocation() + C->GetActorForwardVector() * 150.f,
			ECC_Visibility, Q) && Mine())
		{
			bInCover = false;                                             // ran out of wall
			C->UnCrouch();
			ServerCover(false);
		}
	}
	UpdateWater(Dt);
}

void UNCPlayerMove::UpdateWater(float Dt)
{
	ACharacter* C = Cast<ACharacter>(GetOwner());
	UCharacterMovementComponent* M = C->GetCharacterMovement();
	if (C->IsHidden() || C->GetAttachParentActor())
	{
		return;                                                           // in a car / boat
	}
	const FVector L = C->GetActorLocation();
	const float Surf = ANCWaterManager::SurfaceZ(GetWorld(), L);
	const float Half = C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	// what is under us?
	FHitResult G;
	FCollisionObjectQueryParams O;
	O.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(NCSwimBed),true,C);
	const float Ground = GetWorld()->LineTraceSingleByObjectType(G, L, L - FVector(0, 0, 200000.f), O,GroundQuery) ? G.ImpactPoint.Z : L.Z - 200000.f;
	const bool bDeep = Surf - Ground > Half * 1.6f;                        // deeper than chest height
	const bool bWet = L.Z - Half * 0.3f < Surf;
	if (!bSwimming && bDeep && bWet && ANCWaterManager::Get(GetWorld()))
	{
		bSwimming = true;
		bInCover=false;
		M->SetMovementMode(MOVE_Flying);
		M->Velocity *= 0.3f;
		ANCSoundBank::Play(C, TEXT("Splash"), L, 0.8f);
		if (C->bIsCrouched)
		{
			C->UnCrouch();
		}
	}
	else if (bSwimming && (!bDeep || L.Z > Surf + Half))
	{
		bSwimming = false;
		M->SetMovementMode(MOVE_Falling);
		if (!bDeep)
		{
			M->Velocity.Z = 0.f;
		}
	}
	if (bSwimming)
	{
		// Exiting a vehicle must restore water movement even when bSwimming was already true.
		if (M->MovementMode!=MOVE_Flying) M->SetMovementMode(MOVE_Flying);
		if ((Mine() || C->HasAuthority()) && M->Velocity.SizeSquared2D()>400.f && !bFirstPerson)
			C->SetActorRotation(FMath::RInterpTo(C->GetActorRotation(),FRotator(0,M->Velocity.Rotation().Yaw,0),Dt,6.f));
		const float Top = Surf - Half * 0.55f;                            // head out of the water
		if (Mine())
		{
			if (bDive)
			{
				C->AddMovementInput(FVector::DownVector, 1.f);
			}
			else if (bRise)
			{
				C->AddMovementInput(FVector::UpVector,1.f);
			}
		}
		if (L.Z > Top && M->Velocity.Z > 0.f)
		{
			M->Velocity.Z = 0.f;
			C->SetActorLocation(FVector(L.X, L.Y, Top),true);
		}
	}
	// air
	bUnderWater = bSwimming && L.Z + Half * 0.8f < Surf;
	Oxygen = bUnderWater ? FMath::Max(0.f, Oxygen - Dt) : FMath::Min(MaxOxygen, Oxygen + Dt * 8.f);
	if (bUnderWater && Oxygen <= 0.f && C->HasAuthority())
	{
		HurtTimer -= Dt;
		if (HurtTimer <= 0.f)
		{
			HurtTimer = 1.f;
			if (UNCHealthComponent* H = UNCHealthComponent::Of(C))
			{
				H->Hurt(12.f, nullptr, FVector::ZeroVector);                // drowning
			}
		}
	}
}
