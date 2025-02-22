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
/*
 * $Source: r:/prj/cit/src/RCS/vmail.c $
 * $Revision: 1.31 $
 * $Author: xemu $
 * $Date: 1994/11/01 09:17:43 $
 *
 */

#include "Shock.h"

#include "anim.h"
#include "email.h"
#include "game_screen.h"
#include "vmail.h"
#include "input.h"
#include "invent.h"
#include "mainloop.h"
#include "gameloop.h"
#include "tools.h"
#include "gametime.h"
#include "sfxlist.h"
#include "musicai.h"
#include "player.h"
#include "sdl_events.h"
#include "statics.h"
#include "fullscrn.h"
#include "render.h"
#include "gr2ss.h"
#include "criterr.h"


//#define LOTS_O_SPEW
uchar vmail_wait_for_input = TRUE;

//#define CONTINUOUS_VMAIL_TEST

ActAnim *main_anim;

#define NUM_VMAIL 6
#define INTRO_VMAIL (NUM_VMAIL+1)

byte current_vmail = -1;
extern LGCursor vmail_cursor;

Ref vmail_frame_anim[NUM_VMAIL] = {
   RES_FRAMES_shield,
   RES_FRAMES_grove,
   RES_FRAMES_bridge,
   RES_FRAMES_laser1,
   RES_FRAMES_status,
   RES_FRAMES_explode1
};

Ref vmail_res[NUM_VMAIL] = {
   RES_shield,
   RES_grove,
   RES_bridge,
   RES_laser1,
   RES_status,
   RES_explode1
};

ubyte vmail_len[NUM_VMAIL] = {
   1, // shield
   1, // grove
   1, // bridge
   2, // laser
   1, // status
   5, // explode
};

#define MAX_VMAIL_SIZE 100000

// --------------------------------------------------------------------
//
//

extern grs_canvas *anim_offscreen;
uchar copied_background = FALSE;
grs_bitmap *vmail_background = NULL;

#pragma disable_message(202)
void vmail_intro(LGRect *area, ubyte flags)
{
   if (flags & BEFORE_ANIM_BITMAP)
   {
      if (vmail_background)
         gr_bitmap(vmail_background, 0, 0);
   }
}
#pragma enable_message(202)

// --------------------------------------------------------------------
//
//

#pragma disable_message(202)
void vmail_anim_end(ActAnim *paa, AnimCode ancode, AnimCodeData *pdata)
{
#ifdef PLAYTEST
   if (current_vmail == -1)
      Warning(("Trying to end vmail, with no current vmail!\n"));
#endif
   current_vmail = -1;
}
#pragma enable_message(202)

// --------------------------------------------------------------------
//
//

#pragma disable_message(202)
void vmail_start_anim_end(ActAnim *paa, AnimCode ancode, AnimCodeData *pdata)
{
#ifdef PLAYTEST
   if (current_vmail == -1)
   {
      Warning(("Trying to end vmail, with no current vmail!\n"));
   }
#endif
   current_vmail = -1;
}
#pragma enable_message(202)

#define EARLY_EXIT ((errtype)200)

// ---------------------------------------------
//
// play_vmail_intro()
//

#define MOVIE_BUFFER_SIZE  (512 * 1024)

errtype play_vmail_intro(uchar use_texture_buffer)
{
   LGPoint animloc = {VINTRO_X, VINTRO_Y};
   uchar *p, *useBuffer;
   int bsize;
   short w,h;

   DEBUG("Playing vmail intro");

   main_anim = AnimPlayRegion(REF_ANIM_vintro, mainview_region, animloc, 0, vmail_intro);
   if (main_anim == NULL)
      return(ERR_NOEFFECT);

   if (use_texture_buffer)
   {
      AnimSetDataBufferSafe(main_anim, tmap_static_mem,sizeof(tmap_static_mem));
      AnimPreloadFrames(main_anim, REF_ANIM_vintro);
   }

   // let's slork up memory!!!!
   w = VINTRO_W;
   h = VINTRO_H;
   {
      useBuffer = frameBuffer;
      bsize = sizeof(frameBuffer);
   }
   if (w * h + sizeof(grs_bitmap) > bsize)
      critical_error(CRITERR_MEM|8);
   p = useBuffer+bsize-(w*h);
   vmail_background = (grs_bitmap *) (p - sizeof(grs_bitmap));

   gr_init_bm(vmail_background, p, BMT_FLAT8, 0, w,h);
   uiHideMouse(NULL);
#ifdef SVGA_SUPPORT
   if (convert_use_mode)
   {
      grs_canvas tempcanv;
      gr_make_canvas(vmail_background,&tempcanv);
      gr_push_canvas(&tempcanv);
      gr_clear(1);
      gr_pop_canvas();
   }
   else
#endif
      gr_get_bitmap(vmail_background, VINTRO_X, VINTRO_Y);
   uiShowMouse(NULL);

   AnimSetNotify(main_anim, NULL, ANCODE_KILL, vmail_start_anim_end);
   current_vmail = INTRO_VMAIL;
   play_digi_fx(SFX_VMAIL, 1);

#ifdef LOTS_O_SPEW
   mprintf("*PLAY INTRO*");
#endif
   while (current_vmail != -1)
   {
      AnimRecur();
      tight_loop(TRUE);
   }
#ifdef LOTS_O_SPEW
   mprintf("*DONE INTRO*");
#endif
   vmail_background = NULL;

   return(OK);
}

// --------------------------------------------------------------------
//
// play_vmail()
//

#pragma disable_message(202)
errtype play_vmail(byte vmail_no)
{
   LGPoint  animloc = {VINTRO_X, VINTRO_Y};
   errtype  intro_error;
   int      vmail_animfile_num = 0;
   uchar    early_exit = FALSE;
   uchar    preload_animation= TRUE;
   uchar    use_texture_buffer = FALSE;
   int      len = vmail_len[vmail_no];
   int      i;

   // let's extern

   DEBUG("Playing vmail %i", vmail_no);

   // the more I look at this procedure - the more I think
   // art - what were you thinking
   extern uiSlab fullscreen_slab;
   extern uiSlab main_slab;
   extern uchar game_paused;
   extern uchar checking_mouse_button_emulation;
   extern short old_invent_page;

   // make sure we don't have a current vmail, and we're given a valid vmail num
   if ((current_vmail != -1) || (vmail_no < 0) || (vmail_no >= NUM_VMAIL))
      return(ERR_NOEFFECT);

   if (full_game_3d)
      render_run();

   // spew the appropriate text for vmail - full screen needs a draw!
   suspend_game_time();
   time_passes = FALSE;
   checking_mouse_button_emulation = game_paused = TRUE;

   // open the res file
   vmail_animfile_num = ResOpenFile("res/data/vidmail.res");
   if (vmail_animfile_num < 0)
      return(ERR_FOPEN);

   uiPushSlabCursor(&fullscreen_slab, &vmail_cursor);
   uiPushSlabCursor(&main_slab, &vmail_cursor);

#ifdef LOTS_O_SPEW
   mprintf("\nBUFFER:(%d)\n", use_texture_buffer);
#endif

   // if we're not using the texture buffer - then we can probably
   // preload the animations
   if (!use_texture_buffer)
   {
      uchar  cant_preload_all = FALSE;

      // load the intro in first! before checking for preloading
      if (ResLock(RES_FRAMES_vintro) == NULL)
         use_texture_buffer = TRUE;
      else
      {
         for (i=0;i<len && !cant_preload_all;i++)
         {
            // preload vmail frame animation first, and then play intro -> no pause between the two
            // if it fails on the lock - then say you can't preload!
            if(ResLock(vmail_frame_anim[vmail_no]+i) == NULL)
            {
               cant_preload_all = TRUE;
               break;
            }
         }
         // if we failed our preloading for whatever reason
         // let's unlock it all - drop it so that it doesn't stay
         // in memory
         if (cant_preload_all)
         {
            int   j;
            preload_animation = FALSE;
            for (j=0; j<i;j++)
            {
               ResUnlock(vmail_frame_anim[vmail_no]+j);
               ResDrop(vmail_frame_anim[vmail_no]+j);
            }
            ResUnlock(RES_FRAMES_vintro);
            ResDrop(RES_FRAMES_vintro);
            use_texture_buffer = TRUE;
#ifdef LOTS_O_SPEW
            mprintf("**TRIED TO PRELOAD-CAN'T PRELOAD**\n");
#endif
         }
      }
   }
   else
      preload_animation = FALSE;

#ifdef LOTS_O_SPEW
   mprintf("**PREL:(%d) INTRO:(%d)**", preload_animation, use_texture_buffer);
#endif
   intro_error = play_vmail_intro(use_texture_buffer);
   if (preload_animation)
   {
      ResUnlock(RES_FRAMES_vintro);
      ResDrop(RES_FRAMES_vintro);
   }

   if (intro_error != OK)     // did it have no effect - don't worry about texture buffer
   {
      // if we had a problem with the intro - then flush
      // the animation all out - close it and
      // then return error code
      DEBUG("Could not play vmail intro");
      if (preload_animation)
         for (i=0;i<len;i++)
         {
            ResUnlock(vmail_frame_anim[vmail_no]+i);
            ResDrop(vmail_frame_anim[vmail_no]+i);
         }
      ResCloseFile(vmail_animfile_num);
      return(intro_error);
   }

   for (i=0; (i<len) && !early_exit;i++)
   {
      DEBUG("Playing segment.");
      Ref   vmail_ref = MKREF(vmail_res[vmail_no]+i,0);
      main_anim = AnimPlayRegion(vmail_ref,mainview_region,animloc, 0, NULL);
      if (main_anim == NULL)
      {
         early_exit = TRUE;
         break;
      }
      if(use_texture_buffer)
      {
         AnimSetDataBufferSafe(main_anim, tmap_static_mem, sizeof(tmap_static_mem));
         AnimPreloadFrames(main_anim, vmail_ref);
      }
      current_vmail = vmail_no;

      AnimSetNotify(main_anim, NULL, ANCODE_KILL, vmail_anim_end);
      uiFlush();
      while (current_vmail != -1)
      {
#ifdef LOTS_O_SPEW
//         mprintf("R");
#endif
         AnimRecur();
#ifdef LOTS_O_SPEW
//         mprintf("S");
#endif
         if(citadel_check_input())
         {
            early_exit = TRUE;
#ifdef LOTS_O_SPEW
            mprintf("Early Exit\n");
#endif
            AnimKill(main_anim);
         }
         tight_loop(TRUE);
      }
   }

   DEBUG("Finished playing!");
   if (preload_animation)
   {
      for (i=0;i<len;i++)
      {
         ResUnlock(vmail_frame_anim[vmail_no]+i);
         ResDrop(vmail_frame_anim[vmail_no]+i);
      }
   }
   ResCloseFile(vmail_animfile_num);

#ifdef LOTS_O_SPEW
      mprintf("T");
#endif
   uiFlush();
#ifdef LOTS_O_SPEW
      mprintf("U");
#endif

   if (use_texture_buffer)
   {
#ifdef LOTS_O_SPEW
      mprintf("V");
#endif
      load_textures();
#ifdef LOTS_O_SPEW
      mprintf("W");
#endif
   }

#ifndef CONTINUOUS_VMAIL_TEST
   if (!early_exit && vmail_wait_for_input)
      while (!citadel_check_input()) {
         uiHideMouse(NULL); //trick to prevent
         uiShowMouse(NULL); //  mouse pointer freeze
         pump_events();
         SDLDraw();
         tight_loop(FALSE); //keep the music playing
      }
#endif

   email_page_exit();

   screen_draw();
   inventory_draw_new_page(old_invent_page);

   DEBUG("Resuming game time");
   resume_game_time();
   checking_mouse_button_emulation = game_paused = FALSE;

   // let the player wait before firing auto fire weapon
   player_struct.auto_fire_click = player_struct.game_time + 60;
   time_passes = TRUE;

#ifdef LOTS_O_SPEW
      mprintf("X");
#endif
   uiPopSlabCursor(&fullscreen_slab);
   uiPopSlabCursor(&main_slab);
#ifdef LOTS_O_SPEW
      mprintf("Y");
#endif
   chg_set_flg(DEMOVIEW_UPDATE);

   return(OK);
}
#pragma enable_message(202)

byte test_vmail = 0;
#pragma disable_message(202)
uchar shield_test_func(short keycode, ulong context, void* data)
{
   int   i;
   vmail_wait_for_input = FALSE;
   for (i=0;i<5; i++)
   {
      play_vmail(test_vmail);
      test_vmail = (test_vmail + 1) % NUM_VMAIL;
   }
   vmail_wait_for_input = TRUE;
   return(TRUE);
}
#pragma enable_message(202)

#ifdef PLAYTEST
#pragma disable_message(202)
uchar shield_off_func(short keycode, ulong context, void* data)
{
   return(TRUE);
}
#pragma enable_message(202)

#endif
