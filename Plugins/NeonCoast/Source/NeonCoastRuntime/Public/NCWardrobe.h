#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "NCWardrobe.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class UAnimSequence;

USTRUCT(BlueprintType)
struct FNCOutfit
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wardrobe") FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wardrobe") TObjectPtr<USkeletalMesh> Mesh = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wardrobe") int32 Price = 500;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wardrobe") TObjectPtr<UAnimSequence> Idle = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wardrobe") TObjectPtr<UAnimSequence> Walk = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wardrobe") TObjectPtr<UAnimSequence> Run = nullptr;
	/** MetaHuman look: a whole character blueprint (body + face + hair) spawned onto the hero instead of Mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wardrobe") TSubclassOf<AActor> ActorClass;
};

/**
 * The hero's outfit. The player pawn (UE mannequin) keeps its own skeleton and animation
 * blueprint (hidden, still posing); this component shows the outfit mesh (MakeHuman skeleton)
 * and plays idle / walk / run from the pawn's speed.
 */
UCLASS()
class NEONCOASTRUNTIME_API UNCOutfitComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UNCOutfitComponent();
	UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY(Transient) TObjectPtr<AActor> Extra;
	UPROPERTY(Transient) FNCOutfit Look;
	void Wear(const FNCOutfit& Outfit, int32 Hair);
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	int32 Playing = -1;
};

/**
 * Outfits the hero can buy in the clothes shop (NEON THREADS). An outfit is another MakeHuman
 * character mesh on the same skeleton, so the hero's animations keep working.
 */
UCLASS()
class NEONCOASTRUNTIME_API ANCWardrobe : public AActor
{
	GENERATED_BODY()
public:
	ANCWardrobe();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wardrobe") TArray<FNCOutfit> Outfits;

	static ANCWardrobe* Get(const UObject* Context);
	/** Puts outfit Index (-1 = the default look) on a character pawn. */
	static void Apply(APawn* Pawn, int32 Index, int32 Hair = -1);
	/** Barber colours (NEON CUTS). */
	static FLinearColor HairColour(int32 Index);
};
