/*
 * SHA1 hashing code copied from Lepton's crack <http://usuarios.lycos.es/reinob/>
 *
 * Adapted to be API-compatible with the previous (GPL-incompatible) code.
 */

/*
 *  sha1.c
 *
 *  Description:
 *      This file implements the Secure Hashing Algorithm 1 as
 *      defined in FIPS PUB 180-1 published April 17, 1995.
 *
 *      The SHA-1, produces a 160-bit message digest for a given
 *      data stream.  It should take about 2**n steps to find a
 *      message with the same digest as a given message and
 *      2**(n/2) to find any two messages with the same digest,
 *      when n is the digest size in bits.  Therefore, this
 *      algorithm can serve as a means of providing a
 *      "fingerprint" for a message.
 *
 *  Portability Issues:
 *      SHA-1 is defined in terms of 32-bit "words".  This code
 *      uses <stdint.h> (included via "sha1.h" to define 32 and 8
 *      bit unsigned integer types.  If your C compiler does not
 *      support 32 bit unsigned integers, this code is not
 *      appropriate.
 *
 *  Caveats:
 *      SHA-1 is designed to work with messages less than 2^64 bits
 *      long.  Although SHA-1 allows a message digest to be generated
 *      for messages of any number of bits less than 2^64, this
 *      implementation only works with messages with a length that is
 *      a multiple of the size of an 8-bit character.
 *
 */

#include <string.h>
#include <stdio.h>
#include "sha1.h"

/*
 *  Define the SHA1 circular left shift macro
 */
#define SHA1CircularShift(bits,word) \
       (((word) << (bits)) | ((word) >> (32-(bits))))

/* Local Function Prototyptes */
static void sha1_pad(sha1_state_t *);
static void sha1_process_block(sha1_state_t *);

/*
 *  sha1_init
 *
 *  Description:
 *      This function will initialize the sha1_state_t in preparation
 *      for computing a new SHA1 message digest.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to reset.
 *
 *  Returns:
 *      sha Error Code.
 *
 */
int sha1_init(sha1_state_t * context)
{
	context->Length_Low = 0;
	context->Length_High = 0;
	context->Message_Block_Index = 0;

	context->Intermediate_Hash[0] = 0x67452301;
	context->Intermediate_Hash[1] = 0xEFCDAB89;
	context->Intermediate_Hash[2] = 0x98BADCFE;
	context->Intermediate_Hash[3] = 0x10325476;
	context->Intermediate_Hash[4] = 0xC3D2E1F0;

	context->Computed = 0;
	context->Corrupted = 0;
	
	return shaSuccess;
}

/*
 *  sha1_finish
 *
 *  Description:
 *      This function will return the 160-bit message digest into the
 *      Message_Digest array  provided by the caller.
 *      NOTE: The first octet of hash is stored in the 0th element,
 *            the last octet of hash in the 19th element.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to use to calculate the SHA-1 hash.
 *      Message_Digest: [out]
 *          Where the digest is returned.
 *
 *  Returns:
 *      sha Error Code.
 *
 */
int sha1_finish(sha1_state_t * context, uint8_t Message_Digest[sha1_hash_size])
{
	int i;

	if (!context || !Message_Digest) {
		return shaNull;
	}

	if (context->Corrupted) {
		return context->Corrupted;
	}

	if (!context->Computed) {
		sha1_pad(context);
		for (i = 0; i < 64; ++i) {
			/* message may be sensitive, clear it out */
			context->Message_Block[i] = 0;
		}
		context->Length_Low = 0;	/* and clear length */
		context->Length_High = 0;
		context->Computed = 1;

	}

	for (i = 0; i < sha1_hash_size; ++i) {
		Message_Digest[i] = context->Intermediate_Hash[i >> 2]
		    >> 8 * (3 - (i & 0x03));
	}

	return shaSuccess;
}

/*
 *  sha1_append
 *
 *  Description:
 *      This function accepts an array of octets as the next portion
 *      of the message.
 *
 *  Parameters:
 *      context: [in/out]
 *          The SHA context to update
 *      message_array: [in]
 *          An array of characters representing the next portion of
 *          the message.
 *      length: [in]
 *          The length of the message in message_array
 *
 *  Returns:
 *      sha Error Code.
 *
 */
int
sha1_append(sha1_state_t * context,
	  const uint8_t * message_array, unsigned length)
{
	if (!length) {
		return shaSuccess;
	}

	if (!context || !message_array) {
		return shaNull;
	}

	if (context->Computed) {
		context->Corrupted = shaStateError;

		return shaStateError;
	}

	if (context->Corrupted) {
		return context->Corrupted;
	}
	while (length-- && !context->Corrupted) {
		context->Message_Block[context->Message_Block_Index++] =
		    (*message_array & 0xFF);

		context->Length_Low += 8;
		if (context->Length_Low == 0) {
			context->Length_High++;
			if (context->Length_High == 0) {
				/* Message is too long */
				context->Corrupted = 1;
			}
		}

		if (context->Message_Block_Index == 64) {
			sha1_process_block(context);
		}

		message_array++;
	}

	return shaSuccess;
}

/*
 *  sha1_process_block
 *
 *  Description:
 *      This function will process the next 512 bits of the message
 *      stored in the Message_Block array.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      Many of the variable names in this code, especially the
 *      single character names, were used because those were the
 *      names used in the publication.
 *
 *
 */
static void sha1_process_block(sha1_state_t * context)
{
	const uint32_t K[] = {	/* Constants defined in SHA-1   */
		0x5A827999,
		0x6ED9EBA1,
		0x8F1BBCDC,
		0xCA62C1D6
	};
	int t;			/* Loop counter                */
	uint32_t temp;		/* Temporary word value        */
	uint32_t W[80];		/* Word sequence               */
	uint32_t A, B, C, D, E;	/* Word buffers                */

	/*
	 *  Initialize the first 16 words in the array W
	 */
	for (t = 0; t < 16; t++) {
		W[t] = context->Message_Block[t * 4] << 24;
		W[t] |= context->Message_Block[t * 4 + 1] << 16;
		W[t] |= context->Message_Block[t * 4 + 2] << 8;
		W[t] |= context->Message_Block[t * 4 + 3];
	}

	for (t = 16; t < 80; t++) {
		W[t] =
		    SHA1CircularShift(1,
				      W[t - 3] ^ W[t - 8] ^ W[t -
							      14] ^ W[t -
								      16]);
	}

	A = context->Intermediate_Hash[0];
	B = context->Intermediate_Hash[1];
	C = context->Intermediate_Hash[2];
	D = context->Intermediate_Hash[3];
	E = context->Intermediate_Hash[4];

	for (t = 0; t < 20; t++) {
		temp = SHA1CircularShift(5, A) +
		    ((B & C) | ((~B) & D)) + E + W[t] + K[0];
		E = D;
		D = C;
		C = SHA1CircularShift(30, B);

		B = A;
		A = temp;
	}

	for (t = 20; t < 40; t++) {
		temp =
		    SHA1CircularShift(5,
				      A) + (B ^ C ^ D) + E + W[t] + K[1];
		E = D;
		D = C;
		C = SHA1CircularShift(30, B);
		B = A;
		A = temp;
	}

	for (t = 40; t < 60; t++) {
		temp = SHA1CircularShift(5, A) +
		    ((B & C) | (B & D) | (C & D)) + E + W[t] + K[2];
		E = D;
		D = C;
		C = SHA1CircularShift(30, B);
		B = A;
		A = temp;
	}

	for (t = 60; t < 80; t++) {
		temp =
		    SHA1CircularShift(5,
				      A) + (B ^ C ^ D) + E + W[t] + K[3];
		E = D;
		D = C;
		C = SHA1CircularShift(30, B);
		B = A;
		A = temp;
	}

	context->Intermediate_Hash[0] += A;
	context->Intermediate_Hash[1] += B;
	context->Intermediate_Hash[2] += C;
	context->Intermediate_Hash[3] += D;
	context->Intermediate_Hash[4] += E;

	context->Message_Block_Index = 0;
}

/*
 *  sha1_pad
 *
 *  Description:
 *      According to the standard, the message must be padded to an even
 *      512 bits.  The first padding bit must be a '1'.  The last 64
 *      bits represent the length of the original message.  All bits in
 *      between should be 0.  This function will pad the message
 *      according to those rules by filling the Message_Block array
 *      accordingly.  It will also call the ProcessMessageBlock function
 *      provided appropriately.  When it returns, it can be assumed that
 *      the message digest has been computed.
 *
 *  Parameters:
 *      context: [in/out]
 *          The context to pad
 *      ProcessMessageBlock: [in]
 *          The appropriate SHA*ProcessMessageBlock function
 *  Returns:
 *      Nothing.
 *
 */

static void sha1_pad(sha1_state_t * context)
{
	/*
	 *  Check to see if the current message block is too small to hold
	 *  the initial padding bits and length.  If so, we will pad the
	 *  block, process it, and then continue padding into a second
	 *  block.
	 */
	if (context->Message_Block_Index > 55) {
		context->Message_Block[context->Message_Block_Index++] =
		    0x80;
		while (context->Message_Block_Index < 64) {
			context->Message_Block[context->
					       Message_Block_Index++] = 0;
		}

		sha1_process_block(context);

		while (context->Message_Block_Index < 56) {
			context->Message_Block[context->
					       Message_Block_Index++] = 0;
		}
	} else {
		context->Message_Block[context->Message_Block_Index++] =
		    0x80;
		while (context->Message_Block_Index < 56) {

			context->Message_Block[context->
					       Message_Block_Index++] = 0;
		}
	}

	/*
	 *  Store the message length as the last 8 octets
	 */
	context->Message_Block[56] = context->Length_High >> 24;
	context->Message_Block[57] = context->Length_High >> 16;
	context->Message_Block[58] = context->Length_High >> 8;
	context->Message_Block[59] = context->Length_High;
	context->Message_Block[60] = context->Length_Low >> 24;
	context->Message_Block[61] = context->Length_Low >> 16;
	context->Message_Block[62] = context->Length_Low >> 8;
	context->Message_Block[63] = context->Length_Low;

	sha1_process_block(context);
}

#define HMAC_BLOCK_SIZE 64

/* BitlBee addition: */
void sha1_hmac(const char *key_, size_t key_len, const char *payload, size_t payload_len, uint8_t Message_Digest[sha1_hash_size])
{
	sha1_state_t sha1;
	uint8_t hash[sha1_hash_size];
	uint8_t key[HMAC_BLOCK_SIZE+1];
	int i;
	
	if( key_len == 0 )
		key_len = strlen( key_ );
	if( payload_len == 0 )
		payload_len = strlen( payload );
	
	/* Create K. If our current key is >64 chars we have to hash it,
	   otherwise just pad. */
	memset( key, 0, HMAC_BLOCK_SIZE + 1 );
	if( key_len > HMAC_BLOCK_SIZE )
	{
		sha1_init( &sha1 );
		sha1_append( &sha1, (uint8_t*) key_, key_len );
		sha1_finish( &sha1, key );
	}
	else
	{
		memcpy( key, key_, key_len );
	}
	
	/* Inner part: H(K XOR 0x36, text) */
	sha1_init( &sha1 );
	for( i = 0; i < HMAC_BLOCK_SIZE; i ++ )
		key[i] ^= 0x36;
	sha1_append( &sha1, key, HMAC_BLOCK_SIZE );
	sha1_append( &sha1, (const uint8_t*) payload, payload_len );
	sha1_finish( &sha1, hash );
	
	/* Final result: H(K XOR 0x5C, inner stuff) */
	sha1_init( &sha1 );
	for( i = 0; i < HMAC_BLOCK_SIZE; i ++ )
		key[i] ^= 0x36 ^ 0x5c;
	sha1_append( &sha1, key, HMAC_BLOCK_SIZE );
	sha1_append( &sha1, hash, sha1_hash_size );
	sha1_finish( &sha1, Message_Digest );
}

/* I think this follows the scheme described on:
   http://en.wikipedia.org/wiki/Universally_unique_identifier#Version_4_.28random.29
   My random data comes from a SHA1 generator but hey, it's random enough for
   me, and RFC 4122 looks way more complicated than I need this to be.
   
   Returns a value that must be free()d. */
char *sha1_random_uuid( sha1_state_t * context )
{
	uint8_t dig[sha1_hash_size];
	char *ret = g_new0( char, 40 ); /* 36 chars + \0 */
	int i, p;
	
	sha1_finish(context, dig);
	for( p = i = 0; i < 16; i ++ )
	{
		if( i == 4 || i == 6 || i == 8 || i == 10 )
			ret[p++] = '-';
		if( i == 6 )
			dig[i] = ( dig[i] & 0x0f ) | 0x40;
		if( i == 8 )
			dig[i] = ( dig[i] & 0x30 ) | 0x80;
		
		sprintf( ret + p, "%02x", dig[i] );
		p += 2;
	}
	ret[p] = '\0';
	
	return ret;
}
