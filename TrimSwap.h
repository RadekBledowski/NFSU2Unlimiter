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
// from whichever version is selected.
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

bool Trim_ValidRide(DWORD* Ride)
{
	uintptr_t v = (uintptr_t)Ride;
	return v >= 0x00010000 && v <= 0xC0000000 && !(v & 3);
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
// Redirection
//
// One place decides which CarTypeInfo a car's identity is read from. Everything a trim changes
// goes through here, so adding a field to the list is a matter of pointing one more consumer at
// it rather than inventing a new mechanism each time.
// ---------------------------------------------------------------------------------------------

DWORD* Trim_CarTypeInfoForRide(DWORD* Ride)
{
	if (!Trim_ValidRide(Ride)) return nullptr;

	int CarType = *(int*)Ride;
	if (CarType < 0 || CarType >= CarCount) return nullptr;

	int Trim = Trim_OnRide(Ride);

	if (Trim >= 0 && Trim < CarCount) return CarConfigs[Trim].CarTypeInfo;

	return CarConfigs[CarType].CarTypeInfo;
}

// For callers that hold a car type and nothing else, which is most of the front end.
//
// CarViewer_GetRideInfo(0) at 0x4A7890 returns TopOrFullScreenRide and follows the car actually
// on screen. gTheRideInfo does NOT: it reported the same car in every menu and cost a build.
// The car type is compared so that a screen asking about some other car gets that car's own
// answer, which is what keeps the regional cases (CORSA -> OPEL, MIATA -> MX5) working.
int Trim_EffectiveCarType(int CarType)
{
	if (CarType < 0 || CarType >= CarCount) return CarType;

	DWORD* Ride = CarViewer_GetRideInfo(0);

	if (Trim_ValidRide(Ride) && *(int*)Ride == CarType)
	{
		int Trim = Trim_OnRide(Ride);
		if (Trim >= 0 && Trim < CarCount) return Trim;
	}

	return CarType;
}

DWORD* Trim_CarTypeInfoForCarType(int CarType)
{
	if (CarType < 0 || CarType >= CarCount) return nullptr;

	return CarConfigs[Trim_EffectiveCarType(CarType)].CarTypeInfo;
}

// ---------------------------------------------------------------------------------------------
// What follows the trim
// ---------------------------------------------------------------------------------------------

// Base paint. RideInfo::SetStockParts does exactly this at 0x63709B: read DefaultBasePaint off
// the CarTypeInfo, ask the part database for the BASE_PAINT part with that name hash, drop it in
// slot 63. Paint slots are the one case where NewGetCarPart's hash argument IS the name, because
// for them GetTypesFromSlot returns the paint type and the table entry is the colour name.
//
// Only applied when the trim is put on, never on the way off: coming off a trim leaves whatever
// colour is there rather than repainting the car behind the player's back.
bool Trim_ApplyBasePaint(DWORD* Ride, int TrimType)
{
	if (!Trim_ValidRide(Ride) || TrimType < 0 || TrimType >= CarCount) return false;

	DWORD Paint = CarTypeInfo_DefaultBasePaint(CarConfigs[TrimType].CarTypeInfo);
	if (!Paint) return false;

	int CarType = *(int*)Ride;

	DWORD* Part = CarPartDatabase_NewGetCarPart((DWORD*)_CarPartDB, CarType, CARSLOTID_BASE_PAINT, Paint, 0, -1);

	if (!Part)
	{
		TrimTraceLine("  paint 0x%08X not available on %s\n", (unsigned int)Paint, GetCarTypeName(CarType));
		return false;
	}

	Ride[356 + CARSLOTID_BASE_PAINT] = (DWORD)Part;

	TrimTraceLine("  paint -> 0x%08X\n", (unsigned int)Paint);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Persistence
//
// The save does store the slot. PresetCarSlot::FillWithRide at 0x503950 walks CAR_SLOT_ID 0 to
// 0xAA and packs every installed part as (word[part+2] << 16) | word[part+0], RIGHT_SIDE_MIRROR
// included, and FECarConfig::BuildRide unpacks the same range on the way back. So the format was
// never the problem.
//
// What was the problem is which RideInfo the trim went into. CarViewer_GetRideInfo(0) returns
// TopOrFullScreenRide, one global the viewer reuses for every car it shows, and nothing in it is
// ever packed into a record. The ride that IS packed is the one the customize manager owns, and
// the game write-throughs it on every change:
//
//     lea ecx, [eax+940h]      the manager's own RideInfo
//     cmp esi, ecx             only if this is the ride that changed
//     mov ecx, [eax+2Ch]       the FECarConfig record
//     add ecx, 18h             its PresetCarSlot
//     call PresetCarSlot::FillWithRide
//
// which is 0x5213AC, the tail of the perf package installer. Same three lines here.
// ---------------------------------------------------------------------------------------------

#define CarCustomizeManager_RideInfo    592     // +0x940, in DWORDs
#define CarCustomizeManager_CarConfig   11      // +0x2C, in DWORDs
#define FECarConfig_PresetCarSlot       (0x18 / 4)

void(__thiscall* Trim_PresetCarSlotFillWithRide)(DWORD* PresetCarSlot, DWORD* Ride)
	= (void(__thiscall*)(DWORD*, DWORD*))0x503950;

DWORD* Trim_CustomizeManagerRide()
{
	DWORD* Manager = (DWORD*)gCarCustomizeManager;
	if (!Manager) return nullptr;

	DWORD* Ride = Manager + CarCustomizeManager_RideInfo;

	return Trim_ValidRide(Ride) ? Ride : nullptr;
}

// Pack the ride back into the record the save reads. Only ever called with the manager's own
// ride, which is the same guard the game uses before doing this itself.
bool Trim_WriteThrough(DWORD* Ride)
{
	DWORD* Manager = (DWORD*)gCarCustomizeManager;
	if (!Manager || Ride != Manager + CarCustomizeManager_RideInfo) return false;

	DWORD* Record = (DWORD*)Manager[CarCustomizeManager_CarConfig];
	if (!Trim_ValidRide(Record)) return false;

	Trim_PresetCarSlotFillWithRide(Record + FECarConfig_PresetCarSlot, Ride);

	TrimTraceLine("  written through to record %p\n", Record);
	return true;
}

// The trim the player last put on each car type, so that picking one in the car lot survives the
// car being bought. A record is built from a stock ride, never from the car on screen, so without
// this the trim would be thrown away the moment the car became the player's.
//
// Per car type and not per profile. A trim chosen for a car that is then never bought stays
// remembered until something else is chosen for the same car, which shows up as a car arriving
// already wearing the trim that was last looked at. That is the intended answer often enough to
// leave it alone until there is a real ownership test.
std::vector<int> TrimChosen;

void Trim_Remember(int CarType, int TrimType)
{
	if (CarType < 0 || CarType >= CarCount) return;

	if ((int)TrimChosen.size() != CarCount) TrimChosen.assign(CarCount, -1);

	TrimChosen[CarType] = TrimType;
}

int Trim_Remembered(int CarType)
{
	if (CarType < 0 || CarType >= (int)TrimChosen.size()) return -1;

	return TrimChosen[CarType];
}

// Put a trim on a ride. The one place that writes the slot, so nothing else has to know that a
// trim is a part or which slot it lives in.
bool Trim_Install(DWORD* Ride, int TrimType)
{
	if (!Trim_ValidRide(Ride)) return false;

	int CarType = *(int*)Ride;
	if (CarType < 0 || CarType >= CarCount) return false;

	DWORD* Part = (TrimType >= 0) ? Trim_PartFor(CarType, TrimType) : nullptr;

	if (TrimType >= 0 && !Part)
	{
		TrimTraceLine("%s: no part named %s_%s in RIGHT_SIDE_MIRROR\n",
			GetCarTypeName(CarType), GetCarTypeName(CarType), GetCarTypeName(TrimType));
		return false;
	}

	Ride[356 + CARSLOTID_RIGHT_SIDE_MIRROR] = (DWORD)Part;

	if (TrimType >= 0) Trim_ApplyBasePaint(Ride, TrimType);

	return true;
}

// The record for a car the player has just come to own is built from a stock ride, never from the
// car that was on screen in the car lot, so the trim has to be put back before the record is
// packed. Hooked at the CALL inside FECarConfig::Init rather than at FillWithRide itself, which
// keeps every other caller of FillWithRide untouched.
void __fastcall Trim_FillWithRide_NewRecord(DWORD* PresetCarSlot, void* EDX_Unused, DWORD* Ride)
{
	if (Trim_ValidRide(Ride))
	{
		int CarType = *(int*)Ride;
		int Wanted = Trim_Remembered(CarType);

		if (Wanted >= 0 && Trim_OnRide(Ride) != Wanted && Trim_Install(Ride, Wanted))
			TrimTraceLine("new record for %s: trim %s carried over\n",
				GetCarTypeName(CarType), GetCarTypeName(Wanted));
	}

	Trim_PresetCarSlotFillWithRide(PresetCarSlot, Ride);
}

// ---------------------------------------------------------------------------------------------
// Redrawing
//
// Writing a part into the ride does not update what is on screen. The badges are redrawn by
// calling the RefreshHeader of whichever car screen is up. None of those screens has a global
// instance, so the this pointer is caught the first time the game calls one and reused. Every
// screen calls its own on the way in, so by the time a key can be pressed there is always one.
//
// Hooked at the CALL SITES rather than at the function entries, so no prologue has to be
// replayed and the originals stay callable.
// ---------------------------------------------------------------------------------------------

typedef void(__thiscall* RefreshHeaderFn)(DWORD* Screen);

RefreshHeaderFn LastRefreshHeader = nullptr;
DWORD* LastRefreshHeaderThis = nullptr;

RefreshHeaderFn GameCarLotRefreshHeader   = (RefreshHeaderFn)0x4AFE00; // UICareerCarLot
RefreshHeaderFn GameQRSelectRefreshHeader = (RefreshHeaderFn)0x4B2310; // UIQRCarSelect
RefreshHeaderFn GameCribRefreshHeader     = (RefreshHeaderFn)0x4B0140; // UICareerCribCarSelect

void __fastcall TrimCarLotRefreshHeader(DWORD* Screen, void* EDX_Unused)
{
	LastRefreshHeader = GameCarLotRefreshHeader;
	LastRefreshHeaderThis = Screen;
	GameCarLotRefreshHeader(Screen);
}

void __fastcall TrimQRSelectRefreshHeader(DWORD* Screen, void* EDX_Unused)
{
	LastRefreshHeader = GameQRSelectRefreshHeader;
	LastRefreshHeaderThis = Screen;
	GameQRSelectRefreshHeader(Screen);
}

void __fastcall TrimCribRefreshHeader(DWORD* Screen, void* EDX_Unused)
{
	LastRefreshHeader = GameCribRefreshHeader;
	LastRefreshHeaderThis = Screen;
	GameCribRefreshHeader(Screen);
}

void Trim_RedrawHeader()
{
	if (!LastRefreshHeader || !LastRefreshHeaderThis) return;

	LastRefreshHeader(LastRefreshHeaderThis);
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
// Cycling
// ---------------------------------------------------------------------------------------------

bool TrimSwapKeyWasDown = false;

// Where the trim may still be changed. The car lot reports the same 0x20 as customizing, so the
// two cannot be told apart this way, and blocking on menu state alone blocked the one screen
// where it has to work. Left open everywhere until there is a real "the player owns this car"
// test. ASSUMPTION, unverified: menu state is the wrong tool for the My Cars lock.
#define _profileData     0x83A9D0
#define profileMenuState *(DWORD*)(_profileData + 0x156A8)

#define MENU_STATE_CAREER_MENU    0x01
#define MENU_STATE_MAIN_MENU      0x02
#define MENU_STATE_2P_SPLITSCREEN 0x04
#define MENU_STATE_CAR_CUSTOMIZE  0x20

#define MENU_STATES_TRIM_CHANGEABLE (MENU_STATE_CAREER_MENU | MENU_STATE_MAIN_MENU \
	| MENU_STATE_2P_SPLITSCREEN | MENU_STATE_CAR_CUSTOMIZE)

// Which RideInfo the game will actually keep. The trim is written into the one the viewer is
// showing, which is a single global the viewer reuses for every car, so nothing stored in it
// belongs to a particular car for long. Logged next to the other candidates so the next run says
// which object the save reads from, rather than another round of guessing at it.
void Trim_TraceRideCandidates(DWORD* Chosen)
{
	if (!TrimTrace) return;

	DWORD* Viewer0 = CarViewer_GetRideInfo(0);
	DWORD* Viewer1 = CarViewer_GetRideInfo(1);

	TrimTraceLine("  rides: chosen %p, viewer0 %p, viewer1 %p, TopOrFullScreenRide %p, gTheRideInfo %p\n",
		Chosen, Viewer0, Viewer1, (void*)TopOrFullScreenRide, (void*)gTheRideInfo);

	if (Trim_ValidRide(Viewer0)) TrimTraceLine("    viewer0 car type %d\n", *(int*)Viewer0);
	if (Trim_ValidRide(Viewer1)) TrimTraceLine("    viewer1 car type %d\n", *(int*)Viewer1);
}

void Trim_Set(DWORD* Ride, int TrimType)
{
	if (!Trim_ValidRide(Ride)) return;

	int CarType = *(int*)Ride;

	if (!Trim_Install(Ride, TrimType)) return;

	TrimTraceLine("%s: trim -> %s\n", GetCarTypeName(CarType),
		TrimType >= 0 ? GetCarTypeName(TrimType) : "(none)");

	Trim_Remember(CarType, TrimType);

	Trim_TraceRideCandidates(Ride);

	// Physics is read out of the trim's CarTypeInfo the next time it is rebuilt, so ask for that
	// rebuild now instead of waiting for something else to trigger one.
	RideInfo_RebuildPhysicsInfo(Ride, 0, true, true, true);

	// The car on screen is not always the car being kept. In the garage the customize manager owns
	// its own RideInfo and that is the one packed into the record, so the same trim goes on there
	// and the record is rewritten.
	//
	// The manager is a static object, so it is always addressable and its contents are whatever the
	// last customize session left behind. The car type is the guard: a manager still holding some
	// other car is stale as far as this trim is concerned and is left alone, record included.
	DWORD* Owned = Trim_CustomizeManagerRide();

	if (Owned && *(int*)Owned == CarType)
	{
		if (Owned != Ride)
		{
			Trim_Install(Owned, TrimType);
			RideInfo_RebuildPhysicsInfo(Owned, 0, true, true, true);
		}

		Trim_WriteThrough(Owned);
	}

	Trim_RedrawHeader();
}

void Trim_PollKey()
{
	if (!TrimSwapKey) return;

	bool Down = (GetAsyncKeyState(TrimSwapKey) & 0x8000) != 0;

	if (!Down) { TrimSwapKeyWasDown = false; return; }
	if (TrimSwapKeyWasDown) return;

	TrimSwapKeyWasDown = true;

	// The poll sits in CarRenderInfo::Render, which also runs in a race. Changing a trim there
	// would rebuild physics mid-lap and pack a record from a car that is being driven, so the
	// front end is a hard precondition rather than something the menu state happens to imply.
	if (*(int*)_TheGameFlowManager != 3) return; // TheGameFlowManager->mCurrentState, 3 = front end

	DWORD* Ride = CarViewer_GetRideInfo(0);
	if (!Trim_ValidRide(Ride)) return;

	int CarType = *(int*)Ride;
	if (CarType < 0 || CarType >= CarCount) return;

	if (!(profileMenuState & MENU_STATES_TRIM_CHANGEABLE))
	{
		TrimTraceLine("%s: not changeable here, menu state 0x%X\n",
			GetCarTypeName(CarType), (unsigned int)profileMenuState);
		return;
	}

	int Trims[32];
	int Count = Trim_ListFor(CarType, Trims, 32);

	if (!Count)
	{
		TrimTraceLine("%s: no trims declare TrimOf = %s\n", GetCarTypeName(CarType), GetCarTypeName(CarType));
		return;
	}

	int Current = Trim_OnRide(Ride);

	int At = -1;

	for (int i = 0; i < Count; i++)
		if (Trims[i] == Current) { At = i; break; }

	Trim_Set(Ride, (At + 1 < Count) ? Trims[At + 1] : -1);
}

void InitTrimSwap()
{
	// Call sites rather than the functions themselves, so no prologue has to be replayed
	injector::MakeCALL(0x4B00EF, TrimCarLotRefreshHeader, true); // UICareerCarLot::EnterCarLotState
	injector::MakeCALL(0x4ED93C, TrimCarLotRefreshHeader, true);

	injector::MakeCALL(0x4E28E3, TrimQRSelectRefreshHeader, true);
	injector::MakeCALL(0x4EEFA4, TrimQRSelectRefreshHeader, true);
	injector::MakeCALL(0x4FA41E, TrimQRSelectRefreshHeader, true);
	injector::MakeCALL(0x4FC576, TrimQRSelectRefreshHeader, true);

	injector::MakeCALL(0x4B0664, TrimCribRefreshHeader, true); // UICareerCribCarSelect

	// Garage name art, the one SECONDARY_LOGO that does not go through GetCarTypeLogoHash
	injector::MakeCALL(0x4B00DC, GarageMainScreen_SetCarType, true); // GarageMainScreen::SetCarType
	injector::MakeCALL(0x4B065D, GarageMainScreen_SetCarType, true);
	injector::MakeCALL(0x4D91D1, GarageMainScreen_SetCarType, true);
	injector::MakeCALL(0x4E27C9, GarageMainScreen_SetCarType, true);
	injector::MakeCALL(0x4E8780, GarageMainScreen_SetCarType, true);

	// Carry the trim into a record the moment the car becomes the player's
	injector::MakeCALL(0x516C52, Trim_FillWithRide_NewRecord, true); // FECarConfig::Init
}
