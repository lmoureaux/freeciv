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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <gtk/gtk.h>

#include <gdk_imlib.h>

#include "game.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "unit.h"
#include "version.h"

#include "climisc.h"
#include "colors.h"
#include "mapview_g.h"
#include "tilespec.h"

#include "graphics.h"

#include "goto_cursor.xbm"
#include "goto_cursor_mask.xbm"
#include "drop_cursor.xbm"
#include "drop_cursor_mask.xbm"
#include "nuke_cursor.xbm"
#include "nuke_cursor_mask.xbm"

extern GtkWidget *	toplevel;
extern GdkWindow *	root_window;
extern int 		use_solid_color_behind_units;

SPRITE *		intro_gfx_sprite;
SPRITE *		radar_gfx_sprite;

GdkCursor *		goto_cursor;
GdkCursor *		drop_cursor;
GdkCursor *		nuke_cursor;

extern GdkFont *	main_font;

extern GdkGC *		civ_gc;
extern GdkGC *		fill_bg_gc;

extern GdkGC *		mask_fg_gc;
extern GdkGC *		mask_bg_gc;
extern GdkBitmap *	mask_bitmap;

static SPRITE *ctor_sprite_mask(GdkPixmap *mypixmap, GdkPixmap *mask,
				int width, int height);

/***************************************************************************
...
***************************************************************************/
void load_intro_gfx( void )
{
  int tot, lin, y, w;
  char s[64];

  intro_gfx_sprite = load_gfxfile(main_intro_filename);
  radar_gfx_sprite = load_gfxfile(minimap_intro_filename);

  tot=radar_gfx_sprite->width;
  lin=main_font->ascent+main_font->descent;
  y=radar_gfx_sprite->height-(lin+((int)(1.5*main_font->descent)));

  w = gdk_string_width(main_font, WORD_VERSION);
  gdk_draw_string(radar_gfx_sprite->pixmap, main_font,
		  toplevel->style->black_gc, (tot/2-w/2)+1, y+1, WORD_VERSION);
  gdk_draw_string(radar_gfx_sprite->pixmap, main_font,
		  toplevel->style->white_gc, tot/2-w/2, y, WORD_VERSION);

  y+=lin;

  sprintf(s, "%d.%d.%d%s", MAJOR_VERSION, MINOR_VERSION,
	  PATCH_VERSION, VERSION_LABEL);
  w = gdk_string_width( main_font, s );
  gdk_draw_string(radar_gfx_sprite->pixmap, main_font,
		  toplevel->style->black_gc, (tot/2-w/2)+1, y+1, s);
  gdk_draw_string(radar_gfx_sprite->pixmap, main_font,
		  toplevel->style->white_gc, tot/2-w/2, y, s);

  return;
}


/***************************************************************************
return newly allocated sprite cropped from source
***************************************************************************/
struct Sprite *crop_sprite(struct Sprite *source,
			   int x, int y,
			   int width, int height)
{
  GdkPixmap *mypixmap, *mask;

  mypixmap = gdk_pixmap_new(root_window, width, height, -1);

  gdk_draw_pixmap(mypixmap, civ_gc, source->pixmap, x, y, 0, 0,
		  width, height);

  mask=gdk_pixmap_new(mask_bitmap, width, height, 1);
  gdk_draw_rectangle(mask, mask_bg_gc, TRUE, 0, 0, -1, -1 );
  	    
  gdk_draw_pixmap(mask, mask_fg_gc, source->mask, x, y, 0, 0,
		  width, height);
  
  return ctor_sprite_mask(mypixmap, mask, width, height);
}


/***************************************************************************
...
***************************************************************************/
void load_cursors(void)
{
  GdkBitmap *pixmap, *mask;
  GdkColor *white, *black;

  white = colors_standard[COLOR_STD_WHITE];
  black = colors_standard[COLOR_STD_BLACK];

  /* goto */
  pixmap = gdk_bitmap_create_from_data(root_window, goto_cursor_bits,
				      goto_cursor_width,
				      goto_cursor_height);
  mask   = gdk_bitmap_create_from_data(root_window, goto_cursor_mask_bits,
				      goto_cursor_mask_width,
				      goto_cursor_mask_height);
  goto_cursor = gdk_cursor_new_from_pixmap(pixmap, mask,
					  white, black,
					  goto_cursor_x_hot, goto_cursor_y_hot);
  gdk_bitmap_unref(pixmap);
  gdk_bitmap_unref(mask);

  /* drop */
  pixmap = gdk_bitmap_create_from_data(root_window, drop_cursor_bits,
				      drop_cursor_width,
				      drop_cursor_height);
  mask   = gdk_bitmap_create_from_data(root_window, drop_cursor_mask_bits,
				      drop_cursor_mask_width,
				      drop_cursor_mask_height);
  drop_cursor = gdk_cursor_new_from_pixmap(pixmap, mask,
					  white, black,
					  drop_cursor_x_hot, drop_cursor_y_hot);
  gdk_bitmap_unref(pixmap);
  gdk_bitmap_unref(mask);

  /* nuke */
  pixmap = gdk_bitmap_create_from_data(root_window, nuke_cursor_bits,
				      nuke_cursor_width,
				      nuke_cursor_height);
  mask   = gdk_bitmap_create_from_data(root_window, nuke_cursor_mask_bits,
				      nuke_cursor_mask_width,
				      nuke_cursor_mask_height);
  nuke_cursor = gdk_cursor_new_from_pixmap(pixmap, mask,
					  white, black,
					  nuke_cursor_x_hot, nuke_cursor_y_hot);
  gdk_bitmap_unref(pixmap);
  gdk_bitmap_unref(mask);
}

#ifdef UNUSED
/***************************************************************************
...
***************************************************************************/
static SPRITE *ctor_sprite( GdkPixmap *mypixmap, int width, int height )
{
    SPRITE *mysprite = fc_malloc(sizeof(SPRITE));

    mysprite->pixmap	= mypixmap;
    mysprite->width	= width;
    mysprite->height	= height;
    mysprite->has_mask	= 0;

    return mysprite;
}
#endif

/***************************************************************************
...
***************************************************************************/
SPRITE *ctor_sprite_mask( GdkPixmap *mypixmap, GdkPixmap *mask, 
			  int width, int height )
{
    SPRITE *mysprite = fc_malloc(sizeof(SPRITE));

    mysprite->pixmap	= mypixmap;
    mysprite->mask	= mask;

    mysprite->width	= width;
    mysprite->height	= height;
    mysprite->has_mask	= 1;

    return mysprite;
}


#ifdef UNUSED
/***************************************************************************
...
***************************************************************************/
void dtor_sprite( SPRITE *mysprite )
{
    free_sprite( mysprite );
    return;
}
#endif

/***************************************************************************
 Returns the filename extensions the client supports
 Order is important.
***************************************************************************/
char **gfx_fileextensions(void)
{
  static char *ext[] =
  {
    "xpm",
    NULL
  };

  return ext;
}

/***************************************************************************
...
***************************************************************************/
struct Sprite *load_gfxfile(const char *filename)
{
  GdkBitmap	*m;
  GdkImlibImage *im;
  SPRITE	*mysprite;
  int		 w, h;

  if(!(im=gdk_imlib_load_image((char*)filename))) {
    freelog(LOG_FATAL, "Failed reading XPM file: %s", filename);
    exit(1);
  }

  mysprite=fc_malloc(sizeof(struct Sprite));

  w=im->rgb_width; h=im->rgb_height;

  if(!gdk_imlib_render (im, w, h)) {
    freelog(LOG_FATAL, "failed render of sprite struct for %s", filename);
    exit(1);
  }
  
  mysprite->pixmap    = gdk_imlib_move_image (im);
  m		      = gdk_imlib_move_mask  (im);
  mysprite->mask      = m;
  mysprite->has_mask  = (m != NULL);
  mysprite->width     = w;
  mysprite->height    = h;

  gdk_imlib_destroy_image (im);

  return mysprite;
}

/***************************************************************************
   Deletes a sprite.  These things can use a lot of memory.
***************************************************************************/
void free_sprite(SPRITE *s)
{
  gdk_imlib_free_pixmap(s->pixmap);
  free(s);
  return;
}

/***************************************************************************
 ...
***************************************************************************/
GdkPixmap *create_overlay_unit(int i)
{
  GdkPixmap *pm;
  enum color_std bg_color;
  
  pm=gdk_pixmap_new(root_window, NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT, -1);

  /* Give tile a background color, based on the type of unit */
  switch (get_unit_type(i)->move_type) {
    case LAND_MOVING: bg_color = COLOR_STD_GROUND; break;
    case SEA_MOVING:  bg_color = COLOR_STD_OCEAN;  break;
    case HELI_MOVING: bg_color = COLOR_STD_YELLOW; break;
    case AIR_MOVING:  bg_color = COLOR_STD_CYAN;   break;
    default:	      bg_color = COLOR_STD_BLACK;  break;
  }
  gdk_gc_set_foreground(fill_bg_gc, colors_standard[bg_color]);
  gdk_draw_rectangle(pm, fill_bg_gc, TRUE, 0, 0,
        	     NORMAL_TILE_WIDTH, NORMAL_TILE_HEIGHT);

  /* If we're using flags, put one on the tile */
  if(!use_solid_color_behind_units)  {
    struct Sprite *flag=get_nation_by_plr(game.player_ptr)->flag_sprite;

    gdk_gc_set_clip_origin(civ_gc, 0, 0);
    gdk_gc_set_clip_mask(civ_gc, flag->mask);

    gdk_draw_pixmap(pm, civ_gc, flag->pixmap, 0, 0, 0, 0,
					flag->width, flag->height);
    gdk_gc_set_clip_mask(civ_gc, NULL);
  }

  /* Finally, put a picture of the unit in the tile */
  if(i<game.num_unit_types) {
    struct Sprite *s=get_unit_type(i)->sprite;

    gdk_gc_set_clip_origin(civ_gc, 0, 0);
    gdk_gc_set_clip_mask(civ_gc, s->mask);

    gdk_draw_pixmap(pm, civ_gc, s->pixmap, 0, 0, 0, 0, s->width, s->height);
    gdk_gc_set_clip_mask(civ_gc, NULL);
  }

  return pm;
}

/***************************************************************************
  This function is so that packhand.c can be gui-independent, and
  not have to deal with Sprites itself.
***************************************************************************/
void free_intro_radar_sprites(void)
{
  if (intro_gfx_sprite) {
    free_sprite(intro_gfx_sprite);
    intro_gfx_sprite=NULL;
  }
  if (radar_gfx_sprite) {
    free_sprite(radar_gfx_sprite);
    radar_gfx_sprite=NULL;
  }
}
