/*
 *
 *
 *
 */

#include <aim.h>
#include <ctype.h>

int aimutil_putstr(u_char *dest, const char *src, int len)
{
	memcpy(dest, src, len);
	return len;
}

/*
 * Tokenizing functions.  Used to portably replace strtok/sep.
 *   -- DMP.
 *
 */
int aimutil_tokslen(char *toSearch, int index, char dl)
{
	int curCount = 1;
	char *next;
	char *last;
	int toReturn;

	last = toSearch;
	next = strchr(toSearch, dl);

	while(curCount < index && next != NULL) {
		curCount++;
		last = next + 1;
		next = strchr(last, dl);
	}

	if ((curCount < index) || (next == NULL))
		toReturn = strlen(toSearch) - (curCount - 1);
	else
		toReturn = next - toSearch - (curCount - 1);

	return toReturn;
}

int aimutil_itemcnt(char *toSearch, char dl)
{
	int curCount;
	char *next;

	curCount = 1;

	next = strchr(toSearch, dl);

	while(next != NULL) {
		curCount++;
		next = strchr(next + 1, dl);
	}

	return curCount;
}

char *aimutil_itemidx(char *toSearch, int index, char dl)
{
	int curCount;
	char *next;
	char *last;
	char *toReturn;

	curCount = 0;

	last = toSearch;
	next = strchr(toSearch, dl);

	while (curCount < index && next != NULL) {
		curCount++;
		last = next + 1;
		next = strchr(last, dl);
	}

	if (curCount < index) {
		toReturn = g_strdup("");
	}
	next = strchr(last, dl);

	if (curCount < index) {
		toReturn = g_strdup("");
	} else {
		if (next == NULL) {
			toReturn = g_malloc((strlen(last) + 1) * sizeof(char));
			strcpy(toReturn, last);
		} else {
			toReturn = g_malloc((next - last + 1) * sizeof(char));
			memcpy(toReturn, last, (next - last));
			toReturn[next - last] = '\0';
		}
	}
	return toReturn;
}

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
	while ( (*curPtr) != (char) NULL) {
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
	while ( (*curPtr1 != (char) NULL) && (*curPtr2 != (char) NULL) ) {
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
