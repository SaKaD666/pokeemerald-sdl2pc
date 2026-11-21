#ifndef GUARD_BATTLE_BG_H
#define GUARD_BATTLE_BG_H

// Vertical distance between the message, action and move panels in the BG0 tilemap.
#define BATTLE_TEXTBOX_PAGE_HEIGHT (20 * 8)

#ifdef PLATFORM_SDL2
#define BATTLE_TERRAIN_MAP_BASE 22
#define BATTLE_TEXTBOX_X_OFFSET ((DISPLAY_WIDTH - 240) / 2)
#define BATTLE_PLAYER_PLATFORM_SHIFT 16
#define BATTLE_OPPONENT_PLATFORM_SHIFT 40
void DrawCaughtMonBattleBackground(void);
#else
#define BATTLE_TERRAIN_MAP_BASE 26
#endif

void BattleInitBgsAndWindows(void);
void InitBattleBgsVideo(void);
void LoadBattleMenuWindowGfx(void);
void DrawMainBattleBackground(void);
void LoadBattleTextboxAndBackground(void);
void InitLinkBattleVsScreen(u8 taskId);
void DrawBattleEntryBackground(void);
bool8 LoadChosenBattleElement(u8 caseId);

#endif // GUARD_BATTLE_BG_H
