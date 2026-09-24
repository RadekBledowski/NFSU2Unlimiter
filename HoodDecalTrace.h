#pragma once

#include "stdafx.h"
#include "stdio.h"
#include <filesystem>
#include "GlobalVariables.h"
#include "CarPart.h"

// Says whether a hood decal is actually set up, for when the menu offers one and nothing appears.
//
// Three things have to hold before anything can be drawn. The layout part has to be in slot 52,
// DECAL_HOOD, and RideInfo_UpdatePartsEnabled has to leave its visibility byte at 1. The layout
// part has to carry SIZE and SHAPE, because CarRenderInfo only loads a decal texture when both are
// present. And one of the eight texture slots, 97 to 104, has to hold a part with a NAME.
//
// Each of those fails in a different place, and from the outside all three look the same: a decal
// that was bought and is not there.

bool HoodDecalTrace = false;
int HoodDecalTraceUsed = 0;
constexpr int HoodDecalTraceLimit = 40;

void HoodDecalTraceLine(const char* fmt, ...)
{
	if (HoodDecalTraceUsed >= HoodDecalTraceLimit) return;

	auto Path = CurrentWorkingDirectory / "UnlimiterData" / "_HoodDecal.txt";
	FILE* f = fopen(Path.string().c_str(), "a");

	if (!f) return;

	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);

	fclose(f);
}

DWORD HoodDecalPartHash(DWORD* Part)
{
	return Part ? *(unsigned int*)Part : 0;
}

void HoodDecalTraceDump(DWORD* RideInfo)
{
	if (!HoodDecalTrace) return;
	if (HoodDecalTraceUsed >= HoodDecalTraceLimit) return;

	HoodDecalTraceUsed++;

	DWORD* Hood = (DWORD*)RideInfo[356 + CARSLOTID_HOOD];
	DWORD* Layout = (DWORD*)RideInfo[356 + CARSLOTID_DECAL_HOOD];

	HoodDecalTraceLine("hood    part %08X  hash %08X  tier %d  excludedecal slot %d  visible %d\n",
		(DWORD)Hood, HoodDecalPartHash(Hood),
		Hood ? (*((BYTE*)Hood + 5) >> 5) : -1,
		Hood ? CarPart_GetExcludeDecal(Hood, nullptr) : -1,
		*((BYTE*)RideInfo + 2104 + CARSLOTID_HOOD));

	HoodDecalTraceLine("layout  part %08X  hash %08X  size %08X  shape %08X  visible %d\n",
		(DWORD)Layout, HoodDecalPartHash(Layout),
		Layout ? CarPart_GetAppliedAttributeUParam(Layout, CT_bStringHash("SIZE"), 0) : 0,
		Layout ? CarPart_GetAppliedAttributeUParam(Layout, CT_bStringHash("SHAPE"), 0) : 0,
		*((BYTE*)RideInfo + 2104 + CARSLOTID_DECAL_HOOD));

	for (int i = 0; i < 8; i++)
	{
		int Slot = CARSLOTID_DECAL_HOOD_TEX0 + i;
		DWORD* Tex = (DWORD*)RideInfo[356 + Slot];

		if (!Tex) continue;

		HoodDecalTraceLine("  tex%d  part %08X  hash %08X  name %08X  visible %d\n",
			i, (DWORD)Tex, HoodDecalPartHash(Tex),
			CarPart_GetAppliedAttributeUParam(Tex, CT_bStringHash("NAME"), 0),
			*((BYTE*)RideInfo + 2104 + Slot));
	}

	HoodDecalTraceLine("\n");
}

void InitHoodDecalTrace()
{
	if (!HoodDecalTrace) return;

	std::error_code ErrorCode;
	std::filesystem::remove(CurrentWorkingDirectory / "UnlimiterData" / "_HoodDecal.txt", ErrorCode);
}
