// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include <Psapi.h>
#include <thread>
#include <atomic>
#include <mutex>

#include <Xinput.h>
#pragma comment(lib, "Xinput.lib")

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

// TODO:
// - Refactor code organization
// - InputHandlers.cpp/h for keyboard/gamepad input
// - Move all config keys and defaults to ConfigKeys.h or Config.cpp
// - Extract combo check logic into reusable interface
// - Review GetActiveGamepadState() caching behavior
// - Add graceful fallback or warning if XInput is not present or fails to load
// - Add better user feedback in-game if deletion fails
// - Add better ASI/OBSE path detection and initialization

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

// Config flags
static bool protectSpells = ConfigFile::GetBool("bProtectSpells", true);
static bool translationFile = ConfigFile::GetBool("bUseTranslationFile", true);
static bool spellInfoLog = ConfigFile::GetBool("bSpellInfoLog", false);
static bool gamepadSupport = ConfigFile::GetBool("bGamepadSupport", true);

// Keyboard keys
static int keyboardModifierKey = ConfigFile::GetInt("iKeyboardModifierKey", VK_LSHIFT);

// Gamepad buttons
static int gamepadDeleteButton = ConfigFile::GetInt("iGamepadDeleteButton", 0x1000); // XINPUT_GAMEPAD_A (PSCross, Xbox A)
static int gamepadModifierButton = ConfigFile::GetInt("iGamepadModifierButton", 0x0020); // XINPUT_GAMEPAD_BACK (PSSelect, Xbox Back)

// Blacklisted FormIDs
static const auto& ignoredSpells = ConfigFile::GetBlacklistedSpells();

// Returns the state of the active gamepad, or an error code if none is connected
static DWORD GetActiveGamepadState(XINPUT_STATE& outState) {
	static int activeGamepad = -1;

	// Try the cached gamepad
	if (activeGamepad != -1) {
		DWORD result = XInputGetState(activeGamepad, &outState);
		if (result == ERROR_SUCCESS)
			return ERROR_SUCCESS;
	}

	// Search for a new active gamepad
	for (int i = 0; i < XUSER_MAX_COUNT; ++i) {
		if (XInputGetState(i, &outState) == ERROR_SUCCESS) {
			activeGamepad = i;
			printf("[Delete Spells] Found active gamepad: %d\n", i);
			return ERROR_SUCCESS;
		}
	}

	activeGamepad = -1;
	return ERROR_DEVICE_NOT_CONNECTED;
}

// Check if the gamepad delete combo is currently pressed (modifier + delete)
static bool IsGamepadDeleteComboPressed() {
	if (!gamepadSupport)
		return false;

	XINPUT_STATE state{};
	if (GetActiveGamepadState(state) != ERROR_SUCCESS)
		return false;

	const WORD buttons = state.Gamepad.wButtons;
	return (buttons & gamepadModifierButton) && (buttons & gamepadDeleteButton);
}

// List of all known modifier keys (SHIFT, CTRL, ALT, WIN, etc.)
// TODO: move this to a separate input handler file
static constexpr int kModifierKeys[] = {
	VK_SHIFT, VK_LSHIFT, VK_RSHIFT,
	VK_CONTROL, VK_LCONTROL, VK_RCONTROL,
	VK_MENU, VK_LMENU, VK_RMENU,
	VK_LWIN, VK_RWIN, VK_APPS
};

// Checks if any non-modifier key is currently pressed (excluding mouse buttons)
// TODO: move this to a separate input handler file
static bool IsAnyNonModifierKeyHeld() {
	for (int vk = VK_BACK; vk <= VK_OEM_CLEAR; ++vk) {
		// Skip if this key is in the modifier list
		if (std::ranges::any_of(kModifierKeys, [vk](int modKey) { return vk == modKey; }))
			continue;

		if (GetAsyncKeyState(vk) & 0x8000) {
			return true;
		}
	}

	return false;
}

// Checks if the configured modifier key is currently held
// TODO: move this to a separate input handler file
static bool IsModifierKeyHeld() {
	if (GetAsyncKeyState(keyboardModifierKey) & 0x8000) {
		return true;
	}

	return false;
}

// Checks whether only the modifier key is currently held (no other keys except mouse).
// We reverted to using mouse click as the trigger (same as the original design), with
// only the modifier key being customizable. Fully custom hotkey combinations caused too
// many conflicts with other mods (e.g. Spell Hotkeys) that intercept or override input.
// Since mouse clicks are processed on release, the hook is triggered after the click —
// we don't need to detect the mouse itself, only confirm no unrelated keys were held.
// This safeguards against false triggers (e.g. Shift+3 binding a spell and deleting).
// In the future, this system should ideally be replaced with a proper UE5 input hook.
// TODO: move this to a separate input handler file
static bool IsKeyboardDeleteComboPressed() {
	return IsModifierKeyHeld() && !IsAnyNonModifierKeyHeld();
}

// Hooks
static void hk_MagicMenu_DoClick(MagicMenu* menu, int aiID, Tile* apTarget) {
	// Skip if menu not visible or if confirmation dialog is open
	if (!menu->IsVisible || GetMenuByClass(1016)) {
		og_MagicMenu_DoClick(menu, aiID, apTarget);
		return;
	}

	// Check if the delete combo is pressed from any input method
	const bool keyboardCombo = IsKeyboardDeleteComboPressed();
	const bool gamepadCombo = IsGamepadDeleteComboPressed();

	if (!(keyboardCombo || gamepadCombo)) {
		og_MagicMenu_DoClick(menu, aiID, apTarget);
		return;
	}

	printf("[Delete Spells] Deletion combo confirmed (%s)\n", keyboardCombo ? "Keyboard" : "Gamepad");

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
	if (spellInfoLog) {
		auto& data = curItem->data;
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
		translationFile ? "LOC_HC_DeleteSpell_Confirm" : "Are you sure you want to delete this spell?",
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

	printf("[Delete Spells] Initializing pointers\n");
	Scanner::Add("8D 81 ? ? ? ? 83 F8 ? 77 ? 0F B7 05", &GetMenuByClass);
	Scanner::Add("4C 8B 41 ? 4D 85 C0 74 ? 0F 1F 80 ? ? ? ? 49 8B 48 ? 49 8D 40 ? ? ? ? 0F B7 41 ? 3B C2 74 ? 7F ? 4D 85 C0 75 ? 0F 57 C0", &TileGetFloat);
	Scanner::Add("48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? B9", &MagicMenu_UpdateList);
	Scanner::Add({ "E8 ? ? ? ? 48 83 C4 ? 5F C3 33 D2", 1, 4 }, &Interface_CreateMessageMenu);
	Scanner::Add("40 53 48 83 EC ? B2 ? 33 C9 E8 ? ? ? ? B2", &GetMessageMenuresult);
	Scanner::AddPrologueHook("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 4C 8B F1 4C 89 64 24", hk_MagicMenu_DoClick, &og_MagicMenu_DoClick);

	printf("[Delete Spells] Scanning pointers\n");
	Scanner::Scan();

	printf("[Delete Spells] DeleteSpells loaded!\n");

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