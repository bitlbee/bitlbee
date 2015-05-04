/********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2010 Wilmer van der Gaast and others                *
  \********************************************************************/

/* MSN module - Some tables with useful data                            */

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
  if not, write to the Free Software Foundation, Inc., 51 Franklin St.,
  Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "nogaim.h"
#include "msn.h"

const struct msn_away_state msn_away_state_list[] =
{
	{ "NLN", "" },
	{ "AWY", "Away" },
	{ "BSY", "Busy" },
	{ "IDL", "Idle" },
	{ "BRB", "Be Right Back" },
	{ "PHN", "On the Phone" },
	{ "LUN", "Out to Lunch" },
	{ "HDN", "Hidden" },
	{ "",    "" }
};

const struct msn_away_state *msn_away_state_by_code(char *code)
{
	int i;

	for (i = 0; *msn_away_state_list[i].code; i++) {
		if (g_strcasecmp(msn_away_state_list[i].code, code) == 0) {
			return(msn_away_state_list + i);
		}
	}

	return NULL;
}

const struct msn_away_state *msn_away_state_by_name(char *name)
{
	int i;

	for (i = 0; *msn_away_state_list[i].code; i++) {
		if (g_strcasecmp(msn_away_state_list[i].name, name) == 0) {
			return(msn_away_state_list + i);
		}
	}

	return NULL;
}

const struct msn_status_code msn_status_code_list[] =
{
	{ 200, "Invalid syntax",                                        0 },
	{ 201, "Invalid parameter",                                     0 },
	{ 205, "Invalid (non-existent) handle",                         0 },
	{ 206, "Domain name missing",                                   0 },
	{ 207, "Already logged in",                                     0 },
	{ 208, "Invalid handle",                                        0 },
	{ 209, "Forbidden nickname",                                    0 },
	{ 210, "Buddy list too long",                                   0 },
	{ 215, "Handle is already in list",                             0 },
	{ 216, "Handle is not in list",                                 0 },
	{ 217, "Person is off-line or non-existent",                    0 },
	{ 218, "Already in that mode",                                  0 },
	{ 219, "Handle is already in opposite list",                    0 },
	{ 223, "Too many groups",                                       0 },
	{ 224, "Invalid group or already in list",                      0 },
	{ 225, "Handle is not in that group",                           0 },
	{ 229, "Group name too long",                                   0 },
	{ 230, "Cannot remove that group",                              0 },
	{ 231, "Invalid group",                                         0 },
	{ 240, "ADL/RML command with corrupted payload",                STATUS_FATAL },
	{ 241, "ADL/RML command with invalid modification",             0 },
	{ 280, "Switchboard failed",                                    STATUS_SB_FATAL },
	{ 281, "Transfer to switchboard failed",                        0 },

	{ 300, "Required field missing",                                0 },
	{ 302, "Not logged in",                                         0 },

	{ 500, "Internal server error/Account banned",                  STATUS_FATAL },
	{ 501, "Database server error",                                 STATUS_FATAL },
	{ 502, "Command disabled",                                      0 },
	{ 510, "File operation failed",                                 STATUS_FATAL },
	{ 520, "Memory allocation failed",                              STATUS_FATAL },
	{ 540, "Challenge response invalid",                            STATUS_FATAL },

	{ 600, "Server is busy",                                        STATUS_FATAL },
	{ 601, "Server is unavailable",                                 STATUS_FATAL },
	{ 602, "Peer nameserver is down",                               STATUS_FATAL },
	{ 603, "Database connection failed",                            STATUS_FATAL },
	{ 604, "Server is going down",                                  STATUS_FATAL },
	{ 605, "Server is unavailable",                                 STATUS_FATAL },

	{ 700, "Could not create connection",                           STATUS_FATAL },
	{ 710, "Invalid CVR parameters",                                STATUS_FATAL },
	{ 711, "Write is blocking",                                     STATUS_FATAL },
	{ 712, "Session is overloaded",                                 STATUS_FATAL },
	{ 713, "Calling too rapidly",                                   0 },
	{ 714, "Too many sessions",                                     STATUS_FATAL },
	{ 715, "Not expected/Invalid argument/action",                  0 },
	{ 717, "Bad friend file",                                       STATUS_FATAL },
	{ 731, "Not expected/Invalid argument",                         0 },

	{ 800, "Changing too rapidly",                                  0 },

	{ 910, "Server is busy",                                        STATUS_FATAL },
	{ 911, "Authentication failed",                                 STATUS_SB_FATAL | STATUS_FATAL },
	{ 912, "Server is busy",                                        STATUS_FATAL },
	{ 913, "Not allowed when hiding",                               0 },
	{ 914, "Server is unavailable",                                 STATUS_FATAL },
	{ 915, "Server is unavailable",                                 STATUS_FATAL },
	{ 916, "Server is unavailable",                                 STATUS_FATAL },
	{ 917, "Authentication failed",                                 STATUS_FATAL },
	{ 918, "Server is busy",                                        STATUS_FATAL },
	{ 919, "Server is busy",                                        STATUS_FATAL },
	{ 920, "Not accepting new principals",                          0 }, /* When a sb is full? */
	{ 922, "Server is busy",                                        STATUS_FATAL },
	{ 923, "Kids Passport without parental consent",                STATUS_FATAL },
	{ 924, "Passport account not yet verified",                     STATUS_FATAL },
	{ 928, "Bad ticket",                                            STATUS_FATAL },
	{  -1, NULL,                                                    0 }
};

const struct msn_status_code *msn_status_by_number(int number)
{
	static struct msn_status_code *unknown = NULL;
	int i;

	for (i = 0; msn_status_code_list[i].number >= 0; i++) {
		if (msn_status_code_list[i].number == number) {
			return(msn_status_code_list + i);
		}
	}

	if (unknown == NULL) {
		unknown = g_new0(struct msn_status_code, 1);
		unknown->text = g_new0(char, 128);
	}

	unknown->number = number;
	unknown->flags = 0;
	g_snprintf(unknown->text, 128, "Unknown error (%d)", number);

	return(unknown);
}
