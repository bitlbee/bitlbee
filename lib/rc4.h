/***************************************************************************\
*                                                                           *
*  BitlBee - An IRC to IM gateway                                           *
*  Simple (but secure) RC4 implementation for safer password storage.       *
*                                                                           *
*  Copyright 2006 Wilmer van der Gaast <wilmer@gaast.net>                   *
*                                                                           *
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


struct rc4_state
{
	unsigned char S[256];
	unsigned char i, j;
};

struct rc4_state *rc4_keymaker( unsigned char *key, int kl, int cycles );
unsigned char rc4_getbyte( struct rc4_state *st );
int rc4_encode( unsigned char *clear, int clear_len, unsigned char **crypt, char *password );
int rc4_decode( unsigned char *crypt, int crypt_len, unsigned char **clear, char *password );
