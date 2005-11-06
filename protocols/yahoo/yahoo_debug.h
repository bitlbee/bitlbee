/*
 * libyahoo2: yahoo_debug.h
 *
 * Copyright (C) 2002-2004, Philip S Tellis <philip.tellis AT gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

extern int yahoo_log_message(char *fmt, ...);

#define NOTICE(x) if(yahoo_get_log_level() >= YAHOO_LOG_NOTICE) { yahoo_log_message x; yahoo_log_message("\n"); }

#define LOG(x) if(yahoo_get_log_level() >= YAHOO_LOG_INFO) { yahoo_log_message("%s:%d: ", __FILE__, __LINE__); \
	yahoo_log_message x; \
	yahoo_log_message("\n"); }

#define WARNING(x) if(yahoo_get_log_level() >= YAHOO_LOG_WARNING) { yahoo_log_message("%s:%d: warning: ", __FILE__, __LINE__); \
	yahoo_log_message x; \
	yahoo_log_message("\n"); }

#define DEBUG_MSG(x) if(yahoo_get_log_level() >= YAHOO_LOG_DEBUG) { yahoo_log_message("%s:%d: debug: ", __FILE__, __LINE__); \
	yahoo_log_message x; \
	yahoo_log_message("\n"); }


