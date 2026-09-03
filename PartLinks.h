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
// CUSTOM_FENDER in the widebody block. Slot names are the game's own CAR_SLOT_ID names, not
// the CarPartGroupID list Binary shows when authoring a part; they agree up to ID 26 and diverge
// after, so BRAKE is FRONT_BRAKE / REAR_BRAKE here and WHEEL is FRONT_WHEEL / REAR_WHEEL.
//
// Type matters. Binary stores only the field matching Type, so a Boolean SWAPSLOT stores the
// boolean and ignores ValueKey. HIDESLOT does not care, only its presence is checked.
//
// Attributes are read through CarPart_GetAppliedAttributeUParam (0x61B500) only, the accessor
// CarPart_GetExcludeDecal uses. Nothing iterates repeated attributes via 0x60FEF0 with a non-null
// previous node: that overload is used nowhere else in this tree and faulted when tried.
// ---------------------------------------------------------------------------------------------


// Presence probe sentinel. What value a Boolean attribute carries is up to the authoring tool, so
// ask for a default no real value collides with and treat anything else as present.
#define PARTLINK_ABSENT 0xDEADBEEF

bool PartLinkTrace = false;

// One file, off by default. Two questions it answers, which I have now guessed at twice: is
// SWAPSLOT_<SLOT> being read off the part at all, and is that state current when the Body Shop
// builds its categories.
void PartLinkTraceLine(const char* fmt, ...)
{
	if (!PartLinkTrace) return;

	auto Path = CurrentWorkingDirectory / "UnlimiterData" / "_PartLinkTrace.txt";
	FILE* f = fopen(Path.string().c_str(), "a");
	if (!f) return;

	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);

	fclose(f);
}

DWORD PartLinkHideSlotHashes[CAR_SLOT_ID::__NUM]; // "HIDESLOT_<slot>"
DWORD PartLinkSwapSlotHashes[CAR_SLOT_ID::__NUM]; // "SWAPSLOT_<slot>"

bool PartLinkSlotHidden[CAR_SLOT_ID::__NUM];
DWORD PartLinkSwapTarget[CAR_SLOT_ID::__NUM];

// Which slots any link on this car type has ever driven. Keyed by car type, not by RideInfo:
// link definitions live on the parts, so two RideInfos of the same car share the same set. This
// is the only thing remembered, and both restores below are self limiting, so nothing can be
// applied to the wrong car or stomp a part the player picked.
int PartLinkGovernedCarType = -1;
bool PartLinkGoverned[CAR_SLOT_ID::__NUM];

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
	if (!PartLink_ValidRideInfo(RideInfo))
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

	// Same test the widebody block itself uses: byte 5's upgrade level, not byte 0
	PartLinkTraceLine("resolve: car type %d\n", *RideInfo);

	DWORD* WideBodyPart = (DWORD*)RideInfo[356 + CAR_SLOT_ID::WIDE_BODY];
	bool HasWidebody = WideBodyPart && ((*((BYTE*)WideBodyPart + 5) & 0xE0) != 0);

	for (int slot = CAR_SLOT_ID::__MODEL_FIRST; slot <= CAR_SLOT_ID::__MODEL_LAST; slot++)
	{
		DWORD* Part = (DWORD*)RideInfo[356 + slot];
		if (!Part) continue;

		// Widebody hides FRONT_BUMPER by zeroing the visibility byte but leaves the part in the
		// slot, so a front swap bumper would otherwise go on driving its hood and fenders from
		// under the widebody. Only skip for that reason: a blanket "invisible means inactive"
		// rule kills every link the moment anything else zeroes a byte, which is exactly what
		// stopped front swaps from working after they already did.
		if (HasWidebody && !*((BYTE*)RideInfo + 2104 + slot)) continue;

		for (int target = CAR_SLOT_ID::__MODEL_FIRST; target < CAR_SLOT_ID::__NUM; target++)
		{
			if (target == slot) continue;

			DWORD h = CarPart_GetAppliedAttributeUParam(Part, PartLinkHideSlotHashes[target], PARTLINK_ABSENT);

			if (h != PARTLINK_ABSENT)
			{
				PartLinkSlotHidden[target] = true;

				PartLinkTraceLine("  slot %d %s: HIDESLOT_%s (raw 0x%08X)\n",
					slot, GetCarSlotIDName(slot), GetCarSlotIDName(target), (unsigned int)h);
			}

			DWORD v = CarPart_GetAppliedAttributeUParam(Part, PartLinkSwapSlotHashes[target], PARTLINK_ABSENT);

			if (v == PARTLINK_ABSENT) continue;

			// A part name hash is never a small number. Under 0x10000 means the attribute was
			// typed Boolean or Integer, so what got stored is that field rather than ValueKey.
			if (v < 0x10000)
			{
				PartLinkTraceLine("  slot %d %s: SWAPSLOT_%s = %u, not a part name hash."
					" Set Type = Key in Binary.\n",
					slot, GetCarSlotIDName(slot), GetCarSlotIDName(target), (unsigned int)v);
				continue;
			}

			PartLinkSwapTarget[target] = v;

			PartLinkTraceLine("  slot %d %s: SWAPSLOT_%s = 0x%08X\n",
				slot, GetCarSlotIDName(slot), GetCarSlotIDName(target), (unsigned int)v);
		}
	}

	// Governed set, per car type
	if ((int)*RideInfo != PartLinkGovernedCarType)
	{
		PartLinkGovernedCarType = (int)*RideInfo;
		memset(PartLinkGoverned, 0, sizeof(PartLinkGoverned));
	}

	// Hiding a hood has to take the hood's own parts with it. The game groups them the same way:
	// the ENGINE case in UpdatePartsEnabled sets HOOD, HOOD_UNDER and DECAL_HOOD visible in one
	// go. Headlights have nothing hanging off them, which is why HIDESLOT looked like it worked
	// there and not on a hood: the hood went, its underside and decal stayed, and what was left
	// still read as a hood.
	static const int Assembly[][3] =
	{
		{ CAR_SLOT_ID::HOOD,       CAR_SLOT_ID::HOOD_UNDER,        CAR_SLOT_ID::DECAL_HOOD },
		{ CAR_SLOT_ID::TRUNK,      CAR_SLOT_ID::TRUNK_UNDER,       -1 },
		{ CAR_SLOT_ID::DOOR_LEFT,  CAR_SLOT_ID::DOOR_PANEL_LEFT,   CAR_SLOT_ID::DOOR_SILL_LEFT },
		{ CAR_SLOT_ID::DOOR_RIGHT, CAR_SLOT_ID::DOOR_PANEL_RIGHT,  CAR_SLOT_ID::DOOR_SILL_RIGHT },
	};

	for (int a = 0; a < (int)(sizeof(Assembly) / sizeof(Assembly[0])); a++)
	{
		if (!PartLinkSlotHidden[Assembly[a][0]]) continue;

		for (int k = 1; k < 3; k++)
		{
			int Slot = Assembly[a][k];

			// A slot something else is already driving keeps its own answer
			if (Slot < 0 || PartLinkSwapTarget[Slot]) continue;

			PartLinkSlotHidden[Slot] = true;
		}
	}

	for (int i = 0; i < CAR_SLOT_ID::__NUM; i++)
		if (PartLinkSlotHidden[i] || PartLinkSwapTarget[i]) PartLinkGoverned[i] = true;
}

// Runs at the very end of RideInfo_UpdatePartsEnabled, after the widebody and showengine blocks,
// which push some slots back to visible.
bool PartLink_IsHiddenFromMenu(DWORD* CarPart); // defined below

void PartLink_ApplyVisibility(DWORD* RideInfo)
{
	if (!PartLink_ValidRideInfo(RideInfo)) return;

	int CarType = *RideInfo;

	for (int i = 0; i < CAR_SLOT_ID::__NUM; i++)
	{
		// HIDESLOT wins over SWAPSLOT when both name the same slot. Hiding is the stronger
		// statement of the two, and a front swap that brings its own hood but also wants the
		// hood gone on one particular kit would otherwise have no way to say so.
		if (PartLinkSwapTarget[i] && !PartLinkSlotHidden[i])
		{
			DWORD* Wanted = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, i, PartLinkSwapTarget[i], 0, -1);
			if (Wanted) RideInfo[356 + i] = (DWORD)Wanted;

			continue;
		}

		if (PartLinkSlotHidden[i])
		{
			// Runs after the whole slot loop, so this is the last word on the byte. The ENGINE
			// case in UpdatePartsEnabled sets HOOD, HOOD_UNDER and DECAL_HOOD back to 1 whenever
			// an engine part is fitted and the game flow is the front end, and would otherwise
			// undo this every time the car is rebuilt.
			*((BYTE*)RideInfo + 2104 + i) = 0;

			// Animated parts ignore the byte, so take the part away as well
			RideInfo[356 + i] = 0;
			continue;
		}

		if (!PartLinkGoverned[i]) continue;

		// Nothing drives this slot any more. Two restores, both self limiting, so neither can run
		// twice or overwrite something the player chose.
		DWORD* Installed = (DWORD*)RideInfo[356 + i];

		// A part carrying HIDE_MENU is unreachable from any menu, so it can only have arrived
		// through a SWAPSLOT. Once swapped back it no longer carries it and this stops firing.
		if (Installed && PartLink_IsHiddenFromMenu(Installed))
		{
			RideInfo[356 + i] = (DWORD)CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, i, 0, 0, -1);
			continue;
		}

		// The slot was emptied by a HIDESLOT that is no longer active. Only ever acts on an empty
		// slot, so the moment the default is back this stops firing too.
		if (!Installed)
		{
			RideInfo[356 + i] = (DWORD)CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, i, 0, 0, -1);
		}
	}
}

// Used by PartSelectionScreen_SetupBodyShop to drop a category from the strip.
// Set by RideInfo_UpdatePartsEnabled, read by the menu. If UpdatePartsEnabled has not run for
// the car being looked at, this is the state of whatever car it ran for last, which in the body
// shop is the car on screen.
bool PartLink_IsSlotHidden(int CarSlotID)
{
	if (CarSlotID < 0 || CarSlotID >= CAR_SLOT_ID::__NUM) return false;

	// A slot driven by SWAPSLOT is not browsable either: anything picked there would be
	// overwritten on the next resolve. Widebody takes its slots out of the strip for the same
	// reason.
	return PartLinkSwapTarget[CarSlotID] != 0 || PartLinkSlotHidden[CarSlotID];
}

// Called from SetupBodyShop right after the resolve, so the file shows what the menu is working
// from rather than what the resolve found a moment earlier on some other car.
void PartLinkTraceCategories()
{
	if (!PartLinkTrace) return;

	int n = 0;

	for (int i = 0; i < CAR_SLOT_ID::__NUM; i++)
	{
		if (!PartLinkSlotHidden[i] && !PartLinkSwapTarget[i]) continue;

		PartLinkTraceLine("  category gate: slot %d %s %s%s\n", i, GetCarSlotIDName(i),
			PartLinkSlotHidden[i] ? "hidden " : "",
			PartLinkSwapTarget[i] ? "swapped" : "");

		n++;
	}

	PartLinkTraceLine("body shop: %d slot(s) governed\n\n", n);
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
	if (!CarPart) return false;

	// Both spellings: HIDE_MENU is moses' name for the same idea, HIDEMENU matches the other
	// attributes in this file. Reading one and not the other is a silent no-op on existing data.
	if (CarPart_GetAppliedAttributeUParam(CarPart, CT_bStringHash("HIDE_MENU"), PARTLINK_ABSENT) != PARTLINK_ABSENT)
		return true;

	return CarPart_GetAppliedAttributeUParam(CarPart, CT_bStringHash("HIDEMENU"), PARTLINK_ABSENT) != PARTLINK_ABSENT;
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
	if (!PartLink_ValidRideInfo(RideInfo)) return 0;

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
	PartLink_RedirectCallsTo(0x612B70, PartLink_GetDoorlineHash);
	PartLink_RedirectCallsTo(0x612C40, PartLink_GetDoorlineMaskHash);
}

// Called from DoUnlimiterStuffCodeCave.
void LoadPartLinks()
{
	PartLink_BuildHashTables();
}
