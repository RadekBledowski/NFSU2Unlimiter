#pragma once

#include "stdafx.h"
#include "stdio.h"
#include "InGameFunctions.h"
#include "GlobalVariables.h"
#include "FeCarLimits.h"

// ExtendFeCarLimits reimplements FEPlayerCarDB::GetCarFiltered (0x5162D0),
// GetCarRecordByHandle (0x503510) and the RaceStarter patch at 0x525FBB because the vanilla
// versions cannot walk its enlarged arrays. Those are the exact three addresses the preset hooks
// below take over, and whichever Init ran last silently destroyed the other's patch. So the
// fallthrough paths chain into the FeCarLimits replacements when that feature is on, instead of
// jumping back into vanilla code that would then walk a resized array.

#include <vector>

// ---------------------------------------------------------------------------------------------
// Preset cars
//
// Ported from yugecin's nfsu2-re research:
//   BLOGx02 Customizing sponsor cars
//   BLOGx03 Customizing preset cars
//   BLOGx04 Use preset cars in quickrace
//   https://github.com/yugecin/nfsu2-re
//
// The game data contains a linked list of CarPreset entries (DDAY_PLAYER_CAR, CALEB_GTO,
// SNOOP_DOGG, the DEMO_* presets, ...). Only the sponsor subset is normally reachable, and only
// in Quick Race after typing a cheat. This exposes all of them as an extra car-select category.
//
// The original hooks were raw naked-asm patches over the C stubs. Here the dispatch logic lives
// in plain C++ and the naked stubs only do the register shuffling that cannot be expressed in C,
// matching how the rest of the Unlimiter is written.
//
// Presets are enumerated from the game's runtime CarPreset list at 0x8A31E4, which is populated
// from bin block 0x00030220 in GLOBAL\\GLOBALB.LZC. Presets added through Binary therefore show
// up here automatically, with no fixed cap on how many.
//
// Note: SpeedReflect (MaxHwoy) solves the same problem differently, by writing presets into the
// 12 vanilla sponsor car slots via a hook at 0x516402. That approach caps at 12 and only covers
// presets whose FE marker is recognised. It does not overlap with the hooks below, but running
// both at once will list the same cars twice.
// ---------------------------------------------------------------------------------------------

// Made-up bit that does not collide with INVENTORY_CAR_FLAGS (1 stock, 2 tuned, 4 career, 8 sponsor)
#define CUSTOM_IS_PRESET_CAR 0x20

#define MENU_STATE_MAIN_MENU      0x02
#define MENU_STATE_2P_SPLITSCREEN 0x04
#define MENU_STATE_CAR_CUSTOMIZE  0x20

#define MSG_CARSELECT_PREV 0x5073EF13
#define MSG_CARSELECT_NEXT 0xD9FEEC59

#define _carSelectCategory 0x7F444C
#define _carPresets        0x8A31E4
#define _sponsorCarVtable  0x79AD18
#define _profileData       0x83A9D0
#define _menuCarInstanceB  0x8389D0 // scratch MenuCarInstance, overwritten later in Customize anyway

#define carSelectCategory   *(DWORD*)_carSelectCategory
#define profileMenuState    *(DWORD*)(_profileData + 0x156A8)
#define profilePlayerIndex  *(DWORD*)(_profileData + 0x20358)

// ProfileData.players[i].d4.currentCarSelectionCategory
#define SizeOfPlayer            0xAB4C
#define OffsetOfPlayers         0x10
#define OffsetOfD4              0xD4
#define OffsetOfSelectCategory  0x0C

struct ObjectLink
{
	ObjectLink* next;
	ObjectLink* prev;
};

struct CarPreset // size 0x338
{
	ObjectLink link;      // 0x00, sentinel node is carPresets
	char modelName[32];   // 0x08, e.g. ESCALADE
	char Name[32];        // 0x28, e.g. SNOOP_DOGG
	char _pad[0x2F0];
};

struct InventoryCar // size 0x18
{
	DWORD* vtable;    // 0x00
	int field_4;      // 0x04
	DWORD slotHash;   // 0x08
	float field_C;    // 0x0C
	int field_10;     // 0x10
	DWORD flags;      // 0x14, INVENTORY_CAR_FLAGS
};

struct SponsorCar // size 0x1C
{
	InventoryCar Parent;  // 0x00
	DWORD CarPresetHash;  // 0x18
};

CarPreset* (__cdecl* FindCarPreset)(DWORD NameHash) = (CarPreset * (__cdecl*)(DWORD))0x61C460;

int (__thiscall* CarSelectFNGObject_CountAvailableCars)(DWORD* CarSelectFNGObject, DWORD Flags)
	= (int(__thiscall*)(DWORD*, DWORD))0x497EE0;
void (__thiscall* CarSelectFNGObject_ResetBrowableCars)(DWORD* CarSelectFNGObject)
	= (void(__thiscall*)(DWORD*))0x4EEC90;
void (__thiscall* CarSelectFNGObject_UpdateUI)(DWORD* CarSelectFNGObject)
	= (void(__thiscall*)(DWORD*))0x4B2310;
DWORD* (__thiscall* CarCollection_CreateNewTunedCarFromDataAtSlot)(DWORD* CarCollection, DWORD SlotNameHash)
	= (DWORD * (__thiscall*)(DWORD*, DWORD))0x52A710;
void (__thiscall* SponsorCar_ApplyTuningToInstance)(SponsorCar* Car, int PlayerIndex, DWORD* MenuCarInstance, int Unk)
	= (void(__thiscall*)(SponsorCar*, int, DWORD*, int))0x5039D0;
void (__thiscall* TunedCar18_CopyTuningFromMenuCarInstance)(DWORD* TunedCar18, DWORD* MenuCarInstance)
	= (void(__thiscall*)(DWORD*, DWORD*))0x503950;

// No fixed cap. The entries live in a vector that is sized once from the game's own CarPreset
// list, and the naked stubs index it through PresetCarsBase rather than a static array symbol.
// Only cap: slotHash is the 1-based index, and the browser skips a slotHash of 0.
std::vector<SponsorCar> PresetCarsAsInventoryCars;
SponsorCar* PresetCarsBase = nullptr;
int NumPresetCars = 0;

// Builds fake SponsorCar entries over the CarPreset list. Runs once, on first use, because the
// preset list is not populated yet when the Unlimiter initialises.
void BuildPresetCarList()
{
	if (NumPresetCars) return;

	ObjectLink* Sentinel = (ObjectLink*)_carPresets;
	if (!Sentinel->next) return; // list not loaded yet

	std::vector<DWORD> Entries;

	for (ObjectLink* i = Sentinel->next; i && i != Sentinel; i = i->next)
		Entries.push_back(FEHashUpper(((CarPreset*)i)->Name));

	if (Entries.empty()) return;

	PresetCarsAsInventoryCars.clear();
	PresetCarsAsInventoryCars.reserve(Entries.size());

	for (size_t n = 0; n < Entries.size(); n++)
	{
		SponsorCar s;

		s.Parent.vtable = (DWORD*)_sponsorCarVtable;
		s.Parent.field_4 = 0;
		// +1 because entries with a slotHash of 0 get skipped by the car browser
		s.Parent.slotHash = (DWORD)n + 1;
		s.Parent.field_C = .0f;
		s.Parent.field_10 = 0;
		s.Parent.flags = CUSTOM_IS_PRESET_CAR;
		s.CarPresetHash = Entries[n];

		PresetCarsAsInventoryCars.push_back(s);
	}

	// The vector never grows again after this point, so the base pointer stays valid
	PresetCarsBase = PresetCarsAsInventoryCars.data();
	NumPresetCars = (int)PresetCarsAsInventoryCars.size();
}

int CountCarPresets()
{
	BuildPresetCarList();
	return NumPresetCars;
}

void SetCarSelectCategory(DWORD NewCategory)
{
	carSelectCategory = NewCategory;

	*(DWORD*)(_profileData + OffsetOfPlayers + profilePlayerIndex * SizeOfPlayer
		+ OffsetOfD4 + OffsetOfSelectCategory) = NewCategory;
}

// Returns 1 if the category rotation was handled here, 0 to fall through to the game's own code.
int __fastcall CarSelectFNGObject_ChangeCategory(DWORD* CarSelectFNGObject, void* EDX_Unused, DWORD Message)
{
	DWORD NewCategory = carSelectCategory;
	DWORD MenuState = profileMenuState;

	bool InCustomize = (MenuState == MENU_STATE_CAR_CUSTOMIZE) && PresetCarsInCustomize;
	bool InQuickRace = (MenuState == MENU_STATE_MAIN_MENU || MenuState == MENU_STATE_2P_SPLITSCREEN)
		&& PresetCarsInQuickRace;

	if (!InCustomize && !InQuickRace) return 0;

	if (Message == MSG_CARSELECT_PREV)
	{
		// stock|tuned -> preset -> (sponsor ->) (career ->) (tuned ->) stock -> loop
		switch (NewCategory)
		{
		case 1 | 2: // IS_STOCK_CAR | IS_TUNED_CAR
			NewCategory = CUSTOM_IS_PRESET_CAR;
			break;

		case CUSTOM_IS_PRESET_CAR:
			if (InQuickRace && CarSelectFNGObject_CountAvailableCars(CarSelectFNGObject, 8)) // IS_SPONSOR_CAR
			{
				NewCategory = 8;
				break;
			}
			// fall through
		case 8: // IS_SPONSOR_CAR
			if (InQuickRace && CarSelectFNGObject_CountAvailableCars(CarSelectFNGObject, 4)) // IS_CAREER_CAR
			{
				NewCategory = 4;
				break;
			}
			// fall through
		case 4: // IS_CAREER_CAR
			if (CarSelectFNGObject_CountAvailableCars(CarSelectFNGObject, 2)) // IS_TUNED_CAR
			{
				NewCategory = 2;
				break;
			}
			// fall through
		case 2: // IS_TUNED_CAR
			NewCategory = 1;
			break;

		case 1: // IS_STOCK_CAR
			NewCategory = 1 | 2;
			break;
		}
	}
	else if (Message == MSG_CARSELECT_NEXT)
	{
		// stock|tuned -> stock -> (tuned ->) (career ->) (sponsor ->) preset -> loop
		switch (NewCategory)
		{
		case 1 | 2:
			NewCategory = 1;
			break;

		case 1:
			if (CarSelectFNGObject_CountAvailableCars(CarSelectFNGObject, 2))
			{
				NewCategory = 2;
				break;
			}
			// fall through
		case 2:
			if (InQuickRace && CarSelectFNGObject_CountAvailableCars(CarSelectFNGObject, 4))
			{
				NewCategory = 4;
				break;
			}
			// fall through
		case 4:
			if (InQuickRace && CarSelectFNGObject_CountAvailableCars(CarSelectFNGObject, 8))
			{
				NewCategory = 8;
				break;
			}
			// fall through
		case 8:
			NewCategory = CUSTOM_IS_PRESET_CAR;
			break;

		case CUSTOM_IS_PRESET_CAR:
			NewCategory = 1 | 2;
			break;
		}
	}

	SetCarSelectCategory(NewCategory);
	CarSelectFNGObject_ResetBrowableCars(CarSelectFNGObject);
	CarSelectFNGObject_UpdateUI(CarSelectFNGObject);

	return 1;
}

// 0x4EED10 CarSelectFNGObject::ChangeCategory
void __declspec(naked) CarSelectFNGObject_ChangeCategoryCodeCave()
{
	_asm
	{
		push ecx                                   // preserve this
		push dword ptr[esp + 8]                    // message
		mov ecx, [esp + 4]                         // this
		call CarSelectFNGObject_ChangeCategory     // __fastcall: ecx = this, stack = message
		pop ecx                                    // restore this
		test eax, eax
		jnz Handled

		// Not our category rotation: replay the original prologue and jump back in.
		// The overwritten instruction was a 5 byte 'mov eax, ds:[0x7F444C]'.
		mov eax, _carSelectCategory                // immediate
		mov eax, [eax]
		mov edx, 0x4EED15                          // edx is clobbered later in that proc anyway
		jmp edx

		Handled :
		retn 0x4
	}
}

// 0x534850 CarCollectionWithPointers::CountAvailableCars
int __stdcall CountAvailablePresetCars()
{
	// Outside the customize menu the category must not stick around unless quick race support is
	// on, otherwise the player can carry it into a menu that cannot handle it. Returning 0 makes
	// the browser fall back to "all cars".
	DWORD MenuState = profileMenuState;

	if (MenuState == MENU_STATE_CAR_CUSTOMIZE)
	{
		if (!PresetCarsInCustomize) return 0;
	}
	else if (MenuState == MENU_STATE_MAIN_MENU || MenuState == MENU_STATE_2P_SPLITSCREEN)
	{
		if (!PresetCarsInQuickRace) return 0;
	}
	else return 0;

	return CountCarPresets();
}

void __declspec(naked) CarCollection_CountAvailableCarsCodeCave()
{
	_asm
	{
		test dword ptr[esp + 4], CUSTOM_IS_PRESET_CAR
		jnz ItsPreset

		// Original prologue, then jump back
		sub esp, 8
		push ebx
		mov ebx, [esp + 0xC + 8]
		mov eax, 0x534858
		jmp eax

		ItsPreset :
		call CountAvailablePresetCars
		retn 8
	}
}

// 0x5162D0 CarCollectionWithPointers::FindCarWithFlagAfterGivenCar
InventoryCar* __stdcall FindPresetCarAfterGivenCar(DWORD FlagsToCheck, InventoryCar* GivenCar)
{
	BuildPresetCarList();

	if (!NumPresetCars) return nullptr;
	if (!GivenCar) return &PresetCarsBase[0].Parent;

	for (int i = 0; i < NumPresetCars - 1; i++)
	{
		if ((InventoryCar*)&PresetCarsBase[i] == GivenCar)
			return &PresetCarsBase[i + 1].Parent;
	}

	return nullptr;
}

void __declspec(naked) CarCollection_FindCarWithFlagAfterGivenCarCodeCave()
{
	_asm
	{
		test dword ptr[esp + 4], CUSTOM_IS_PRESET_CAR
		jnz ItsPreset

		cmp byte ptr[ExtendFeCarLimits], 0
		jne ChainFe

		// Original prologue, then jump back
		push ebx
		push esi
		push edi
		mov edi, [esp + 0x14]
		mov eax, 0x5162D7
		jmp eax

		ChainFe :
		jmp GetCarFiltered

		ItsPreset :
		jmp FindPresetCarAfterGivenCar
	}
}

// 0x503510 CarCollectionWithPointers::GetCarForSlot
void __declspec(naked) CarCollection_GetCarForSlotCodeCave()
{
	_asm
	{
		// Slot hash 0 means "no car in this slot" and turns up constantly while the collection
		// is walked. The old check was a bare jbe, so 0 fell into the preset path, dec'd to -1
		// and returned PresetCarsBase - 0x1C instead of null. Needs a lower bound and a null
		// base check; flags-only comparisons so nothing is clobbered on the fallthrough.
		cmp dword ptr[esp + 4], 0
		je NotPreset
		mov eax, [NumPresetCars]
		cmp dword ptr[esp + 4], eax
		ja NotPreset
		cmp dword ptr[PresetCarsBase], 0
		je NotPreset

		mov eax, [esp + 4]
		dec eax
		imul eax, 0x1C                 // sizeof(SponsorCar)
		add eax, [PresetCarsBase]
		retn 4

		NotPreset :
		cmp byte ptr[ExtendFeCarLimits], 0
		jne ChainFe

		// Original prologue, then jump back
		push esi
		push edi
		mov edi, [esp + 0xC]
		mov eax, 0x503516
		jmp eax

		ChainFe :
		// __fastcall(this in ecx, unused edx, hash on the stack) matches this entry state
		jmp GetCarRecordByHandle
	}
}

// 0x552DBB inside CustomizeCar: no TunedCar instance was found for the slot hash, so build one
// from the preset and copy its tuning in.
DWORD* __stdcall CreateTunedCarFromPresetCar(DWORD* CarCollection, DWORD SlotNameHash)
{
	char Buf[64];

	BuildPresetCarList();

	if (SlotNameHash == 0 || (int)SlotNameHash > NumPresetCars || !PresetCarsBase) return nullptr;

	SponsorCar* PresetCar = &PresetCarsBase[SlotNameHash - 1];

	CarPreset* Preset = FindCarPreset(PresetCar->CarPresetHash);
	if (!Preset) return nullptr;

	sprintf(Buf, "STOCK_%s", Preset->modelName);

	DWORD* TunedCar = CarCollection_CreateNewTunedCarFromDataAtSlot(CarCollection, bStringHash(Buf));
	if (!TunedCar) return nullptr;

	DWORD* MenuCarInstance = (DWORD*)_menuCarInstanceB;

	SponsorCar_ApplyTuningToInstance(PresetCar, profilePlayerIndex, MenuCarInstance, 0);
	TunedCar18_CopyTuningFromMenuCarInstance(TunedCar + (0x18 / 4), MenuCarInstance);

	return TunedCar;
}

void __declspec(naked) CustomizeCar_SetCarInstanceIfMissingCodeCave()
{
	_asm
	{
		test esi, esi
		jnz AllGood

		// No car instance: assume a preset car was picked
		push ebx                       // slotNameHash
		push ecx                       // this (CarCollection)
		call CreateTunedCarFromPresetCar
		test eax, eax
		jz AllGood                     // not one of ours, let the game fail the way it always did
		mov esi, eax
		mov byte ptr[ebp + 8 + 3], 1   // same flag the game sets when creating a tuned car from stock

		AllGood :
		mov cl, byte ptr[ebp + 8 + 3]  // overwrote this
			push 0                     // overwrote this
			mov eax, 0x552DC0
			jmp eax
	}
}

// 0x525FBB: going in-game, the player car collection is searched again by slot hash
void __declspec(naked) FindPresetCarWhenTuningForIngameCarCodeCave()
{
	_asm
	{
		// Same lower bound problem as GetCarForSlot above
		test edi, edi
		jz NotPreset
		cmp edi, [NumPresetCars]
		ja NotPreset
		cmp dword ptr[PresetCarsBase], 0
		je NotPreset

		dec edi
		imul edi, 0x1C                 // sizeof(SponsorCar)
		add edi, [PresetCarsBase]
		mov eax, 0x525FE6
		jmp eax

		NotPreset :
		cmp byte ptr[ExtendFeCarLimits], 0
		jne ChainFe

		// Original instruction, then jump back
		add edx, 0x9BD8                // sizeof(CarCollection)
		mov ecx, 0x525FC1
		jmp ecx

		ChainFe :
		jmp StartQuickRaceHook1
	}
}

// 0x4B2855 tail of CarSelectFNGObject::UpdateUI. The game only knows its own categories and
// falls back to the "Stock cars" label for anything else, so relabel and show the preset name.
#define hashof_carselect_category_label 0x3E81DE59
#define hashof_OL_CarMode_Group         0x2043ABA0
#define hashof_racemode                 0x6578FB3F
#define hashof_racemodevalue            0xA9205BD8

#define OffsetOfCurrentSelectedCar 0x58 // CarSelectFNGObject.currentSelectedCar
#define OffsetOfEntrySlotHash      0x920
#define OffsetOfUIElementPos       0x2C
#define OffsetOfUIPosLeftOffset    0x1C

void __stdcall CarSelectFNGObject_PostUpdateUI(DWORD* CarSelectFNGObject)
{
	if (carSelectCategory != CUSTOM_IS_PRESET_CAR)
	{
		FEngSetInvisible_Pkg("UI_QRCarSelect.fng", hashof_OL_CarMode_Group);
		return;
	}

	FEPrintf("UI_QRCarSelect.fng", hashof_carselect_category_label, "%s", PresetCarsCategoryName);

	// Reuse the online ranked-car "race mode" label group to show the preset name
	DWORD* Group = (DWORD*)FEngFindObject("UI_QRCarSelect.fng", hashof_OL_CarMode_Group);
	if (Group)
	{
		FEngSetVisible(Group);
		// Clearing the hidden flag is not enough, the group also has a HIDE script running
		FEngSetScript_Obj(Group, "SHOW", 0);
	}

	FEngSetInvisible_Pkg("UI_QRCarSelect.fng", hashof_racemodevalue);

	DWORD* SelectedEntry = (DWORD*)*(DWORD*)((BYTE*)CarSelectFNGObject + OffsetOfCurrentSelectedCar);
	if (!SelectedEntry) return;

	DWORD SlotHash = *(DWORD*)((BYTE*)SelectedEntry + OffsetOfEntrySlotHash);
	if (SlotHash == 0 || (int)SlotHash > NumPresetCars || !PresetCarsBase) return;

	DWORD PresetHash = PresetCarsBase[SlotHash - 1].CarPresetHash;

		CarPreset* Preset = FindCarPreset(PresetHash);
		if (!Preset) return;

		FEPrintf("UI_QRCarSelect.fng", hashof_racemode, "%s", Preset->Name);


	DWORD* Label = (DWORD*)FEngFindObject("UI_QRCarSelect.fng", hashof_racemode);
	if (Label)
	{
		BYTE* Pos = *(BYTE**)((BYTE*)Label + OffsetOfUIElementPos);
		if (Pos) *(float*)(Pos + OffsetOfUIPosLeftOffset) = 85.0f; // the label text is right aligned
	}
}

void __declspec(naked) CarSelectFNGObject_UpdateUICodeCave()
{
	_asm
	{
		pushad
		push ecx
		call CarSelectFNGObject_PostUpdateUI
		popad
		// We overwrote a jmp, so just do it
		mov eax, 0x497CD0
		jmp eax
	}
}

// 0x579D70 CheatScreenData::IsSponsorCarCheatTriggered
int __stdcall IsSponsorCarCheatTriggered_AlwaysTrue(DWORD* SponsorCarHash)
{
	return 1;
}

void InitPresetCars()
{
	if (UnlockSponsorCarsWithoutCheats)
	{
		injector::MakeJMP(0x579D70, IsSponsorCarCheatTriggered_AlwaysTrue, true); // CheatScreenData::IsSponsorCarCheatTriggered
	}

	if (!PresetCarsInCustomize && !PresetCarsInQuickRace) return;

	injector::MakeJMP(0x4EED10, CarSelectFNGObject_ChangeCategoryCodeCave, true);              // CarSelectFNGObject::ChangeCategory
	injector::MakeJMP(0x4B2855, CarSelectFNGObject_UpdateUICodeCave, true);                    // CarSelectFNGObject::UpdateUI (tail)
	injector::MakeJMP(0x534850, CarCollection_CountAvailableCarsCodeCave, true);               // CarCollectionWithPointers::CountAvailableCars
	injector::MakeJMP(0x5162D0, CarCollection_FindCarWithFlagAfterGivenCarCodeCave, true);     // CarCollectionWithPointers::FindCarWithFlagAfterGivenCar
	injector::MakeJMP(0x503510, CarCollection_GetCarForSlotCodeCave, true);                    // CarCollectionWithPointers::GetCarForSlot

	if (PresetCarsInCustomize)
	{
		// Not a byte overlap with FeCarLimits, but 0x552D60 (IsCarStockHook) redirects the start
		// of the same function, so this patch may simply never be reached with that feature on.
		injector::MakeJMP(0x552DBB, CustomizeCar_SetCarInstanceIfMissingCodeCave, true);       // CustomizeCar
	}

	if (PresetCarsInQuickRace)
	{
		injector::MakeJMP(0x525FBB, FindPresetCarWhenTuningForIngameCarCodeCave, true);        // RaceStarter
	}
}
