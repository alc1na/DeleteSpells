// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include <Psapi.h>

#include "obse64_version.h"
#include "PluginAPI.h"

#include "Actor.h"
#include "BaseProcess.h"
#include "ConfigFile.h"
#include "MagicMenu.h"
#include "PlayerCharacter.h"
#include "SpellItem.h"
#include "Tile.h"

#include "Utils/Hooklib.h"
#include "Utils/Scanner.h"
#include "Utils/Signatures.h"

// Function types
using FnGetMenuByClass			= Tile * (__fastcall*)(int);
using FnTileGetFloat			= float(__fastcall*)(Tile*, int);
using FnMagicMenu_UpdateList	= void(__fastcall*)();
using FnGetMessageMenuResult	= int64_t(__fastcall*)();
using FnInterfaceMessageMenu	= bool(__fastcall*)(const char*, void(__fastcall*)(), int, const char*, ...);
using FnMagicMenu_DoClick		= void(__fastcall*)(MagicMenu*, int, Tile*);

// Function pointers
static FnGetMenuByClass			GetMenuByClass;
static FnTileGetFloat			TileGetFloat;
static FnMagicMenu_UpdateList	MagicMenu_UpdateList;
static FnGetMessageMenuResult	GetMessageMenuresult;
static FnInterfaceMessageMenu	Interface_CreateMessageMenu;
static FnMagicMenu_DoClick		og_MagicMenu_DoClick;

// Variables
static bool protectSpells = ConfigFile::GetBool("bProtectSpells", true);
static bool spellInfoLog = ConfigFile::GetBool("bSpellInfoLog", false);
static const auto& ignoredSpells = ConfigFile::GetBlacklistedSpells();

// Hooks
static void hk_MagicMenu_DoClick(MagicMenu* menu, int aiID, Tile* apTarget) {
	// Skip if confirmation dialog is already open or SHIFT is not held
	if (GetMenuByClass(1016) || !(GetAsyncKeyState(VK_LSHIFT) & 0x8000)) {
		og_MagicMenu_DoClick(menu, aiID, apTarget);
		return;
	}

	// Check AI ID range
	if ((aiID - 13) <= 1 || aiID < 1001) {
		og_MagicMenu_DoClick(menu, aiID, apTarget);
		return;
	}

	// Check tile type (skip if it's known unclickable)
	const int tileType = static_cast<int>(TileGetFloat(apTarget, 4021));
	if (tileType == 16 || tileType == 8) {
		og_MagicMenu_DoClick(menu, aiID, apTarget);
		return;
	}

	// Retrieve index of clicked spell
	const int index = static_cast<int>(TileGetFloat(apTarget, 4027));
	SpellItem* curItem = nullptr;
	auto* list = &menu->xSpellList;
	int curIdx = 0;

	do {
		if (!list) break;

		curItem = list->m_item;
		curIdx++;
		list = list->m_pNext;
	} while (curIdx != index && list);

	// Skip if the spell could not be resolved from the list
	if (!curItem) {
		og_MagicMenu_DoClick(menu, aiID, apTarget);
		return;
	}

	// Log spell information if enabled
	auto& data = curItem->data;
	if (spellInfoLog) {
		printf("[Delete Spells] FormID: 0x%08X | Type: %d | CostOverride: %d | Flags: 0x%02X\n",
			curItem->iFormID,
			data.iSpellType,
			data.iCostOverride,
			data.flags
		);
	}

	// Check if the spell is protected or blacklisted
	if (protectSpells && ignoredSpells.contains(curItem->iFormID)) {
		printf("[Delete Spells] Skipping deletion for blacklisted spell: %08X\n", curItem->iFormID);
		og_MagicMenu_DoClick(menu, aiID, apTarget);
		return;
	}

	// Confirmation dialog
	static SpellItem* selectedItem = nullptr;
	selectedItem = curItem;

	Interface_CreateMessageMenu(
		"LOC_HC_DeleteSpell_Confirm",
		[] {
			if (GetMessageMenuresult() == 1) {
				PlayerCharacter::GetSingleton()->RemoveSpell(selectedItem);
				MagicMenu_UpdateList();
			}
		},
		1,
		"LOC_HC_MenuGamesettings_sYes",
		"LOC_HC_MenuGamesettings_sNo",
		0
	);
}


static bool Init() {
#ifdef DEBUG
	AllocConsole();
	(void)freopen_s(reinterpret_cast<FILE**>(stdout), "CONOUT$", "w", stdout);
#endif
	HookLib::Init();
	Signatures::Init();

	Scanner::Add("8D 81 ? ? ? ? 83 F8 ? 77 ? 0F B7 05", &GetMenuByClass);
	Scanner::Add("4C 8B 41 ? 4D 85 C0 74 ? 0F 1F 80 ? ? ? ? 49 8B 48 ? 49 8D 40 ? ? ? ? 0F B7 41 ? 3B C2 74 ? 7F ? 4D 85 C0 75 ? 0F 57 C0", &TileGetFloat);
	Scanner::Add("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? B9", &MagicMenu_UpdateList);
	Scanner::Add({ "E8 ? ? ? ? 48 83 C4 ? 5F C3 33 D2", 1, 4 }, &Interface_CreateMessageMenu);
	Scanner::Add("40 53 48 83 EC ? B2 ? 33 C9 E8 ? ? ? ? B2", &GetMessageMenuresult);
	Scanner::AddPrologueHook("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 4C 8B F1 4C 89 64 24", hk_MagicMenu_DoClick, &og_MagicMenu_DoClick);

	Scanner::Scan();
	printf("DeleteSpells loaded!\n");

	return true;
}


#ifdef ASI
BOOL WINAPI DllMain(const HINSTANCE hinstDLL, const DWORD fdwReason, LPVOID) {
	if (fdwReason == DLL_PROCESS_ATTACH) {
		return Init();
	}

	return TRUE;
}
#else
extern "C" {
	__declspec(dllexport) OBSEPluginVersionData OBSEPlugin_Version = {
		OBSEPluginVersionData::kVersion, 1,
		"Delete Spells", "xConfused",
		1, // address independent
		0, // not structure independent
		{RUNTIME_VERSION_1_512_105, 0}, // compatible with 1.512.105 and that's it
		0, // works with any version of the script extender. you probably do not need to put anything here
		0, 0, 0 // set these reserved fields to 0
	};

	__declspec(dllexport) bool OBSEPlugin_Load(const OBSEInterface*) {
		return Init();
	}
};
#endif