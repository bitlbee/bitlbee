/*
 * libyahoo2 - originally from gaim patches by Amatus
 *
 * Copyright (C) 2003-2004
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
 */

#define IDENT  1 /* identify function */
#define XOR    2 /* xor with arg1 */
#define MULADD 3 /* multipy by arg1 then add arg2 */
#define LOOKUP 4 /* lookup each byte in the table pointed to by arg1 */
#define BITFLD 5 /* reorder bits according to table pointed to by arg1 */

struct yahoo_fn
{
	int type; 
	long arg1, arg2;
};

int yahoo_xfrm( int table, int depth, int seed );
