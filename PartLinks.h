#pragma once

#include "stdafx.h"
#include "stdio.h"
#include "InGameFunctions.h"
#include "GlobalVariables.h"

// ---------------------------------------------------------------------------------------------
// Part links
//
// Lets one installed part govern another slot, the way the widebody block in
// RideInfo_UpdatePartsEnabled already does for a single hardcoded case. Everything is authored in
// Binary's Car Parts Editor as Custom attributes on a part; there is no ini.
//
//   HIDESLOT_<SLOT>   Boolean   remove that slot while this part is installed
//   SWAPSLOT_<SLOT>   Key       ValueKey = the part to put in that slot
//   HIDE_MENU         Boolean   this part is never listed, it can only be pulled in
//   DOORLINE          Key       ValueKey = a doorline texture to use instead of the game's
//   DOORLINE_MASK     Key       optional, the game's own mask is kept when absent
//
// The target slot lives in the attribute NAME, matching CUSTOM_FRONT_BUMPER / CUSTOM_SKIRT /
// CUSTOM_FENDER in the widebody block. UnlimiterData\_SlotNames.txt lists all 170 of them.
//
// Type matters. Binary stores only the field matching Type, so a Boolean SWAPSLOT stores the
// boolean and ignores ValueKey. HIDESLOT does not care, only its presence is checked.
//
// Attributes are read through CarPart_GetAppliedAttributeUParam (0x61B500) only, the accessor
// CarPart_GetExcludeDecal uses. Nothing iterates repeated attributes via 0x60FEF0 with a non-null
// previous node: that overload is used nowhere else in this tree and faulted when tried.
// ---------------------------------------------------------------------------------------------

bool PartLinksEnabled = false;
bool PartLinkRemoveHiddenParts = true;
bool PartLinkHideMenuAttribute = true;
bool PartLinkDoorlineOverride = true;
bool PartLinkCatalogue = false;

// Presence probe sentinel. What value a Boolean attribute carries is up to the authoring tool, so
// ask for a default no real value collides with and treat anything else as present.
#define PARTLINK_ABSENT 0xDEADBEEF

DWORD PartLinkHideSlotHashes[CAR_SLOT_ID::__NUM]; // "HIDESLOT_<slot>"
DWORD PartLinkSwapSlotHashes[CAR_SLOT_ID::__NUM]; // "SWAPSLOT_<slot>"

bool PartLinkSlotHidden[CAR_SLOT_ID::__NUM];
DWORD PartLinkSwapTarget[CAR_SLOT_ID::__NUM];

// Slots a part was taken from or replaced in, so the default can be handed back when the part
// driving them is removed. The visibility byte alone does nothing to parts that carry an
// animation (hood, trunk, doors, lights), which is why hiding also removes the part.
bool PartLinkWeRemoved[CAR_SLOT_ID::__NUM];
bool PartLinkWeSwapped[CAR_SLOT_ID::__NUM];

bool PartLinkHashesReady = false;

void PartLink_BuildHashTables()
{
	if (PartLinkHashesReady) return;

	char NameBuf[80];

	for (int i = 0; i < CAR_SLOT_ID::__NUM; i++)
	{
		char const* SlotName = GetCarSlotIDName(i);

		sprintf(NameBuf, "HIDESLOT_%s", SlotName);
		PartLinkHideSlotHashes[i] = bStringHash(NameBuf);

		sprintf(NameBuf, "SWAPSLOT_%s", SlotName);
		PartLinkSwapSlotHashes[i] = bStringHash(NameBuf);
	}

	PartLinkHashesReady = true;
}

// This code reads 680 bytes past the pointer it is given, so a small integer means a caller passed
// something that is not a pointer. It happened once, when SetupBodyShop dereferenced gTheRideInfo
// and handed over the car type instead of the struct.
bool PartLink_ValidRideInfo(DWORD* RideInfo)
{
	uintptr_t v = (uintptr_t)RideInfo;
	return v >= 0x00010000 && v <= 0xC0000000 && !(v & 3);
}

DWORD PartLinkCachedSignature = 0;
bool PartLinkCacheValid = false;

void PartLink_Resolve(DWORD* RideInfo)
{
	if (!PartLinksEnabled || !PartLink_ValidRideInfo(RideInfo))
	{
		memset(PartLinkSlotHidden, 0, sizeof(PartLinkSlotHidden));
		memset(PartLinkSwapTarget, 0, sizeof(PartLinkSwapTarget));
		PartLinkCacheValid = false;
		return;
	}

	PartLink_BuildHashTables();

	// The scan below probes two attribute names per slot per installed part. Fine once, not on
	// every part scroll, so skip it while nothing has moved.
	DWORD Signature = (DWORD)*RideInfo + 1;

	for (int slot = CAR_SLOT_ID::__MODEL_FIRST; slot <= CAR_SLOT_ID::__MODEL_LAST; slot++)
		Signature = Signature * 0x21 + (DWORD)RideInfo[356 + slot];

	if (PartLinkCacheValid && Signature == PartLinkCachedSignature) return;

	PartLinkCachedSignature = Signature;
	PartLinkCacheValid = true;

	memset(PartLinkSlotHidden, 0, sizeof(PartLinkSlotHidden));
	memset(PartLinkSwapTarget, 0, sizeof(PartLinkSwapTarget));

	for (int slot = CAR_SLOT_ID::__MODEL_FIRST; slot <= CAR_SLOT_ID::__MODEL_LAST; slot++)
	{
		DWORD* Part = (DWORD*)RideInfo[356 + slot];
		if (!Part) continue;

		for (int target = CAR_SLOT_ID::__MODEL_FIRST; target < CAR_SLOT_ID::__NUM; target++)
		{
			if (target == slot) continue;

			if (CarPart_GetAppliedAttributeUParam(Part, PartLinkHideSlotHashes[target], PARTLINK_ABSENT) != PARTLINK_ABSENT)
				PartLinkSlotHidden[target] = true;

			DWORD v = CarPart_GetAppliedAttributeUParam(Part, PartLinkSwapSlotHashes[target], PARTLINK_ABSENT);

			// A part name hash is never a small number. Under 0x10000 means the attribute was
			// typed Boolean or Integer, so what got stored is that field rather than ValueKey.
			if (v != PARTLINK_ABSENT && v >= 0x10000) PartLinkSwapTarget[target] = v;
		}
	}
}

// Runs at the very end of RideInfo_UpdatePartsEnabled, after the widebody and showengine blocks,
// which push some slots back to visible.
void PartLink_ApplyVisibility(DWORD* RideInfo)
{
	if (!PartLinksEnabled || !PartLink_ValidRideInfo(RideInfo)) return;

	int CarType = *RideInfo;

	for (int i = 0; i < CAR_SLOT_ID::__NUM; i++)
	{
		if (PartLinkSwapTarget[i])
		{
			DWORD* Wanted = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, i, PartLinkSwapTarget[i], 0, -1);

			if (Wanted)
			{
				RideInfo[356 + i] = (DWORD)Wanted;
				PartLinkWeSwapped[i] = true;
			}

			continue; // a swapped slot is not also hidden
		}

		if (PartLinkWeSwapped[i] && !PartLinkSlotHidden[i])
		{
			RideInfo[356 + i] = (DWORD)CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, i, 0, 0, -1);
			PartLinkWeSwapped[i] = false;
		}

		if (PartLinkSlotHidden[i])
		{
			// Enough for static geometry
			*((BYTE*)RideInfo + 2104 + i) = 0;

			// Animated parts ignore the byte, so take the part away as well
			if (PartLinkRemoveHiddenParts && RideInfo[356 + i])
			{
				RideInfo[356 + i] = 0;
				PartLinkWeRemoved[i] = true;
			}
		}
		else if (PartLinkWeRemoved[i])
		{
			// The same call the widebody block uses to restore the stock doors
			RideInfo[356 + i] = (DWORD)CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, i, 0, 0, -1);
			PartLinkWeRemoved[i] = false;
		}
	}
}

// Used by PartSelectionScreen_SetupBodyShop to drop a category from the strip.
bool PartLink_IsSlotHidden(int CarSlotID)
{
	if (!PartLinksEnabled) return false;
	if (CarSlotID < 0 || CarSlotID >= CAR_SLOT_ID::__NUM) return false;

	// A slot driven by SWAPSLOT is not browsable either: anything picked there would be
	// overwritten on the next resolve. Widebody takes its slots out of the strip for the same
	// reason.
	return PartLinkSwapTarget[CarSlotID] != 0 || PartLinkSlotHidden[CarSlotID];
}

// Parts that only ever arrive by being pulled in, never by being picked. Generalises a rule the
// game hardcodes one line below where this is called:
//
//     && (CarSlotID != 9 || (*((BYTE*)TheCarPart + 5) & 0x1F) != 5)    "hood style 5 is never listed"
//
// Widebody doors already behave this way: <CAR>_KITW%02d_DOOR_LEFT is in no category and exists
// only because the widebody part drags it in.
bool PartLink_IsHiddenFromMenu(DWORD* CarPart)
{
	if (!PartLinkHideMenuAttribute || !CarPart) return false;

	return CarPart_GetAppliedAttributeUParam(CarPart, CT_bStringHash("HIDE_MENU"), PARTLINK_ABSENT) != PARTLINK_ABSENT;
}

// --- DOORLINE --------------------------------------------------------------------------------
//
// The game picks one of three doorline textures (stock, with body parts, widebody) inside
// GetDoorlineHash (0x612B70) and GetDoorlineMaskHash (0x612C40). Both are called from
// GetTempCarSkinTextures, which decides what to load, and from the compositing code inside the
// game, which decides what is drawn, so patching only our own call site would load a custom
// texture and still paint the vanilla one.
//
// Rather than overwrite the two functions, which would mean replaying a prologue nobody has
// disassembled, every E8 call landing on them is redirected. The originals stay intact and
// callable, so the default path is the real vanilla logic instead of a guess at it.

DWORD PartLink_FindDoorlineAttribute(DWORD* RideInfo, DWORD KeyHash)
{
	if (!PartLinkDoorlineOverride || !PartLink_ValidRideInfo(RideInfo)) return 0;

	// Widebody first: it is the case the attribute exists for
	DWORD* Wide = (DWORD*)RideInfo[356 + CAR_SLOT_ID::WIDE_BODY];

	if (Wide)
	{
		DWORD v = CarPart_GetAppliedAttributeUParam(Wide, KeyHash, PARTLINK_ABSENT);
		if (v != PARTLINK_ABSENT && v >= 0x10000) return v;
	}

	for (int slot = CAR_SLOT_ID::__MODEL_FIRST; slot <= CAR_SLOT_ID::__MODEL_LAST; slot++)
	{
		if (slot == CAR_SLOT_ID::WIDE_BODY) continue;

		DWORD* Part = (DWORD*)RideInfo[356 + slot];
		if (!Part) continue;

		DWORD v = CarPart_GetAppliedAttributeUParam(Part, KeyHash, PARTLINK_ABSENT);
		if (v != PARTLINK_ABSENT && v >= 0x10000) return v;
	}

	return 0;
}

DWORD __cdecl PartLink_GetDoorlineHash(DWORD* RideInfo)
{
	DWORD Override = PartLink_FindDoorlineAttribute(RideInfo, CT_bStringHash("DOORLINE"));
	return Override ? Override : GetDoorlineHash(RideInfo);
}

DWORD __cdecl PartLink_GetDoorlineMaskHash(DWORD* RideInfo)
{
	// Only overridden when a part actually names a mask. A custom line usually reuses the vanilla
	// mask, so falling through is the useful default.
	DWORD Override = PartLink_FindDoorlineAttribute(RideInfo, CT_bStringHash("DOORLINE_MASK"));
	return Override ? Override : GetDoorlineMaskHash(RideInfo);
}

// Redirects every 5 byte relative call landing exactly on Target.
template<class T>
int PartLink_RedirectCallsTo(DWORD Target, T Replacement)
{
	const DWORD ScanStart = 0x401000;
	const DWORD ScanEnd = 0x7C0000;

	int Patched = 0;

	for (DWORD a = ScanStart; a < ScanEnd - 5; a++)
	{
		if (*(BYTE*)a != 0xE8) continue;
		if ((DWORD)(a + 5 + *(int*)(a + 1)) != Target) continue;

		injector::MakeCALL(a, Replacement, true);
		Patched++;
	}

	return Patched;
}

void InitPartLinkDoorline()
{
	if (!PartLinkDoorlineOverride) return;

	PartLink_RedirectCallsTo(0x612B70, PartLink_GetDoorlineHash);
	PartLink_RedirectCallsTo(0x612C40, PartLink_GetDoorlineMaskHash);
}

// --- authoring reference ----------------------------------------------------------------------
//
// SWAPSLOT takes a part name, and a CarPart stores only a hash, so there is no way to read back
// what a part is called. These two write out what is needed to fill in a ValueKey.

void PartLink_DumpSlotTable()
{
	PartLink_BuildHashTables();

	auto Path = CurrentWorkingDirectory / "UnlimiterData" / "_SlotHashes.txt";
	FILE* f = fopen(Path.string().c_str(), "w");
	if (!f) return;

	fprintf(f, "Slot names as the game reports them, and the attribute names built from each.\n");
	fprintf(f, "HIDESLOT is Boolean. SWAPSLOT must be Key, with the part name in ValueKey.\n\n");

	for (int i = 0; i < CAR_SLOT_ID::__NUM; i++)
	{
		char const* n = GetCarSlotIDName(i);
		fprintf(f, "%-5d%-24s HIDESLOT_%-22s SWAPSLOT_%s\n", i, n, n, n);
	}

	fclose(f);
}

void PartLink_DumpPartCatalogue(int CarType)
{
	char const* CarName = GetCarTypeName(CarType);
	if (!CarName) return;

	char Rel[128];
	sprintf(Rel, "UnlimiterData\\_PartCatalogue_%s.txt", CarName);

	auto Path = CurrentWorkingDirectory / Rel;
	FILE* f = fopen(Path.string().c_str(), "w");
	if (!f) return;

	fprintf(f, "Part catalogue for %s (car type %d)\n\n", CarName, CarType);
	fprintf(f, "Put the NAME into a SWAPSLOT_<SLOT> attribute's ValueKey, with Type = Key.\n");
	fprintf(f, "Names are recovered by hashing candidates, so (unnamed) rows exist but match no\n");
	fprintf(f, "known naming pattern.\n\n");

	static const char* Patterns[] = { "%s_%s", "%s_KIT%02d_%s", "%s_STYLE%02d_%s", "%s_KITW%02d_%s" };

	char NameBuf[128];

	for (int slot = CAR_SLOT_ID::__MODEL_FIRST; slot <= CAR_SLOT_ID::__MODEL_LAST; slot++)
	{
		char const* SlotName = GetCarSlotIDName(slot);

		DWORD* Part = CarPartDatabase_NewGetFirstCarPart((DWORD*)_CarPartDB, CarType, slot, 0, -1);
		if (!Part) continue;

		fprintf(f, "slot %d %s\n", slot, SlotName);

		int Guard = 0;

		while (Part && Guard++ < 256)
		{
			DWORD Hash = Part[0];
			BYTE Byte5 = *((BYTE*)Part + 5);

			sprintf(NameBuf, "%s_%s", CarName, SlotName);
			bool Named = (bStringHash(NameBuf) == Hash);

			for (int p = 1; p < 4 && !Named; p++)
				for (int n = 0; n <= 99 && !Named; n++)
				{
					sprintf(NameBuf, Patterns[p], CarName, n, SlotName);
					if (bStringHash(NameBuf) == Hash) Named = true;
				}

			fprintf(f, "    0x%08X  level %d  style %2d  %s\n",
				(unsigned int)Hash, Byte5 >> 5, Byte5 & 0x1F, Named ? NameBuf : "(unnamed)");

			DWORD* Next = CarPartDatabase_NewGetNextCarPart((DWORD*)_CarPartDB, Part, CarType, slot, 0, -1);
			if (Next == Part) break;
			Part = Next;
		}

		fprintf(f, "\n");
	}

	fclose(f);
}

int PartLinkLastCatalogueCar = -1;

void PartLink_DumpPartCatalogueOnce(int CarType)
{
	if (CarType == PartLinkLastCatalogueCar) return;

	PartLinkLastCatalogueCar = CarType;
	PartLink_DumpPartCatalogue(CarType);
}

// Called from DoUnlimiterStuffCodeCave.
void LoadPartLinks()
{
	PartLink_BuildHashTables();

	if (PartLinkCatalogue) PartLink_DumpSlotTable();
}
