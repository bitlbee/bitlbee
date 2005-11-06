  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* Random debug stuff                                                  */

/*
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License with
  the Debian GNU/Linux distribution in /usr/share/common-licenses/GPL;
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#define BITLBEE_CORE
#include "bitlbee.h"

GHashTable *iocounter=NULL;
FILE *activity_output;

static void for_each_node(gpointer key, gpointer value, gpointer user_data);

void count_io_event(GIOChannel *source, char *section) {
	long int *newcounter;

	if(iocounter==NULL) {
		iocounter=g_hash_table_new(NULL, NULL);
	}

	if(g_hash_table_lookup(iocounter, section)==NULL) {
		newcounter=g_new0(long int, 1);
		g_hash_table_insert(iocounter, section, newcounter);
	} else {
		newcounter=g_hash_table_lookup(iocounter, section);
		(*newcounter)++;	
	}	 	
}

void write_io_activity(void) {
	activity_output=fopen("ioactivity.log", "a");
	fprintf(activity_output, "Amount of GIO events raised for each section of the code:\n");
	g_hash_table_foreach(iocounter, &for_each_node, NULL);
	fprintf(activity_output, "End of list\n");
	fclose(activity_output);
}

static void for_each_node(gpointer key, gpointer value, gpointer user_data) {
	fprintf(activity_output, "%s %ld\n", (char *)key, (*(long int *)value));
}
