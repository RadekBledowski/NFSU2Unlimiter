#pragma once

#include "stdio.h"
#include "InGameFunctions.h"

// Game-owned global. It is kept in sync for anything else that may read it, but it is never
// used as a "no override" sentinel: the Unlimiter does not own its initial value and never
// writes -1.0f to it, so testing it against -1.0f can short-circuit the whole calculation.
#define ForceCarStars *(float*)0x7FA290

int GetRepFromNumStars(float NumStars)
{
	if (TheStarGazer.MaxStars <= 0) return 0;

	if (NumStars < 0.0f) NumStars = 0.0f;
	if (NumStars > (float)TheStarGazer.MaxStars) NumStars = (float)TheStarGazer.MaxStars;

	float BeforeDot;
	float AfterDot = modff(NumStars, &BeforeDot);
	int iNumStars = (int)floorf(BeforeDot);

	// At (or past) the top level there is no next level to interpolate into
	if (iNumStars >= TheStarGazer.MaxStars) return TheStarGazer.Rep[TheStarGazer.MaxStars];

	return TheStarGazer.Rep[iNumStars]
		+ (int)(AfterDot * (float)(TheStarGazer.Rep[iNumStars + 1] - TheStarGazer.Rep[iNumStars]));
}

float GetNumStarsFromRep(int Rep)
{
	if (TheStarGazer.MaxStars <= 0) return 0.0f;
	if (Rep < 0) Rep = 0;

	int i;

	for (i = 0; i < TheStarGazer.MaxStars; i++)
	{
		if (Rep < TheStarGazer.Rep[i + 1]) break;                            // we are inside level i
		if (TheStarGazer.Rep[i + 1] <= TheStarGazer.Rep[i]) return (float)i; // flat or broken table
	}

	if (i >= TheStarGazer.MaxStars) return (float)TheStarGazer.MaxStars;

	int RepForThisLevel = TheStarGazer.Rep[i + 1] - TheStarGazer.Rep[i];
	int Remainder = Rep - TheStarGazer.Rep[i];

	if (RepForThisLevel <= 0 || Remainder <= 0) return (float)i;

	return (float)i + (float)Remainder / (float)RepForThisLevel;
}

float __fastcall StarGazerGuide_GetNumberOfStars(DWORD* StarGazerGuide, void* EDX_Unused, DWORD* ride)
{
	// Global override from _StarGazer.ini. Handled first and returned immediately, so a stale
	// value at 0x7FA290 can never hijack the calculation below.
	if (TheStarGazer.ForceRep != -1)
	{
		float Forced = GetNumStarsFromRep(TheStarGazer.ForceRep);
		ForceCarStars = Forced;
		return Forced;
	}

	int CarType = ride[0];
	int Rep = CarConfigs[CarType].StarGazer.StartingRep;

	DWORD* WideBodyPart = (DWORD*)ride[356 + CAR_SLOT_ID::WIDE_BODY];

	// FIXED: read byte 5 (upgrade level), not byte 0 + 5
	bool HasWidebody = (WideBodyPart != nullptr) && ((*((BYTE*)WideBodyPart + 5) & 0xE0) != 0);

	for (int i = 0; i < CAR_SLOT_ID::__NUM; i++)
	{
		// Vinyl colours are packed values, not parts
		if (i >= CAR_SLOT_ID::VINYL_COLOUR0_0 && i <= CAR_SLOT_ID::VINYL_COLOUR3_3) continue;

		// Counted together with FRONT_WHEEL below
		if (i == CAR_SLOT_ID::REAR_WHEEL) continue;

		DWORD* Part = (DWORD*)ride[356 + i];

		if (i == CAR_SLOT_ID::FRONT_WHEEL) // take the greater of front/rear
		{
			DWORD* RearPart = (DWORD*)ride[356 + CAR_SLOT_ID::REAR_WHEEL];

			int FWRep = Part
				? PlayerCareerState_GetCarPartRep((DWORD*)ThePlayerCareer, EDX_Unused, CarType, i, Part) : 0;
			int RWRep = RearPart
				? PlayerCareerState_GetCarPartRep((DWORD*)ThePlayerCareer, EDX_Unused, CarType, i, RearPart) : 0;

			Rep += (FWRep > RWRep) ? FWRep : RWRep;
			continue;
		}

		if (!Part) continue;
		if (HasWidebody && !IsCustomWidebody(WideBodyPart, i)) continue;

		Rep += PlayerCareerState_GetCarPartRep((DWORD*)ThePlayerCareer, EDX_Unused, CarType, i, Part);
	}

	return GetNumStarsFromRep(Rep);
}
