/**********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

/*
  This file includes the definition of a new savegame format introduced with
  2.3.0. It is defined by the mandatory option '+version2'. The main load
  function checks if this option is present. If not, the old (pre-2.3.0)
  loading routines are used.
  The format version is also saved in the settings section of the savefile, as an
  integer (savefile.version). The integer is used to determine the version
  of the savefile.
  
  For each savefile format after 2.3.0, compatibility functions are defined
  which translate secfile structures from previous version to that version;
  all necessary compat functions are called in order to
  translate between the file and current version. See sg_load_compat().
 
  The integer version ID should be increased every time the format is changed.
  If the change is not backwards compatible, please state the changes in the
  following list and update the compat functions at the end of this file.

  - what was added / removed
  - when was it added / removed (date and version)
  - when can additional capability checks be set to mandatory (version)
  - which compatibility checks are needed and till when (version)

  freeciv | what                                           | date       | id
  --------+------------------------------------------------+------------+----
  current | (mapped to current savegame format)            | ----/--/-- |  0
          | first version (svn17538)                       | 2010/07/05 |  -
  2.3.0   | 2.3.0 release                                  | 2010/11/?? |  3
  2.4.0   | 2.4.0 release                                  | 201./../.. | 10
          | * player ai type                               |            |
          | * delegation                                   |            |
          | * citizens                                     |            |
          | * save player color                            |            |
          | * "known" info format change                   |            |
  2.5.0   | 2.5.0 release (development)                    | 201./../.. | 20
          |                                                |            |

  Structure of this file:

  - The main functions are savegame2_load() and savegame2_save(). Within
    former function the savegame version is tested and the requested savegame version is
    loaded.

  - The real work is done by savegame2_load_real() and savegame2_save_real().
    This function call all submodules (settings, players, etc.)

  - The remaining part of this file is split into several sections:
     * helper functions
     * save / load functions for all submodules (and their subsubmodules)

  - If possible, all functions for load / save submodules should exit in
    pairs named sg_load_<submodule> and sg_save_<submodule>. If one is not
    needed please add a comment why.

  - The submodules can be further divided as:
    sg_load_<submodule>_<subsubmodule>

  - If needed (due to static variables in the *.c files) these functions
    can be located in the corresponding source files (as done for the settings
    and the event_cache).

  Creating a savegame:

  (nothing at the moment)

  Loading a savegame:

  - The status of the process is saved within the static variable
    'sg_success'. This variable is set to TRUE within savegame2_load_real().
    If you encounter an error use sg_failure_*() to set it to FALSE and
    return an error message. Furthermore, sg_check_* should be used at the
    start of each (submodule) function to return if previous functions failed.

  - While the loading process dependencies between different modules exits.
    They can be handled within the struct loaddata *loading which is used as
    first argument for all sg_load_*() function. Please indicate the
    dependencies within the definition of this struct.

*/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "bitvector.h"
#include "fcintl.h"
#include "idex.h"
#include "log.h"
#include "mem.h"
#include "rand.h"
#include "registry.h"
#include "shared.h"
#include "support.h"            /* bool type */
#include "timing.h"

/* common */
#include "ai.h"
#include "bitvector.h"
#include "capability.h"
#include "citizens.h"
#include "city.h"
#include "game.h"
#include "government.h"
#include "map.h"
#include "mapimg.h"
#include "movement.h"
#include "packets.h"
#include "research.h"
#include "rgbcolor.h"
#include "specialist.h"
#include "unit.h"
#include "unitlist.h"

/* server */
#include "aiiface.h"
#include "barbarian.h"
#include "citizenshand.h"
#include "citytools.h"
#include "cityturn.h"
#include "diplhand.h"
#include "maphand.h"
#include "meta.h"
#include "notify.h"
#include "plrhand.h"
#include "ruleset.h"
#include "sanitycheck.h"
#include "savegame.h"
#include "score.h"
#include "settings.h"
#include "spacerace.h"
#include "srv_main.h"
#include "stdinhand.h"
#include "techtools.h"
#include "unittools.h"

/* server/advisors */
#include "advdata.h"
#include "advbuilding.h"
#include "infracache.h"

/* server/generator */
#include "mapgen.h"
#include "utilities.h"

/* server/scripting */
#include "script_server.h"

#include "savegame2.h"

#define log_sg log_error

static bool sg_success;

#define sg_check_ret(...)                                                   \
  if (!sg_success) {                                                        \
    return;                                                                 \
  }
#define sg_check_ret_val(_val)                                              \
  if (!sg_success) {                                                        \
    return _val;                                                            \
  }

#define sg_warn(condition, message, ...)                                    \
  if (!(condition)) {                                                       \
    log_sg(message, ## __VA_ARGS__);                                        \
  }
#define sg_warn_ret(condition, message, ...)                                \
  if (!(condition)) {                                                       \
    log_sg(message, ## __VA_ARGS__);                                        \
    return;                                                                 \
  }
#define sg_warn_ret_val(condition, _val, message, ...)                      \
  if (!(condition)) {                                                       \
    log_sg(message, ## __VA_ARGS__);                                        \
    return _val;                                                            \
  }

#define sg_failure_ret(condition, message, ...)                             \
  if (!(condition)) {                                                       \
    sg_success = FALSE;                                                     \
    log_sg(message, ## __VA_ARGS__);                                        \
    sg_check_ret();                                                         \
  }
#define sg_failure_ret_val(condition, _val, message, ...)                   \
  if (!(condition)) {                                                       \
    sg_success = FALSE;                                                     \
    log_sg(message, ## __VA_ARGS__);                                        \
    sg_check_ret_val(_val);                                                 \
  }

/*
 * This loops over the entire map to save data. It collects all the data of
 * a line using GET_XY_CHAR and then executes the macro SECFILE_INSERT_LINE.
 *
 * Parameters:
 *   ptile:         current tile within the line (used by GET_XY_CHAR)
 *   GET_XY_CHAR:   macro returning the map character for each position
 *   secfile:       a secfile struct
 *   secpath, ...:  path as used for sprintf() with arguments; the last item
 *                  will be the the y coordinate
 * Example:
 *   SAVE_MAP_CHAR(ptile, terrain2char(ptile->terrain), file, "map.t%04d");
 */
#define SAVE_MAP_CHAR(ptile, GET_XY_CHAR, secfile, secpath, ...)            \
{                                                                           \
  char _line[map.xsize + 1];                                                \
  int _nat_x, _nat_y;                                                       \
                                                                            \
  for (_nat_y = 0; _nat_y < map.ysize; _nat_y++) {                          \
    for (_nat_x = 0; _nat_x < map.xsize; _nat_x++) {                        \
      struct tile *ptile = native_pos_to_tile(_nat_x, _nat_y);              \
      fc_assert_action(ptile != NULL, continue);                            \
      _line[_nat_x] = (GET_XY_CHAR);                                        \
      sg_failure_ret(fc_isprint(_line[_nat_x] & 0x7f),                      \
                     "Trying to write invalid map data at position "        \
                     "(%d, %d) for path %s: '%c' (%d)", _nat_x, _nat_y,     \
                     secpath, _line[_nat_x], _line[_nat_x]);                \
    }                                                                       \
    _line[map.xsize] = '\0';                                                \
    secfile_insert_str(secfile, _line, secpath, ## __VA_ARGS__, _nat_y);    \
  }                                                                         \
}

/*
 * This loops over the entire map to load data. It inputs a line of data
 * using the macro SECFILE_LOOKUP_LINE and then loops using the macro
 * SET_XY_CHAR to load each char into the map at (map_x, map_y). Internal
 * variables ch, map_x, map_y, nat_x, and nat_y are allocated within the
 * macro but definable by the caller.
 *
 * Parameters:
 *   ch:            a variable to hold a char (data for a single position,
 *                  used by SET_XY_CHAR)
 *   ptile:         current tile within the line (used by SET_XY_CHAR)
 *   SET_XY_CHAR:   macro to load the map character at each (map_x, map_y)
 *   secfile:       a secfile struct
 *   secpath, ...:  path as used for sprintf() with arguments; the last item
 *                  will be the the y coordinate
 * Example:
 *   LOAD_MAP_CHAR(ch, ptile,
 *                 map_get_player_tile(ptile, plr)->terrain
 *                   = char2terrain(ch), file, "player%d.map_t%04d", plrno);
 *
 * Note: some (but not all) of the code this is replacing used to skip over
 *       lines that did not exist. This allowed for backward-compatibility.
 *       We could add another parameter that specified whether it was OK to
 *       skip the data, but there's not really much advantage to exiting
 *       early in this case. Instead, we let any map data type to be empty,
 *       and just print an informative warning message about it.
 */
#define LOAD_MAP_CHAR(ch, ptile, SET_XY_CHAR, secfile, secpath, ...)        \
{                                                                           \
  int _nat_x, _nat_y;                                                       \
  bool _printed_warning = FALSE;                                            \
  for (_nat_y = 0; _nat_y < map.ysize; _nat_y++) {                          \
    const char *_line = secfile_lookup_str(secfile, secpath,                \
                                           ## __VA_ARGS__, _nat_y);         \
    if (NULL == _line) {                                                    \
      char buf[64];                                                         \
      fc_snprintf(buf, sizeof(buf), secpath, ## __VA_ARGS__, _nat_y);       \
      log_verbose("Line not found='%s'", buf);                              \
      _printed_warning = TRUE;                                              \
      continue;                                                             \
    } else if (strlen(_line) != map.xsize) {                                \
      char buf[64];                                                         \
      fc_snprintf(buf, sizeof(buf), secpath, ## __VA_ARGS__, _nat_y);       \
      log_verbose("Line too short (expected %d got %lu)='%s'",              \
                  map.xsize, (unsigned long) strlen(_line), buf);           \
      _printed_warning = TRUE;                                              \
      continue;                                                             \
    }                                                                       \
    for (_nat_x = 0; _nat_x < map.xsize; _nat_x++) {                        \
      const char ch = _line[_nat_x];                                        \
      struct tile *ptile = native_pos_to_tile(_nat_x, _nat_y);              \
      (SET_XY_CHAR);                                                        \
    }                                                                       \
  }                                                                         \
  if (_printed_warning) {                                                   \
    /* TRANS: Minor error message. */                                       \
    log_sg(_("Saved game contains incomplete map data. This can"            \
             " happen with old saved games, or it may indicate an"          \
             " invalid saved game file. Proceed at your own risk."));       \
  }                                                                         \
}

/* Iterate on the specials half-bytes */
#define halfbyte_iterate_special(s, num_specials_types)                     \
{                                                                           \
  enum tile_special_type s;                                                 \
  for(s = 0; 4 * s < (num_specials_types); s++) {

#define halfbyte_iterate_special_end                                        \
  }                                                                         \
}

/* Iterate on the bases half-bytes */
#define halfbyte_iterate_bases(b, num_bases_types)                          \
{                                                                           \
  int b;                                                                    \
  for(b = 0; 4 * b < (num_bases_types); b++) {

#define halfbyte_iterate_bases_end                                          \
  }                                                                         \
}

/* Iterate on the roads half-bytes */
#define halfbyte_iterate_roads(r, num_roads_types)                          \
{                                                                           \
  int r;                                                                    \
  for(r = 0; 4 * r < (num_roads_types); r++) {

#define halfbyte_iterate_roads_end                                          \
  }                                                                         \
}

struct loaddata {
  struct section_file *file;
  const char *secfile_options;
  int version;

  /* loaded in sg_load_savefile(); needed in sg_load_player() */
  struct {
    const char **order;
    size_t size;
  } improvement;
  /* loaded in sg_load_savefile(); needed in sg_load_player() */
  struct {
    const char **order;
    size_t size;
  } technology;
  /* loaded in sg_load_savefile(); needed in sg_load_player() */
  struct {
    const char **order;
    size_t size;
  } trait;
  /* loaded in sg_load_savefile(); needed in sg_load_map(), ... */
  struct {
    enum tile_special_type *order;
    size_t size;
  } special;
  /* loaded in sg_load_savefile(); needed in sg_load_map(), ... */
  struct {
    struct base_type **order;
    size_t size;
  } base;
  /* loaded in sg_load_savefile(); needed in sg_load_map(), ... */
  struct {
    struct road_type **order;
    size_t size;
  } road;

  /* loaded in sg_load_game(); needed in sg_load_random(), ... */
  enum server_states server_state;

  /* loaded in sg_load_random(); needed in sg_load_sanitycheck() */
  RANDOM_STATE rstate;

  /* loaded in sg_load_map_worked(); needed in sg_load_player_cities() */
  int *worked_tiles;
};

struct savedata {
  struct section_file *file;
  char secfile_options[512];

  /* set by the caller */
  const char *save_reason;
  bool scenario;

  /* Set in sg_save_game(); needed in sg_save_map_*(); ... */
  bool save_players;
};

#define TOKEN_SIZE 10

#define log_worker      log_verbose

static const char savefile_options_default[] =
  " +version2";
/* The following savefile option are added if needed:
 *  - specials
 *  - riversoverlay
 * See also calls to sg_save_savefile_options(). */

static const char hex_chars[] = "0123456789abcdef";
static const char num_chars[] =
  "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_-+";

static void savegame2_save_real(struct section_file *file,
                                const char *save_reason,
                                bool scenario);

struct savedata *savedata_new(struct section_file *file,
                              const char *save_reason,
                              bool scenario);
void savedata_destroy(struct savedata *saving);

static char order2char(enum unit_orders order);
static char dir2char(enum direction8 dir);
static char activity2char(enum unit_activity activity);
static char *quote_block(const void *const data, int length);
static void worklist_save(struct section_file *file,
                          const struct worklist *pwl,
                          int max_length, const char *path, ...);
static void unit_ordering_calc(void);
static char sg_special_get(bv_special specials,
                           const enum tile_special_type *index);
static char sg_bases_get(bv_bases bases, const int *index);
static char sg_roads_get(bv_roads roads, const int *index);
static char resource2char(const struct resource *presource);
/* bin2ascii_hex() is defined as macro */
static char num2char(unsigned int num);
static char terrain2char(const struct terrain *pterrain);
static void technology_save(struct section_file *file,
                            const char* path, int plrno, Tech_type_id tech);

static void sg_save_savefile(struct savedata *saving);
static void sg_save_savefile_options(struct savedata *saving,
                                     const char *option);

static void sg_save_game(struct savedata *saving);

static void sg_save_random(struct savedata *saving);

static void sg_save_script(struct savedata *saving);

static void sg_save_scenario(struct savedata *saving);

static void sg_save_settings(struct savedata *saving);

static void sg_save_map(struct savedata *saving);
static void sg_save_map_tiles(struct savedata *saving);
static void sg_save_map_tiles_bases(struct savedata *saving);
static void sg_save_map_tiles_roads(struct savedata *saving);
static void sg_save_map_tiles_specials(struct savedata *saving,
                                       bool rivers_overlay);
static void sg_save_map_tiles_resources(struct savedata *saving);

static void sg_save_map_startpos(struct savedata *saving);
static void sg_save_map_owner(struct savedata *saving);
static void sg_save_map_worked(struct savedata *saving);
static void sg_save_map_known(struct savedata *saving);

static void sg_save_players(struct savedata *saving);
static void sg_save_player_main(struct savedata *saving,
                                struct player *plr);
static void sg_save_player_cities(struct savedata *saving,
                                  struct player *plr);
static void sg_save_player_units(struct savedata *saving,
                                 struct player *plr);
static void sg_save_player_attributes(struct savedata *saving,
                                      struct player *plr);
// static void sg_save_player_vision(struct savedata *saving,
//                                   struct player *plr);

// static void sg_save_event_cache(struct savedata *saving);

static void sg_save_mapimg(struct savedata *saving);

static void sg_save_sanitycheck(struct savedata *saving);

struct compatibility {
  int version;
  void *none;
//   const load_version_func_t load;
};

/* The struct below contains the information about the savegame versions. It
 * is identified by the version number (first element), which should be
 * steadily increasing. It is saved as 'savefile.version'. The support
 * string (first element of 'name') is not saved in the savegame; it is
 * saved in settings files (so, once assigned, cannot be changed). The
 * 'pretty' string (second element of 'name') can be changed if necessary
 * For changes in the development version, edit the definitions above and
 * add the needed code to load the old version below. Thus, old
 * savegames can still be loaded while the main definition
 * represents the current state of the art. */
/* While developing freeciv 2.5.0, add the compatibility functions to
 * - compat_load_020500 to load old savegame. */
static struct compatibility compat[] = {
  /* dummy; equal to the current version (last element) */
  { 0, NULL },
  /* version 1 and 2 is not used */
  /* version 3: first savegame2 format, so no compat functions for translation
   * from previous format */
  { 3, NULL },
  /* version 4 to 9 are reserved for possible changes in 2.3.x */
  { 10, NULL },
  /* version 11 to 19 are reserved for possible changes in 2.4.x */
  { 20, NULL },
  /* Current savefile version is listed above this line; it corresponds to
     the definitions in this file. */
};

static const int compat_num = ARRAY_SIZE(compat);
#define compat_current (compat_num - 1)

/****************************************************************************
  Main entry point for saving a game.
  Called only in ./server/srv_main.c:save_game().
****************************************************************************/
void client_savegame2_save(struct section_file *file, const char *save_reason,
                    bool scenario)
{
  fc_assert_ret(file != NULL);

#ifdef DEBUG_TIMERS
  struct timer *savetimer = timer_new(TIMER_CPU, TIMER_DEBUG);
  timer_start(savetimer);
#endif

  log_verbose("saving game in new format ...");
  savegame2_save_real(file, save_reason, scenario);

#ifdef DEBUG_TIMERS
  timer_stop(savetimer);
  log_debug("Creating secfile in %.3f seconds.", timer_read_seconds(savetimer));
  timer_destroy(savetimer);
#endif /* DEBUG_TIMERS */
}

/* =======================================================================
 * Basic load / save functions.
 * ======================================================================= */

/****************************************************************************
  Really save the game to a file.
****************************************************************************/
static void savegame2_save_real(struct section_file *file,
                                const char *save_reason,
                                bool scenario)
{
  struct savedata *saving;

  /* initialise loading */
  saving = savedata_new(file, save_reason, scenario);
  sg_success = TRUE;

  /* [scenario] */
  /* This should be first section so scanning through all scenarios just for
   * names and descriptions would go faster. */
  sg_save_scenario(saving);
  /* [savefile] */
  sg_save_savefile(saving);
  /* [game] */
  sg_save_game(saving);
  /* [random] */
  sg_save_random(saving);
  /* [script] */
  sg_save_script(saving);
  /* [settings] */
  sg_save_settings(saving);
  /* [map] */
  sg_save_map(saving);
  /* [player<i>] */
  sg_save_players(saving);
  /* [event_cache] */
//   sg_save_event_cache(saving);
  /* [mapimg] */
  sg_save_mapimg(saving);

  /* Sanity checks for the saved game. */
  sg_save_sanitycheck(saving);

  /* deinitialise saving */
  savedata_destroy(saving);

  if (!sg_success) {
    log_error("Failure saving savegame!");
  }
}

/****************************************************************************
  Create new savedata item for given file.
****************************************************************************/
struct savedata *savedata_new(struct section_file *file,
                              const char *save_reason,
                              bool scenario)
{
  struct savedata *saving = calloc(1, sizeof(*saving));
  saving->file = file;
  saving->secfile_options[0] = '\0';

  saving->save_reason = save_reason;
  saving->scenario = scenario;

  saving->save_players = FALSE;

  return saving;
}

/****************************************************************************
  Free resources allocated for savedata item
****************************************************************************/
void savedata_destroy(struct savedata *saving)
{
  free(saving);
}

/* =======================================================================
 * Helper functions.
 * ======================================================================= */

/****************************************************************************
  Returns a character identifier for an order.  See also char2order.
****************************************************************************/
static char order2char(enum unit_orders order)
{
  switch (order) {
  case ORDER_MOVE:
    return 'm';
  case ORDER_FULL_MP:
    return 'w';
  case ORDER_ACTIVITY:
    return 'a';
  case ORDER_BUILD_CITY:
    return 'b';
  case ORDER_DISBAND:
    return 'd';
  case ORDER_BUILD_WONDER:
    return 'u';
  case ORDER_TRADE_ROUTE:
    return 't';
  case ORDER_HOMECITY:
    return 'h';
  case ORDER_LAST:
    break;
  }

  fc_assert(FALSE);
  return '?';
}

/****************************************************************************
  Returns a character identifier for a direction.  See also char2dir.
****************************************************************************/
static char dir2char(enum direction8 dir)
{
  /* Numberpad values for the directions. */
  switch (dir) {
  case DIR8_NORTH:
    return '8';
  case DIR8_SOUTH:
    return '2';
  case DIR8_EAST:
    return '6';
  case DIR8_WEST:
    return '4';
  case DIR8_NORTHEAST:
    return '9';
  case DIR8_NORTHWEST:
    return '7';
  case DIR8_SOUTHEAST:
    return '3';
  case DIR8_SOUTHWEST:
    return '1';
  }

  fc_assert(FALSE);
  return '?';
}

/****************************************************************************
  Returns a character identifier for an activity.  See also char2activity.
****************************************************************************/
static char activity2char(enum unit_activity activity)
{
  switch (activity) {
  case ACTIVITY_IDLE:
    return 'w';
  case ACTIVITY_POLLUTION:
    return 'p';
  case ACTIVITY_OLD_ROAD:
    return 'r';
  case ACTIVITY_MINE:
    return 'm';
  case ACTIVITY_IRRIGATE:
    return 'i';
  case ACTIVITY_FORTIFIED:
    return 'f';
  case ACTIVITY_FORTRESS:
    return 't';
  case ACTIVITY_SENTRY:
    return 's';
  case ACTIVITY_OLD_RAILROAD:
    return 'l';
  case ACTIVITY_PILLAGE:
    return 'e';
  case ACTIVITY_GOTO:
    return 'g';
  case ACTIVITY_EXPLORE:
    return 'x';
  case ACTIVITY_TRANSFORM:
    return 'o';
  case ACTIVITY_AIRBASE:
    return 'a';
  case ACTIVITY_FORTIFYING:
    return 'y';
  case ACTIVITY_FALLOUT:
    return 'u';
  case ACTIVITY_BASE:
    return 'b';
  case ACTIVITY_GEN_ROAD:
    return 'R';
  case ACTIVITY_CONVERT:
    return 'c';
  case ACTIVITY_UNKNOWN:
  case ACTIVITY_PATROL_UNUSED:
    return '?';
  case ACTIVITY_LAST:
    break;
  }

  fc_assert(FALSE);
  return '?';
}

/****************************************************************************
  Quote the memory block denoted by data and length so it consists only of
  " a-f0-9:". The returned string has to be freed by the caller using free().
****************************************************************************/
static char *quote_block(const void *const data, int length)
{
  char *buffer = fc_malloc(length * 3 + 10);
  size_t offset;
  int i;

  sprintf(buffer, "%d:", length);
  offset = strlen(buffer);

  for (i = 0; i < length; i++) {
    sprintf(buffer + offset, "%02x ", ((unsigned char *) data)[i]);
    offset += 3;
  }
  return buffer;
}

/****************************************************************************
  Save the worklist elements specified by path from the worklist pointed to
  by 'pwl'. 'pwl' should be a pointer to an existing worklist.
****************************************************************************/
static void worklist_save(struct section_file *file,
                          const struct worklist *pwl,
                          int max_length, const char *path, ...)
{
  char path_str[1024];
  int i;
  va_list ap;

  /* The first part of the registry path is taken from the varargs to the
   * function. */
  va_start(ap, path);
  fc_vsnprintf(path_str, sizeof(path_str), path, ap);
  va_end(ap);

  secfile_insert_int(file, pwl->length, "%s.wl_length", path_str);

  for (i = 0; i < pwl->length; i++) {
    const struct universal *entry = pwl->entries + i;
    secfile_insert_str(file, universal_type_rule_name(entry),
                       "%s.wl_kind%d", path_str, i);
    secfile_insert_str(file, universal_rule_name(entry),
                       "%s.wl_value%d", path_str, i);
  }

  fc_assert_ret(max_length <= MAX_LEN_WORKLIST);

  /* We want to keep savegame in tabular format, so each line has to be
   * of equal length. Fill table up to maximum worklist size. */
  for (i = pwl->length ; i < max_length; i++) {
    secfile_insert_str(file, "", "%s.wl_kind%d", path_str, i);
    secfile_insert_str(file, "", "%s.wl_value%d", path_str, i);
  }
}

/****************************************************************************
  Assign values to ord_city and ord_map for each unit, so the values can be
  saved.
****************************************************************************/
static void unit_ordering_calc(void)
{
  int j;

  players_iterate(pplayer) {
    /* to avoid junk values for unsupported units: */
    unit_list_iterate(pplayer->units, punit) {
      punit->server.ord_city = 0;
    } unit_list_iterate_end;
    city_list_iterate(pplayer->cities, pcity) {
      j = 0;
      unit_list_iterate(pcity->units_supported, punit) {
        punit->server.ord_city = j++;
      } unit_list_iterate_end;
    } city_list_iterate_end;
  } players_iterate_end;

  whole_map_iterate(ptile) {
    j = 0;
    unit_list_iterate(ptile->units, punit) {
      punit->server.ord_map = j++;
    } unit_list_iterate_end;
  } whole_map_iterate_end;
}

/****************************************************************************
  Complicated helper function for saving specials into a savegame.

  Specials are packed in four to a character in hex notation. 'index'
  specifies which set of specials are included in this character.
****************************************************************************/
static char sg_special_get(bv_special specials,
                           const enum tile_special_type *index)
{
  int i, bin = 0;

  for (i = 0; i < 4; i++) {
    enum tile_special_type sp = index[i];

    if (sp >= S_LAST) {
      break;
    }
    if (contains_special(specials, sp)) {
      bin |= (1 << i);
    }
  }

  return hex_chars[bin];
}

/****************************************************************************
  Helper function for saving bases into a savegame.

  Specials are packed in four to a character in hex notation. 'index'
  specifies which set of bases are included in this character.
****************************************************************************/
static char sg_bases_get(bv_bases bases, const int *index)
{
  int i, bin = 0;

  for (i = 0; i < 4; i++) {
    int base = index[i];

    if (base < 0) {
      break;
    }
    if (BV_ISSET(bases, base)) {
      bin |= (1 << i);
    }
  }

  return hex_chars[bin];
}

/****************************************************************************
  Helper function for saving roads into a savegame.

  Specials are packed in four to a character in hex notation. 'index'
  specifies which set of roads are included in this character.
****************************************************************************/
static char sg_roads_get(bv_roads roads, const int *index)
{
  int i, bin = 0;

  for (i = 0; i < 4; i++) {
    int road = index[i];

    if (road < 0) {
      break;
    }
    if (BV_ISSET(roads, road)) {
      bin |= (1 << i);
    }
  }

  return hex_chars[bin];
}

/****************************************************************************
  Return the identifier for the given resource.
****************************************************************************/
static char resource2char(const struct resource *presource)
{
  if (!presource) {
    return RESOURCE_NONE_IDENTIFIER;
  } else if (strcmp(untranslated_name(&presource->name), "Gold") == 0) {
    return '$';
  } else if (strcmp(untranslated_name(&presource->name), "Iron") == 0) {
    return '/';
  } else if (strcmp(untranslated_name(&presource->name), "?animals:Game") == 0) {
    return 'e';
  } else if (strcmp(untranslated_name(&presource->name), "Furs") == 0) {
    return 'u';
  } else if (strcmp(untranslated_name(&presource->name), "Coal") == 0) {
    return 'c';
  } else if (strcmp(untranslated_name(&presource->name), "Fish") == 0) {
    return 'y';
  } else if (strcmp(untranslated_name(&presource->name), "Fruit") == 0) {
    return 'f';
  } else if (strcmp(untranslated_name(&presource->name), "Gems") == 0) {
    return 'g';
  } else if (strcmp(untranslated_name(&presource->name), "Buffalo") == 0) {
    return 'b';
  } else if (strcmp(untranslated_name(&presource->name), "Wheat") == 0) {
    return 'j';
  } else if (strcmp(untranslated_name(&presource->name), "Oasis") == 0) {
    return 'o';
  } else if (strcmp(untranslated_name(&presource->name), "Peat") == 0) {
    return 'a';
  } else if (strcmp(untranslated_name(&presource->name), "Pheasant") == 0) {
    return 'p';
  } else if (strcmp(untranslated_name(&presource->name), "Resources") == 0) {
    return 'r';
  } else if (strcmp(untranslated_name(&presource->name), "Ivory") == 0) {
    return 'i';
  } else if (strcmp(untranslated_name(&presource->name), "Silk") == 0) {
    return 's';
  } else if (strcmp(untranslated_name(&presource->name), "Spice") == 0) {
    return 't';
  } else if (strcmp(untranslated_name(&presource->name), "Whales") == 0) {
    return 'v';
  } else if (strcmp(untranslated_name(&presource->name), "Wine") == 0) {
    return 'w';
  } else if (strcmp(untranslated_name(&presource->name), "Oil") == 0) {
    return 'x';
  } else {
    printf("%s\n", untranslated_name(&presource->name));
    return ' ';
  }
  return presource ? presource->identifier : RESOURCE_NONE_IDENTIFIER;
}

/****************************************************************************
  This returns an ascii hex value of the given half-byte of the binary
  integer. See ascii_hex2bin().
  example: bin2ascii_hex(0xa00, 2) == 'a'
****************************************************************************/
#define bin2ascii_hex(value, halfbyte_wanted) \
  hex_chars[((value) >> ((halfbyte_wanted) * 4)) & 0xf]

/****************************************************************************
  Converts number in to single character. This works to values up to ~70.
****************************************************************************/
static char num2char(unsigned int num)
{
  if (num >= strlen(num_chars)) {
    return '?';
  }

  return num_chars[num];
}

/****************************************************************************
  References the terrain character.  See terrains[].identifier
    example: terrain2char(T_ARCTIC) => 'a'
****************************************************************************/
static char terrain2char(const struct terrain *pterrain)
{
  if (pterrain == T_UNKNOWN) {
//     return TERRAIN_UNKNOWN_IDENTIFIER;
    return 'i';
  } else if (strcmp(untranslated_name(&pterrain->name), "Inaccessible") == 0) {
    return 'i';
  } else if (strcmp(untranslated_name(&pterrain->name), "Lake") == 0) {
    return '+';
  } else if (strcmp(untranslated_name(&pterrain->name), "Ocean") == 0) {
    return ' ';
  } else if (strcmp(untranslated_name(&pterrain->name), "Deep Ocean") == 0) {
    return ':';
  } else if (strcmp(untranslated_name(&pterrain->name), "Glacier") == 0) {
    return 'a';
  } else if (strcmp(untranslated_name(&pterrain->name), "Desert") == 0) {
    return 'd';
  } else if (strcmp(untranslated_name(&pterrain->name), "Forest") == 0) {
    return 'f';
  } else if (strcmp(untranslated_name(&pterrain->name), "Grassland") == 0) {
    return 'g';
  } else if (strcmp(untranslated_name(&pterrain->name), "Hills") == 0) {
    return 'h';
  } else if (strcmp(untranslated_name(&pterrain->name), "Jungle") == 0) {
    return 'j';
  } else if (strcmp(untranslated_name(&pterrain->name), "Mountains") == 0) {
    return 'm';
  } else if (strcmp(untranslated_name(&pterrain->name), "Plains") == 0) {
    return 'p';
  } else if (strcmp(untranslated_name(&pterrain->name), "Swamp") == 0) {
    return 's';
  } else if (strcmp(untranslated_name(&pterrain->name), "Tundra") == 0) {
    return 't';
  } else {
    printf("%p %s\n", pterrain, untranslated_name(&pterrain->name));
    return ' ';
  }
}

/*****************************************************************************
  Save technology in secfile entry called path_name.
*****************************************************************************/
static void technology_save(struct section_file *file,
                            const char* path, int plrno, Tech_type_id tech)
{
  char path_with_name[128];
  const char* name;

  fc_snprintf(path_with_name, sizeof(path_with_name),
              "%s_name", path);

  switch (tech) {
    case A_UNKNOWN: /* used by researching_saved */
       name = "";
       break;
    case A_NONE:
      name = "A_NONE";
      break;
    case A_UNSET:
      name = "A_UNSET";
      break;
    case A_FUTURE:
      name = "A_FUTURE";
      break;
    default:
      name = advance_rule_name(advance_by_number(tech));
      break;
  }

  secfile_insert_str(file, name, path_with_name, plrno);
}

/* =======================================================================
 * Load / save savefile data.
 * ======================================================================= */

/****************************************************************************
  Save '[savefile]'.
****************************************************************************/
static void sg_save_savefile(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Save savefile options. */
  sg_save_savefile_options(saving, savefile_options_default);

  secfile_insert_int(saving->file, compat[compat_current].version, "savefile.version");

  /* Save reason of the savefile generation. */
  secfile_insert_str(saving->file, saving->save_reason, "savefile.reason");

  /* Save rulesetdir at this point as this ruleset is required by this
   * savefile. */
  secfile_insert_str(saving->file, "LT49", "savefile.rulesetdir");

  /* Save improvement order in savegame, so we are not dependent on ruleset
   * order. If the game isn't started improvements aren't loaded so we can
   * not save the order. */
  secfile_insert_int(saving->file, improvement_count(),
                     "savefile.improvement_size");
  if (improvement_count() > 0) {
    const char* buf[improvement_count()];

    improvement_iterate(pimprove) {
      buf[improvement_index(pimprove)] = improvement_rule_name(pimprove);
    } improvement_iterate_end;

    secfile_insert_str_vec(saving->file, buf, improvement_count(),
                           "savefile.improvement_vector");
  }

  /* Save technology order in savegame, so we are not dependent on ruleset
   * order. If the game isn't started advances aren't loaded so we can not
   * save the order. */
  secfile_insert_int(saving->file, game.control.num_tech_types,
                     "savefile.technology_size");
  if (game.control.num_tech_types > 0) {
    const char* buf[game.control.num_tech_types];

    buf[A_NONE] = "A_NONE";
    advance_iterate(A_FIRST, a) {
      buf[advance_index(a)] = advance_rule_name(a);
    } advance_iterate_end;
    secfile_insert_str_vec(saving->file, buf, game.control.num_tech_types,
                           "savefile.technology_vector");
  }

  /* Save activities order in the savegame. */
  secfile_insert_int(saving->file, ACTIVITY_LAST,
                     "savefile.activities_size");
  if (ACTIVITY_LAST > 0) {
    const char **modname;
    int i = 0;
    int j;

    modname = fc_calloc(ACTIVITY_LAST, sizeof(*modname));

    for (j = 0; j < ACTIVITY_LAST; j++) {
      modname[i++] = unit_activity_name(j);
    }

    secfile_insert_str_vec(saving->file, modname,
                           ACTIVITY_LAST,
                           "savefile.activities_vector");
    free(modname);
  }

  /* Save trait order in savegame. */
  secfile_insert_int(saving->file, TRAIT_COUNT,
                     "savefile.trait_size");
  {
    const char **modname;
    enum trait tr;
    int j;

    modname = fc_calloc(TRAIT_COUNT, sizeof(*modname));

    for (tr = trait_begin(), j = 0; tr != trait_end(); tr = trait_next(tr), j++) {
      modname[j] = trait_name(tr);
    }

    secfile_insert_str_vec(saving->file, modname, TRAIT_COUNT,
                           "savefile.trait_vector");
    free(modname);
  }

  /* Save specials order in savegame. */
  secfile_insert_int(saving->file, S_LAST, "savefile.specials_size");
  {
    const char **modname;

    modname = fc_calloc(S_LAST, sizeof(*modname));
    tile_special_type_iterate(j) {
      modname[j] = special_rule_name(j);
    } tile_special_type_iterate_end;

    secfile_insert_str_vec(saving->file, modname, S_LAST,
                           "savefile.specials_vector");
    free(modname);
  }

  /* Save bases order in the savegame. */
  secfile_insert_int(saving->file, game.control.num_base_types,
                     "savefile.bases_size");
  if (game.control.num_base_types > 0) {
    const char **modname;
    int i = 0;

    modname = fc_calloc(game.control.num_base_types, sizeof(*modname));

    base_type_iterate(pbase) {
      modname[i++] = base_rule_name(pbase);
    } base_type_iterate_end;

    secfile_insert_str_vec(saving->file, modname,
                           game.control.num_base_types,
                           "savefile.bases_vector");
    free(modname);
  }

  /* Save roads order in the savegame. */
  secfile_insert_int(saving->file, game.control.num_road_types,
                     "savefile.roads_size");
  if (game.control.num_road_types > 0) {
    const char **modname;
    int i = 0;

    modname = fc_calloc(game.control.num_road_types, sizeof(*modname));

    road_type_iterate(proad) {
      modname[i++] = road_rule_name(proad);
    } road_type_iterate_end;

    secfile_insert_str_vec(saving->file, modname,
                           game.control.num_road_types,
                           "savefile.roads_vector");
    free(modname);
  }
}

/****************************************************************************
  Save options for this savegame. sg_load_savefile_options() is not defined.
****************************************************************************/
static void sg_save_savefile_options(struct savedata *saving,
                                     const char *option)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (option == NULL) {
    /* no additional option */
    return;
  }

  sz_strlcat(saving->secfile_options, option);
  secfile_replace_str(saving->file, saving->secfile_options,
                      "savefile.options");
}

/* =======================================================================
 * Load / save game status.
 * ======================================================================= */

/****************************************************************************
  Save '[game]'.
****************************************************************************/
static void sg_save_game(struct savedata *saving)
{
  int game_version;
  const char *user_message;
  enum server_states srv_state;
  char global_advances[game.control.num_tech_types + 1];
  int i;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  game_version = MAJOR_VERSION *10000 + MINOR_VERSION *100 + PATCH_VERSION;
  secfile_insert_int(saving->file, game_version, "game.version");

  /* Game state: once the game is no longer a new game (ie, has been
   * started the first time), it should always be considered a running
   * game for savegame purposes. */
  if (saving->scenario && !game.scenario.players) {
    srv_state = S_S_INITIAL;
  } else {
    srv_state = game.info.is_new_game ? server_state() : S_S_RUNNING;
  }
  secfile_insert_str(saving->file, server_states_name(srv_state),
                     "game.server_state");

  secfile_insert_str(saving->file, get_meta_patches_string(),
                     "game.meta_patches");
  secfile_insert_bool(saving->file, FALSE,
                      "game.meta_usermessage");
  user_message = get_user_meta_message_string();
  if (user_message != NULL) {
//     secfile_insert_str(saving->file, user_message, "game.meta_message");
  }
  secfile_insert_str(saving->file, meta_addr_port(), "game.meta_server");

  secfile_insert_str(saving->file, server.game_identifier, "game.id");
  secfile_insert_str(saving->file, srvarg.serverid, "game.serverid");

  secfile_insert_int(saving->file, game.info.skill_level,
                     "game.skill_level");
  secfile_insert_int(saving->file, game.info.phase_mode,
                     "game.phase_mode");
  secfile_insert_int(saving->file, 0,
                     "game.phase_mode_stored");
  secfile_insert_int(saving->file, game.info.phase,
                     "game.phase");
  secfile_insert_int(saving->file, 0,
                     "game.scoreturn");

  secfile_insert_int(saving->file, 0,
                     "game.timeoutint");
  secfile_insert_int(saving->file, 0,
                     "game.timeoutintinc");
  secfile_insert_int(saving->file, 0,
                     "game.timeoutinc");
  secfile_insert_int(saving->file, 0,
                     "game.timeoutincmult");
  secfile_insert_int(saving->file, 0,
                     "game.timeoutcounter");

  secfile_insert_int(saving->file, game.info.turn, "game.turn");
  secfile_insert_int(saving->file, game.info.year, "game.year");
  secfile_insert_bool(saving->file, game.info.year_0_hack,
                      "game.year_0_hack");

  secfile_insert_int(saving->file, game.info.globalwarming,
                     "game.globalwarming");
  secfile_insert_int(saving->file, game.info.heating,
                     "game.heating");
  secfile_insert_int(saving->file, game.info.warminglevel,
                     "game.warminglevel");

  secfile_insert_int(saving->file, game.info.nuclearwinter,
                     "game.nuclearwinter");
  secfile_insert_int(saving->file, game.info.cooling,
                     "game.cooling");
  secfile_insert_int(saving->file, game.info.coolinglevel,
                     "game.coolinglevel");

  /* Global advances. */
  for (i = 0; i < game.control.num_tech_types; i++) {
    global_advances[i] = game.info.global_advances[i] ? '1' : '0';
  }
  global_advances[i] = '\0';
  secfile_insert_str(saving->file, global_advances, "game.global_advances");

  if (!game_was_started()) {
    saving->save_players = FALSE;
  } else if (saving->scenario) {
    saving->save_players = game.scenario.players;
  } else {
    saving->save_players = TRUE;
  }
  secfile_insert_bool(saving->file, saving->save_players,
                      "game.save_players");
}

/* =======================================================================
 * Load / save random status.
 * ======================================================================= */

/****************************************************************************
  Save '[random]'.
****************************************************************************/
static void sg_save_random(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (fc_rand_is_init() && FALSE /*(!saving->scenario || game.server.save_options.save_random)*/) {
    int i;
    RANDOM_STATE rstate = fc_rand_state();

    secfile_insert_bool(saving->file, TRUE, "random.save");
    fc_assert(rstate.is_init);

    secfile_insert_int(saving->file, rstate.j, "random.index_J");
    secfile_insert_int(saving->file, rstate.k, "random.index_K");
    secfile_insert_int(saving->file, rstate.x, "random.index_X");

    for (i = 0; i < 8; i++) {
      char vec[100];

      fc_snprintf(vec, sizeof(vec),
                  "%8x %8x %8x %8x %8x %8x %8x", rstate.v[7 * i],
                  rstate.v[7 * i + 1], rstate.v[7 * i + 2],
                  rstate.v[7 * i + 3], rstate.v[7 * i + 4],
                  rstate.v[7 * i + 5], rstate.v[7 * i + 6]);
      secfile_insert_str(saving->file, vec, "random.table%d", i);
    }
  } else {
    secfile_insert_bool(saving->file, FALSE, "random.save");
  }
}

/* =======================================================================
 * Load / save lua script data.
 * ======================================================================= */

/****************************************************************************
  Save '[script]'.
****************************************************************************/
static void sg_save_script(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  script_server_state_save(saving->file);
}

/* =======================================================================
 * Load / save scenario data.
 * ======================================================================= */

/****************************************************************************
  Save '[scenario]'.
****************************************************************************/
static void sg_save_scenario(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (!saving->scenario || !game.scenario.is_scenario) {
    secfile_insert_bool(saving->file, FALSE, "scenario.is_scenario");
    return;
  }

  secfile_insert_bool(saving->file, TRUE, "scenario.is_scenario");

  /* Name is mandatory to the level that is saved even if empty. */
  secfile_insert_str(saving->file, game.scenario.name, "scenario.name");

  if (game.scenario.description[0] != '\0') {
    secfile_insert_str(saving->file, game.scenario.description,
                       "scenario.description");
  }

  secfile_insert_bool(saving->file, game.scenario.players, "scenario.players");
  secfile_insert_bool(saving->file, game.scenario.startpos_nations,
                      "scenario.startpos_nations");
}

/* =======================================================================
 * Load / save game settings.
 * ======================================================================= */

/****************************************************************************
  Save [settings].
****************************************************************************/
static void sg_save_settings(struct savedata *saving)
{
//   enum map_generator real_generator = MAPGEN_SCENARIO;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (saving->scenario) {
//     map.server.generator = MAPGEN_SCENARIO; /* We want a scenario. */
  }
  settings_game_save(saving->file, "settings");
  /* Restore real map generator. */
//   map.server.generator = real_generator;

  /* Add all compatibility settings here. */
}

/* =======================================================================
 * Load / save the main map.
 * ======================================================================= */

/****************************************************************************
  Save 'map'.
****************************************************************************/
static void sg_save_map(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (map_is_empty()) {
    /* No map. */
    return;
  }

  if (saving->scenario) {
    secfile_insert_bool(saving->file, TRUE, "map.have_huts");
  } else {
    secfile_insert_bool(saving->file, TRUE, "map.have_huts");
  }

  sg_save_map_tiles(saving);
  sg_save_map_startpos(saving);
  sg_save_map_tiles_bases(saving);
  sg_save_map_tiles_roads(saving);
//   if (!map.server.have_resources) {
//     if (map.server.have_rivers_overlay) {
      /* Save the rivers overlay map; this is a special case to allow
       * re-saving scenarios which have rivers overlay data. This only
       * applies if you don't have the rest of the specials. */
//       sg_save_savefile_options(saving, " riversoverlay");
//       sg_save_map_tiles_specials(saving, TRUE);
//     }
//   } else {
    sg_save_savefile_options(saving, " specials");
    sg_save_map_tiles_specials(saving, FALSE);
    sg_save_map_tiles_resources(saving);
//   }

  sg_save_map_owner(saving);
  sg_save_map_worked(saving);
  sg_save_map_known(saving);
}

/****************************************************************************
  Save all map tiles
****************************************************************************/
static void sg_save_map_tiles(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Save the terrain type. */
  SAVE_MAP_CHAR(ptile, terrain2char(ptile->terrain), saving->file,
                "map.t%04d");

  /* Save special tile sprites. */
  whole_map_iterate(ptile) {
    int nat_x, nat_y;

    index_to_native_pos(&nat_x, &nat_y, tile_index(ptile));
    if (ptile->spec_sprite) {
      secfile_insert_str(saving->file, ptile->spec_sprite,
                         "map.spec_sprite_%d_%d", nat_x, nat_y);
    }
    if (ptile->label != NULL) {
      secfile_insert_str(saving->file, ptile->label,
                         "map.label_%d_%d", nat_x, nat_y);
    }
  } whole_map_iterate_end;
}

/****************************************************************************
  Save information about bases on map
****************************************************************************/
static void sg_save_map_tiles_bases(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Save bases. */
  halfbyte_iterate_bases(j, game.control.num_base_types) {
    int mod[4];
    int l;

    for (l = 0; l < 4; l++) {
      if (4 * j + 1 > game.control.num_base_types) {
        mod[l] = -1;
      } else {
        mod[l] = 4 * j + l;
      }
    }
    SAVE_MAP_CHAR(ptile, sg_bases_get(ptile->bases, mod), saving->file,
                  "map.b%02d_%04d", j);
  } halfbyte_iterate_bases_end;
}

/****************************************************************************
  Save information about roads on map
****************************************************************************/
static void sg_save_map_tiles_roads(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* Save roads. */
  halfbyte_iterate_roads(j, game.control.num_road_types) {
    int mod[4];
    int l;

    for (l = 0; l < 4; l++) {
      if (4 * j + 1 > game.control.num_road_types) {
        mod[l] = -1;
      } else {
        mod[l] = 4 * j + l;
      }
    }
    SAVE_MAP_CHAR(ptile, sg_roads_get(ptile->roads, mod), saving->file,
                  "map.r%02d_%04d", j);
  } halfbyte_iterate_roads_end;
}

/****************************************************************************
  Save information about specials on map.
****************************************************************************/
static void sg_save_map_tiles_specials(struct savedata *saving,
                                       bool rivers_overlay)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  halfbyte_iterate_special(j, S_LAST) {
    enum tile_special_type mod[4];
    int l;

    for (l = 0; l < 4; l++) {
      if (rivers_overlay) {
        /* Save only rivers overlay. */
        mod[l] = (4 * j + l == S_OLD_RIVER) ? S_OLD_RIVER : S_LAST;
      } else {
        /* Save all specials. */
        mod[l] = MIN(4 * j + l, S_LAST);
      }
    }
    SAVE_MAP_CHAR(ptile, sg_special_get(ptile->special, mod), saving->file,
                  "map.spe%02d_%04d", j);
  } halfbyte_iterate_special_end;
}

/****************************************************************************
  Save information about resources on map.
****************************************************************************/
static void sg_save_map_tiles_resources(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  SAVE_MAP_CHAR(ptile, resource2char(ptile->resource), saving->file,
                "map.res%04d");
}

/****************************************************************************
  Save the map start positions.
****************************************************************************/
static void sg_save_map_startpos(struct savedata *saving)
{
  struct tile *ptile;
  const char SEPARATOR = '#';
  int i = 0;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

//   if (!game.server.save_options.save_starts) {
    return;
//   }

  secfile_insert_int(saving->file, map_startpos_count(),
                     "map.startpos_count");

  map_startpos_iterate(psp) {
    int nat_x, nat_y;

    ptile = startpos_tile(psp);

    index_to_native_pos(&nat_x, &nat_y, tile_index(ptile));
    secfile_insert_int(saving->file, nat_x, "map.startpos%d.x", i);
    secfile_insert_int(saving->file, nat_y, "map.startpos%d.y", i);

    secfile_insert_bool(saving->file, startpos_is_excluding(psp),
                        "map.startpos%d.exclude", i);
    if (startpos_allows_all(psp)) {
      secfile_insert_str(saving->file, "", "map.startpos%d.nations", i);
    } else {
      const struct nation_hash *nations = startpos_raw_nations(psp);
      char nation_names[MAX_LEN_NAME * nation_hash_size(nations)];

      nation_names[0] = '\0';
      nation_hash_iterate(nations, pnation) {
        if ('\0' == nation_names[0]) {
          fc_strlcpy(nation_names, nation_rule_name(pnation),
                     sizeof(nation_names));
        } else {
          cat_snprintf(nation_names, sizeof(nation_names),
                       "%c%s", SEPARATOR, nation_rule_name(pnation));
        }
      } nation_hash_iterate_end;
      secfile_insert_str(saving->file, nation_names,
                         "map.startpos%d.nations", i);
    }
    i++;
  } map_startpos_iterate_end;

  fc_assert(map_startpos_count() == i);
}

/****************************************************************************
  Save tile owner information
****************************************************************************/
static void sg_save_map_owner(struct savedata *saving)
{
  int x, y;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (saving->scenario && !saving->save_players) {
    /* Nothing to do for a scenario without saved players. */
    return;
  }

  /* Store owner and ownership source as plain numbers. */
  for (y = 0; y < map.ysize; y++) {
    char line[map.xsize * TOKEN_SIZE];

    line[0] = '\0';
    for (x = 0; x < map.xsize; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(x, y);

      if (!saving->save_players || tile_owner(ptile) == NULL) {
        strcpy(token, "-");
      } else {
        fc_snprintf(token, sizeof(token), "%d",
                    player_number(tile_owner(ptile)));
      }
      strcat(line, token);
      if (x + 1 < map.xsize) {
        strcat(line, ",");
      }
    }
    secfile_insert_str(saving->file, line, "map.owner%04d", y);
  }

  for (y = 0; y < map.ysize; y++) {
    char line[map.xsize * TOKEN_SIZE];

    line[0] = '\0';
    for (x = 0; x < map.xsize; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(x, y);

      if (ptile->claimer == NULL) {
        strcpy(token, "-");
      } else {
        fc_snprintf(token, sizeof(token), "%d", tile_index(ptile->claimer));
      }
      strcat(line, token);
      if (x + 1 < map.xsize) {
        strcat(line, ",");
      }
    }
    secfile_insert_str(saving->file, line, "map.source%04d", y);
  }
}

/****************************************************************************
  Save worked tiles information
****************************************************************************/
static void sg_save_map_worked(struct savedata *saving)
{
  int x, y;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (saving->scenario && !saving->save_players) {
    /* Nothing to do for a scenario without saved players. */
    return;
  }

  /* additionally save the tiles worked by the cities */
  for (y = 0; y < map.ysize; y++) {
    char line[map.xsize * TOKEN_SIZE];

    line[0] = '\0';
    for (x = 0; x < map.xsize; x++) {
      char token[TOKEN_SIZE];
      struct tile *ptile = native_pos_to_tile(x, y);
      struct city *pcity = tile_worked(ptile);

      if (pcity == NULL) {
        strcpy(token, "-");
      } else {
        fc_snprintf(token, sizeof(token), "%d", pcity->id);
      }
      strcat(line, token);
      if (x < map.xsize) {
        strcat(line, ",");
      }
    }
    secfile_insert_str(saving->file, line, "map.worked%04d", y);
  }
}

/****************************************************************************
  Save tile known status for whole map and all players
****************************************************************************/
static void sg_save_map_known(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if (!saving->save_players) {
    secfile_insert_bool(saving->file, FALSE, "game.save_known");
    return;
  } else {
    int lines = player_slot_max_used_number()/32 + 1;

    secfile_insert_bool(saving->file, TRUE,
                        "game.save_known");
    if (TRUE) {
      int j, p, l, i;
      unsigned int *known = fc_calloc(lines * MAP_INDEX_SIZE, sizeof(*known));

      /* HACK: we convert the data into a 32-bit integer, and then save it as
       * hex. */

      whole_map_iterate(ptile) {
        players_iterate(pplayer) {
          if (map_is_known(ptile, pplayer)) {
            p = player_index(pplayer);
            l = p / 32;
            known[l * MAP_INDEX_SIZE + tile_index(ptile)]
              |= (1u << (p % 32)); /* "p % 32" = "p - l * 32" */ 
          }
        } players_iterate_end;
      } whole_map_iterate_end;

      for (l = 0; l < lines; l++) {
        for (j = 0; j < 8; j++) {
          for (i = 0; i < 4; i++) {
            /* Only bother saving the map for this halfbyte if at least one
             * of the corresponding player slots is in use */
            if (player_slot_is_used(player_slot_by_number(l*32 + j*4 + i))) {
              /* put 4-bit segments of the 32-bit "known" field */
              SAVE_MAP_CHAR(ptile, bin2ascii_hex(known[l * MAP_INDEX_SIZE
                                                       + tile_index(ptile)], j),
                            saving->file, "map.k%02d_%04d", l * 8 + j);
              break;
            }
          }
        }
      }

      FC_FREE(known);
    }
  }
}

/* =======================================================================
 * Load / save player data.
 *
 * This is splitted into two parts as some data can only be loaded if the
 * number of players is known and the corresponding player slots are
 * defined.
 * ======================================================================= */

/****************************************************************************
  Save '[player]'.
****************************************************************************/
static void sg_save_players(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  if ((saving->scenario && !saving->save_players)
      || !game_was_started()) {
    /* Nothing to do for a scenario without saved players or a game in
     * INITIAL state. */
    return;
  }

  secfile_insert_int(saving->file, player_count(), "players.nplayers");

  /* Save destroyed wonders as bitvector. Note that improvement order
   * is saved in 'savefile.improvement.order'. */
  {
    char destroyed[B_LAST+1];

    improvement_iterate(pimprove) {
      if (is_great_wonder(pimprove)
          && great_wonder_is_destroyed(pimprove)) {
        destroyed[improvement_index(pimprove)] = '1';
      } else {
        destroyed[improvement_index(pimprove)] = '0';
      }
    } improvement_iterate_end;
    destroyed[improvement_count()] = '\0';
    secfile_insert_str(saving->file, destroyed,
                       "players.destroyed_wonders");
  }

  secfile_insert_int(saving->file, server.identity_number,
                     "players.identity_number_used");

  /* Save player order. */
  {
    int i = 0;
    shuffled_players_iterate(pplayer) {
      secfile_insert_int(saving->file, player_number(pplayer),
                         "players.shuffled_player_%d", i);
      i++;
    } shuffled_players_iterate_end;
  }

  /* Sort units. */
  unit_ordering_calc();

  /* Save players. */
  players_iterate(pplayer) {
    sg_save_player_main(saving, pplayer);
    sg_save_player_cities(saving, pplayer);
    sg_save_player_units(saving, pplayer);
    sg_save_player_attributes(saving, pplayer);
//     sg_save_player_vision(saving, pplayer);
  } players_iterate_end;
}

/****************************************************************************
  Main player data saving function.
****************************************************************************/
static void sg_save_player_main(struct savedata *saving,
                                struct player *plr)
{
  int i, plrno = player_number(plr);
  struct player_spaceship *ship = &plr->spaceship;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  secfile_insert_str(saving->file, "classic"/*ai_name(plr->ai)*/,
                     "player%d.ai_type", plrno);
  secfile_insert_str(saving->file, player_name(plr),
                     "player%d.name", plrno);
  secfile_insert_str(saving->file, plr->username,
                     "player%d.username", plrno);
  if (plr->rgb != NULL) {
    rgbcolor_save(saving->file, plr->rgb, "player%d.color", plrno);
  } else {
    /* Colorless players are ok in pregame */
    if (game_was_started()) {
      log_sg("Game has started, yet player %d has no color defined.", plrno);
    }
  }
  secfile_insert_str(saving->file, plr->ranked_username,
                     "player%d.ranked_username", plrno);
  secfile_insert_str(saving->file,
                     player_delegation_get(plr) ? player_delegation_get(plr)
                                                : "",
                     "player%d.delegation_username", plrno);
  secfile_insert_str(saving->file, nation_rule_name(nation_of_player(plr)),
                     "player%d.nation", plrno);
  secfile_insert_int(saving->file, plr->team ? team_index(plr->team) : -1,
                     "player%d.team_no", plrno);

  secfile_insert_str(saving->file,
                     government_rule_name(government_of_player(plr)),
                     "player%d.government_name", plrno);
  if (plr->target_government) {
    secfile_insert_str(saving->file,
                       government_rule_name(plr->target_government),
                       "player%d.target_government_name", plrno);
  }

  secfile_insert_str(saving->file, city_style_rule_name(plr->city_style),
                      "player%d.city_style_by_name", plrno);

  secfile_insert_bool(saving->file, plr->is_male,
                      "player%d.is_male", plrno);
  secfile_insert_bool(saving->file, plr->is_alive,
                      "player%d.is_alive", plrno);
  secfile_insert_bool(saving->file, plr->ai_controlled,
                      "player%d.ai.control", plrno);

  players_iterate(pplayer) {
    char buf[32];
    struct player_diplstate *ds = player_diplstate_get(plr, pplayer);

    i = player_index(pplayer);

    /* save diplomatic state */
    fc_snprintf(buf, sizeof(buf), "player%d.diplstate%d", plrno, i);

    secfile_insert_int(saving->file, ds->type,
                       "%s.type", buf);
    secfile_insert_int(saving->file, ds->max_state,
                       "%s.max_state", buf);
    secfile_insert_int(saving->file, ds->first_contact_turn,
                       "%s.first_contact_turn", buf);
    secfile_insert_int(saving->file, ds->turns_left,
                       "%s.turns_left", buf);
    secfile_insert_int(saving->file, ds->has_reason_to_cancel,
                       "%s.has_reason_to_cancel", buf);
    secfile_insert_int(saving->file, ds->contact_turns_left,
                       "%s.contact_turns_left", buf);
    secfile_insert_bool(saving->file, player_has_real_embassy(plr, pplayer),
                        "%s.embassy", buf);
    secfile_insert_bool(saving->file, gives_shared_vision(plr, pplayer),
                        "%s.gives_shared_vision", buf);
  } players_iterate_end;

  players_iterate(aplayer) {
    i = player_index(aplayer);
    /* save ai data */
    secfile_insert_int(saving->file, plr->ai_common.love[i],
                       "player%d.ai%d.love", plrno, i);
  } players_iterate_end;

  CALL_FUNC_EACH_AI(player_save, plr, saving->file, plrno);

  secfile_insert_int(saving->file, plr->ai_common.skill_level,
                     "player%d.ai.skill_level", plrno);
  secfile_insert_int(saving->file, plr->ai_common.barbarian_type,
                     "player%d.ai.is_barbarian", plrno);
  secfile_insert_int(saving->file, plr->economic.gold,
                     "player%d.gold", plrno);
  secfile_insert_int(saving->file, plr->economic.tax,
                     "player%d.rates.tax", plrno);
  secfile_insert_int(saving->file, plr->economic.science,
                     "player%d.rates.science", plrno);
  secfile_insert_int(saving->file, plr->economic.luxury,
                     "player%d.rates.luxury", plrno);

  technology_save(saving->file, "player%d.research.goal",
                  plrno, player_research_get(plr)->tech_goal);
  secfile_insert_int(saving->file, plr->server.bulbs_last_turn,
                     "player%d.research.bulbs_last_turn", plrno);
  secfile_insert_int(saving->file,
                     player_research_get(plr)->techs_researched,
                     "player%d.research.techs", plrno);
  secfile_insert_int(saving->file, player_research_get(plr)->future_tech,
                     "player%d.research.futuretech", plrno);
  secfile_insert_int(saving->file,
                     player_research_get(plr)->bulbs_researching_saved,
                     "player%d.research.bulbs_before", plrno);
  technology_save(saving->file, "player%d.research.saved", plrno,
                  player_research_get(plr)->researching_saved);
  secfile_insert_int(saving->file,
                     player_research_get(plr)->bulbs_researched,
                     "player%d.research.bulbs", plrno);
  technology_save(saving->file, "player%d.research.now", plrno,
                  player_research_get(plr)->researching);
  secfile_insert_bool(saving->file, player_research_get(plr)->got_tech,
                      "player%d.research.got_tech", plrno);

  /* Save technology lists as bytevector. Note that technology order is
   * saved in savefile.technology.order */
  {
    char invs[A_LAST+1];
    advance_index_iterate(A_NONE, tech_id) {
      invs[tech_id] = (player_invention_state(plr, tech_id) == TECH_KNOWN)
                      ? '1' : '0';
    } advance_index_iterate_end;
    invs[game.control.num_tech_types] = '\0';
    secfile_insert_str(saving->file, invs, "player%d.research.done", plrno);
  }

  /* Save traits */
  {
    enum trait tr;
    int j;

    for (tr = trait_begin(), j = 0; tr != trait_end(); tr = trait_next(tr), j++) {
      secfile_insert_int(saving->file, /*plr->ai_common.traits[tr].mod*/0,
                         "player%d.trait.mod%d", plrno, j);
    }
  }

  /* Called 'capital' in the savefile for historical reasons */
  secfile_insert_bool(saving->file, plr->server.got_first_city,
                      "player%d.capital", plrno);
  secfile_insert_int(saving->file, plr->revolution_finishes,
                     "player%d.revolution_finishes", plrno);

  /* Unit statistics. */
  secfile_insert_int(saving->file, plr->score.units_built,
                     "player%d.units_built", plrno);
  secfile_insert_int(saving->file, plr->score.units_killed,
                     "player%d.units_killed", plrno);
  secfile_insert_int(saving->file, plr->score.units_lost,
                     "player%d.units_lost", plrno);

  /* Save space ship status. */
  secfile_insert_int(saving->file, ship->state, "player%d.spaceship.state",
                     plrno);
  if (ship->state != SSHIP_NONE) {
    char buf[32];
    char st[NUM_SS_STRUCTURALS+1];
    int i;

    fc_snprintf(buf, sizeof(buf), "player%d.spaceship", plrno);

    secfile_insert_int(saving->file, ship->structurals,
                       "%s.structurals", buf);
    secfile_insert_int(saving->file, ship->components,
                       "%s.components", buf);
    secfile_insert_int(saving->file, ship->modules,
                       "%s.modules", buf);
    secfile_insert_int(saving->file, ship->fuel, "%s.fuel", buf);
    secfile_insert_int(saving->file, ship->propulsion, "%s.propulsion", buf);
    secfile_insert_int(saving->file, ship->habitation, "%s.habitation", buf);
    secfile_insert_int(saving->file, ship->life_support,
                       "%s.life_support", buf);
    secfile_insert_int(saving->file, ship->solar_panels,
                       "%s.solar_panels", buf);

    for(i = 0; i < NUM_SS_STRUCTURALS; i++) {
      st[i] = BV_ISSET(ship->structure, i) ? '1' : '0';
    }
    st[i] = '\0';
    secfile_insert_str(saving->file, st, "%s.structure", buf);
    if (ship->state >= SSHIP_LAUNCHED) {
      secfile_insert_int(saving->file, ship->launch_year,
                         "%s.launch_year", buf);
    }
  }

  /* Save lost wonders info. */
  {
    char lost[B_LAST+1];

    improvement_iterate(pimprove) {
      if (is_wonder(pimprove) && wonder_is_lost(plr, pimprove)) {
        lost[improvement_index(pimprove)] = '1';
      } else {
        lost[improvement_index(pimprove)] = '0';
      }
    } improvement_iterate_end;
    lost[improvement_count()] = '\0';
    secfile_insert_str(saving->file, lost,
                       "player%d.lost_wonders", plrno);
  }
}

/****************************************************************************
  Save cities data
****************************************************************************/
static void sg_save_player_cities(struct savedata *saving,
                                  struct player *plr)
{
  int wlist_max_length = 0;
  int i = 0;
  int plrno = player_number(plr);
  bool nations[MAX_NUM_PLAYER_SLOTS];

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  secfile_insert_int(saving->file, city_list_size(plr->cities),
                     "player%d.ncities", plrno);

  if (game.info.citizen_nationality) {
    /* Initialise the nation list for the citizens information. */
    player_slots_iterate(pslot) {
      nations[player_slot_index(pslot)] = FALSE;
    } player_slots_iterate_end;
  }

  /* First determine lenght of longest worklist and the nations we have. */
  city_list_iterate(plr->cities, pcity) {
    /* Check the sanity of the city. */
    city_refresh(pcity);
    sanity_check_city(pcity);

    if (pcity->worklist.length > wlist_max_length) {
      wlist_max_length = pcity->worklist.length;
    }

    if (game.info.citizen_nationality) {
      /* Find all nations of the citizens,*/
      players_iterate(pplayer) {
        if (!nations[player_index(pplayer)]
            && citizens_nation_get(pcity, pplayer->slot) != 0) {
          nations[player_index(pplayer)] = TRUE;
        }
      } players_iterate_end;
    }
  } city_list_iterate_end;

  city_list_iterate(plr->cities, pcity) {
    struct tile *pcenter = city_tile(pcity);
    char impr_buf[MAX_NUM_ITEMS + 1];
    char buf[32];
    int j, nat_x, nat_y;

    fc_snprintf(buf, sizeof(buf), "player%d.c%d", plrno, i);


    index_to_native_pos(&nat_x, &nat_y, tile_index(pcenter));
    secfile_insert_int(saving->file, nat_y, "%s.y", buf);
    secfile_insert_int(saving->file, nat_x, "%s.x", buf);

    secfile_insert_int(saving->file, pcity->id, "%s.id", buf);

    secfile_insert_int(saving->file, player_number(pcity->original),
                       "%s.original", buf);
    secfile_insert_int(saving->file, city_size_get(pcity), "%s.size", buf);

    specialist_type_iterate(sp) {
      secfile_insert_int(saving->file, pcity->specialists[sp], "%s.n%s", buf,
                         specialist_rule_name(specialist_by_number(sp)));
    } specialist_type_iterate_end;

    for (j = 0; j < MAX_TRADE_ROUTES; j++) {
      secfile_insert_int(saving->file, pcity->trade[j], "%s.traderoute%d",
                         buf, j);
    }

    secfile_insert_int(saving->file, pcity->food_stock, "%s.food_stock",
                       buf);
    secfile_insert_int(saving->file, pcity->shield_stock, "%s.shield_stock",
                       buf);

    secfile_insert_int(saving->file, pcity->airlift, "%s.airlift",
                       buf);
    secfile_insert_bool(saving->file, pcity->was_happy, "%s.was_happy",
                        buf);
    secfile_insert_int(saving->file, pcity->turn_plague, "%s.turn_plague",
                       buf);

    secfile_insert_int(saving->file, pcity->anarchy, "%s.anarchy", buf);
    secfile_insert_int(saving->file, pcity->rapture, "%s.rapture", buf);
    secfile_insert_int(saving->file, pcity->server.steal, "%s.steal", buf);

    secfile_insert_int(saving->file, pcity->turn_founded, "%s.turn_founded",
                       buf);
    if (pcity->turn_founded == game.info.turn) {
      j = -1; /* undocumented hack */
    } else {
      fc_assert(pcity->did_buy == TRUE || pcity->did_buy == FALSE);
      j = pcity->did_buy ? 1 : 0;
    }
    secfile_insert_int(saving->file, j, "%s.did_buy", buf);
    secfile_insert_bool(saving->file, pcity->did_sell, "%s.did_sell", buf);
    secfile_insert_int(saving->file, pcity->turn_last_built,
                       "%s.turn_last_built", buf);

    /* for visual debugging, variable length strings together here */
    secfile_insert_str(saving->file, city_name(pcity), "%s.name", buf);

    secfile_insert_str(saving->file, universal_type_rule_name(&pcity->production),
                       "%s.currently_building_kind", buf);
    secfile_insert_str(saving->file, universal_rule_name(&pcity->production),
                       "%s.currently_building_name", buf);

    secfile_insert_str(saving->file, universal_type_rule_name(&pcity->changed_from),
                       "%s.changed_from_kind", buf);
    secfile_insert_str(saving->file, universal_rule_name(&pcity->changed_from),
                       "%s.changed_from_name", buf);

    secfile_insert_int(saving->file, pcity->before_change_shields,
                       "%s.before_change_shields", buf);
    secfile_insert_int(saving->file, pcity->caravan_shields,
                       "%s.caravan_shields", buf);
    secfile_insert_int(saving->file, pcity->disbanded_shields,
                       "%s.disbanded_shields", buf);
    secfile_insert_int(saving->file, pcity->last_turns_shield_surplus,
                       "%s.last_turns_shield_surplus", buf);

    /* Save the squared city radius and all tiles within the corresponing
     * city map. */
    secfile_insert_int(saving->file, pcity->city_radius_sq,
                       "player%d.c%d.city_radius_sq", plrno, i);
    /* The tiles worked by the city are saved using the main map.
     * See also sg_save_map_worked(). */

    /* Save improvement list as bytevector. Note that improvement order
     * is saved in savefile.improvement_order. */
    improvement_iterate(pimprove) {
      impr_buf[improvement_index(pimprove)]
        = (pcity->built[improvement_index(pimprove)].turn <= I_NEVER) ? '0'
                                                                      : '1';
    } improvement_iterate_end;
    impr_buf[improvement_count()] = '\0';
    sg_failure_ret(strlen(impr_buf) < sizeof(impr_buf),
                   "Invalid size of the improvement vector (%s.improvements: "
                   "%lu < %lu).", buf, (long unsigned int) strlen(impr_buf),
                   (long unsigned int) sizeof(impr_buf));
    secfile_insert_str(saving->file, impr_buf, "%s.improvements", buf);

    worklist_save(saving->file, &pcity->worklist, wlist_max_length, "%s",
                  buf);

    for (j = 0; j < CITYO_LAST; j++) {
      secfile_insert_bool(saving->file, BV_ISSET(pcity->city_options, j),
                          "%s.option%d", buf, j);
    }

    CALL_FUNC_EACH_AI(city_save, saving->file, pcity, buf);

    if (game.info.citizen_nationality) {
      /* Save nationality of the citizens,*/
      players_iterate(pplayer) {
        if (nations[player_index(pplayer)]) {
          secfile_insert_int(saving->file,
                             citizens_nation_get(pcity, pplayer->slot),
                             "%s.citizen%d", buf, player_index(pplayer));
        }
      } players_iterate_end;
    }

    i++;
  } city_list_iterate_end;
}

/****************************************************************************
  Save unit data
****************************************************************************/
static void sg_save_player_units(struct savedata *saving,
                                 struct player *plr)
{
  int i = 0;

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  secfile_insert_int(saving->file, unit_list_size(plr->units),
                     "player%d.nunits", player_number(plr));

  unit_list_iterate(plr->units, punit) {
    char buf[32];
    char dirbuf[2] = " ";
    int nat_x, nat_y;

    fc_snprintf(buf, sizeof(buf), "player%d.u%d", player_number(plr), i);
    dirbuf[0] = dir2char(punit->facing);
    secfile_insert_int(saving->file, punit->id, "%s.id", buf);

    index_to_native_pos(&nat_x, &nat_y, tile_index(unit_tile(punit)));
    secfile_insert_int(saving->file, nat_x, "%s.x", buf);
    secfile_insert_int(saving->file, nat_y, "%s.y", buf);

    secfile_insert_str(saving->file, dirbuf, "%s.facing", buf);
    if (game.info.citizen_nationality) {
      secfile_insert_int(saving->file, player_number(unit_nationality(punit)),
                         "%s.nationality", buf);
    }
    secfile_insert_int(saving->file, punit->veteran, "%s.veteran", buf);
    secfile_insert_int(saving->file, punit->hp, "%s.hp", buf);
    secfile_insert_int(saving->file, punit->homecity, "%s.homecity", buf);
    secfile_insert_str(saving->file, unit_rule_name(punit),
                       "%s.type_by_name", buf);

    secfile_insert_int(saving->file, punit->activity, "%s.activity", buf);
    secfile_insert_int(saving->file, punit->activity_count,
                       "%s.activity_count", buf);
    if (punit->activity_target.type == ATT_SPECIAL) {
      secfile_insert_int(saving->file, punit->activity_target.obj.spe,
                         "%s.activity_target", buf);
    } else {
      secfile_insert_int(saving->file, S_LAST,
                         "%s.activity_target", buf);
    }
    if (punit->activity_target.type == ATT_BASE) {
      secfile_insert_int(saving->file, punit->activity_target.obj.base,
                         "%s.activity_base", buf);
    } else {
      secfile_insert_int(saving->file, BASE_NONE,
                         "%s.activity_base", buf);
    }
    if (punit->activity_target.type == ATT_ROAD) {
      secfile_insert_int(saving->file, punit->activity_target.obj.road,
                         "%s.activity_road", buf);
    } else {
      secfile_insert_int(saving->file, ROAD_NONE,
                         "%s.activity_road", buf);
    }
    secfile_insert_int(saving->file, punit->changed_from,
                       "%s.changed_from", buf);
    secfile_insert_int(saving->file, punit->changed_from_count,
                       "%s.changed_from_count", buf);
    if (punit->changed_from_target.type == ATT_SPECIAL) {
      secfile_insert_int(saving->file, punit->changed_from_target.obj.spe,
                         "%s.changed_from_target", buf);
    } else {
      secfile_insert_int(saving->file, S_LAST,
                         "%s.changed_from_target", buf);
    }
    if (punit->changed_from_target.type == ATT_BASE) {
      secfile_insert_int(saving->file, punit->changed_from_target.obj.base,
                         "%s.changed_from_base", buf);
    } else {
      secfile_insert_int(saving->file, BASE_NONE,
                         "%s.changed_from_base", buf);
    }
    if (punit->changed_from_target.type == ATT_ROAD) {
      secfile_insert_int(saving->file, punit->changed_from_target.obj.road,
                         "%s.changed_from_road", buf);
    } else {
      secfile_insert_int(saving->file, ROAD_NONE,
                         "%s.changed_from_road", buf);
    }
    secfile_insert_bool(saving->file, punit->done_moving,
                        "%s.done_moving", buf);
    secfile_insert_int(saving->file, punit->moves_left, "%s.moves", buf);
    secfile_insert_int(saving->file, punit->fuel, "%s.fuel", buf);
    secfile_insert_int(saving->file, punit->server.birth_turn,
                      "%s.born", buf);
    secfile_insert_int(saving->file, punit->battlegroup,
                       "%s.battlegroup", buf);

    if (punit->goto_tile) {
      index_to_native_pos(&nat_x, &nat_y, tile_index(punit->goto_tile));
      secfile_insert_bool(saving->file, TRUE, "%s.go", buf);
      secfile_insert_int(saving->file, nat_x, "%s.goto_x", buf);
      secfile_insert_int(saving->file, nat_y, "%s.goto_y", buf);
    } else {
      secfile_insert_bool(saving->file, FALSE, "%s.go", buf);
      /* Set this values to allow saving it as table. */
      secfile_insert_int(saving->file, 0, "%s.goto_x", buf);
      secfile_insert_int(saving->file, 0, "%s.goto_y", buf);
    }

    secfile_insert_bool(saving->file, punit->ai_controlled,
                        "%s.ai", buf);

    /* Save AI data of the unit. */
    CALL_FUNC_EACH_AI(unit_save, saving->file, punit, buf);

    secfile_insert_int(saving->file, punit->server.ord_map,
                       "%s.ord_map", buf);
    secfile_insert_int(saving->file, punit->server.ord_city,
                       "%s.ord_city", buf);
    secfile_insert_bool(saving->file, punit->moved, "%s.moved", buf);
    secfile_insert_bool(saving->file, punit->paradropped,
                        "%s.paradropped", buf);
    secfile_insert_int(saving->file, unit_transport_get(punit)
                                     ? unit_transport_get(punit)->id : -1,
                       "%s.transported_by", buf);

    if (punit->has_orders) {
      int len = punit->orders.length, j;
      char orders_buf[len + 1], dir_buf[len + 1];
      char act_buf[len + 1], base_buf[len + 1];
      char road_buf[len + 1];

      secfile_insert_int(saving->file, len, "%s.orders_length", buf);
      secfile_insert_int(saving->file, punit->orders.index,
                         "%s.orders_index", buf);
      secfile_insert_bool(saving->file, punit->orders.repeat,
                          "%s.orders_repeat", buf);
      secfile_insert_bool(saving->file, punit->orders.vigilant,
                          "%s.orders_vigilant", buf);
      secfile_insert_bool(saving->file,
                          punit->server.last_order_move_is_safe,
                          "%s.orders_last_move_safe", buf);

      for (j = 0; j < len; j++) {
        orders_buf[j] = order2char(punit->orders.list[j].order);
        dir_buf[j] = '?';
        act_buf[j] = '?';
        base_buf[j] = '?';
        road_buf[j] = '?';
        switch (punit->orders.list[j].order) {
        case ORDER_MOVE:
          dir_buf[j] = dir2char(punit->orders.list[j].dir);
          break;
        case ORDER_ACTIVITY:
          if (punit->orders.list[j].activity == ACTIVITY_BASE) {
            base_buf[j] = num2char(punit->orders.list[j].base);
          } else if (punit->orders.list[j].activity == ACTIVITY_GEN_ROAD) {
            road_buf[j] = num2char(punit->orders.list[j].road);
          }
          act_buf[j] = activity2char(punit->orders.list[j].activity);
          break;
        case ORDER_FULL_MP:
        case ORDER_BUILD_CITY:
        case ORDER_DISBAND:
        case ORDER_BUILD_WONDER:
        case ORDER_TRADE_ROUTE:
        case ORDER_HOMECITY:
        case ORDER_LAST:
          break;
        }
      }
      orders_buf[len] = dir_buf[len] = act_buf[len] = base_buf[len] = '\0';
      road_buf[len] = '\0';

      secfile_insert_str(saving->file, orders_buf, "%s.orders_list", buf);
      secfile_insert_str(saving->file, dir_buf, "%s.dir_list", buf);
      secfile_insert_str(saving->file, act_buf, "%s.activity_list", buf);
      secfile_insert_str(saving->file, base_buf, "%s.base_list", buf);
      secfile_insert_str(saving->file, road_buf, "%s.road_list", buf);
    } else {
      /* Put all the same fields into the savegame - otherwise the
       * registry code can't correctly use a tabular format and the
       * savegame will be bigger. */
      secfile_insert_int(saving->file, 0, "%s.orders_length", buf);
      secfile_insert_int(saving->file, 0, "%s.orders_index", buf);
      secfile_insert_bool(saving->file, FALSE, "%s.orders_repeat", buf);
      secfile_insert_bool(saving->file, FALSE, "%s.orders_vigilant", buf);
      secfile_insert_bool(saving->file, FALSE,
                          "%s.orders_last_move_safe", buf);
      secfile_insert_str(saving->file, "-", "%s.orders_list", buf);
      secfile_insert_str(saving->file, "-", "%s.dir_list", buf);
      secfile_insert_str(saving->file, "-", "%s.activity_list", buf);
      secfile_insert_str(saving->file, "-", "%s.base_list", buf);
      secfile_insert_str(saving->file, "-", "%s.road_list", buf);
    }

    i++;
  } unit_list_iterate_end;
}

/****************************************************************************
  Save player (client) attributes data.
****************************************************************************/
static void sg_save_player_attributes(struct savedata *saving,
                                      struct player *plr)
{
  int plrno = player_number(plr);

  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  /* This is a big heap of opaque data from the client.  Although the binary
   * format is not user editable, keep the lines short enough for debugging,
   * and hope that data compression will keep the file a reasonable size.
   * Note that the "quoted" format is a multiple of 3.
   */
#define PART_SIZE (3*256)
#define PART_ADJUST (3)
  if (plr->attribute_block.data) {
    char part[PART_SIZE + PART_ADJUST];
    int parts;
    int current_part_nr;
    char *quoted = quote_block(plr->attribute_block.data,
                               plr->attribute_block.length);
    char *quoted_at = strchr(quoted, ':');
    size_t bytes_left = strlen(quoted);
    size_t bytes_at_colon = 1 + (quoted_at - quoted);
    size_t bytes_adjust = bytes_at_colon % PART_ADJUST;

    secfile_insert_int(saving->file, plr->attribute_block.length,
                       "player%d.attribute_v2_block_length", plrno);
    secfile_insert_int(saving->file, bytes_left,
                       "player%d.attribute_v2_block_length_quoted", plrno);

    /* Try to wring some compression efficiencies out of the "quoted" format.
     * The first line has a variable length decimal, mis-aligning triples.
     */
    if ((bytes_left - bytes_adjust) > PART_SIZE) {
      /* first line can be longer */
      parts = 1 + (bytes_left - bytes_adjust - 1) / PART_SIZE;
    } else {
      parts = 1;
    }

    secfile_insert_int(saving->file, parts,
                       "player%d.attribute_v2_block_parts", plrno);

    if (parts > 1) {
      size_t size_of_current_part = PART_SIZE + bytes_adjust;

      /* first line can be longer */
      memcpy(part, quoted, size_of_current_part);
      part[size_of_current_part] = '\0';
      secfile_insert_str(saving->file, part,
                         "player%d.attribute_v2_block_data.part%d",
                         plrno, 0);
      bytes_left -= size_of_current_part;
      quoted_at = &quoted[size_of_current_part];
      current_part_nr = 1;
    } else {
      quoted_at = quoted;
      current_part_nr = 0;
    }

    for (; current_part_nr < parts; current_part_nr++) {
      size_t size_of_current_part = MIN(bytes_left, PART_SIZE);

      memcpy(part, quoted_at, size_of_current_part);
      part[size_of_current_part] = '\0';
      secfile_insert_str(saving->file, part,
                         "player%d.attribute_v2_block_data.part%d",
                         plrno,
                         current_part_nr);
      bytes_left -= size_of_current_part;
      quoted_at = &quoted_at[size_of_current_part];
    }
    fc_assert(bytes_left == 0);
    free(quoted);
  }
#undef PART_ADJUST
#undef PART_SIZE
}

/****************************************************************************
  Save vision data
****************************************************************************/
// static void sg_save_player_vision(struct savedata *saving,
//                                   struct player *plr)
// {
//   int i, plrno = player_number(plr);
//
//   /* Check status and return if not OK (sg_success != TRUE). */
//   sg_check_ret();
//
//   if (!game.info.fogofwar /*|| !game.server.save_options.save_private_map*/) {
//     /* The player can see all, there's no reason to save the private map. */
//     return;
//   }
//
//   if (!plr->is_alive) {
//     /* Nothing to save. */
//     return;
//   }
//
//   /* Save the map (terrain). */
//   SAVE_MAP_CHAR(ptile,
//                 terrain2char(map_get_player_tile(ptile, plr)->terrain),
//                 saving->file, "player%d.map_t%04d", plrno);
//
//   /* Save the map (resources). */
//   SAVE_MAP_CHAR(ptile,
//                 resource2char(map_get_player_tile(ptile, plr)->resource),
//                 saving->file, "player%d.map_res%04d", plrno);
//
// //   if (game.server.foggedborders) {
//     /* Save the map (borders). */
//     int x, y;
//
//     for (y = 0; y < map.ysize; y++) {
//       char line[map.xsize * TOKEN_SIZE];
//
//       line[0] = '\0';
//       for (x = 0; x < map.xsize; x++) {
//         char token[TOKEN_SIZE];
//         struct tile *ptile = native_pos_to_tile(x, y);
//         struct player_tile *plrtile = map_get_player_tile(ptile, plr);
//
//         if (plrtile == NULL || plrtile->owner == NULL) {
//           strcpy(token, "-");
//         } else {
//           fc_snprintf(token, sizeof(token), "%d",
//                       player_number(plrtile->owner));
//         }
//         strcat(line, token);
//         if (x < map.xsize) {
//           strcat(line, ",");
//         }
//       }
//       secfile_insert_str(saving->file, line, "player%d.map_owner%04d",
//                          plrno, y);
// //     }
//   }
//
//   /* Save the map (specials). */
//   halfbyte_iterate_special(j, S_LAST) {
//     enum tile_special_type mod[4];
//     int l;
//
//     for (l = 0; l < 4; l++) {
//       mod[l] = MIN(4 * j + l, S_LAST);
//     }
//     SAVE_MAP_CHAR(ptile,
//                   sg_special_get(map_get_player_tile(ptile, plr)->special,
//                                  mod), saving->file,
//                   "player%d.map_spe%02d_%04d", plrno, j);
//   } halfbyte_iterate_special_end;
//
//   /* Save the map (bases). */
//   halfbyte_iterate_bases(j, game.control.num_base_types) {
//     int mod[4];
//     int l;
//
//     for (l = 0; l < 4; l++) {
//       if (4 * j + 1 > game.control.num_base_types) {
//         mod[l] = -1;
//       } else {
//         mod[l] = 4 * j + l;
//       }
//     }
//
//     SAVE_MAP_CHAR(ptile,
//                   sg_bases_get(map_get_player_tile(ptile, plr)->bases, mod),
//                   saving->file, "player%d.map_b%02d_%04d", plrno, j);
//   } halfbyte_iterate_bases_end;
//
//   /* Save the map (roads). */
//   halfbyte_iterate_roads(j, game.control.num_road_types) {
//     int mod[4];
//     int l;
//
//     for (l = 0; l < 4; l++) {
//       if (4 * j + 1 > game.control.num_road_types) {
//         mod[l] = -1;
//       } else {
//         mod[l] = 4 * j + l;
//       }
//     }
//
//     SAVE_MAP_CHAR(ptile,
//                   sg_roads_get(map_get_player_tile(ptile, plr)->roads, mod),
//                   saving->file, "player%d.map_r%02d_%04d", plrno, j);
//   } halfbyte_iterate_roads_end;
//
//   /* Save the map (update time). */
//   for (i = 0; i < 4; i++) {
//     /* put 4-bit segments of 16-bit "updated" field */
//     SAVE_MAP_CHAR(ptile,
//                   bin2ascii_hex(
//                     map_get_player_tile(ptile, plr)->last_updated, i),
//                   saving->file, "player%d.map_u%02d_%04d", plrno, i);
//   }
//
//   /* Save known cities. */
//   i = 0;
//   whole_map_iterate(ptile) {
//     struct vision_site *pdcity = map_get_player_city(ptile, plr);
//     char impr_buf[MAX_NUM_ITEMS + 1];
//     char buf[32];
//
//     fc_snprintf(buf, sizeof(buf), "player%d.dc%d", plrno, i);
//
//     if (NULL != pdcity && plr != vision_site_owner(pdcity)) {
//       int nat_x, nat_y;
//
//       index_to_native_pos(&nat_x, &nat_y, tile_index(ptile));
//       secfile_insert_int(saving->file, nat_y, "%s.y", buf);
//       secfile_insert_int(saving->file, nat_x, "%s.x", buf);
//
//       secfile_insert_int(saving->file, pdcity->identity, "%s.id", buf);
//       secfile_insert_int(saving->file, player_number(vision_site_owner(pdcity)),
//                          "%s.owner", buf);
//
//       secfile_insert_int(saving->file, vision_site_size_get(pdcity),
//                          "%s.size", buf);
//       secfile_insert_bool(saving->file, pdcity->occupied,
//                           "%s.occupied", buf);
//       secfile_insert_bool(saving->file, pdcity->walls, "%s.walls", buf);
//       secfile_insert_bool(saving->file, pdcity->happy, "%s.happy", buf);
//       secfile_insert_bool(saving->file, pdcity->unhappy, "%s.unhappy", buf);
//       secfile_insert_int(saving->file, pdcity->city_image, "%s.city_image", buf);
//
//       /* Save improvement list as bitvector. Note that improvement order
//        * is saved in savefile.improvement.order. */
//       improvement_iterate(pimprove) {
//         impr_buf[improvement_index(pimprove)]
//           = BV_ISSET(pdcity->improvements, improvement_index(pimprove))
//             ? '1' : '0';
//       } improvement_iterate_end;
//       impr_buf[improvement_count()] = '\0';
//       sg_failure_ret(strlen(impr_buf) < sizeof(impr_buf),
//                      "Invalid size of the improvement vector (%s.improvements: "
//                      "%lu < %lu).", buf, (long unsigned int) strlen(impr_buf),
//                      (long unsigned int) sizeof(impr_buf));
//       secfile_insert_str(saving->file, impr_buf, "%s.improvements", buf);
//       secfile_insert_str(saving->file, pdcity->name, "%s.name", buf);
//
//       i++;
//     }
//   } whole_map_iterate_end;
//
//   secfile_insert_int(saving->file, i, "player%d.dc_total", plrno);
// }

/* =======================================================================
 * Load / save the event cache. Should be the last thing to do.
 * ======================================================================= */

/****************************************************************************
  Save '[event_cache]'.
****************************************************************************/
// static void sg_save_event_cache(struct savedata *saving)
// {
//   /* Check status and return if not OK (sg_success != TRUE). */
//   sg_check_ret();
//
//   if (saving->scenario) {
//     /* Do _not_ save events in a scenario. */
//     return;
//   }
//
//   event_cache_save(saving->file, "event_cache");
// }

/* =======================================================================
 * Load / save the mapimg definitions.
 * ======================================================================= */

/****************************************************************************
  Save '[mapimg]'.
****************************************************************************/
static void sg_save_mapimg(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();

  secfile_insert_int(saving->file, mapimg_count(), "mapimg.count");
  if (mapimg_count() > 0) {
    int i;

    for (i = 0; i < mapimg_count(); i++) {
      char buf[MAX_LEN_MAPDEF];

      mapimg_id2str(i, buf, sizeof(buf));
      secfile_insert_str(saving->file, buf, "mapimg.mapdef%d", i);
    }
  }
}

/* =======================================================================
 * Sanity checks for loading / saving a game.
 * ======================================================================= */

/****************************************************************************
  Sanity check for saved game.
****************************************************************************/
static void sg_save_sanitycheck(struct savedata *saving)
{
  /* Check status and return if not OK (sg_success != TRUE). */
  sg_check_ret();
}
