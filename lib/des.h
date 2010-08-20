/*
 *  FIPS-46-3 compliant 3DES implementation
 *
 *  Copyright (C) 2001-2003  Christophe Devine
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _DES_H
#define _DES_H

#include <stdint.h>

typedef struct
{
    uint32_t esk[32];     /* DES encryption subkeys */
    uint32_t dsk[32];     /* DES decryption subkeys */
}
des_context;

typedef struct
{
    uint32_t esk[96];     /* Triple-DES encryption subkeys */
    uint32_t dsk[96];     /* Triple-DES decryption subkeys */
}
des3_context;

int  des_set_key( des_context *ctx, uint8_t key[8] );
void des_encrypt( des_context *ctx, uint8_t input[8], uint8_t output[8] );
void des_decrypt( des_context *ctx, uint8_t input[8], uint8_t output[8] );

int  des3_set_2keys( des3_context *ctx, const uint8_t key1[8], const uint8_t key2[8] );
int  des3_set_3keys( des3_context *ctx, const uint8_t key1[8], const uint8_t key2[8],
                                        const uint8_t key3[8] );

void des3_encrypt( des3_context *ctx, uint8_t input[8], uint8_t output[8] );
void des3_decrypt( des3_context *ctx, uint8_t input[8], uint8_t output[8] );

#endif /* des.h */
