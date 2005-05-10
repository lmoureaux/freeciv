/********************************************************************** 
 Freeciv - Copyright (C) 2005 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__SPECIALIST_H
#define FC__SPECIALIST_H

#include "shared.h"

#include "fc_types.h"
#include "requirements.h"

struct specialist {
  int index;
  char name[MAX_LEN_NAME];
  char short_name[MAX_LEN_NAME];
  int bonus[O_MAX];
  struct requirement req[MAX_NUM_REQS];
};

#define SP_COUNT num_specialist_types
#define DEFAULT_SPECIALIST default_specialist

extern struct specialist specialists[SP_MAX];
extern int num_specialist_types;
extern int default_specialist;

void specialists_init(void);
struct specialist *get_specialist(Specialist_type_id spec);

const char *specialists_string(const int *specialists);

#define specialist_type_iterate(sp)					    \
{									    \
  Specialist_type_id sp;						    \
                                                                            \
  for (sp = 0; sp < SP_COUNT; sp++) {

#define specialist_type_iterate_end                                         \
  }                                                                         \
}

#endif /* FC__SPECIALIST_H */
