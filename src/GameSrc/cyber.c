/*

Copyright (C) 2015-2018 Night Dive Studios, LLC.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include <string.h>

#include "player.h"
#include "objsim.h"
#include "objects.h"
#include "map.h"
#include "faketime.h"
#include "mfdext.h"
#include "fullscrn.h"
#include "cyber.h"
#include "hud.h"
#include "invent.h"
#include "invpages.h"
#include "input.h"
#include "mainloop.h"
#include "hkeyfunc.h"
#include "rendfx.h"
#include "gr2ss.h"
#include "drugs.h"
#include "init.h"
#include "musicai.h"
#include "saveload.h"
#include "wares.h"

#define FIRST_CSPACE_LEVEL 14
#define MIN_CSPACE_EXIT_HP 10
#define MAX_FATIGUE 10000

#define MAX_SHODAN_FAILURES 10
#define GAME_OVER_HACK 0x6

#define min(a,b) a > b ? b : a

extern uchar *shodan_bitmask;

ObjID shodan_avatar_id = OBJ_NULL;
uint32_t time_until_shodan_avatar = 0;

ObjID cspace_decoy_obj = OBJ_NULL;
ObjLoc recall_objloc;

ulong cspace_effect_times[NUM_CS_EFFECTS] = {0, 0, 0};
ulong cspace_effect_durations[NUM_CS_EFFECTS] = {CIT_CYCLE * 30, CIT_CYCLE * 15, CIT_CYCLE};
void (*cspace_effect_turnoff[])(uchar visible, uchar real) = {turbo_turnoff, decoy_turnoff, NULL};

uchar cyber_nodie = FALSE;

errtype check_cspace_death() {
    if (global_fullmap->cyber) {
        if (player_struct.cspace_hp == 1)
            hud_set(HUD_CYBERDANGER);
        else if (player_struct.cspace_hp == 0) {
            // If we're in endgame mode, we lose.
            if (shodan_bitmask != NULL) {
                extern char thresh_fail;
                if (!cyber_nodie) {
                    cyber_nodie = TRUE;
                    mai_player_death();
                    time_until_shodan_avatar = player_struct.game_time + (CIT_CYCLE * 8);
                }
                if (thresh_fail <= MAX_SHODAN_FAILURES) {
                    memset(shodan_bitmask, 0xFF, SHODAN_BITMASK_SIZE / 8);
                    thresh_fail = MAX_SHODAN_FAILURES + 1;
                }
            } else if (!cyber_nodie) {
                // Delete the shodan_object, if there is one.
                if (shodan_avatar_id != OBJ_NULL) {
                    obj_destroy(shodan_avatar_id);
                    shodan_avatar_id = OBJ_NULL;
                }

                // boot player out of cspace
                player_struct.cspace_time_base =
                    lg_max(CSPACE_MIN_TIME, player_struct.cspace_time_base - CSPACE_DEATH_PENALTY);
                go_to_different_level(player_struct.realspace_level);
                obj_move_to(PLAYER_OBJ, &player_struct.realspace_loc, TRUE);
                reset_input_system();

                // make him tired & hurt
                player_struct.hit_points = min(player_struct.hit_points / 2, MIN_CSPACE_EXIT_HP);
                player_struct.fatigue = MAX_FATIGUE;
            }
        }
    }
    return (OK);
}

MFD_Status status_back[MFD_NUM_REAL_SLOTS];
int old_loop;

errtype enter_cyberspace_stuff(char dest_lev) {
    int i;
    cyber_nodie = FALSE;
    old_loop = _current_loop;

    // Store away our realspace info
    player_struct.realspace_loc = objs[PLAYER_OBJ].loc;
    player_struct.realspace_level = player_struct.level;
    player_struct.cspace_hp = PLAYER_MAX_HP;

    if (input_cursor_mode == INPUT_OBJECT_CURSOR) {
        player_struct.save_obj_cursor = object_on_cursor;
        pop_cursor_object();
    } else
        player_struct.save_obj_cursor = OBJ_NULL;

        // Set timer for Avatar O'SHODAN
//   Warning(("player_struct.csp_time_base = %d\n",player_struct.cspace_time_base));
#ifdef STUPID_HACK
    player_struct.cspace_time_base = CIT_CYCLE * 3;
#else
    if (player_struct.cspace_time_base > CSPACE_MAX_TIME)
        player_struct.cspace_time_base = CSPACE_MAX_TIME;
#endif
    time_until_shodan_avatar = player_struct.game_time + player_struct.cspace_time_base;

    // Clear effect timers
    for (i = 0; i < NUM_CS_EFFECTS; i++)
        cspace_effect_times[i] = 0;

    // MFD hacks

    for (i = 0; i < NUM_MFDS; i++)
        save_mfd_slot(i);

    for (i = 0; i < MFD_NUM_REAL_SLOTS; i++)
        status_back[i] = player_struct.mfd_slot_status[i];

    // Mark some MFD panels as being unavailable.
    {
        int wipingIndex[4] = {MFD_WEAPON_SLOT, MFD_ITEM_SLOT,
                              MFD_MAP_SLOT, MFD_TARGET_SLOT};
        for(i = 0; i <= sizeof(wipingIndex)/sizeof(wipingIndex[0]); i++)
            player_struct.mfd_slot_status[wipingIndex[i]] = MFD_UNAVAIL;
    }

    inventory_page = INV_SOFTWARE_PAGE;
    set_inventory_mfd(MFD_INV_SOFT_COMBAT, player_struct.actives[ACTIVE_COMBAT_SOFT], TRUE);
    player_struct.current_active = ACTIVE_COMBAT_SOFT;

    mfd_notify_func(MFD_EMPTY_FUNC, MFD_INFO_SLOT, TRUE, MFD_ACTIVE, TRUE);
    mfd_change_slot(MFD_LEFT, MFD_INFO_SLOT);
    mfd_change_slot(MFD_RIGHT, MFD_INFO_SLOT);

#ifdef STEREO_SUPPORT
    if (convert_use_mode == 5)
        full_visible = FULL_R_MFD_MASK | FULL_INVENT_MASK;
    else
#endif
        full_visible |= FULL_R_MFD_MASK | FULL_INVENT_MASK;
    hardware_closedown(TRUE);
    drug_closedown(TRUE);
    change_mode_func(0, 0, FULLSCREEN_LOOP);

    // Hud stuff
    if (dest_lev >= FIRST_CSPACE_LEVEL)
        hud_set(HUD_CYBERTIME);

    shock_alloc_ipal();

    return (OK);
}

errtype early_exit_cyberspace_stuff() {
    // Delete the shodan_object, if there is one.
    if (shodan_avatar_id != OBJ_NULL) {
        obj_destroy(shodan_avatar_id);
        shodan_avatar_id = OBJ_NULL;
    }
    return (OK);
}

errtype exit_cyberspace_stuff() {
    int i;

    // Blast away the cspace MFD
    mfd_notify_func(MFD_EMPTY_FUNC, MFD_INFO_SLOT, TRUE, MFD_ACTIVE, TRUE);

    // Restore old MFD button state
    for (i = 0; i < MFD_NUM_REAL_SLOTS; i++)
        player_struct.mfd_slot_status[i] = status_back[i];

    // these visibilities will be set for real by the restore, but
    // the restore will punt unless they are set.
    full_visible = full_visible | FULL_R_MFD_MASK | FULL_L_MFD_MASK;
    for (i = 0; i < NUM_MFDS; i++)
        restore_mfd_slot(i);

    // Clear out all the various cspace state variables
    time_until_shodan_avatar = 0;
    hud_unset(HUD_TURBO | HUD_FAKEID | HUD_DECOY | HUD_CYBERTIME | HUD_CYBERDANGER | HUD_SHIELD);
    if (cspace_decoy_obj != OBJ_NULL)
        obj_destroy(cspace_decoy_obj);
    cspace_decoy_obj = OBJ_NULL;

    if (player_struct.save_obj_cursor != OBJ_NULL) {
        push_cursor_object(player_struct.save_obj_cursor);
        player_struct.save_obj_cursor = OBJ_NULL;
    }
    drug_startup(TRUE);
    hardware_startup(TRUE);
    if (old_loop != FULLSCREEN_LOOP)
        change_mode_func(0, 0, old_loop);
    inventory_draw_new_page(0);

    grd_ipal = NULL; // hack hack hack hack

    return (OK);
}
