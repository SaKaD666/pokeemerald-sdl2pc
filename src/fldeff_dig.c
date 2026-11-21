#include "global.h"
#include "braille_puzzles.h"
#include "field_effect.h"
#include "field_player_avatar.h"
#include "fldeff.h"
#include "item_use.h"
#include "overworld.h"
#include "party_menu.h"
#include "sprite.h"
#include "constants/field_effects.h"

// static functions
static void FieldCallback_Dig(void);
static void StartDigFieldEffect(void);

// text
bool8 SetUpFieldMove_Dig(void)
{
    if (CanUseDigOrEscapeRopeOnCurMap() == TRUE)
    {
        gFieldCallback2 = FieldCallback_PrepareFadeInFromMenu;
        gPostMenuFieldCallback = FieldCallback_Dig;
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

static void FieldCallback_Dig(void)
{
    gFieldEffectArguments[0] = GetCursorSelectionMonId();
    FieldEffectStart(FLDEFF_USE_DIG);
}

bool8 FldEff_UseDig(void)
{
    u8 taskId = CreateFieldMoveTask();

    if (taskId == TASK_NONE)
    {
        FieldEffectActiveListRemove(FLDEFF_USE_DIG);
        RestoreFieldControlsAfterFailedMove();
        return FALSE;
    }

    gTasks[taskId].ptr.funcPtr = StartDigFieldEffect;
    return FALSE;
}

static void StartDigFieldEffect(void)
{
    FieldEffectActiveListRemove(FLDEFF_USE_DIG);
    if (ShouldDoBrailleDigEffect())
    {
        Overworld_ResetStateAfterDigEscRope();
        DoBrailleDigEffect();
    }
    else if (StartEscapeRopeFieldEffect())
    {
        Overworld_ResetStateAfterDigEscRope();
        SetPlayerAvatarTransitionFlags(PLAYER_AVATAR_FLAG_ON_FOOT);
    }
    else
    {
        RestoreFieldControlsAfterFailedMove();
    }
}
