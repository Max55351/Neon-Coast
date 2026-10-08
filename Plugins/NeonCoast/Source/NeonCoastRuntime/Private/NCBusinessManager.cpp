#include "NCBusinessManager.h"
#include "NCPickup.h"
#include "NCPlayerState.h"
#include "NCWeaponComponent.h"
#include "NCHealthComponent.h"
#include "NCPoliceManager.h"
#include "NCCrowdManager.h"
#include "NCTuningGarage.h"
#include "NCHUD.h"
#include "NCTypes.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"

ANCBusinessManager::ANCBusinessManager()
{
 PrimaryActorTick.bCanEverTick = true;
 PrimaryActorTick.TickInterval = .25f;
 bReplicates = true;
 bAlwaysRelevant = true;
 RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}
ANCBusinessManager* ANCBusinessManager::Get(const UObject* Ctx)
{
 if (Ctx && Ctx->GetWorld()) for (TActorIterator<ANCBusinessManager> It(Ctx->GetWorld()); It; ++It) return *It;
 return nullptr;
}
void ANCBusinessManager::BeginPlay()
{
 Super::BeginPlay();
 GetWorldTimerManager().SetTimer(SetupTimer,this,&ANCBusinessManager::Setup,1.f,false);
}
UMaterialInterface* ANCBusinessManager::Material(FName Name)
{
 if (auto* Cached = Materials.Find(Name)) return Cached->Get();
 const FString N = Name.ToString();
 auto* Result = LoadObject<UMaterialInterface>(nullptr,*(TEXT("/Game/NeonCoast/Materials/Instances/")+N+TEXT(".")+N));
 Materials.Add(Name,Result);
 return Result;
}
void ANCBusinessManager::RepairMaterials(UMeshComponent* Mesh)
{
 if (!Mesh) return;
 if (auto* Bridge=Cast<UStaticMeshComponent>(Mesh); Bridge && !Bridge->IsSimulatingPhysics() && Bridge->GetStaticMesh() && Bridge->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Game/NeonCoast/")) && (Bridge->GetStaticMesh()->GetName().Contains(TEXT("Bridge")) || Bridge->GetStaticMesh()->GetName().StartsWith(TEXT("SM_NC_Road")) || Bridge->GetStaticMesh()->GetName().StartsWith(TEXT("SM_NC_Terrain"))))
 {
  // A convex hull across the whole bascule bridge blocks its ramps/open span.
  if (auto* Body=Bridge->GetStaticMesh()->GetBodySetup())
  {
   Body->CollisionTraceFlag=CTF_UseComplexAsSimple;
   Body->bDoubleSidedGeometry=true;
   Body->CreatePhysicsMeshes();
   Bridge->SetCollisionProfileName(TEXT("BlockAll"));
   Bridge->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
   if (Bridge->GetOwner()) Bridge->GetOwner()->SetActorEnableCollision(true);
   Bridge->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
   Bridge->RecreatePhysicsState();
   if (Body->TriMeshGeometries.Num()==0) UE_LOG(LogTemp,Error,TEXT("[NCSurfaceRepair] No triangle collision: %s"),*Bridge->GetStaticMesh()->GetPathName());
  }
 }
 const auto* Static = Cast<UStaticMeshComponent>(Mesh);
 const auto* Skel = Cast<USkeletalMeshComponent>(Mesh);
 const FString Asset = Static && Static->GetStaticMesh() ? Static->GetStaticMesh()->GetPathName() : Skel && Skel->GetSkeletalMeshAsset() ? Skel->GetSkeletalMeshAsset()->GetPathName() : FString();
 const bool CityAsset = Asset.StartsWith(TEXT("/Game/NeonCoast/"));
 const auto Slots = Mesh->GetMaterialSlotNames();
 bool Glass = false;
 for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
 {
  auto* Current = Mesh->GetMaterial(i);
  if (!Current || Current->GetPathName().Contains(TEXT("WorldGridMaterial")) || Current->GetPathName().Contains(TEXT("DefaultMaterial")))
  {
   UMaterialInterface* Replacement = nullptr;
   // Respect authored mesh defaults before trying named replacements or a fallback.
   if (Static && Static->GetStaticMesh()->GetStaticMaterials().IsValidIndex(i)) Replacement = Static->GetStaticMesh()->GetStaticMaterials()[i].MaterialInterface;
   if (Skel && Skel->GetSkeletalMeshAsset()->GetMaterials().IsValidIndex(i)) Replacement = Skel->GetSkeletalMeshAsset()->GetMaterials()[i].MaterialInterface;
   if (Replacement && (Replacement->GetPathName().Contains(TEXT("WorldGridMaterial")) || Replacement->GetPathName().Contains(TEXT("DefaultMaterial")))) Replacement = nullptr;
   if (!Replacement && CityAsset && Slots.IsValidIndex(i))
   {
    FString Name = Slots[i].ToString();
    if (Name.StartsWith(TEXT("M_NC"))) Name = TEXT("MI_") + Name.RightChop(2);
    Replacement = Material(FName(*Name));
   }
   // Do not put concrete onto a MetaHuman skin/face or an intentional transparent slot.
   if (!Replacement && CityAsset) Replacement = Material(TEXT("MI_NC2_Concrete_PBR"));
   if (Replacement) { Mesh->SetMaterial(i,Replacement); Current = Replacement; }
  }
  Glass |= Current && IsTranslucentBlendMode(Current->GetBlendMode());
 }
 if (auto* S = Cast<UStaticMeshComponent>(Mesh); S && CityAsset && Glass) S->bDisallowNanite = true;
}
void ANCBusinessManager::RepairBatch()
{
 // Spread large world scans over frames instead of blocking gameplay for one big repair.
 const int32 End = FMath::Min(RepairIndex+24,RepairQueue.Num());
 for (; RepairIndex < End; ++RepairIndex) if (RepairQueue[RepairIndex].IsValid()) RepairMaterials(RepairQueue[RepairIndex].Get());
 if (RepairIndex >= RepairQueue.Num()) { RepairQueue.Reset(); GetWorldTimerManager().ClearTimer(RepairTimer); }
}
void ANCBusinessManager::Setup()
{
 TArray<UStaticMeshComponent*> Interiors;
 for (TActorIterator<AActor> It(GetWorld()); It; ++It)
 {
  TArray<UMeshComponent*> Meshes; It->GetComponents(Meshes);
  for (auto* Mesh : Meshes)
  {
   RepairQueue.Add(Mesh);
   if (auto* Static = Cast<UStaticMeshComponent>(Mesh); Static && Static->GetStaticMesh() && Static->GetStaticMesh()->GetName().StartsWith(TEXT("SM_NC2_Int_"))) Interiors.Add(Static);
  }
 }
 GetWorldTimerManager().SetTimer(RepairTimer,this,&ANCBusinessManager::RepairBatch,.1f,true);
 FString Text; TSharedPtr<FJsonObject> Root;
 if (!FFileHelper::LoadFileToString(Text,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("NeonCoast/Data/placements.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root) || !Root) return;
 const TSharedPtr<FJsonObject>* Meta; const TArray<TSharedPtr<FJsonValue>>* Places;
 if (!Root->TryGetObjectField(TEXT("meta"),Meta) || !(*Meta)->TryGetArrayField(TEXT("interiors"),Places)) return;
 for (const auto& Value : *Places)
 {
  const auto O = Value->AsObject(); if (!O) continue;
  const FString Asset = O->GetStringField(TEXT("asset"));
  // Public buildings remain on the map, but are not fabricated as shops.
  if (!Asset.StartsWith(TEXT("NC2_Int_"))) continue;
  FBusiness B; B.Id = FName(*Asset);
  if (Asset.Contains(TEXT("GunShop"))) { B.Type = 0; B.Title = TEXT("NEON ARMS / ZBRANE"); }
  else if (Asset.Contains(TEXT("Clothes"))) { B.Type = 2; B.Title = TEXT("NEON THREADS / OBLECENI"); }
  else if (Asset.Contains(TEXT("Barber"))) { B.Type = 10; B.Title = TEXT("NEON CUTS / HOLIC"); }
  else if (Asset.Contains(TEXT("Bank"))) { B.Type = 12; B.Title = TEXT("NEON BANK"); }
  else if (Asset.Contains(TEXT("Casino"))) { B.Type = 3; B.Title = TEXT("NEON PALACE / KASINO"); }
  else if (Asset.Contains(TEXT("Shop")) || Asset.Contains(TEXT("Restaurant"))) { B.Type = 9; B.Title = TEXT("OBCHOD / JIDLO"); }
  else if (Asset.Contains(TEXT("Bar")) || Asset.Contains(TEXT("Disco"))) { B.Type = 6; B.Title = TEXT("BAR / KLUB"); }
  else B.Title = TEXT("BYT / INTERIER");
  const FVector Center = NCData::ToUE(O->GetNumberField(TEXT("x")),O->GetNumberField(TEXT("y")),O->GetNumberField(TEXT("z")));
  const FVector Out = FRotator(0,-O->GetNumberField(TEXT("yaw")),0).RotateVector(FVector(0,-1,0));
  B.Bounds = FBox(Center-FVector(600,600,10),Center+FVector(600,600,400));
  for (auto* Mesh : Interiors) if (Mesh->GetStaticMesh()->GetName() == TEXT("SM_")+Asset && FVector::DistSquared2D(Mesh->Bounds.Origin,Center) < FMath::Square(3000.f))
  {
   B.Bounds = Mesh->Bounds.GetBox();
   if (auto* Body = Mesh->GetStaticMesh()->GetBodySetup()) { Body->CollisionTraceFlag = CTF_UseComplexAsSimple; Mesh->SetCollisionProfileName(TEXT("BlockAll")); Mesh->RecreatePhysicsState(); }
   break;
  }
  FVector C = B.Bounds.GetCenter(); C.Z = Center.Z;
  const FVector Ext = B.Bounds.GetExtent();
  const float HalfDepth = FMath::Abs(Out.X)*Ext.X + FMath::Abs(Out.Y)*Ext.Y;
  B.Entrance = C + Out*(HalfDepth+160.f) + FVector(0,0,100);
  B.Inside = C + Out*FMath::Max(0.f,HalfDepth-220.f) + FVector(0,0,100);
  B.Counter = C - Out*FMath::Min(250.f,HalfDepth*.4f) + FVector(0,0,100);
  // A textured support floor covers gaps without closing windows or door openings.
  auto* Floor = NewObject<UStaticMeshComponent>(this);
  Floor->SetupAttachment(RootComponent);
  Floor->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
  Floor->SetMaterial(0,Material(TEXT("MI_NC2_Garage_Floor")));
  Floor->SetWorldLocation(FVector(C.X,C.Y,Center.Z-8.f));
  Floor->SetWorldScale3D(FVector(FMath::Max(1.f,(Ext.X*2-40.f)/100.f),FMath::Max(1.f,(Ext.Y*2-40.f)/100.f),.12f));
  Floor->SetCollisionProfileName(TEXT("BlockAll")); Floor->RegisterComponent();
  auto* Sign = NewObject<UTextRenderComponent>(this); Sign->SetupAttachment(RootComponent);
  Sign->SetText(FText::FromString(B.Title+TEXT("\nE - VSTOUPIT"))); Sign->SetWorldSize(26.f);
  Sign->SetHorizontalAlignment(EHTA_Center); Sign->SetTextRenderColor(FColor(100,230,255));
  Sign->SetWorldLocation(B.Entrance+FVector(0,0,120)); Sign->SetWorldRotation(Out.Rotation()); Sign->RegisterComponent();
  if (B.Type >= 0)
  {
   for (TActorIterator<ANCShopZone> It(GetWorld()); It; ++It)
    if (It->ShopType != 7 && FVector::DistSquared2D(It->GetActorLocation(),C) < FMath::Square(900.f)) { B.Zone = *It; break; }
   if (!B.Zone.IsValid())
   {
    auto* Zone = GetWorld()->SpawnActorDeferred<ANCShopZone>(ANCShopZone::StaticClass(),FTransform(B.Counter));
    if (Zone) { Zone->ShopType = B.Type; Zone->Title = B.Title; Zone->bAutoOpen=false; Zone->FinishSpawning(FTransform(B.Counter)); B.Zone = Zone; }
   }
   else { B.Zone->ShopType = B.Type; B.Zone->Title = B.Title; B.Zone->bAutoOpen=false; B.Zone->SetActorLocation(B.Counter); }
  }
  Businesses.Add(B);
  if (B.Type>=0) for (TActorIterator<ANCCrowdManager> It(GetWorld());It;++It) It->AddInteriorResidents(B.Id,B.Bounds,B.Counter);
 }
	const TSharedPtr<FJsonObject>* Garage;
	if ((*Meta)->TryGetObjectField(TEXT("tuning_garage"),Garage))
	{
		const FVector At = NCData::ToUE((*Garage)->GetNumberField(TEXT("x")),(*Garage)->GetNumberField(TEXT("y")),(*Garage)->GetNumberField(TEXT("z")));
		ANCTuningGarage* Bay = nullptr;
		for (TActorIterator<ANCTuningGarage> It(GetWorld());It;++It) if (FVector::DistSquared2D(It->GetActorLocation(),At)<FMath::Square(1200.f)) { Bay=*It; break; }
		if (!Bay) Bay=GetWorld()->SpawnActor<ANCTuningGarage>(ANCTuningGarage::StaticClass(),At+FVector(0,0,150),FRotator::ZeroRotator);
		auto* Sign=NewObject<UTextRenderComponent>(this); Sign->SetupAttachment(RootComponent);
		Sign->SetText(FText::FromString(TEXT("NEON COAST CUSTOMS\nTUNING / OPRAVY - PRIJED AUTEM")));
		Sign->SetWorldSize(32.f); Sign->SetHorizontalAlignment(EHTA_Center); Sign->SetTextRenderColor(FColor(255,190,80));
		Sign->SetWorldLocation(At+FVector(0,0,340)); Sign->SetWorldRotation(FRotator(0,0,0)); Sign->RegisterComponent();
	}
 for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
  if (auto* PC = It->Get(); PC && PC->IsLocalController()) if (auto* HUD = Cast<ANCHUD>(PC->GetHUD())) HUD->RefreshCityMap();
}
FString ANCBusinessManager::Hint(const APawn* Pawn) const
{
 if (!Pawn || !Pawn->IsLocallyControlled() || Pawn->IsHidden()) return FString();
 const FVector L = Pawn->GetActorLocation();
 const auto* Weapon = UNCWeaponComponent::Of(Pawn);
 for (const auto& B : Businesses)
 {
  if (Weapon && Weapon->IsAiming() && (B.Type == 0 || B.Type == 9 || B.Type == 6 || B.Type == 12) && FVector::DistSquared(L,B.Counter)<FMath::Square(450.f)) return TEXT("MIR A E - PREPADNOUT POKLADNU (6 s)");
  if (FVector::DistSquared(L,B.Inside)<FMath::Square(200.f)) return TEXT("E - VYSTOUPIT");
  if (FVector::DistSquared(L,B.Entrance)<FMath::Square(250.f)) return TEXT("E - VSTOUPIT: ")+B.Title;
  if (B.Type>=0 && FVector::DistSquared(L,B.Counter)<FMath::Square(450.f)) return TEXT("E - OBCHOD / SLUZBY");
 }
 return FString();
}
bool ANCBusinessManager::NearService(const APawn* Pawn,int32 Type,float Radius) const
{
 if (!Pawn) return false;
 for (const auto& B : Businesses) if (B.Type==Type && FVector::DistSquared(Pawn->GetActorLocation(),B.Counter)<FMath::Square(Radius)) return true;
 return false;
}
bool ANCBusinessManager::Use(APawn* Pawn)
{
 auto* C = Cast<ACharacter>(Pawn); if (!HasAuthority() || !C || C->IsHidden() || C->GetAttachParentActor()) return false;
	if (const auto* Health=UNCHealthComponent::Of(C); Health && Health->bDead) return false;
 auto* PC = Cast<APlayerController>(C->GetController()); auto* PS = C->GetPlayerState<ANCPlayerState>(); if (!PC || !PS) return false;
 const FVector L = C->GetActorLocation();
 for (int32 i=0;i<Businesses.Num();++i)
 {
  auto& B=Businesses[i];
  if (UNCWeaponComponent::Of(C) && UNCWeaponComponent::Of(C)->IsAiming() && FVector::DistSquared(L,B.Counter)<FMath::Square(450.f)) return StartRobbery(PC,B.Counter);
  const bool Exit = (InsidePlaces.FindRef(C)==i+1 || B.Bounds.IsInsideOrOn(L)) && FVector::DistSquared(L,B.Inside)<FMath::Square(200.f);
  const bool Enter = FVector::DistSquared(L,B.Entrance)<FMath::Square(250.f);
  if (Enter || Exit)
  {
   FVector Dest = Exit ? B.Entrance : B.Inside;
   const FRotator Facing = Exit ? (B.Entrance-B.Inside).Rotation() : (B.Counter-B.Inside).Rotation();
   bool Safe = GetWorld()->FindTeleportSpot(C,Dest,Facing);
   if (!Safe && !Exit) { Dest=B.Bounds.GetCenter(); Dest.Z=B.Inside.Z; Safe=GetWorld()->FindTeleportSpot(C,Dest,Facing); }
   if (!Safe || !C->TeleportTo(Dest,FRotator(0,Facing.Yaw,0))) { PS->ClientBanner(TEXT("Vstup je blokovany. Ustup od prekazky."),false); return true; }
   C->GetCharacterMovement()->StopMovementImmediately(); C->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
   PC->SetControlRotation(FRotator(0,Facing.Yaw,0));
   InsidePlaces.Add(C,Exit?0:i+1);
   if (!Exit) { ++PS->BusinessVisits; PS->LastBusiness = B.Id; PS->ClientBanner(B.Title+TEXT(" - u pultu E, zpet ke dverim E"),true); }
   PS->ClientCloseShop(); return true;
  }
  if (B.Type>=0 && FVector::DistSquared(L,B.Counter)<FMath::Square(450.f)) { PS->ClientOpenShop(B.Type,B.Title); return true; }
 }
 return false;
}
bool ANCBusinessManager::StartRobbery(APlayerController* PC,const FVector& Where)
{
 if (!HasAuthority() || !PC || !PC->GetPawn()) return false;
 APawn* Pawn=PC->GetPawn(); auto* PS=PC->GetPlayerState<ANCPlayerState>(); auto* Weapon=UNCWeaponComponent::Of(Pawn);
 if (!PS) return false;
 if (!Weapon || !Weapon->IsAiming() || Weapon->CurrentClip()<=0 || (Weapon->CurrentDef().Kind!=ENCWeaponKind::Hitscan && Weapon->CurrentDef().Kind!=ENCWeaponKind::Shotgun))
 { PS->ClientBanner(TEXT("MIR nabitou zbrani na pult a stiskni E."),false); return true; }
 if (HoldUps.Contains(PC)) return true;
 for (int32 i=0;i<Businesses.Num();++i)
 {
  auto& B=Businesses[i];
  if (!(B.Type==0 || B.Type==9 || B.Type==6 || B.Type==12) || FVector::DistSquared(Pawn->GetActorLocation(),B.Counter)>FMath::Square(500.f) || FVector::DistSquared(Where,B.Counter)>FMath::Square(700.f)) continue;
  if (B.NextRobbery>GetWorld()->GetTimeSeconds()) { PS->ClientBanner(TEXT("Pokladna je uzamcena. Vrat se za pet minut."),false); return true; }
  B.NextRobbery=GetWorld()->GetTimeSeconds()+300.f;
  FHoldUp R; R.Place=i; R.Loot=B.Type==12?FMath::RandRange(3000,6000):FMath::RandRange(400,1500); HoldUps.Add(PC,R);
  PS->ClientCloseShop(); PS->ClientSubtitle(TEXT("Prodavac"),TEXT("Klid! Oteviram pokladnu. Nesundavej zbran..."),6.f);
  ANCPoliceManager::ReportCrime(GetWorld(),PC,B.Type==12?3:2,Pawn->GetActorLocation(),false,true);
  ANCCrowdManager::PanicAt(GetWorld(),B.Counter,2500.f); return true;
 }
 PS->ClientBanner(TEXT("Prepadeni je mozne jen u skutecne pokladny."),false); return true;
}
void ANCBusinessManager::Tick(float Dt)
{
 Super::Tick(Dt); if (!HasAuthority()) return;
 // Walking through an already open doorway counts too; do not require a portal.
 for (auto Player=GetWorld()->GetPlayerControllerIterator();Player;++Player)
 {
  const auto* PC=Player->Get(); ACharacter* C=PC?Cast<ACharacter>(PC->GetPawn()):nullptr;
  auto* PS=PC?PC->GetPlayerState<ANCPlayerState>():nullptr;
  if (!C || !PS || C->IsHidden() || C->GetAttachParentActor()) continue;
  int32 Place=0;
  for (int32 i=0;i<Businesses.Num();++i) if (Businesses[i].Bounds.IsInsideOrOn(C->GetActorLocation())) { Place=i+1; break; }
  if (Place>0 && InsidePlaces.FindRef(C)!=Place) { ++PS->BusinessVisits; PS->LastBusiness=Businesses[Place-1].Id; }
  InsidePlaces.Add(C,Place);
 }
 for (auto It=InsidePlaces.CreateIterator();It;++It) if (!It.Key().IsValid()) It.RemoveCurrent();
 for (auto It=HoldUps.CreateIterator();It;++It)
 {
  auto* PC=It.Key().Get(); auto& R=It.Value(); APawn* Pawn=PC?PC->GetPawn():nullptr;
  auto* PS=PC?PC->GetPlayerState<ANCPlayerState>():nullptr; auto* Weapon=UNCWeaponComponent::Of(Pawn); auto* Health=UNCHealthComponent::Of(Pawn);
  if (!PS || !Pawn || !Businesses.IsValidIndex(R.Place) || (Health && Health->bDead) || FVector::DistSquared(Pawn->GetActorLocation(),Businesses[R.Place].Counter)>FMath::Square(650.f))
  { if (PS) PS->ClientBanner(TEXT("PREPADENI PRERUSENO - zadne penize."),false); It.RemoveCurrent(); continue; }
  const bool Threat = Weapon && Weapon->IsAiming() && Weapon->CurrentClip()>0 && (Weapon->CurrentDef().Kind==ENCWeaponKind::Hitscan || Weapon->CurrentDef().Kind==ENCWeaponKind::Shotgun);
  R.Interrupted=Threat ? 0.f : R.Interrupted+Dt;
  if (R.Interrupted>.75f) { PS->ClientBanner(TEXT("Spustil jsi zbran. Prodavac uzamkl pokladnu."),false); It.RemoveCurrent(); continue; }
  if (Threat) R.Time+=Dt;
  if (R.Time>=6.f)
  { PS->Earn(R.Loot); ++PS->CompletedRobberies; PS->LastRobbedBusiness=Businesses[R.Place].Id; PS->ClientBanner(FString::Printf(TEXT("PREPADENI +$%d - unikni policii!"),R.Loot),true); It.RemoveCurrent(); }
 }
}
