#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "NCPlayerState.generated.h"

/** Money (and later wanted level / reputation) per player, replicated. */
UCLASS()
class NEONCOASTRUNTIME_API ANCPlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "NeonCoast") int32 Money = 50000;

	/** Server: returns false if the player cannot afford it. */
	UFUNCTION(BlueprintCallable, Category = "NeonCoast") bool Spend(int32 Amount);
	UFUNCTION(BlueprintCallable, Category = "NeonCoast") void Earn(int32 Amount);

	/** Wanted level 0-5 stars (see ANCPoliceManager). bWantedSeen = police currently see the player
	 *  (HUD: solid stars; flashing = they are searching). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "NeonCoast") int32 Wanted = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "NeonCoast") bool bWantedSeen = false;
	/** Server bookkeeping for losing the cops. */
	FVector LastSeenLoc = FVector::ZeroVector;
	float UnseenTime = 0.f;
	/** Server: +Stars (bMinimum: raise to at least Stars). */
	void AddWanted(int32 Stars, bool bMinimum = false);
	void ClearWanted();

	/** Wanted level is never raised while true (cheat NOCOPS). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "NeonCoast") bool bNeverWanted = false;

	/** GTA-style cheat codes typed during play (see NCHUD / README). */
	UFUNCTION(Server, Reliable) void ServerCheat(FName Code);
	UFUNCTION(NetMulticast, Reliable) void MulticastWorldCheat(FName Code);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- story missions (ANCMissionManager)
	UPROPERTY(Replicated) TArray<FName> Completed;
	UPROPERTY(Replicated) FName ActiveMission;
	UPROPERTY(Replicated) FString Objective;
	UPROPERTY(Replicated) FVector_NetQuantize ObjectiveLoc;
	UPROPERTY(Replicated) bool bObjective = false;
	UPROPERTY(Replicated) int32 BusinessVisits = 0;
	UPROPERTY(Replicated) int32 CompletedRobberies = 0;
	FName LastBusiness, LastRobbedBusiness;
	UFUNCTION(Client, Reliable) void ClientOpenShop(int32 Type, const FString& Title);
	UFUNCTION(Client, Reliable) void ClientCloseShop();
	UFUNCTION(Server, Reliable) void ServerStartMission(FName Id);
	UFUNCTION(Client, Reliable) void ClientSubtitle(const FString& Speaker, const FString& Text, float Seconds);
	UFUNCTION(Client, Reliable) void ClientBanner(const FString& Text, bool bGood);
	FString SubSpeaker, SubText;
	double SubUntil = 0.0;

	// ---- services: fuel, robbery, barber, Pay'n'Spray, bank
	/** Op: 0 refuel, 1 rob the till, 2 barber (Option = hair colour), 3 Pay'n'Spray, 4 deposit, 5 withdraw. */
	UFUNCTION(Server, Reliable) void ServerService(int32 Op, int32 Option, FVector_NetQuantize Where);
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "NeonCoast") int32 BankMoney = 0;
	UPROPERTY(ReplicatedUsing = OnRep_Outfit, BlueprintReadOnly, Category = "NeonCoast") int32 HairColor = -1;

	// ---- wardrobe (NEON THREADS)
	UPROPERTY(ReplicatedUsing = OnRep_Outfit, BlueprintReadOnly, Category = "NeonCoast") int32 Outfit = -1;
	UFUNCTION(Server, Reliable) void ServerBuyOutfit(int32 Index);
	UFUNCTION() void OnRep_Outfit();

	// ---- casino (NEON PALACE) and bars: the server rolls, the owner gets the result
	/** Game 0 slots, 1 roulette, 2 blackjack deal, 3 blackjack hit, 4 blackjack stand, 5 drink. */
	UFUNCTION(Server, Reliable) void ServerCasino(int32 Game, int32 Option, int32 Bet);
	UFUNCTION(Client, Reliable) void ClientCasinoResult(const FString& Text, int32 Win);
	/** Blackjack hand (card values 1..13), 0 = no game, 1 = playing, 2 = finished. */
	UPROPERTY(Replicated) TArray<uint8> BJPlayer;
	UPROPERTY(Replicated) TArray<uint8> BJDealer;
	UPROPERTY(Replicated) uint8 BJState = 0;
	int32 BJBet = 0;
	/** Last casino result shown by the HUD (owner only). */
	FString CasinoText;
	int32 CasinoWin = 0;
	double CasinoTime = -100.0;
	static int32 HandValue(const TArray<uint8>& Cards);
protected:
	/** House limit: at most $5000 profit per 10 minutes. */
	double WinWindow = 0.0;
	int32 WonInWindow = 0;
	int32 PayOut(int32 Amount);
	void FinishBlackjack();
public:


	/** Sun / sky / fog preset: 0 = day, 1 = golden hour, 2 = neon night. */
	static void ApplyTimeOfDay(UWorld* World, int32 Preset);
};
