#pragma once

#include "stdio.h"
#include "InGameFunctions.h"
#include "GlobalVariables.h"
#include "includes\injector\injector.hpp"

// ---------------------------------------------------------------------------------------------
// Car trims
//
// One car type, several factory versions. SUPRA and TURBOSUPRA are the same car slot as far as
// the car lot is concerned, but the badges, the name art, the base paint and the physics all come
// from whichever version is fitted.
//
// A trim is a Universal CarTypeInfo.
//
//   UsageType = Universal, which is 4. IsRacer/IsCop/IsTraffic compare against 0/1/2, so a
//   Universal entry falls out of all three: never spawned, never sold, never picked by AI. It
//   still gets its own <NAME>.ini for free, because LoadCarConfigs reads "<CollectionName>.ini"
//   for every car type in the table. That means a trim has the full per-car config, [BodyShop]
//   included, which is how a track only trim can restrict bodykits later on.
//
// The trim declares its parent in that ini:
//
//   [Main]
//   TrimOf = SUPRA
//
// Read into CarConfigs[].Main.TrimOf as a name hash. Never inherited from _General.ini.
//
// The selection is a part in RIGHT_SIDE_MIRROR, named <CAR>_<TRIM>:
//
//   SUPRA_SUPRA, SUPRA_TURBOSUPRA, SUPRA_BASESUPRA
//
// The name is read back with CarPart_GetName and the car prefix stripped, so the part needs no
// attributes at all. AttributeCount = 0 is fine.
//
// RIGHT_SIDE_MIRROR was chosen because LEFT_SIDE_MIRROR and RIGHT_SIDE_MIRROR appear in the whole
// tree only inside the carbon slot list; the mirrors a car actually wears live in WING_MIRROR as
// one part for both sides. CV (slot 167) was tried first and abandoned. WHEEL_MANUFACTURER (168)
// is spoken for, NLG plans burnt rubber colour there.
// ---------------------------------------------------------------------------------------------

int TrimSwapKey = 0;   // virtual key code, 0 turns it off
bool TrimTrace = false;

// CarTypeInfo fields a trim overrides. Stride is 0x890, layout taken from Nikki's serialisation
// order in Darius/Nikki/Support.Underground2/Class/CarTypeInfo.cs.
//
//   +0x0C0  ManufacturerName    accessor 0x610170, lea eax,[ecx+0C0h]
//   +0x110  physics block       480 floats, what RidePhysicsInfo::RebuildPhysicsInfo copies
//   +0x840  Index
//   +0x844  UsageType           Universal = 4
//   +0x84C  DefaultBasePaint    paint NAME hash, read by RideInfo::SetStockParts at 0x6370B6
//
// Cost at +0x87C is NOT in this list on purpose. Nothing in SPEED2.EXE ever reads that offset off
// a CarTypeInfo, cars in NFSU2 career are unlocked rather than bought, so making a trim cost
// something would have to invent the concept first.
#define CarTypeInfo_DefaultBasePaint(info) (*(DWORD*)((BYTE*)(info) + 0x84C))
#define CarTypeInfo_UsageType(info)        (*(BYTE*)((BYTE*)(info) + 0x844))

void TrimTraceLine(const char* fmt, ...)
{
	if (!TrimTrace) return;

	auto Path = CurrentWorkingDirectory / "UnlimiterData" / "_TrimSwap.txt";
	FILE* f = fopen(Path.string().c_str(), "a");
	if (!f) return;

	va_list args;
	va_start(args, fmt);
	vfprintf(f, fmt, args);
	va_end(args);

	fclose(f);
}

// This reads hundreds of bytes past whatever it is given, so a small integer means a caller
// passed something that is not a pointer at all. Same shape as PartLink_ValidRideInfo.
bool Trim_ValidPtr(void* p)
{
	uintptr_t v = (uintptr_t)p;
	return v >= 0x00010000 && v <= 0xC0000000 && !(v & 3);
}

bool Trim_ValidRide(DWORD* Ride)
{
	return Trim_ValidPtr(Ride);
}

// ---------------------------------------------------------------------------------------------
// Identity
// ---------------------------------------------------------------------------------------------

// Name hash and parent per car type, built once. Both answers are wanted on paths that run per
// car per frame, and working them out from the strings every time turns every one of those into
// a scan of the whole car table.
std::vector<DWORD> TrimNameHashes;
std::vector<int> TrimParents;

DWORD Trim_NameHashOf(int CarType)
{
	if (CarType < 0 || CarType >= (int)TrimNameHashes.size()) return 0;

	return TrimNameHashes[CarType];
}

int Trim_CarTypeFromNameHash(DWORD Hash)
{
	if (!Hash) return -1;

	for (int i = 0; i < (int)TrimNameHashes.size(); i++)
		if (TrimNameHashes[i] == Hash) return i;

	return -1;
}

// The car this one is a trim of, or -1 when it is a car in its own right.
int Trim_ParentOf(int CarType)
{
	if (CarType < 0 || CarType >= (int)TrimParents.size()) return -1;

	return TrimParents[CarType];
}

bool Trim_IsTrim(int CarType)
{
	return Trim_ParentOf(CarType) >= 0;
}

// Called once from LoaderCarInfo_Hook, right after LoadCarConfigs has read every TrimOf.
void Trim_BuildTables()
{
	TrimNameHashes.assign(CarCount, 0);
	TrimParents.assign(CarCount, -1);

	for (int i = 0; i < CarCount; i++)
	{
		char const* Name = GetCarTypeName(i);
		TrimNameHashes[i] = (Name && Name[0]) ? bStringHash((char*)Name) : 0;
	}

	for (int i = 0; i < CarCount; i++)
		TrimParents[i] = Trim_CarTypeFromNameHash(CarConfigs[i].Main.TrimOf);

	for (int i = 0; i < CarCount; i++)
	{
		if (TrimParents[i] < 0) continue;

		TrimTraceLine("trim %s of %s, usage type %d\n", GetCarTypeName(i),
			GetCarTypeName(TrimParents[i]), (int)CarTypeInfo_UsageType(CarConfigs[i].CarTypeInfo));

		// A trim that is still a Racer is also a car in the car lot, which is almost never what
		// was meant and is invisible until someone scrolls past a duplicate.
		if (CarTypeInfo_UsageType(CarConfigs[i].CarTypeInfo) == 0)
			TrimTraceLine("  warning: %s is a trim but its UsageType is Racer, set it to Universal\n",
				GetCarTypeName(i));

		// PartLink is what HIDESLOT and SWAPSLOT on a trim part run on, and it is off unless the
		// PARENT car's own ini turns it on. A trim part carrying link attributes with no
		// <PARENT>.ini next to it looks broken for no visible reason, so say so here.
		if (!CarConfigs[TrimParents[i]].PartLinking.Enabled)
			TrimTraceLine("  note: %s has [PartLink] Enabled = 0, so HIDESLOT and SWAPSLOT on its"
				" trim parts do nothing. Add UnlimiterData\\%s.ini with [PartLink] Enabled = 1.\n",
				GetCarTypeName(TrimParents[i]), GetCarTypeName(TrimParents[i]));
	}
}

// Every trim of a car, in car type order. The parent itself is not included, so a car with one
// trim cycles between "no trim" and that trim.
int Trim_ListFor(int CarType, int* Out, int Max)
{
	if (CarType < 0 || CarType >= CarCount) return 0;

	int n = 0;

	for (int i = 0; i < CarCount && n < Max; i++)
		if (Trim_ParentOf(i) == CarType) Out[n++] = i;

	return n;
}

// ---------------------------------------------------------------------------------------------
// Selection
// ---------------------------------------------------------------------------------------------

// The part's own name, via CarPart_GetName. NewGetCarPart cannot be asked for a part by name:
// 0x61B980 calls GetTypesFromSlot and compares CarPartTypeNameHashTable[part+7] against what that
// returns, so the hash argument is not the part name and looking a trim part up that way comes
// back empty even though the part is there. A part already in hand will still say what it is
// called, so the slot is enumerated instead and each part asked.
DWORD Trim_NameHashOfPart(int CarType, DWORD* Part)
{
	if (!Part) return 0;

	char const* Name = CarPart_GetName(Part);
	if (!Name || !Name[0]) return 0;

	char const* CarName = GetCarTypeName(CarType);
	if (!CarName || !CarName[0]) return 0;

	// <CAR>_<TRIM>, so skip the car name and the underscore
	size_t Prefix = strlen(CarName);

	if (strncmp(Name, CarName, Prefix) != 0 || Name[Prefix] != '_') return 0;

	return bStringHash((char*)(Name + Prefix + 1));
}

// The part standing for a trim, found by walking the slot rather than by name.
//
//   Part = NewGetCarPart(db, CarType, Slot, 0, 0, -1);      first
//   Part = NewGetCarPart(db, CarType, Slot, 0, Part, -1);   next
DWORD* Trim_PartFor(int CarType, int TrimType)
{
	if (TrimType < 0 || TrimType >= CarCount) return nullptr;

	DWORD Wanted = Trim_NameHashOf(TrimType);

	for (DWORD* Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, CARSLOTID_RIGHT_SIDE_MIRROR, 0, 0, -1);
		Part;
		Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, CARSLOTID_RIGHT_SIDE_MIRROR, 0, Part, -1))
	{
		if (Trim_NameHashOfPart(CarType, Part) == Wanted) return Part;
	}

	return nullptr;
}

// Which trim this car is wearing, by matching the part in RIGHT_SIDE_MIRROR against each trim.
int Trim_OnRide(DWORD* Ride)
{
	if (!Trim_ValidRide(Ride)) return -1;

	int CarType = *(int*)Ride;
	if (CarType < 0 || CarType >= CarCount) return -1;

	DWORD* Installed = (DWORD*)Ride[356 + CARSLOTID_RIGHT_SIDE_MIRROR];

	int Trim = Trim_CarTypeFromNameHash(Trim_NameHashOfPart(CarType, Installed));

	// A part whose name happens to parse still has to be a declared trim OF THIS CAR, otherwise
	// any part called <CAR>_<SOMETHING> would be read as one.
	if (Trim >= 0 && Trim_ParentOf(Trim) != CarType) return -1;

	return Trim;
}

// ---------------------------------------------------------------------------------------------
// Which CarTypeInfo the badges come from
//
// Every screen that draws a car's badges does the same three instructions:
//
//     mov eax, [this+58h]      the SelectableCar it is drawing
//     mov ecx, [eax+918h]      its CarTypeInfo
//     mov eax, [ecx+840h]      its car type, which is all GetCarTypeLogoHash is given
//
// A car type is not enough to find a trim, because two saved cars can be the same car type
// wearing different trims. Reading the trim off the car viewer's ride answers with whichever car
// is on screen, which is right for the selected car and wrong for every other one: changing the
// trim on one saved SUPRA made the next saved SUPRA's badge follow it, while its performance,
// which is per ride, stayed correct. That mismatch is what gave the cause away.
//
// So the screens are wrapped at their call sites and the trim of the car they are about to draw
// is put where GetCarTypeLogoHash can see it, for exactly the length of that call. Nothing is
// kept afterwards, which is the whole difference from the captured this pointer that crashed the
// build before last.
// ---------------------------------------------------------------------------------------------

DWORD* (__thiscall* SelectableCar_GetRide)(DWORD* SelectableCar) = (DWORD * (__thiscall*)(DWORD*))0x511E00;

bool TrimOverrideActive = false;  // a screen is asking on behalf of one particular car
int TrimOverrideTrim = -1;        // and that car wears this trim, -1 for none

void Trim_PushOverrideFromRide(DWORD* Ride)
{
	TrimOverrideActive = false;
	TrimOverrideTrim = -1;

	if (!Trim_ValidRide(Ride)) return;

	TrimOverrideTrim = Trim_OnRide(Ride);
	TrimOverrideActive = true;
}

// The screens do NOT agree on where the selected car sits, which is worth checking before
// wrapping one rather than after:
//
//   UICareerCarLot::RefreshHeader          SelectableCar at +0x58
//   UIQRCarSelect::RefreshHeader           SelectableCar at +0x58
//   sub_4BE440, the customize brand logos  SelectableCar at +0x58
//   UICareerCribCarSelect::RefreshHeader   SelectableCar at +0x54
//   sub_4E7F50, the opponent ride screen   a RideInfo* at +0x124, no SelectableCar at all
void Trim_PushOverrideFromSelectableCar(DWORD* Screen, int Offset)
{
	TrimOverrideActive = false;
	TrimOverrideTrim = -1;

	if (!Trim_ValidPtr(Screen)) return;

	DWORD* Selected = (DWORD*)Screen[Offset / 4];
	if (!Trim_ValidPtr(Selected)) return;

	Trim_PushOverrideFromRide(SelectableCar_GetRide(Selected));
}

void Trim_PopOverride()
{
	TrimOverrideActive = false;
	TrimOverrideTrim = -1;
}

// The car type whose CarTypeInfo the badges and the name art should be read from.
int Trim_EffectiveCarType(int CarType)
{
	if (CarType < 0 || CarType >= CarCount) return CarType;

	// A screen told us which car it is drawing, so that is the answer, trim or no trim. The
	// parent check keeps a trim from leaking onto a different car that happens to ask inside the
	// same call.
	if (TrimOverrideActive)
	{
		if (TrimOverrideTrim >= 0 && Trim_ParentOf(TrimOverrideTrim) == CarType) return TrimOverrideTrim;

		return CarType;
	}

	// Nobody said, so fall back to the car on screen. CarViewer_GetRideInfo(0) at 0x4A7890
	// returns TopOrFullScreenRide and follows it; gTheRideInfo does NOT, it belongs to the
	// customize manager and only moves when something is customized.
	DWORD* Ride = CarViewer_GetRideInfo(0);

	if (Trim_ValidRide(Ride) && *(int*)Ride == CarType)
	{
		int Trim = Trim_OnRide(Ride);
		if (Trim >= 0 && Trim < CarCount) return Trim;
	}

	return CarType;
}

DWORD* Trim_CarTypeInfoForRide(DWORD* Ride)
{
	if (!Trim_ValidRide(Ride)) return nullptr;

	int CarType = *(int*)Ride;
	if (CarType < 0 || CarType >= CarCount) return nullptr;

	int Trim = Trim_OnRide(Ride);

	if (Trim >= 0 && Trim < CarCount) return CarConfigs[Trim].CarTypeInfo;

	return CarConfigs[CarType].CarTypeInfo;
}

DWORD* Trim_CarTypeInfoForCarType(int CarType)
{
	if (CarType < 0 || CarType >= CarCount) return nullptr;

	return CarConfigs[Trim_EffectiveCarType(CarType)].CarTypeInfo;
}

// ---------------------------------------------------------------------------------------------
// Wrapping the screens that draw badges
//
// All five are thiscall(Screen*) and all five read the selected car from [this+0x58], so one
// wrapper shape covers them. These are pass through: the screen pointer is used only while the
// game is inside the call, never stored for later.
// ---------------------------------------------------------------------------------------------

typedef void(__thiscall* RefreshHeaderFn)(DWORD* Screen);

RefreshHeaderFn GameCarLotRefreshHeader   = (RefreshHeaderFn)0x4AFE00; // UICareerCarLot
RefreshHeaderFn GameCribRefreshHeader     = (RefreshHeaderFn)0x4B0140; // UICareerCribCarSelect
RefreshHeaderFn GameQRSelectRefreshHeader = (RefreshHeaderFn)0x4B2310; // UIQRCarSelect
RefreshHeaderFn GameCustomizeBrands       = (RefreshHeaderFn)0x4BE440; // brand logos in customize
RefreshHeaderFn GameOtherCarSelect        = (RefreshHeaderFn)0x4E7F50;

#define TRIM_WRAP_SELECTABLE(name, target, offset)                \
	void __fastcall name(DWORD* Screen, void* EDX_Unused)         \
	{                                                             \
		Trim_PushOverrideFromSelectableCar(Screen, offset);       \
		target(Screen);                                           \
		Trim_PopOverride();                                       \
	}

TRIM_WRAP_SELECTABLE(TrimCarLotRefreshHeader,   GameCarLotRefreshHeader,   0x58)
TRIM_WRAP_SELECTABLE(TrimCribRefreshHeader,     GameCribRefreshHeader,     0x54)
TRIM_WRAP_SELECTABLE(TrimQRSelectRefreshHeader, GameQRSelectRefreshHeader, 0x58)
TRIM_WRAP_SELECTABLE(TrimCustomizeBrands,       GameCustomizeBrands,       0x58)

// The opponent ride screen holds a RideInfo outright, so there is no SelectableCar to ask.
void __fastcall TrimOtherCarSelect(DWORD* Screen, void* EDX_Unused)
{
	Trim_PushOverrideFromRide(Trim_ValidPtr(Screen) ? (DWORD*)Screen[0x124 / 4] : nullptr);
	GameOtherCarSelect(Screen);
	Trim_PopOverride();
}

// GarageMainScreen::SetCarType builds SECONDARY_LOGO_<name> straight off the CarTypeInfo instead
// of going through GetCarTypeLogoHash, so the garage name art is a second place to redirect. It
// indexes the array by car type, which means handing it the trim's car type is the whole fix.
//
// Its five call sites are hooked rather than its entry, because a hook on the entry that wants to
// call the original has nowhere left to call: the entry is the hook.
void(__thiscall* GameGarageMainScreenSetCarType)(DWORD* Screen, int CarType) = (void(__thiscall*)(DWORD*, int))0x4A5EE0;

void __fastcall GarageMainScreen_SetCarType(DWORD* Screen, void* EDX_Unused, int CarType)
{
	GameGarageMainScreenSetCarType(Screen, Trim_EffectiveCarType(CarType));
}

// ---------------------------------------------------------------------------------------------
// Applying a trim
//
// Writing RideInfo[356 + slot] does not update what is on screen. That is what made the first
// build look like it changed nothing but the badges: the part went in, the paint went in, and
// the car standing there was the one that had already been built.
//
// CarCustomizeManager::InstallPart at 0x55C230 is the game's own answer and does all of it:
//
//     RideInfo::SetPart(manager+0x940, slot, part)
//     ... slot specific cleanups, trunk audio, hood vinyls, carbon vinyls
//     RideInfo::operator=(manager+0x50, manager+0x940)
//     CarViewer::SetRideInfo(ride, 1, 0)                     rebuilds the car on screen
//     PresetCarSlot::FillWithRide([manager+0x2C]+0x18, ride) writes the record the save reads
//     StarGazerGuide::NotifyVisualRatingChange
//
// So a trim is installed exactly the way the Body Shop installs a bumper, and the refresh, the
// record and the rep rating all come along.
// ---------------------------------------------------------------------------------------------

#define CarCustomizeManager_RideInfo (0x940 / 4)

// Byte 0 of the manager is its "has control" flag: BeginCarCustomize sets it to 1 at 0x552DF2 and
// CarCustomizeManager::UnTakeControl clears it at 0x54FDAA. That is a real customize session, as
// opposed to the menu state, which reports 0x20 for the car lot as well.
//
// The build before this one compared the viewer ride against the manager ride part for part
// instead. It worked, but only once the parts screen had synced the two, so the key did nothing
// until you had been into Parts once. Eleven slots differ before that, WIDE_BODY first.
bool Trim_CustomizeSessionActive()
{
	return *(BYTE*)gCarCustomizeManager == 1;
}

DWORD* Trim_CustomizeRide()
{
	return (DWORD*)gCarCustomizeManager + CarCustomizeManager_RideInfo;
}

// Put a trim on the car the customize manager is editing. Everything visible follows from the
// two InstallPart calls; nothing is written into a RideInfo by hand.
bool Trim_Set(int CarType, int TrimType)
{
	DWORD* Manager = (DWORD*)gCarCustomizeManager;

	DWORD* Part = (TrimType >= 0) ? Trim_PartFor(CarType, TrimType) : nullptr;

	if (TrimType >= 0 && !Part)
	{
		TrimTraceLine("%s: no part named %s_%s in RIGHT_SIDE_MIRROR\n",
			GetCarTypeName(CarType), GetCarTypeName(CarType), GetCarTypeName(TrimType));
		return false;
	}

	CarCustomizeManager_InstallPart(Manager, CARSLOTID_RIGHT_SIDE_MIRROR, Part);

	// The trim's own DefaultBasePaint, installed as a part the same way the Paint Shop does it.
	// Only on the way on: coming off a trim leaves the colour alone rather than repainting the
	// car behind the player's back.
	if (TrimType >= 0)
	{
		DWORD Paint = CarTypeInfo_DefaultBasePaint(CarConfigs[TrimType].CarTypeInfo);

		DWORD* PaintPart = Paint
			? CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, CARSLOTID_BASE_PAINT, Paint, 0, -1)
			: nullptr;

		if (PaintPart) CarCustomizeManager_InstallPart(Manager, CARSLOTID_BASE_PAINT, PaintPart);
		else if (Paint) TrimTraceLine("  paint 0x%08X not available on %s\n",
			(unsigned int)Paint, GetCarTypeName(CarType));
	}

	// Physics comes out of the trim's CarTypeInfo the next time it is rebuilt. InstallPart never
	// asks for one, because a visual part does not change the numbers, so ask here.
	RideInfo_RebuildPhysicsInfo(Trim_CustomizeRide(), 0, true, true, true);

	TrimTraceLine("%s: trim -> %s\n", GetCarTypeName(CarType),
		TrimType >= 0 ? GetCarTypeName(TrimType) : "(none)");

	return true;
}

// The next trim in the cycle for this car, wrapping back through "no trim" at the end.
// Returns -2 when the car has no trims at all, which is not the same answer as -1.
int Trim_Next(int CarType, int Current)
{
	int Trims[32];
	int Count = Trim_ListFor(CarType, Trims, 32);

	if (!Count) return -2;

	int At = -1;

	for (int i = 0; i < Count; i++)
		if (Trims[i] == Current) { At = i; break; }

	return (At + 1 < Count) ? Trims[At + 1] : -1;
}

// ---------------------------------------------------------------------------------------------
// Cycling
// ---------------------------------------------------------------------------------------------

bool TrimSwapKeyWasDown = false;

// Menu state, kept for the My Cars lock that still has to be written. The car lot reports the
// same 0x20 as customizing, so it cannot tell those two apart and is not used as a gate here.
#define _profileData     0x83A9D0
#define profileMenuState *(DWORD*)(_profileData + 0x156A8)

#define MENU_STATE_CAREER_MENU    0x01
#define MENU_STATE_MAIN_MENU      0x02
#define MENU_STATE_2P_SPLITSCREEN 0x04
#define MENU_STATE_CAR_CUSTOMIZE  0x20

void Trim_PollKey()
{
	if (!TrimSwapKey) return;

	bool Down = (GetAsyncKeyState(TrimSwapKey) & 0x8000) != 0;

	if (!Down) { TrimSwapKeyWasDown = false; return; }
	if (TrimSwapKeyWasDown) return;

	TrimSwapKeyWasDown = true;

	// The poll sits in CarRenderInfo::Render, which also runs in a race. The front end is a hard
	// precondition rather than something the menu state happens to imply.
	if (*(int*)_TheGameFlowManager != 3) return; // TheGameFlowManager->mCurrentState, 3 = front end

	// Customize only, for now. It is the one place the car can actually be rebuilt, and it is
	// where the trim browser is going to live. In the car lot the badges already follow a trim
	// that is on the car, there is just no way to change it there yet.
	if (!Trim_CustomizeSessionActive()) return;

	DWORD* Ride = Trim_CustomizeRide();
	if (!Trim_ValidRide(Ride)) return;

	int CarType = *(int*)Ride;
	if (CarType < 0 || CarType >= CarCount) return;

	int Next = Trim_Next(CarType, Trim_OnRide(Ride));

	if (Next == -2)
	{
		TrimTraceLine("%s: no trims declare TrimOf = %s\n",
			GetCarTypeName(CarType), GetCarTypeName(CarType));
		return;
	}

	Trim_Set(CarType, Next);
}

void InitTrimSwap()
{
	// Call sites rather than the function entries, so no prologue has to be replayed and the
	// originals stay callable.

	// Every screen that draws a car's badges, so the trim comes from the car being drawn rather
	// than from whichever car the viewer happens to hold
	injector::MakeCALL(0x4B00EF, TrimCarLotRefreshHeader, true);   // UICareerCarLot
	injector::MakeCALL(0x4ED93C, TrimCarLotRefreshHeader, true);

	injector::MakeCALL(0x4B0664, TrimCribRefreshHeader, true);     // UICareerCribCarSelect

	injector::MakeCALL(0x4E28E3, TrimQRSelectRefreshHeader, true); // UIQRCarSelect
	injector::MakeCALL(0x4EEFA4, TrimQRSelectRefreshHeader, true);
	injector::MakeCALL(0x4FA41E, TrimQRSelectRefreshHeader, true);
	injector::MakeCALL(0x4FC576, TrimQRSelectRefreshHeader, true);

	injector::MakeCALL(0x4E889A, TrimCustomizeBrands, true);       // sub_4BE440, from sub_4E8530
	injector::MakeCALL(0x4F26C0, TrimOtherCarSelect, true);        // sub_4E7F50

	// Garage name art, the one SECONDARY_LOGO that does not go through GetCarTypeLogoHash
	injector::MakeCALL(0x4B00DC, GarageMainScreen_SetCarType, true); // GarageMainScreen::SetCarType
	injector::MakeCALL(0x4B065D, GarageMainScreen_SetCarType, true);
	injector::MakeCALL(0x4D91D1, GarageMainScreen_SetCarType, true);
	injector::MakeCALL(0x4E27C9, GarageMainScreen_SetCarType, true);
	injector::MakeCALL(0x4E8780, GarageMainScreen_SetCarType, true);
}
