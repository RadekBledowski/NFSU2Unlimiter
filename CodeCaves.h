#pragma once

// CarLotUnlockData is 256 bytes and the index arrives in ecx straight from the game, with nothing
// checking it. A crash in this function at 0x513CCC came in with esi = 0xE5F5E5FB, so the index
// does go wild in the field. Out of range now skips instead of writing over whatever follows.
void __declspec(naked) CarLotFixCodeCaveWrite()
{
	_asm
	{
		cmp ecx, 256
		jae Skip

		mov byte ptr ds : [CarLotUnlockData + ecx] , 0

		Skip :
		push 0x513CDA
		retn
	}
}

void __declspec(naked) CarLotFixCodeCaveRead()
{
	_asm
	{
		cmp esi, 256
		jae OutOfRange

		mov al, [CarLotUnlockData + esi]
		jmp Done

		OutOfRange :
		xor al, al          // a bogus index reads as locked rather than as garbage

		Done :
		test al, al
		push 0x513CF6
		retn
	}
}

// Called from DoUnlimiterStuffCodeCave2, which is naked and so cannot hold locals.
void FixNetQuantizers()
{
	// Network quantizers.
	//
	// A quantizer is four fields, written by sub_5820C0 when it is registered:
	//
	//   +0x12C  bit count, the smallest n where (1 << n) >= max - min + 1
	//   +0x130  min
	//   +0x134  max
	//   +0x138  max + 1
	//
	// Upstream raised max and max+1 and left the bit count alone. Vanilla registers
	// QuantPartIndex with max 0x3E80, which needs 14 bits, so with more parts than that the
	// packer still wrote 14 bits and every index above 16383 came out of the wire wrapped. That
	// is why colours and vinyls synced in multiplayer and body parts did not: those use
	// QuantInt2Bit and QuantInt4Bit, which nobody moved.
	int PartBits = 1;
	while ((1 << PartBits) < CarPartCount + 1) PartBits++;
	injector::WriteMemory<int>(0x89D418, PartBits, true);         // QuantPartIndex bit count
	injector::WriteMemory<int>(0x89D420, CarPartCount, true);     // max
	injector::WriteMemory<int>(0x89D424, CarPartCount + 1, true); // max + 1
	// The registration itself, sub_5EFAC0, pushes the vanilla 0x3E80 as an imm32. If it runs
	// after this it would put all four fields back, so the immediate goes too.
	injector::WriteMemory<int>(0x5EFC43, CarPartCount, true);
	injector::WriteMemory<int>(0x5F04A4, CarPartCount, true); // sub_5F0320
	injector::WriteMemory<int>(0x5F0675, CarPartCount + 1, true); // sub_5F04D0
	// Same treatment for the car type quantizer, registered with max 0x2E. CarCountByte comes
	// from the other code cave, so skip if that has not run yet rather than write a bit count of
	// one. Note the registration there is push imm8, so its own ceiling is 127, not 255.
	if (CarCountByte > 0)
	{
		int TypeBits = 1;
		while ((1 << TypeBits) < CarCountByte + 1) TypeBits++;

		injector::WriteMemory<int>(0x89D2D4, TypeBits, true); // QuantCarType bit count
	}
}

void FillCarPickerArrays()
{
	bool UnlockRegionalCars = 0;

	int RandomCarCount = injector::ReadMemory<int>(0x4FEB9D, true);
	int RandomSUVCount = injector::ReadMemory<int>(0x4FEBDB, true);
	int InitiallyUnlockedCarCount = 8;

	// Check if Extra Options is present. If so, read UnlockRegionalCars value from its config file.
	if (GetModuleHandleA("NFSU2ExtraOptions.asi"))
	{
		auto ExtraOptionsSettings = CurrentWorkingDirectory / "NFSU2ExtraOptionsSettings.ini";
		mINI::INIFile NFSU2ExtraOptionsSettingsINIFile(ExtraOptionsSettings.string());
		mINI::INIStructure Settings;
		NFSU2ExtraOptionsSettingsINIFile.read(Settings);

		UnlockRegionalCars = mINI_ReadInteger(Settings, "Gameplay", "UnlockRegionalCars", 1) != 0;
	}

	// Read current arrays
	for (int i = 0; i < RandomCarCount; i++)
	{
		RandomlyChooseableCarConfigsNorthAmerica[i] = injector::ReadMemory<BYTE>(0x7F6DA4 + i, true);
		RandomlyChooseableCarConfigsRestOfWorld[i] = injector::ReadMemory<BYTE>(0x7F6DC0 + i, true);
	}

	for (int i = 0; i < RandomSUVCount; i++) RandomlyChooseableSUVs[i] = injector::ReadMemory<BYTE>(0x7F6DDC + i, true);

	// Add regional cars
	if (UnlockRegionalCars)
	{
		for (int i = 0; i < 2; i++)
		{
			RandomlyChooseableCarConfigsNorthAmerica[RandomCarCount] = EUExclusiveCars[i];
			RandomlyChooseableCarConfigsRestOfWorld[RandomCarCount++] = USExclusiveCars[i];
		}
	}

	// Add add-on cars
	for (int i = 46; i < CarCount; i++)
	{
		if (CanCarBeDrivenByAI(i) && (IsRacer(i)))
		{
			if (IsSUV(i)) RandomlyChooseableSUVs[RandomSUVCount++] = i;
			else
			{
				RandomlyChooseableCarConfigsNorthAmerica[RandomCarCount] = i;
				RandomlyChooseableCarConfigsRestOfWorld[RandomCarCount++] = i;
			}
		}
	}

	// Introduce new arrays to the game
	injector::WriteMemory(0x4FEBA2, RandomlyChooseableCarConfigsNorthAmerica, true);
	injector::WriteMemory(0x4FEBA9, RandomlyChooseableCarConfigsRestOfWorld, true);
	injector::WriteMemory(0x4FEBE0, RandomlyChooseableSUVs, true);

	injector::WriteMemory<int>(0x4FEB9D, RandomCarCount, true);
	injector::WriteMemory<int>(0x4FEBDB, RandomSUVCount, true);


	// Initially unlocked cars
	for (int i = 0; i < InitiallyUnlockedCarCount; i++) // Read current arrays
	{
		UnlockedAtBootQuickRaceNorthAmerica[i] = injector::ReadMemory<int>(0x7F7C08 + 4 * i, true);
		UnlockedAtBootQuickRaceRestOfWorld[i] = injector::ReadMemory<int>(0x7F7C28 + 4 * i, true);
	}

	// Add regional cars
	if (UnlockRegionalCars)
	{
		for (int i = 0; i < 1; i++) // Only unlock CIVIC or CORSA
		{
			UnlockedAtBootQuickRaceNorthAmerica[InitiallyUnlockedCarCount] = EUExclusiveCars[i];
			UnlockedAtBootQuickRaceRestOfWorld[InitiallyUnlockedCarCount++] = USExclusiveCars[i];
		}
	}

	// Add new cars there
	for (int i = 46; i < CarCount; i++)
	{
		if (IsInitiallyUnlocked(i) && (IsRacer(i)))
		{
			UnlockedAtBootQuickRaceNorthAmerica[InitiallyUnlockedCarCount] = i;
			UnlockedAtBootQuickRaceRestOfWorld[InitiallyUnlockedCarCount++] = i;
			CarLotUnlockData[i] = 1;
		}
	}

	// Introduce new arrays to the game
	injector::WriteMemory(0x529D22, UnlockedAtBootQuickRaceNorthAmerica, true); // Start
	injector::WriteMemory(0x529D2E, UnlockedAtBootQuickRaceNorthAmerica + 4 * InitiallyUnlockedCarCount, true); // End
	injector::WriteMemory(0x529D3B, UnlockedAtBootQuickRaceRestOfWorld, true); // Start
	injector::WriteMemory(0x529D48, UnlockedAtBootQuickRaceRestOfWorld + 4 * InitiallyUnlockedCarCount, true); // End

}

float* TimingStatsKludgeFactor060;
float* TimingStatsKludgeFactor0100;
void FixComputeMiscStats()
{
	TimingStatsKludgeFactor060 = new float[CarCount];
	TimingStatsKludgeFactor0100 = new float[CarCount];

	// Copy original values
	for (int i = 0; i < 46; i++)
	{
		TimingStatsKludgeFactor060[i] = CarConfigs[i].Stats.TimingKludgeFactor060 != 0.0f 
			? CarConfigs[i].Stats.TimingKludgeFactor060 
			: *((float*)0x007FC120 + i);
		TimingStatsKludgeFactor0100[i] = CarConfigs[i].Stats.TimingKludgeFactor0100 != 0.0f 
			? CarConfigs[i].Stats.TimingKludgeFactor0100
			: *((float*)0x007FC1D8 + i);
	}

	// Fill the rest of the cars
	for (int i = 46, j = 0; i < CarCount; i++, j++)
	{
		if (j > 45)
		{
			j = 0;
		}

		TimingStatsKludgeFactor060[i] = CarConfigs[i].Stats.TimingKludgeFactor060 != 0.0f
			? CarConfigs[i].Stats.TimingKludgeFactor060
			: *((float*)0x007FC120 + j);
		TimingStatsKludgeFactor0100[i] = CarConfigs[i].Stats.TimingKludgeFactor0100 != 0.0f
			? CarConfigs[i].Stats.TimingKludgeFactor0100
			: *((float*)0x007FC1D8 + j);
	}

	injector::WriteMemory(0x005B089F, TimingStatsKludgeFactor060, true);
	injector::WriteMemory(0x005B08D6, TimingStatsKludgeFactor0100, true);
}

// 0x636BF7
void __declspec(naked) DoUnlimiterStuffCodeCave()
{
	// Get count
	_asm
	{
		mov dword ptr ds : [_CarTypeInfoArray] , eax
		sub eax, 0x0C
		mov eax, [eax]
		mov CarArraySize, eax
		mov eax, dword ptr ds : [_CarTypeInfoArray] 
		pushad
	}

	CarArraySize -= 8;
	CarCount = CarArraySize / SingleCarTypeInfoBlockSize;

	// Do required stuff
	//CountRandomEngageStrings();

	// Replacement model if model not found in array
	if (ReplacementCar > CarCount) ReplacementCar = 1;

	// Car Type Unlimiter
	injector::WriteMemory<int>(0x41AB83, CarArraySize, true); // StreamingTrafficCarManager::Init
	injector::WriteMemory<int>(0x5165BC, CarArraySize, true); // FEPlayerCarDB::DefaultStockCars
	injector::WriteMemory<int>(0x5207B3, CarArraySize, true); // DebugCarCustomizeScreen::BuildOptionsLists
	injector::WriteMemory<int>(0x609348, CarArraySize, true); // sub_6091D0
	injector::WriteMemory<int>(0x6099B1, CarArraySize, true); // sub_6097D0
	injector::WriteMemory<int>(0x636C24, CarArraySize, true); // LoaderCarInfo

	// These are byte wide fields in the game's own instructions, so more than 255 car types cannot
	// be expressed here. Clamping is not a fix for that, but it degrades to "the first 255" instead
	// of truncating, where 256 would write 0 and make a loop bound disappear entirely.
	CarCountByte = (CarCount > 255) ? 255 : (BYTE)CarCount; // global: a naked function cannot hold an initialised local

	injector::WriteMemory<BYTE>(0x5596CB, CarCountByte, true); // IceSelectionScreen::Setup
	injector::WriteMemory<BYTE>(0x5EFC5A, CarCountByte, true); // sub_5EFAC0
	injector::WriteMemory<BYTE>(0x89D2DC, CarCountByte, true); // QuantCarType (gets set before unlimiter so we need to overwrite it here)
	injector::WriteMemory<BYTE>(0x89D2E0, (BYTE)(CarCountByte + 1), true); // QuantCarType
	injector::WriteMemory<BYTE>(0x610150, CarCountByte, true); // GetCarTypeInfoFromHash
	injector::WriteMemory<BYTE>(0x61C671, CarCountByte, true); // CarLoader::LoadAllPartsAnims
	injector::WriteMemory<BYTE>(0x6372B4, CarCountByte, true); // RideInfo::FillWithPreset
	//injector::WriteMemory<BYTE>(0x4EAE48, CarCount, true); // GarageMainScreen::GarageMainScreen
	injector::WriteMemory<BYTE>(0x513D1D, CarCountByte, true); // PlayerCareerState::BuildUnlockedCareerCarList -> UICareerCarLot::BuildCarList

	// Make them available as opponents
	LoadCarConfigs();
	FillCarPickerArrays();

	// load configs into UnlimiterData structs
	//LoadFNGFixes();
	LoadPaintGroups();
	LoadRimBrands();
	LoadVinylGroups();
	LoadStarGazer();
	LoadPartLinks();
	LoadPresetCarOverrides();
	LoadCameraInfo();

	// Fix misc stats
	FixComputeMiscStats();

	// Continue
	_asm
	{
		popad
		push 0x636BFC
		retn
	}
}

// 0x636D6C
void __declspec(naked) DoUnlimiterStuffCodeCave2()
{
	// Get count
	// Get count
	_asm
	{
		mov dword ptr ds : [_CarPartPartsTable] , edx
		sub edx, 4
		mov edx, [edx]
		mov CarPartPartsTableSize, edx
		mov edx, dword ptr ds : [_CarPartPartsTable]
		pushad
	}

	CarPartCount = CarPartPartsTableSize / SingleCarPartSize;

	FixNetQuantizers();

	// Continue
	_asm popad;
	_asm push 0x636D72;
	_asm retn;
}

void __declspec(naked) PerformanceConfigFixCodeCave()
{
	_asm mov eax, 1;
	_asm pushad;

	FillUpPerformanceConfig();

	_asm popad;
	_asm push 0x5994C0;
	_asm retn;
}

// 0x61B67C
int IsUG1_Hash(DWORD CarTypeNameHash)
{
	return IsUG1(GetCarTypeIDFromHash(CarTypeNameHash));
}

// 0x61B686
int IsUG2_Hash(DWORD CarTypeNameHash)
{
	return IsUG2(GetCarTypeIDFromHash(CarTypeNameHash));
}

// 0x61B68E
int IsSUV_Hash(DWORD CarTypeNameHash)
{
	return IsSUV(GetCarTypeIDFromHash(CarTypeNameHash));
}

void __declspec(naked) IsSUV_UnInlineCodeCave()
{
	_asm
	{
		mov dword ptr ds : [esp + 0x28] , eax // IsUG2 result

		push ebp
		call IsSUV_Hash
		mov dword ptr ds : [esp + 0x20] , eax
		add esp, 4
		push 0x61B6BA
		retn
	}
}