// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

#include "obse64_version.h"
#include "Psapi.h"
#include "PluginAPI.h"

#include "Actor.h"
#include "PlayerCharacter.h"
#include "BaseProcess.h"
#include "Utils/Hooklib.h"
#include "Utils/Scanner.h"
#include "Utils/Signatures.h"
#include "MagicMenu.h"
#include "Tile.h"
#include "SpellItem.h"

Pattern pat_GetMenuByClass = "8D 81 ? ? ? ? 83 F8 ? 77 ? 0F B7 05";
Tile* (__fastcall*GetMenuByClass)(int);

Pattern pat_TileGetFloat = "4C 8B 41 ? 4D 85 C0 74 ? 0F 1F 80 ? ? ? ? 49 8B 48 ? 49 8D 40 ? ? ? ? 0F B7 41 ? 3B C2 74 ? 7F ? 4D 85 C0 75 ? 0F 57 C0";
float (__fastcall*TileGetFloat)(Tile*, int);

Pattern pat_MagicMenu_UpdateList =
	"48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? B9";
void (__fastcall*MagicMenu_UpdateList)();

Pattern pat_Interface_CreateMessageMenu = {"E8 ? ? ? ? 48 83 C4 ? 5F C3 33 D2", 1, 4};
bool (__fastcall*Interface_CreateMessageMenu)(const char*, void (__fastcall*resultFn)(), int, const char*, ...);

Pattern pat_Interface_GetMessageMenuresult = "40 53 48 83 EC ? B2 ? 33 C9 E8 ? ? ? ? B2";
int64_t (__fastcall*GetMessageMenuresult)();

Pattern pat_MagicMenu_DoClick = "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 56 41 57 48 83 EC ? 4C 8B F1 4C 89 64 24";
void (__fastcall*orig_MagicMenu_DoClick)(MagicMenu*, int, Tile*);
void hk_MagicMenu_DoClick(MagicMenu* menu, int aiID, Tile* apTarget) {
	if (GetMenuByClass(1016)) {
		orig_MagicMenu_DoClick(menu, aiID, apTarget);
		return;
	}

	if (!(GetAsyncKeyState(VK_LSHIFT) & 0x8000)) {
		orig_MagicMenu_DoClick(menu, aiID, apTarget);
		return;
	}

	if ((aiID - 13) > 1 && aiID >= 1001) {
		const auto val = static_cast<int>(TileGetFloat(apTarget, 4021));
		if (val != 16 && val != 8) {
			const auto index = static_cast<int>(TileGetFloat(apTarget, 4027));
			auto list = &menu->xSpellList;
			SpellItem* curItem;
			int curIdx = 0;
			do {
				curItem = list->m_item;
				curIdx++;
				list = list->m_pNext;
			}
			while (curIdx != index && list);

			if (curItem) {
				static SpellItem* selectedItem;
				selectedItem = curItem;

				Interface_CreateMessageMenu("Are you sure you want to delete this spell?", [] {
					const auto res = GetMessageMenuresult();
					if (res == 1) {
						PlayerCharacter::GetSingleton()->RemoveSpell(selectedItem);
						MagicMenu_UpdateList();
					}
				}, 1, "LOC_HC_MenuGamesettings_sYes", "LOC_HC_MenuGamesettings_sNo", 0);
				return;
			}
		}
	}

	orig_MagicMenu_DoClick(menu, aiID, apTarget);
}

bool Init() {
#ifdef DEBUG
	AllocConsole();
	(void)freopen_s(reinterpret_cast<FILE**>(stdout), "CONOUT$", "w", stdout);
#endif

	HookLib::Init();
	Signatures::Init();

	Scanner::Add(pat_GetMenuByClass, &GetMenuByClass);
	Scanner::Add(pat_TileGetFloat, &TileGetFloat);
	Scanner::Add(pat_MagicMenu_UpdateList, &MagicMenu_UpdateList);
	Scanner::Add(pat_Interface_CreateMessageMenu, &Interface_CreateMessageMenu);
	Scanner::Add(pat_Interface_GetMessageMenuresult, &GetMessageMenuresult);
	Scanner::AddPrologueHook(pat_MagicMenu_DoClick, hk_MagicMenu_DoClick, &orig_MagicMenu_DoClick);

	Scanner::Scan();
	printf("DeleteSpells loaded!");

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
		{RUNTIME_VERSION_0_411_140, 0}, // compatible with 0.411.140 and that's it
		0, // works with any version of the script extender. you probably do not need to put anything here
		0, 0, 0 // set these reserved fields to 0
	};

	__declspec(dllexport) bool OBSEPlugin_Load(const OBSEInterface*) {
		return Init();
	}
};
#endif