#pragma warning(disable: 4456 4457 4458 4459)
#include "NCTuning.h"

const TArray<FLinearColor>& NCTune::PaintColors()
{
	static const TArray<FLinearColor> C = {
		FLinearColor(0.80f, 0.18f, 0.40f), FLinearColor(0.05f, 0.55f, 0.65f), FLinearColor(0.85f, 0.60f, 0.03f),
		FLinearColor(0.85f, 0.30f, 0.02f), FLinearColor(0.02f, 0.02f, 0.025f), FLinearColor(0.82f, 0.82f, 0.80f),
		FLinearColor(0.50f, 0.02f, 0.02f), FLinearColor(0.03f, 0.10f, 0.35f), FLinearColor(0.05f, 0.25f, 0.12f),
		FLinearColor(0.35f, 0.08f, 0.45f), FLinearColor(0.55f, 0.56f, 0.58f), FLinearColor(0.10f, 0.85f, 0.55f)};
	return C;
}

const TArray<FLinearColor>& NCTune::NeonColors()
{
	static const TArray<FLinearColor> C = {
		FLinearColor(1.f, 0.1f, 0.8f), FLinearColor(0.1f, 0.9f, 1.f), FLinearColor(0.2f, 1.f, 0.3f),
		FLinearColor(1.f, 0.6f, 0.1f), FLinearColor(0.5f, 0.2f, 1.f), FLinearColor(1.f, 1.f, 1.f)};
	return C;
}

int32 NCTune::Price(const FNCTuning& A, const FNCTuning& B)
{
	int32 P = 0;
	if (!(A.Paint == B.Paint)) P += 800;
	if (A.Spoiler != B.Spoiler) P += 1500;
	if (A.Front != B.Front) P += 1200;
	if (A.Skirts != B.Skirts) P += 1000;
	if (A.OffroadMask != B.OffroadMask) P += 2500;
	if (A.Rims != B.Rims) P += 2000;
	if (A.Neon != B.Neon) P += 900;
	P += FMath::Max(0, B.Engine - A.Engine) * 4000;
	P += FMath::Max(0, B.Turbo - A.Turbo) * 6000;
	P += FMath::Max(0, B.Tires - A.Tires) * 1500;
	if (A.Suspension != B.Suspension) P += 2200;
	if (A.Tint != B.Tint) P += 400;
	return P;
}
