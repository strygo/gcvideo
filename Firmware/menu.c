/* GCVideo DVI Firmware

   Copyright (C) 2015-2020, Ingo Korb <ingo@akana.de>
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
   THE POSSIBILITY OF SUCH DAMAGE.


   menu.c: Text mode menus

*/

#include <stdio.h>
#include "colormatrix.h"
#include "infoframe.h"
#include "modeset_common.h"
#include "osd.h"
#include "pad.h"
#include "settings.h"
#include "utils.h"
#include "menu.h"

#define MENUMARKER_LEFT  9
#define MENUMARKER_RIGHT 10

typedef enum {
  UPDATE_DECREMENT,
  UPDATE_INCREMENT
} updatetype_t;

typedef struct {
  int16_t lower;
  int16_t upper;
} cliprange_t;

static const cliprange_t clipranges[] = {
  [ VALTYPE_BOOL ]         = {    0,     1 },
  [ VALTYPE_EVENODD ]      = {    0,     1 },
  [ VALTYPE_ANALOGMODE ]   = {    0,     3 },
  [ VALTYPE_BYTE ]         = {    0,   255 },
  [ VALTYPE_SBYTE_99 ]     = {   -99,   99 },
  [ VALTYPE_SBYTE_127 ]    = {  -128,  127 },
  [ VALTYPE_FIXPOINT1 ]    = {     0,  256 },
  [ VALTYPE_FIXPOINT2 ]    = {     0,  255 },
  [ VALTYPE_SLPROFILEOFF ] = {     0,    3 },
  [ VALTYPE_SLPROFILE ]    = {     1,    3 },
  [ VALTYPE_SLINDEX ]      = {    16,  235 },
  [ VALTYPE_COLORMODE ]    = {     0,    3 },
};

static const uint8_t value_widths[] = {
  [ VALTYPE_BOOL ]         = 6,
  [ VALTYPE_EVENODD ]      = 6,
  [ VALTYPE_ANALOGMODE ]   = 7,
  [ VALTYPE_BYTE ]         = 6,
  [ VALTYPE_SBYTE_99 ]     = 6,
  [ VALTYPE_SBYTE_127 ]    = 6,
  [ VALTYPE_FIXPOINT1 ]    = 8,
  [ VALTYPE_FIXPOINT2 ]    = 8,
  [ VALTYPE_SLPROFILEOFF ] = 6,
  [ VALTYPE_SLPROFILE ]    = 6,
  [ VALTYPE_SLINDEX ]      = 6,
  [ VALTYPE_COLORMODE ]    = 7,
};

/* (un)draw marker on a menu item */
static void mark_item(menu_t *menu, unsigned int item, char ch) {
  osd_putcharat(menu->xpos + 1, menu->ypos + menu->items[item].line, ch, ATTRIB_DIM_BG);
}

static int get_value(const valueitem_t *value) {
  if (value->is_field) {
    uint32_t data, mask;

    mask = 1 << value->field.width;

    if (value->field.flags & VIFLAG_ALLMODES) {
      data = video_settings_global & mask;

    } else if (value->field.flags & VIFLAG_MODESET) {
      data = video_settings[modeset_mode] & mask;

    } else {
      mask -= 1;

      data = *((uint32_t*)value->field.data);
      data = (data >> value->field.shift) & mask;
    }

    if (value->field.flags & VIFLAG_SBYTE) {
      data = (data ^ 0x80) - 128;
    }

    return data;

  } else {
    return value->functions.get();
  }
}

static bool set_value(const valueitem_t *value, int newval) {
  if (value->is_field) {
    uint32_t mask = 1 << value->field.width;

    if (value->field.flags & VIFLAG_ALLMODES) {
      set_all_modes(mask, newval);
      if (value->field.width == VIDEOIF_BIT_SPOOFINTERLACE) {
        update_infoframe(detect_output_videomode());
      }

    } else if (value->field.flags & VIFLAG_MODESET) {
      if (newval) {
        video_settings[modeset_mode] |= mask;
      } else {
        video_settings[modeset_mode] &= ~mask;
      }

      if (current_videomode == modeset_mode)
        VIDEOIF->settings = video_settings[modeset_mode] | video_settings_global;

    } else {
      uint32_t data = *((uint32_t*)value->field.data);
      mask = (mask - 1) << value->field.shift;

      if (value->field.flags & VIFLAG_SBYTE) {
        newval = (newval + 128) ^ 0x80;
      }

      data = (data & ~mask) | (newval << value->field.shift);
      *((uint32_t*)value->field.data) = data;
    }

    if (value->field.flags & VIFLAG_UPDATE_VIDEOIF) {
      if ((video_settings_global & VIDEOIF_SET_COLORMODE_MASK) ==
          VIDEOIF_SET_COLORMODE_Y422) {
        /* force black OSD background */
        VIDEOIF->osd_bg = osdbg_settings & VIDEOIF_OSDBG_ALPHA_MASK;
      } else {
        VIDEOIF->osd_bg = osdbg_settings;
      }
      VIDEOIF->settings = video_settings[current_videomode] | video_settings_global;
    }

    if (value->field.flags & VIFLAG_COLORMATRIX) {
      update_colormatrix();
    }

    if (value->field.flags & VIFLAG_SLUPDATE) {
      update_scanlines();
    }

    return value->field.flags & VIFLAG_REDRAW;

  } else {
    return value->functions.set(newval);
  }
}

static void print_value(menu_t *menu, unsigned int itemnum) {
  int value = get_value(menu->items[itemnum].value);
  const valuetype_t type = menu->items[itemnum].value->type;

  osd_gotoxy(menu->xpos + menu->xsize - value_widths[type],
             menu->ypos + menu->items[itemnum].line);
  switch (type) {
  case VALTYPE_BOOL:
    if (value)
      osd_puts("  On");
    else
      osd_puts(" Off");
    break;

  case VALTYPE_EVENODD:
    if (value)
      osd_puts("Even");
    else
      osd_puts(" Odd");
    break;

  case VALTYPE_ANALOGMODE:
    switch (value) {
    case 0:
      osd_puts("YPbPr");
      break;

    case 1:
      osd_puts("  RGB");
      break;
          
    case 3:
      osd_puts("  BRG");
      break;

    default:
      osd_puts(" RGsB");
      break;
    }
    break;

  case VALTYPE_BYTE:
  case VALTYPE_SBYTE_99:
  case VALTYPE_SLINDEX:
    printf("%4d", value);
    break;

  case VALTYPE_SBYTE_127:
    if (value == 0)
      osd_puts("   0");
    else
      printf("%+4d", value);
    break;

  case VALTYPE_FIXPOINT1:
    printf("%2d.%03d", value / 256, (value % 256) * 1000 / 256);
    break;

  case VALTYPE_FIXPOINT2:
    printf("%2d.%03d", value / 128, (value % 128) * 1000 / 128);
    break;

  case VALTYPE_SLPROFILEOFF:
  case VALTYPE_SLPROFILE:
    if (value) {
      printf("%4d", value);
    } else {
      osd_puts(" Off");
    }
    break;

  case VALTYPE_COLORMODE:
    switch (value) {
      case 0:  osd_puts("RGB-F"); break;
      case 1:  osd_puts("RGB-L"); break;
      case 2:  osd_puts("YC444"); break;
      default: osd_puts("YC422"); break;
    }
    break;
  }
}


/* update a valueitem */
static void update_value(menu_t *menu, unsigned int itemid, updatetype_t upd) {
  valueitem_t *value = menu->items[itemid].value;
  int curval = get_value(value);

  if (upd == UPDATE_INCREMENT) {
    curval++;
  } else {
    curval--;
  }

  if (value->type == VALTYPE_BOOL ||
      value->type == VALTYPE_EVENODD) {
    /* bool always toggles */
    curval = !get_value(value);
  }

  clip_value(&curval, clipranges[value->type].lower, clipranges[value->type].upper);

  if (set_value(value, curval)) {
    /* need a full redraw */
    menu_draw(menu);
    mark_item(menu, itemid, MENUMARKER_LEFT);
  } else {
    /* just update the changed value */
    print_value(menu, itemid);
  }
}

void menu_draw(menu_t *menu) {
  const menuitem_t *items = menu->items;
  unsigned int i;

  /* draw the menu */
  osd_fillbox(menu->xpos, menu->ypos, menu->xsize, menu->ysize, ' ' | ATTRIB_DIM_BG);
  osd_drawborder(menu->xpos, menu->ypos, menu->xsize, menu->ysize);

  /* run the callback, it might update the item flags */
  if (menu->drawcallback)
    menu->drawcallback(menu);

  for (i = 0; i < menu->entries; i++) {
    if (items[i].flags & MENU_FLAG_DISABLED)
      osd_setattr(true, true);
    else
      osd_setattr(true, false);

    /* print item */
    osd_gotoxy(menu->xpos + 2, menu->ypos + items[i].line);
    osd_puts(items[i].text);

    if (items[i].value) {
      print_value(menu, i);
    }
  }
}


int menu_exec(menu_t *menu, unsigned int initial_item) {
  const menuitem_t *items = menu->items;
  unsigned int cur_item = initial_item;

  /* ensure the initial item is valid */
  while (items[cur_item].flags & MENU_FLAG_DISABLED) {
    cur_item++;
    if (cur_item >= menu->entries)
      cur_item = 0;
  }

  /* mark initial menuitem */
  osd_setattr(true, false);
  mark_item(menu, cur_item, MENUMARKER_LEFT);

  /* wait until all buttons are released */
  pad_wait_for_release();

  /* handle input */
  while (1) {
    /* wait for input */
    while (!pad_buttons) ;

    unsigned int curbtns = pad_buttons;

    /* selection movement with up/down */
    if (curbtns & (PAD_UP | IR_UP)) {
      mark_item(menu, cur_item, ' ');

      do {
        if (cur_item > 0)
          cur_item--;
        else
          cur_item = menu->entries - 1;
      } while (items[cur_item].flags & MENU_FLAG_DISABLED);

      mark_item(menu, cur_item, MENUMARKER_LEFT);

      pad_clear(PAD_UP | PAD_LEFT | PAD_RIGHT |
                IR_UP  | IR_LEFT  | IR_RIGHT); // prioritize up/down over left/right
    }

    if (curbtns & (PAD_DOWN | IR_DOWN)) {
      mark_item(menu, cur_item, ' ');

      do {
        cur_item++;
        if (cur_item >= menu->entries)
          cur_item = 0;
      } while (items[cur_item].flags & MENU_FLAG_DISABLED);

      mark_item(menu, cur_item, MENUMARKER_LEFT);

      pad_clear(PAD_DOWN | PAD_LEFT | PAD_RIGHT |
                IR_DOWN  | IR_LEFT  | IR_RIGHT); // prioritize up/down over left/right
    }

    /* value change with left/right */
    if ((curbtns & (PAD_LEFT | IR_LEFT)) && items[cur_item].value) {
      update_value(menu, cur_item, UPDATE_DECREMENT);
      pad_clear(PAD_LEFT | IR_LEFT);
    }

    if ((curbtns & (PAD_RIGHT | IR_RIGHT)) && items[cur_item].value) {
      update_value(menu, cur_item, UPDATE_INCREMENT);
      pad_clear(PAD_RIGHT | IR_RIGHT);
    }

    /* selection with X */
    if (curbtns & (PAD_X | IR_OK)) {
      pad_clear(PAD_X | IR_OK);
      if (!items[cur_item].value) {
        /* no value attached, can exit from here */
        mark_item(menu, cur_item, ' ');
        return cur_item;

      } else {
        /* modify value */
        valueitem_t *vi = items[cur_item].value;

        switch (vi->type) {
        case VALTYPE_BOOL:
        case VALTYPE_EVENODD:
          update_value(menu, cur_item, UPDATE_INCREMENT); // bool always toggles
          break;

        default:
          break;
        }
      }
    }

    /* abort with Y */
    if (curbtns & (PAD_Y | IR_BACK)) {
      pad_clear(PAD_Y | IR_BACK);
      mark_item(menu, cur_item, ' ');
      return MENU_ABORT;
    }

    /* exit on video mode change (simplifies things) */
    if (curbtns & PAD_VIDEOCHANGE)
      return MENU_ABORT;

    /* clear unused buttons */
    pad_clear(PAD_START | PAD_Z | PAD_L | PAD_R | PAD_A | PAD_B);
  }
}
