/* 
 * This is the most simple possible BitlBee plugin. To use, compile it as 
 * a shared library and place it in the plugin directory: 
 *
 * gcc -o example.so -shared example.c `pkg-config --cflags bitlbee`
 * cp example.so /usr/local/lib/bitlbee
 */
#include <stdio.h>
#include <bitlbee.h>

void init_plugin(void)
{
	printf("I am a BitlBee plugin!\n");
}
