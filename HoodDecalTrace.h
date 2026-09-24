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
// Each of those fails in a different place and from the outside all three look the same.
//
// It also lists every DECAL_HOOD part the database holds for the car, and names them by working
// forwards: a part record stores its name as a hash, which cannot be reversed, so the candidate
// names are hashed instead and matched against what is there. That settles how the layouts are
// actually named, and therefore whether a hood is supposed to have one of its own.

bool HoodDecalTrace = false;
int HoodDecalTraceUsed = 0;
constexpr int HoodDecalTraceLimit = 400;
int HoodDecalTraceListedCarType = -1;

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

// Every naming scheme worth considering, including the plain one, the per style and per kit ones
// GetLayoutPart already looks for, and a trailing letter.
bool HoodDecalNameForHash(int CarType, DWORD Hash, char* Out, size_t OutSize)
{
	const char* Car = GetCarTypeName(CarType);

	if (!Car || !Car[0]) return false;

	const char* Sizes[] = { "MEDIUM", "SMALL", "LARGE", "WIDE" };
	char Buf[160];

	for (int s = 0; s < 4; s++)
	{
		sprintf(Buf, "%s_DECAL_HOOD_RECT_%s", Car, Sizes[s]);
		if (bStringHash(Buf) == Hash) { strncpy(Out, Buf, OutSize - 1); Out[OutSize - 1] = 0; return true; }

		for (char L = 'A'; L <= 'H'; L++)
		{
			sprintf(Buf, "%s_DECAL_HOOD_RECT_%s_%c", Car, Sizes[s], L);
			if (bStringHash(Buf) == Hash) { strncpy(Out, Buf, OutSize - 1); Out[OutSize - 1] = 0; return true; }
		}

		for (int n = 0; n <= 24; n++)
		{
			sprintf(Buf, "%s_STYLE%02d_DECAL_HOOD_RECT_%s", Car, n, Sizes[s]);
			if (bStringHash(Buf) == Hash) { strncpy(Out, Buf, OutSize - 1); Out[OutSize - 1] = 0; return true; }

			sprintf(Buf, "%s_KIT%02d_DECAL_HOOD_RECT_%s", Car, n, Sizes[s]);
			if (bStringHash(Buf) == Hash) { strncpy(Out, Buf, OutSize - 1); Out[OutSize - 1] = 0; return true; }
		}
	}

	return false;
}

// The same, for a hood, so the log can say which hood is fitted rather than a bare hash.
bool HoodNameForHash(int CarType, DWORD Hash, char* Out, size_t OutSize)
{
	const char* Car = GetCarTypeName(CarType);

	if (!Car || !Car[0]) return false;

	char Buf[160];

	sprintf(Buf, "%s_HOOD", Car);
	if (bStringHash(Buf) == Hash) { strncpy(Out, Buf, OutSize - 1); Out[OutSize - 1] = 0; return true; }

	for (int n = 0; n <= 24; n++)
	{
		sprintf(Buf, "%s_STYLE%02d_HOOD", Car, n);
		if (bStringHash(Buf) == Hash) { strncpy(Out, Buf, OutSize - 1); Out[OutSize - 1] = 0; return true; }

		sprintf(Buf, "%s_KIT%02d_HOOD", Car, n);
		if (bStringHash(Buf) == Hash) { strncpy(Out, Buf, OutSize - 1); Out[OutSize - 1] = 0; return true; }
	}

	return false;
}

void HoodDecalTraceListLayouts(int CarType)
{
	if (CarType == HoodDecalTraceListedCarType) return;

	HoodDecalTraceListedCarType = CarType;

	const char* Car = GetCarTypeName(CarType);

	HoodDecalTraceLine("\n=== DECAL_HOOD parts the database holds for %s (car type %d) ===\n",
		Car ? Car : "<none>", CarType);

	DWORD* Part = CarPartDatabase_NewGetFirstCarPart((DWORD*)_CarPartDB, CarType, CARSLOTID_DECAL_HOOD, 0, -1);
	int n = 0;
	char Name[160];

	while (Part)
	{
		DWORD Hash = HoodDecalPartHash(Part);

		HoodDecalTraceLine("  [%d] part %08X  hash %08X  partid %d  size %08X  shape %08X  %s\n",
			n++, (DWORD)Part, Hash, *((char*)Part + 4),
			CarPart_GetAppliedAttributeUParam(Part, CT_bStringHash("SIZE"), 0),
			CarPart_GetAppliedAttributeUParam(Part, CT_bStringHash("SHAPE"), 0),
			HoodDecalNameForHash(CarType, Hash, Name, sizeof(Name)) ? Name : "<name not among the patterns tried>");

		Part = CarPartDatabase_NewGetNextCarPart((DWORD*)_CarPartDB, Part, CarType, CARSLOTID_DECAL_HOOD, 0, -1);
	}

	if (!n) HoodDecalTraceLine("  none at all\n");

	HoodDecalTraceLine("\n");
}

void HoodDecalTraceDump(DWORD* RideInfo)
{
	if (!HoodDecalTrace) return;
	if (HoodDecalTraceUsed >= HoodDecalTraceLimit) return;

	DWORD* Hood = (DWORD*)RideInfo[356 + CARSLOTID_HOOD];
	DWORD* Layout = (DWORD*)RideInfo[356 + CARSLOTID_DECAL_HOOD];

	bool AnyTex = false;

	for (int i = 0; i < 8 && !AnyTex; i++)
		if (RideInfo[356 + CARSLOTID_DECAL_HOOD_TEX0 + i]) AnyTex = true;

	// A stock hood with no decal on it is the resting state and there are hundreds of those. Only
	// say something when a hood decal is involved or the hood is not the stock one.
	bool Interesting = Layout || AnyTex || (Hood && (*((BYTE*)Hood + 5) >> 5) > 0);

	if (!Interesting) return;

	HoodDecalTraceUsed++;

	int CarType = *(int*)RideInfo;
	char Name[160];

	HoodDecalTraceListLayouts(CarType);

	HoodDecalTraceLine("hood    part %08X  hash %08X  tier %d  excludedecal slot %d  visible %d  %s\n",
		(DWORD)Hood, HoodDecalPartHash(Hood),
		Hood ? (*((BYTE*)Hood + 5) >> 5) : -1,
		Hood ? CarPart_GetExcludeDecal(Hood, nullptr) : -1,
		*((BYTE*)RideInfo + 2104 + CARSLOTID_HOOD),
		Hood && HoodNameForHash(CarType, HoodDecalPartHash(Hood), Name, sizeof(Name)) ? Name : "");

	HoodDecalTraceLine("layout  part %08X  hash %08X  size %08X  shape %08X  visible %d  %s\n",
		(DWORD)Layout, HoodDecalPartHash(Layout),
		Layout ? CarPart_GetAppliedAttributeUParam(Layout, CT_bStringHash("SIZE"), 0) : 0,
		Layout ? CarPart_GetAppliedAttributeUParam(Layout, CT_bStringHash("SHAPE"), 0) : 0,
		*((BYTE*)RideInfo + 2104 + CARSLOTID_DECAL_HOOD),
		Layout && HoodDecalNameForHash(CarType, HoodDecalPartHash(Layout), Name, sizeof(Name)) ? Name : "");

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

	HoodDecalTraceListedCarType = -1;

	std::error_code ErrorCode;
	std::filesystem::remove(CurrentWorkingDirectory / "UnlimiterData" / "_HoodDecal.txt", ErrorCode);
}
