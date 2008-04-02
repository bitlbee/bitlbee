/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple (but secure) ArcFour implementation for safer password storage.   *
*                                                                           *
*  Copyright 2007 Wilmer van der Gaast <wilmer@gaast.net>                   *
*                                                                           *
*  This program is free software; you can redistribute it and/or modify     *
*  it under the terms of the GNU General Public License as published by     *
*  the Free Software Foundation; either version 2 of the License, or        *
*  (at your option) any later version.                                      *
*                                                                           *
*  This program is distributed in the hope that it will be useful,          *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
*  GNU General Public License for more details.                             *
*                                                                           *
*  You should have received a copy of the GNU General Public License along  *
*  with this program; if not, write to the Free Software Foundation, Inc.,  *
*  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.              *
*                                                                           *
\***************************************************************************/


/* See arc.c for more information. */

struct arc_state
{
	unsigned char S[256];
	unsigned char i, j;
};

G_GNUC_MALLOC struct arc_state *arc_keymaker( unsigned char *key, int kl, int cycles );
unsigned char arc_getbyte( struct arc_state *st );
int arc_encode( char *clear, int clear_len, unsigned char **crypt, char *password, int pad_to );
int arc_decode( unsigned char *crypt, int crypt_len, char **clear, char *password );
