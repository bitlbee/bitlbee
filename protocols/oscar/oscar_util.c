#include <aim.h>
#include <ctype.h>

/*
* int snlen(const char *)
* 
* This takes a screen name and returns its length without
* spaces.  If there are no spaces in the SN, then the 
* return is equal to that of strlen().
*
*/
static int aim_snlen(const char *sn)
{
	int i = 0;
	const char *curPtr = NULL;

	if (!sn)
		return 0;

	curPtr = sn;
	while ( (*curPtr) != (char) '\0') {
		if ((*curPtr) != ' ')
		i++;
		curPtr++;
	}

	return i;
}

/*
* int sncmp(const char *, const char *)
*
* This takes two screen names and compares them using the rules
* on screen names for AIM/AOL.  Mainly, this means case and space
* insensitivity (all case differences and spacing differences are
* ignored).
*
* Return: 0 if equal
*     non-0 if different
*
*/

int aim_sncmp(const char *sn1, const char *sn2)
{
	const char *curPtr1 = NULL, *curPtr2 = NULL;

	if (aim_snlen(sn1) != aim_snlen(sn2))
		return 1;

	curPtr1 = sn1;
	curPtr2 = sn2;
	while ( (*curPtr1 != (char) '\0') && (*curPtr2 != (char) '\0') ) {
		if ( (*curPtr1 == ' ') || (*curPtr2 == ' ') ) {
			if (*curPtr1 == ' ')
				curPtr1++;
			if (*curPtr2 == ' ')
				curPtr2++;
		} else {
			if ( toupper(*curPtr1) != toupper(*curPtr2))
				return 1;
			curPtr1++;
			curPtr2++;
		}
	}

	/* Should both be NULL */
	if (*curPtr1 != *curPtr2)
		return 1;

	return 0;
}
