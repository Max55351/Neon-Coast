#pragma warning(disable: 4456 4457 4458 4459)
#include "NCTypes.h"

#include "Algo/BinarySearch.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void FNCPath::Build()
{
	Dist.SetNum(Points.Num());
	float Acc = 0.f;
	for (int32 i = 0; i < Points.Num(); ++i)
	{
		if (i > 0)
		{
			Acc += FVector::Dist(Points[i - 1], Points[i]);
		}
		Dist[i] = Acc;
	}
	Length = Acc;
}

void FNCPath::Eval(float S, FVector& OutLoc, FVector& OutDir) const
{
	if (Points.Num() < 2)
	{
		OutLoc = Points.Num() ? Points[0] : FVector::ZeroVector;
		OutDir = FVector::ForwardVector;
		return;
	}
	S = FMath::Clamp(S, 0.f, Length);
	int32 Hi = Algo::UpperBound(Dist, S);
	Hi = FMath::Clamp(Hi, 1, Points.Num() - 1);
	const int32 Lo = Hi - 1;
	const float Seg = FMath::Max(Dist[Hi] - Dist[Lo], KINDA_SMALL_NUMBER);
	const float A = (S - Dist[Lo]) / Seg;
	OutLoc = FMath::Lerp(Points[Lo], Points[Hi], A);
	OutDir = (Points[Hi] - Points[Lo]).GetSafeNormal();
}

bool NCData::LoadPaths(const FString& RelPath, const FString& Key, TArray<FNCPath>& Out, float DefaultSpeed)
{
	const FString File = FPaths::Combine(FPaths::ProjectContentDir(), RelPath);
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *File))
	{
		UE_LOG(LogTemp, Warning, TEXT("[NeonCoast] missing %s"), *File);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Root->TryGetArrayField(Key, Items))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& V : *Items)
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		const TArray<TSharedPtr<FJsonValue>>* Pts = nullptr;
		if (!O.IsValid() || !O->TryGetArrayField(TEXT("pts"), Pts) || Pts->Num() < 2)
		{
			continue;
		}
		FNCPath P;
		for (const TSharedPtr<FJsonValue>& PV : *Pts)
		{
			const TArray<TSharedPtr<FJsonValue>>& C = PV->AsArray();
			if (C.Num() >= 3)
			{
				P.Points.Add(ToUE(C[0]->AsNumber(), C[1]->AsNumber(), C[2]->AsNumber()));
			}
		}
		double Speed = DefaultSpeed / 100.0;
		O->TryGetNumberField(TEXT("speed"), Speed);
		P.Speed = static_cast<float>(Speed) * 100.f;
		P.Build();
		if (P.Length > 100.f)
		{
			Out.Add(MoveTemp(P));
		}
	}
	return Out.Num() > 0;
}
