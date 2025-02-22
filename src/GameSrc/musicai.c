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
 * $Source: r:/prj/cit/src/RCS/musicai.c $
 * $Revision: 1.124 $
 * $Author: unknown $
 * $Date: 1994/11/26 03:16:18 $
 */

#include <string.h>

#include "Shock.h"
#include "Prefs.h"

#include "airupt.h"
#include "musicai.h"
#include "MacTune.h"

#include "map.h"
#include "mapflags.h"
#include "player.h"
#include "tickcount.h"
#include "tools.h"

#include "adlmidi.h"
#include "Xmi.h"

#ifdef AUDIOLOGS
#include "audiolog.h"
#endif

//#include <ail.h>

uchar music_card = TRUE, music_on = FALSE;

uchar track_table[NUM_SCORES][SUPERCHUNKS_PER_SCORE];
uchar transition_table[NUM_TRANSITIONS];
uchar layering_table[NUM_LAYERS][MAX_KEYS];
uchar key_table[NUM_LAYERABLE_SUPERCHUNKS][KEY_BAR_RESOLUTION];

char peril_bars = 0;

int new_theme = 0;
int new_x, new_y;
int old_bore;
short mai_override = 0;
uchar cyber_play = 255;

int layer_danger = 0;
int layer_success = 0;
int layer_transition = 0;
int transition_count = 0;
char tmode_time = 0;
int actual_score = 0;
uchar decon_count = 0;
uchar decon_time = 8;
uchar in_deconst = FALSE, old_deconst = FALSE;
uchar in_peril = FALSE;
uchar just_started = TRUE;
int score_playing = 0;
short curr_ramp_time, curr_ramp;
char curr_prioritize, curr_crossfade;

int mlimbs_peril, mlimbs_positive, mlimbs_motion, mlimbs_monster;
ulong mlimbs_combat;
int current_score, current_zone, current_mode, random_flag;
int current_transition, last_score;
int boring_count;
int mlimbs_boredom;
int *output_table;
uchar wait_flag;
int next_mode, ai_cycle;
int cur_digi_channels = 4;

// extern int digifx_volume_shift(short x, short y, short z, short phi, short theta, short basevol);
// extern int digifx_pan_shift(short x, short y, short z, short phi, short theta);
extern uchar mai_semaphor;

uchar park_random = 75;
uchar park_playing = 0;
uchar access_random = 45;

ulong last_damage_sum = 0;
ulong last_vel_time = 0;

// Damage taken decay & quantity of decay
int danger_hp_level = 10;
int danger_damage_level = 40;
int damage_decay_time = 300;
int damage_decay_amount = 6;
int mai_damage_sum = 0;

// How long an attack keeps us in combat music mode
int mai_combat_length = 1000;

uchar bad_digifx = FALSE;

// KLC - no longer need this   Datapath music_dpath;

#define SMALL_ROBOT_LAYER 3

char mlimbs_machine = 0;

//------------------
//  INTERNAL PROTOTYPES
//------------------

errtype musicai_shutdown() {
    int i;
    for (i = 0; i < MLIMBS_MAX_SEQUENCES - 1; i++)
        current_request[i].pieceID = 255;
    MacTuneKillCurrentTheme();
    return (OK);
}

extern uchar run_asynch_music_ai;

errtype musicai_reset(uchar runai) {
    if (runai) // Figure out if there is a theme to start with.
        grind_music_ai();
    mlimbs_counter = 0;
    return (OK);
}

void musicai_clear() {
    mai_damage_sum = 0;
    last_damage_sum = 0;
    mlimbs_combat = 0;
}

void mlimbs_do_ai() {
    extern ObjID damage_sound_id;
    extern char damage_sound_fx;

    if (!IsPlaying(0)) gReadyToQueue = 1;


    //repeat shorter tracks while thread 0 is still playing
    if (!gReadyToQueue)
    {
        for (int i = 1; i < MLIMBS_MAX_CHANNELS - 1; i++)
            if (current_request[i].pieceID != 255 && !IsPlaying(i))
            {
                make_request(i, current_request[i].pieceID);
                current_request[i].pieceID = 255; //make sure it only plays this time
            }
    }


    // Play any queued sound effects, or damage SFX that have yet to get flushed
    if (damage_sound_fx != -1) {
        play_digi_fx_obj(damage_sound_fx, 1, damage_sound_id);
        damage_sound_fx = -1;
    }

    if (music_on) {
        if (mlimbs_combat != 0 && mlimbs_combat < player_struct.game_time)
            mlimbs_combat = 0;

        // Set danger layer
        layer_danger = 0;
        if (mai_damage_sum > danger_damage_level)
            layer_danger = 2;
        else if (player_struct.hit_points < danger_hp_level)
            layer_danger = 1;

        // Decay damage
        if ((last_damage_sum + damage_decay_time) < player_struct.game_time) {
            mai_damage_sum -= damage_decay_amount;
            if (mai_damage_sum < 0)
                mai_damage_sum = 0;
            last_damage_sum = player_struct.game_time;
        }

        if ((score_playing == BRIDGE_ZONE) && in_peril) {
            mlimbs_peril = DEFAULT_PERIL_MIN;
            mlimbs_combat = 0;
        } else {
            if ((mlimbs_combat > 0) || in_peril)
                mlimbs_peril = DEFAULT_PERIL_MAX;
        }

        // KLC - moved here from grind_music_ai, so it can do this check at all times.
        if (global_fullmap->cyber) {
            MapElem *pme;
            int play_me;

            pme = MAP_GET_XY(PLAYER_BIN_X, PLAYER_BIN_Y); // Determine music for this
            if (!me_bits_peril(pme))                      // location in cyberspace.
                play_me = NUM_NODE_THEMES + me_bits_music(pme);
            else
                play_me = me_bits_music(pme);
            if (play_me != cyber_play) // If music needs to be changed, then
            {
                musicai_shutdown();         // stop playing current tune
                make_request(0, play_me);   // setup new tune
                musicai_reset(FALSE);       // reset MLIMBS and
                MacTuneStartCurrentTheme(); // start playing the new tune.
            } else
                make_request(0, play_me); // otherwise just queue up next tune.
            cyber_play = play_me;
        }

        // This is all pretty temporary right now, but here's what's happening.
        // If the gReadyToQueue flag is set, that means the 6-second timer has
        // fired.  So we call check_asynch_ai() to determine the next tune to play
        // then queue it up.
        //  Does not handle layering.  Just one music track!
        if (gReadyToQueue) {
            extern bool mlimbs_update_requests;
            mlimbs_update_requests = TRUE;

            if (!global_fullmap->cyber)
                check_asynch_ai(TRUE);
            int pid = current_request[0].pieceID;
            if (pid != 255) // If there is a theme to play,
            {
                MacTuneQueueTune(pid); // Queue it up.
                mlimbs_counter++;      // Tell mlimbs we've queued another tune.
                gReadyToQueue = FALSE;
            }
        }
    }
}

#ifdef NOT_YET //

void mlimbs_do_credits_ai() {
    extern uchar mlimbs_semaphore;
    if (ai_cycle) {
        ai_cycle = 0;
        grind_credits_music_ai();
        mlimbs_preload_requested_timbres();
        mlimbs_semaphore = FALSE;
    }
}

#endif // NOT_YET

errtype mai_attack() {
    if (music_on)
        mlimbs_combat = player_struct.game_time + mai_combat_length;
    return (OK);
}

errtype mai_intro() {
    if (music_on) {
        if (transition_table[TRANS_INTRO] != 255)
            mai_transition(TRANS_INTRO);
        mlimbs_peril = DEFAULT_PERIL_MIN;
        mlimbs_combat = 0;
    }
    return (OK);
}

errtype mai_monster_nearby(int monster_type) {
    if (music_on) {
        mlimbs_monster = monster_type;
        if (monster_type == NO_MONSTER) {
            mlimbs_combat = 0;
            mlimbs_peril = DEFAULT_PERIL_MIN;
        }
    }
    return (OK);
}

errtype mai_monster_defeated() {
    if (music_on)
        mlimbs_combat = 0;
    return (OK);
}

errtype mai_player_death() {
    if (music_on) {
        mai_transition(TRANS_DEATH);
        mlimbs_peril = DEFAULT_PERIL_MIN;
        peril_bars = 0;
        layer_danger = 0;
        mai_damage_sum = 0;
        layer_success = 0;
        mlimbs_machine = 0;
        mlimbs_monster = 0;
        mlimbs_combat = 0;
        musicai_shutdown();
        make_request(0, transition_table[TRANS_DEATH]);
        musicai_reset(FALSE);
        MacTuneStartCurrentTheme();
    }
    return (OK);
}

errtype mlimbs_AI_init(void) {
    mlimbs_boredom = 0;
    old_bore = 0;
    mlimbs_monster = NO_MONSTER;
    wait_flag = FALSE;
    random_flag = 0;
    boring_count = 0;
    ai_cycle = 0;
    mlimbs_peril = DEFAULT_PERIL_MAX;
    current_transition = TRANS_INTRO;
    current_mode = TRANSITION_MODE;
    tmode_time = 1; // KLC - was 4
    current_score = actual_score = last_score = WALKING_SCORE;
    current_zone = HOSPITAL_ZONE;
    cyber_play = 255;

    return (OK);
}

errtype mai_transition(int new_trans) {
    if ((next_mode == TRANSITION_MODE) || (current_mode == TRANSITION_MODE))
        return (ERR_NOEFFECT);

    if (transition_table[new_trans] < LAYER_BASE) {
        current_transition = new_trans;
        next_mode = TRANSITION_MODE;
        tmode_time = 1; // KLC - was 4
    } else if ((transition_count == 0) && (layering_table[TRANSITION_LAYER_BASE + new_trans][0] != 255)) {
        current_transition = new_trans;
    }
    // temp
    return (OK);
}

int gen_monster(int monster_num) {
    if (monster_num < 3)
        return (0);
    if (monster_num < 6)
        return (1);
    return (2);
}

int ext_rp = -1;

extern struct mlimbs_request_info default_request;

errtype make_request(int chunk_num, int piece_ID) {
    current_request[chunk_num] = default_request;
    current_request[chunk_num].pieceID = piece_ID;

    // These get set all around differently and stuff
    current_request[chunk_num].crossfade = curr_crossfade;
    current_request[chunk_num].ramp_time = curr_ramp_time;
    current_request[chunk_num].ramp = curr_ramp;

    DEBUG("make_request %i %i", chunk_num, piece_ID);

    extern int WonGame_ShowStats;

    int i = chunk_num;
    int track = 1+piece_ID;
    if (i >= 0 && i < NUM_THREADS && track >= 0 && track < NumTracks && !WonGame_ShowStats && !IsPlaying(i))
    {
        StartTrack(i, track);
    }

    return (OK);
}

int old_score;

errtype fade_into_location(int x, int y) {
    MapElem *pme;

    new_x = x;
    new_y = y;
    new_theme = 2;
    pme = MAP_GET_XY(new_x, new_y);
    score_playing = me_bits_music(pme);

    // For going into/outof elevator and cyberspace, don't do any crossfading.
    if ((score_playing == ELEVATOR_ZONE) || (score_playing > CYBERSPACE_SCORE_BASE) || (old_score == ELEVATOR_ZONE) ||
        (old_score > CYBERSPACE_SCORE_BASE)) {
        if (old_score != score_playing) // Don't restart music if going from elevator
        {                               // to elevator (eg, when changing levels).
            load_score_for_location(new_x, new_y);
            MacTuneStartCurrentTheme();
            new_theme = 0;
        }
    } else // for now, we're not going to do any cross-fading.  Just load the new score.
    {
        //		message_info("Sould be fading into new location.");
        load_score_for_location(new_x, new_y);
        MacTuneStartCurrentTheme();
        new_theme = 0;
    }
    return (OK);
}

// don't need?     uchar voices_4op = FALSE;
// don't need?     uchar digi_gain = FALSE;
void load_score_guts(uint8_t score_play) {
    int rv;
    char base[20];

    // Get the theme file name.
    sprintf(base, "thm%d", score_play);
    musicai_shutdown();

    rv = MacTuneLoadTheme(base, score_play);

    if (rv == 0) {
        musicai_reset(false);
    }
    else {
        DEBUG("%s: load theme failed!", __FUNCTION__); // handle this a better way.
    }
}

errtype load_score_for_location(int x, int y) {
    MapElem *pme;
    char sc;
    extern char old_bits;

    pme = MAP_GET_XY(x, y);
    sc = me_bits_music(pme);
    if (global_fullmap->cyber)
        sc = CYBERSPACE_SCORE_BASE;
    old_bits = old_score = score_playing = sc;
    if (sc == 7) // Randomize boredom for the elevator
        mlimbs_boredom = TickCount() % 8;
    else
        mlimbs_boredom = 0;
    load_score_guts(sc);
    return (OK);
}

#ifdef NOT_YET //

// 16384
// 8192
//#define SFX_BUFFER_SIZE    8192
#define MIDI_TYPE 0
#define DIGI_TYPE 1
// #define SPCH_TYPE   2    // perhaps someday, for special CD speech and separate SB digital effects, eh?
#define DEV_TYPES 2

#define DEV_CARD 0
#define DEV_IRQ 1
#define DEV_DMA 2
#define DEV_IO 3
#define DEV_DRQ 4
#define DEV_PARMS 5

// doug gets sneaky, film at 11
#define MIDI_CARD MIDI_TYPE][DEV_CARD
#define MIDI_IRQ  MIDI_TYPE][DEV_IRQ
#define MIDI_DMA  MIDI_TYPE][DEV_DMA
#define MIDI_IO   MIDI_TYPE][DEV_IO
#define MIDI_DRQ  MIDI_TYPE][DEV_DRQ
#define DIGI_CARD DIGI_TYPE][DEV_CARD
#define DIGI_IRQ  DIGI_TYPE][DEV_IRQ
#define DIGI_DMA  DIGI_TYPE][DEV_DMA
#define DIGI_IO   DIGI_TYPE][DEV_IO
#define DIGI_DRQ  DIGI_TYPE][DEV_DRQ

#define SFX_BUFFER_SIZE 8192

static char *dev_suffix[] = {"card", "irq", "dma", "io", "drq"};
static char *dev_prefix[] = {"midi_", "digi_"};

short music_get_config(char *pre, char *suf) {
    int tmp_in, dummy_count = 1;
    char buf[20];
    strcpy(buf, pre);
    strcat(buf, suf);
    if (!config_get_value(buf, CONFIG_INT_TYPE, &tmp_in, &dummy_count))
        return -1;
    else
        return (short)tmp_in;
}

audio_card *fill_audio_card(audio_card *cinf, short *dinf) {
    cinf->type = dinf[DEV_CARD];
    cinf->dname = NULL;
    cinf->io = dinf[DEV_IO];
    cinf->irq = dinf[DEV_IRQ];
    cinf->dma_8bit = dinf[DEV_DMA];
    cinf->dma_16bit = -1; // who knows, eh?
    return cinf;
}

#ifdef PLAYTEST
static char def_sound_path[] = "r:\\prj\\cit\\src\\sound";
#else
static char def_sound_path[] = "sound";
#endif

#ifdef SECRET_SUPPORT
FILE *secret_fp = NULL;
char secret_dc_buf[10000];
volatile char secret_update = FALSE;
void secret_closedown(void) {
    if (secret_fp != NULL)
        fclose(secret_fp);
}
#endif

#endif // NOT_YET

//----------------------------------------------------------------------
//  For Mac version, the vast majority of the config mess just goes away.  But we do check for
//  the presence of QuickTime Musical Instruments.
//----------------------------------------------------------------------
errtype music_init() {
    if (gShockPrefs.soBackMusic) {
        if (MacTuneInit() == 0) // If no error, go ahead and start up.
        {
            music_on = mlimbs_on = TRUE;
            mlimbs_AI_init();
        } else // else turn off the music globals and prefs
        {
            gShockPrefs.soBackMusic = FALSE;
            SavePrefs();
            music_on = mlimbs_on = FALSE;
        }
        //	}
    } else {
        music_on = mlimbs_on = FALSE;
    }
    return (OK);
}
