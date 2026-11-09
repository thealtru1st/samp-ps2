#pragma once
/* For the pad hook in main.c. */
#include "menu.h"

/* Every frame, before anything hides the pad: what the player is really pressing. */
void feat_dialog_pad(const menu_pad_t *pad);
/* True while the game must not see the pad: a dialog, menu or textdraw selection is up. */
int  feat_dialog_modal(void);
