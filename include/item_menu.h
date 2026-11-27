#ifndef GUARD_ITEM_MENU_H
#define GUARD_ITEM_MENU_H

#include "item.h"
#include "main.h"
#include "menu_helpers.h"

#ifdef PLATFORM_SDL2
#define BAG_TILEMAP_SIZE            (2 * BG_SCREEN_SIZE)
#define BAG_MAX_ITEMS_SHOWN         9
#define BAG_LIST_LEFT               24
#define BAG_LIST_TOP                2
#define BAG_LIST_WIDTH              15
#define BAG_LIST_HEIGHT             (BAG_MAX_ITEMS_SHOWN * 2)
#define BAG_DESC_TOP                15
#define BAG_DESC_WIDTH              22
#define BAG_POCKET_BANNER_OFFSET_X  5
#define BAG_SPRITE_X                98
#define BAG_SPRITE_Y                68
#define BAG_PYRAMID_SPRITE_X        92
#define BAG_PYRAMID_SPRITE_Y        64
#define BAG_ICON_X                  24
#define BAG_ICON_Y                  104
#define BAG_SCROLL_ARROW_X          (BAG_LIST_LEFT * 8 + BAG_LIST_WIDTH * 8 / 2)
#define BAG_SCROLL_ARROW_TOP_Y      12
#define BAG_SCROLL_ARROW_BOTTOM_Y   (16 + BAG_LIST_HEIGHT * 8 + 4)
#define BAG_SWAP_LINE_X             ((BAG_LIST_LEFT + 1) * 8)
#define BAG_MSG_TOP                 17
#define BAG_MSG_WIDTH               36
#define BAG_POPUP_OFFSET_X          10
#define BAG_POPUP_OFFSET_Y          2
void InitBagWideTilemap(void *tilemap, bool8 isPyramidBag);
#else
#define BAG_TILEMAP_SIZE            BG_SCREEN_SIZE
#define BAG_MAX_ITEMS_SHOWN         8
#define BAG_LIST_LEFT               14
#define BAG_LIST_TOP                2
#define BAG_LIST_WIDTH              15
#define BAG_LIST_HEIGHT             (BAG_MAX_ITEMS_SHOWN * 2)
#define BAG_DESC_TOP                13
#define BAG_DESC_WIDTH              14
#define BAG_POCKET_BANNER_OFFSET_X  0
#define BAG_SPRITE_X                68
#define BAG_SPRITE_Y                66
#define BAG_PYRAMID_SPRITE_X        68
#define BAG_PYRAMID_SPRITE_Y        56
#define BAG_ICON_X                  24
#define BAG_ICON_Y                  88
#define BAG_SCROLL_ARROW_X          172
#define BAG_SCROLL_ARROW_TOP_Y      12
#define BAG_SCROLL_ARROW_BOTTOM_Y   148
#define BAG_SWAP_LINE_X             120
#define BAG_MSG_TOP                 15
#define BAG_MSG_WIDTH               27
#define BAG_POPUP_OFFSET_X          0
#define BAG_POPUP_OFFSET_Y          0
#endif

void InitBagBgs(void *tilemap);
void FreeBagWindowBuffers(void);

enum {
    ITEMMENULOCATION_FIELD,
    ITEMMENULOCATION_BATTLE,
    ITEMMENULOCATION_PARTY,
    ITEMMENULOCATION_SHOP,
    ITEMMENULOCATION_BERRY_TREE,
    ITEMMENULOCATION_BERRY_BLENDER_CRUSH,
    ITEMMENULOCATION_ITEMPC,
    ITEMMENULOCATION_FAVOR_LADY,
    ITEMMENULOCATION_QUIZ_LADY,
    ITEMMENULOCATION_APPRENTICE,
    ITEMMENULOCATION_WALLY,
    ITEMMENULOCATION_PCBOX,
    ITEMMENULOCATION_LAST,
};

// Window IDs for the item menu
enum {
    ITEMWIN_1x1,
    ITEMWIN_1x2,
    ITEMWIN_2x2,
    ITEMWIN_2x3,
    ITEMWIN_MESSAGE,
    ITEMWIN_YESNO_LOW,
    ITEMWIN_YESNO_HIGH,
    ITEMWIN_QUANTITY,
    ITEMWIN_QUANTITY_WIDE,
    ITEMWIN_MONEY,
    ITEMWIN_COUNT
};

#define ITEMMENU_SWAP_LINE_LENGTH 8  // Swap line is 8 sprites long
enum {
    ITEMMENUSPRITE_BAG,
    ITEMMENUSPRITE_BALL,
    ITEMMENUSPRITE_ITEM,
    ITEMMENUSPRITE_ITEM_ALT, // Need two when selecting new item
    ITEMMENUSPRITE_SWAP_LINE,
    ITEMMENUSPRITE_COUNT = ITEMMENUSPRITE_SWAP_LINE + ITEMMENU_SWAP_LINE_LENGTH,
};

struct BagPosition
{
    MainCallback exitCallback;
    u8 location;
    u8 pocket;
    u16 pocketSwitchArrowPos;
    u16 cursorPosition[POCKETS_COUNT];
    u16 scrollPosition[POCKETS_COUNT];
};

extern struct BagPosition gBagPosition;

struct BagMenu
{
    MainCallback newScreenCallback;
    u8 ALIGNED(4) tilemapBuffer[BAG_TILEMAP_SIZE];
    u8 spriteIds[ITEMMENUSPRITE_COUNT];
    u8 windowIds[ITEMWIN_COUNT];
    u8 toSwapPos;
    u8 pocketSwitchDisabled:4;
    u8 itemIconSlot:2;
    u8 inhibitItemDescriptionPrint:1;
    u8 hideCloseBagText:1;
    u8 unused1[2];
    u8 pocketScrollArrowsTask;
    u8 pocketSwitchArrowsTask;
    const u8 *contextMenuItemsPtr;
    u8 contextMenuItemsBuffer[4];
    u8 contextMenuNumItems;
    u8 numItemStacks[POCKETS_COUNT];
    u8 numShownItems[POCKETS_COUNT];
    s16 graphicsLoadState;
    u8 unused2[14];
    u8 ALIGNED(4) pocketNameBuffer[32][32];
    u8 unused3[4];
};

extern struct BagMenu *gBagMenu;
extern u16 gSpecialVar_ItemId;

void CB2_GoToItemDepositMenu(void);
void FavorLadyOpenBagMenu(void);
void QuizLadyOpenBagMenu(void);
void ApprenticeOpenBagMenu(void);
void CB2_BagMenuFromBattle(void);
void UpdatePocketListPosition(u8 pocketId);
void CB2_ReturnToBagMenuPocket(void);
void CB2_BagMenuFromStartMenu(void);
u8 GetItemListPosition(u8 pocketId);
bool8 UseRegisteredKeyItemOnField(void);
void CB2_GoToSellMenu(void);
void GoToBagMenu(u8 location, u8 pocket, MainCallback exitCallback);
void DoWallyTutorialBagMenu(void);
void ResetBagScrollPositions(void);
void ChooseBerryForMachine(MainCallback exitCallback);
void CB2_ChooseBerry(void);
void Task_FadeAndCloseBagMenu(u8 taskId);
void BagMenu_YesNo(u8 taskId, u8 windowType, const struct YesNoFuncTable *funcTable);
void UpdatePocketItemList(u8 pocketId);
void DisplayItemMessage(u8 taskId, u8 fontId, const u8 *str, TaskFunc callback);
void DisplayItemMessageOnField(u8 taskId, const u8 *string, TaskFunc callback);
void CloseItemMessage(u8 taskId);

#endif //GUARD_ITEM_MENU_H
