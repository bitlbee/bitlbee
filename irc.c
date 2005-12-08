  /********************************************************************\
  * BitlBee -- An IRC to other IM-networks gateway                     *
  *                                                                    *
  * Copyright 2002-2004 Wilmer van der Gaast and others                *
  \********************************************************************/

/* The big hairy IRCd part of the project                               */

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
  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
  Suite 330, Boston, MA  02111-1307  USA
*/

#define BITLBEE_CORE
#include "bitlbee.h"
#include "crypting.h"

static gboolean irc_userping( gpointer _irc );

GSList *irc_connection_list = NULL;

char *passchange (irc_t *irc, void *set, char *value) 
{
	setpassnc (irc, value);
	return (NULL);
}

irc_t *irc_new( int fd )
{
	irc_t *irc = g_new0( irc_t, 1 );
	
	struct sockaddr_in sock[1];
#ifdef IPV6
	struct sockaddr_in6 sock6[1];
#endif
	struct hostent *peer;
	unsigned int i, j;
	
	irc->fd = fd;
	irc->io_channel = g_io_channel_unix_new( fd );
#ifdef GLIB2
	g_io_channel_set_encoding (irc->io_channel, NULL, NULL);
	g_io_channel_set_buffered (irc->io_channel, FALSE);
	g_io_channel_set_flags( irc->io_channel, G_IO_FLAG_NONBLOCK, NULL );
#else
	fcntl( irc->fd, F_SETFL, O_NONBLOCK);
#endif
	irc->r_watch_source_id = g_io_add_watch( irc->io_channel, G_IO_IN | G_IO_ERR | G_IO_HUP, bitlbee_io_current_client_read, irc );
	
	irc->status = USTATUS_OFFLINE;
	irc->last_pong = gettime();
	
	irc->userhash = g_hash_table_new( g_str_hash, g_str_equal );
	irc->watches = g_hash_table_new( g_str_hash, g_str_equal );
	
	strcpy( irc->umode, UMODE );
	irc->mynick = g_strdup( ROOT_NICK );
	irc->channel = g_strdup( ROOT_CHAN );
	
	i = sizeof( *sock );
#ifdef IPV6
	j = sizeof( *sock6 );
#endif
	if( global.conf->hostname )
		irc->myhost = g_strdup( global.conf->hostname );
	else if( getsockname( irc->fd, (struct sockaddr*) sock, &i ) == 0 && sock->sin_family == AF_INET )
	{
		if( ( peer = gethostbyaddr( (char*) &sock->sin_addr, sizeof( sock->sin_addr ), AF_INET ) ) )
			irc->myhost = g_strdup( peer->h_name );
	}
#ifdef IPV6
	else if( getsockname( irc->fd, (struct sockaddr*) sock6, &j ) == 0 && sock6->sin6_family == AF_INET6 )
	{
		if( ( peer = gethostbyaddr( (char*) &sock6->sin6_addr, sizeof( sock6->sin6_addr ), AF_INET6 ) ) )
			irc->myhost = g_strdup( peer->h_name );
	}
#endif
	
	i = sizeof( *sock );
#ifdef IPV6
	j = sizeof( *sock6 );
#endif
	if( getpeername( irc->fd, (struct sockaddr*) sock, &i ) == 0 && sock->sin_family == AF_INET )
	{
		if( ( peer = gethostbyaddr( (char*) &sock->sin_addr, sizeof( sock->sin_addr ), AF_INET ) ) )
			irc->host = g_strdup( peer->h_name );
	}
#ifdef IPV6
	else if( getpeername( irc->fd, (struct sockaddr*) sock6, &j ) == 0 && sock6->sin6_family == AF_INET6 )
	{
		if( ( peer = gethostbyaddr( (char*) &sock6->sin6_addr, sizeof( sock6->sin6_addr ), AF_INET6 ) ) )
			irc->host = g_strdup( peer->h_name );
	}
#endif
	
	if( !irc->host ) irc->host = g_strdup( "localhost." );
	if( !irc->myhost ) irc->myhost = g_strdup( "localhost." );

	if( global.conf->ping_interval > 0 && global.conf->ping_timeout > 0 )
		irc->ping_source_id = g_timeout_add( global.conf->ping_interval * 1000, irc_userping, irc );
	
	irc_write( irc, ":%s NOTICE AUTH :%s", irc->myhost, "BitlBee-IRCd initialized, please go on" );

	irc_connection_list = g_slist_append( irc_connection_list, irc );
	
	set_add( irc, "away_devoice", "true",  set_eval_away_devoice );
	set_add( irc, "auto_connect", "true", set_eval_bool );
	set_add( irc, "auto_reconnect", "false", set_eval_bool );
	set_add( irc, "auto_reconnect_delay", "300", set_eval_int );
	set_add( irc, "buddy_sendbuffer", "false", set_eval_bool );
	set_add( irc, "buddy_sendbuffer_delay", "200", set_eval_int );
	set_add( irc, "charset", "iso8859-1", set_eval_charset );
	set_add( irc, "debug", "false", set_eval_bool );
	set_add( irc, "default_target", "root", NULL );
	set_add( irc, "display_namechanges", "false", set_eval_bool );
	set_add( irc, "handle_unknown", "root", NULL );
	set_add( irc, "lcnicks", "true", set_eval_bool );
	set_add( irc, "ops", "both", set_eval_ops );
	set_add( irc, "private", "true", set_eval_bool );
	set_add( irc, "query_order", "lifo", NULL );
	set_add( irc, "save_on_quit", "true", set_eval_bool );
	set_add( irc, "strip_html", "true", NULL );
	set_add( irc, "to_char", ": ", set_eval_to_char );
	set_add( irc, "typing_notice", "false", set_eval_bool );
	set_add( irc, "password", NULL, passchange);
	
	conf_loaddefaults( irc );
	
	return( irc );
}

static gboolean irc_free_userhash( gpointer key, gpointer value, gpointer data )
{
	g_free( key );
	
	return( TRUE );
}

/* Because we have no garbage collection, this is quite annoying */
void irc_free(irc_t * irc)
{
	account_t *account, *accounttmp;
	user_t *user, *usertmp;
	nick_t *nick, *nicktmp;
	help_t *helpnode, *helpnodetmp;
	set_t *setnode, *setnodetmp;
	
	log_message( LOGLVL_INFO, "Destroying connection with fd %d", irc->fd );
	
	if( irc->status >= USTATUS_IDENTIFIED && set_getint( irc, "save_on_quit" ) ) 
		if( !bitlbee_save( irc ) )
			irc_usermsg( irc, "Error while saving settings!" );
	
	if( irc->ping_source_id > 0 )
		g_source_remove( irc->ping_source_id );
	g_source_remove( irc->r_watch_source_id );
	if( irc->w_watch_source_id > 0 )
		g_source_remove( irc->w_watch_source_id );
	
	g_io_channel_unref( irc->io_channel );
	irc_connection_list = g_slist_remove( irc_connection_list, irc );
	
	for (account = irc->accounts; account; account = account->next)
		if (account->gc)
			signoff(account->gc);
	
	g_free(irc->sendbuffer);
	g_free(irc->readbuffer);
	
	g_free(irc->nick);
	g_free(irc->user);
	g_free(irc->host);
	g_free(irc->realname);
	g_free(irc->password);
	
	g_free(irc->myhost);
	g_free(irc->mynick);
	
	g_free(irc->channel);
	
	while (irc->queries != NULL)
		query_del(irc, irc->queries);
	
	if (irc->accounts != NULL) {
		account = irc->accounts;
		while (account != NULL) {
			g_free(account->user);
			g_free(account->pass);
			g_free(account->server);
			accounttmp = account;
			account = account->next;
			g_free(accounttmp);
		}
	}
	
	if (irc->users != NULL) {
		user = irc->users;
		while (user != NULL) {
			g_free(user->nick);
			g_free(user->away);
			g_free(user->handle);
			if(user->user!=user->nick) g_free(user->user);
			if(user->host!=user->nick) g_free(user->host);
			if(user->realname!=user->nick) g_free(user->realname);
			gaim_input_remove(user->sendbuf_timer);
					
			usertmp = user;
			user = user->next;
			g_free(usertmp);
		}
	}
	
	g_hash_table_foreach_remove(irc->userhash, irc_free_userhash, NULL);
	g_hash_table_destroy(irc->userhash);
	
	g_hash_table_foreach_remove(irc->watches, irc_free_userhash, NULL);
	g_hash_table_destroy(irc->watches);
	
	if (irc->nicks != NULL) {
		nick = irc->nicks;
		while (nick != NULL) {
			g_free(nick->nick);
			g_free(nick->handle);
					
			nicktmp = nick;
			nick = nick->next;
			g_free(nicktmp);
		}
	}
	if (irc->help != NULL) {
		helpnode = irc->help;
		while (helpnode != NULL) {
			g_free(helpnode->string);
			
			helpnodetmp = helpnode;
			helpnode = helpnode->next;
			g_free(helpnodetmp);
		}
	}
	if (irc->set != NULL) {
		setnode = irc->set;
		while (setnode != NULL) {
			g_free(setnode->key);
			g_free(setnode->def);
			g_free(setnode->value);
			
			setnodetmp = setnode;
			setnode = setnode->next;
			g_free(setnodetmp);
		}
	}
	g_free(irc);
	
	if( global.conf->runmode == RUNMODE_INETD )
		g_main_quit( global.loop );
}

int irc_process( irc_t *irc )
{
	char **lines, *temp;	
	int i;

	if( irc->readbuffer != NULL ) {
		lines = irc_tokenize(irc->readbuffer );
		for( i = 0; *lines[i] != '\0'; i++ ) {
			if( lines[i+1] == NULL ) {
				temp = g_strdup( lines[i] );
				g_free( irc->readbuffer );
				irc->readbuffer = temp;
				i++;
				break;
			}			
			if (!irc_process_line(irc, lines[i])) {
				g_free( lines );
				return 0;
			}
		}
		if(lines[i]!=NULL) {
			g_free(irc->readbuffer);
			irc->readbuffer=NULL;	
		}
		g_free( lines );
	}
	return 1;	
}

char **irc_tokenize( char *buffer )
{
	int i, j;
	char **lines;

	/* Count the number of elements we're gonna need. */
	for(i=0, j=1; buffer[i]!='\0'; i++ ) {
		if(buffer[i]=='\n' )
			if(buffer[i+1]!='\r' && buffer[i+1]!='\n')
				j++;
	}
	
	/* Allocate j+1 elements. */
	lines=g_new (char *, j+1);
	
	/* NULL terminate our list. */ 
	lines[j]=NULL;
	
	lines[0]=buffer;
	
	/* Split the buffer in several strings, using \r\n as our seperator, where \r is optional.
	 * Although this is not in the RFC, some braindead ircds (newnet's) use this, so some clients might too. 
	 */
	for( i=0, j=0; buffer[i]!='\0'; i++) {
		if(buffer[i]=='\n') {
			buffer[i]='\0';

			/* We dont want to read 1 byte before our buffer
			 * and (in rare cases) generate a SIGSEGV.
			 */
			if(i!=0)
				if(buffer[i-1]=='\r')
					buffer[i-1]='\0';
			if(buffer[i+1]!='\r'&&buffer[i+1]!='\n')
				lines[++j]=buffer+i+1;
		}
	}

	return(lines);
}

int irc_process_line( irc_t *irc, char *line )
{
	int i, j;
	char **cmd;
	
	/* Move the line pointer to the start of the command, skipping spaces and the optional prefix. */
	if(line[0]==':') {
		for(i=0; line[i]!=32; i++);
		line=line+i;
	}
	for(i=0; line[i]==32; i++);
	line=line+i;

	/* If we're already at the end of the line, return. If not, we're going to need at least one element. */
	if(line[0]=='\0')
		return 1;
	else
		j=1;	
	
	/* Count the number of char **cmd elements we're going to need. */	
	for(i=0; line[i]!='\0'; i++) {
		if((line[i]==32) && (line[i+1]!=32) && (line[i+1]!='\0') && (line[i+1]!=':'))		
			j++;
		else if((line[i]==':') && (line[i+1]!='\0') && (line[i-1]==32)) {
			j++;
			break;
		}
			
	}	

	/* Allocate the space we need. */
	cmd=g_new(char *, j+1);
	cmd[j]=NULL;
	
	/* Do the actual line splitting, format is:
	 * Input: "PRIVMSG #bitlbee :foo bar"
	 * Output: cmd[0]=="PRIVMSG", cmd[1]=="#bitlbee", cmd[2]=="foo bar", cmd[3]==NULL
	 */

	cmd[0]=line;
	for(i=0, j=0; line[i]!='\0'; i++) {
		if((line[i]==32)) {
			line[i]='\0';
			if((line[i+1]!=32) && (line[i+1]!='\0') && (line[i+1]!=':')) 		
				cmd[++j]=line+i+1;
		}
		else if((line[i]==':') && (line[i+1]!='\0') && (line[i-1]=='\0')) {
			cmd[++j]=line+i+1;
			break;
		}
	}
	
	i=irc_exec(irc, cmd);
	g_free(cmd);

	return(i);	
}

int irc_exec( irc_t *irc, char **cmd )
{	
	int i;

	if( (global.conf)->authmode == AUTHMODE_CLOSED && irc->status < USTATUS_AUTHORIZED )
	{
		if( g_strcasecmp( cmd[0], "PASS" ) == 0 )
		{
			if( !cmd[1] )
			{
				irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
			}
			else if( strcmp( cmd[1], (global.conf)->password ) == 0 )
			{
				irc->status = USTATUS_AUTHORIZED;
			}
			else
			{
				irc_reply( irc, 464, ":Nope, maybe you should try it again..." );
			}
		}
		else
		{
			irc_reply( irc, 464, ":Uhh, fine, but I want the password first." );
		}
		
		return( 1 );
	}
	
	if( g_strcasecmp( cmd[0], "USER" ) == 0 )
	{
		if( !( cmd[1] && cmd[2] && cmd[3] && cmd[4] ) )
		{
			irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
		}
		else if( irc->user )
		{
			irc_reply( irc, 462, ":You can't change your nick/userinfo" );
		}
		else
		{
			irc->user = g_strdup( cmd[1] );
			irc->realname = g_strdup( cmd[4] );
			if( irc->nick ) irc_login( irc );
		}
		return( 1 );
	}
	else if( g_strcasecmp( cmd[0], "NICK" ) == 0 )
	{
		if( !cmd[1] )
		{
			irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
		}
		else if( irc->nick )
		{
			irc_reply( irc, 438, ":The hand of the deity is upon thee, thy nick may not change" );
		}
		/* This is not clean, but for now it'll have to be like this... */
		else if( ( nick_cmp( cmd[1], irc->mynick ) == 0 ) || ( nick_cmp( cmd[1], NS_NICK ) == 0 ) )
		{
			irc_reply( irc, 433, ":This nick is already in use" );
		}
		else if( !nick_ok( cmd[1] ) )
		{
			/* [SH] Invalid characters. */
			irc_reply( irc, 432, ":This nick contains invalid characters" );
		}
		else
		{
			irc->nick = g_strdup( cmd[1] );
			if( irc->user ) irc_login( irc );
		}
		return( 1 );
	}
	else if( g_strcasecmp( cmd[0], "QUIT" ) == 0 )
	{
		irc_write( irc, "ERROR :%s%s", cmd[1]?"Quit: ":"", cmd[1]?cmd[1]:"Client Quit" );
		g_io_channel_close( irc->io_channel );
		return( 0 );
	}
	
	if( !irc->user || !irc->nick )
	{
		irc_reply( irc, 451, ":Register first" );
		return( 1 );
	}
	
	if( g_strcasecmp( cmd[0], "PING" ) == 0 )
	{
		irc_write( irc, ":%s PONG %s :%s", irc->myhost, irc->myhost, cmd[1]?cmd[1]:irc->myhost );
	}
	else if( g_strcasecmp( cmd[0], "MODE" ) == 0 )
	{
		if( !cmd[1] )
		{
			irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
		}
		else if( *cmd[1] == '#' || *cmd[1] == '&' )
		{
			if( cmd[2] )
			{
				if( *cmd[2] == '+' || *cmd[2] == '-' )
					irc_reply( irc, 477, "%s :Can't change channel modes", cmd[1] );
				else if( *cmd[2] == 'b' )
					irc_reply( irc, 368, "%s :No bans possible", cmd[1] );
			}
			else
				irc_reply( irc, 324, "%s +%s", cmd[1], CMODE );
		}
		else
		{
			if( nick_cmp( cmd[1], irc->nick ) == 0 )
			{
				if( cmd[2] )
					irc_umode_set( irc, irc->nick, cmd[2] );
			}
			else
				irc_reply( irc, 502, ":Don't touch their modes" );
		}
	}
	else if( g_strcasecmp( cmd[0], "NAMES" ) == 0 )
	{
		irc_names( irc, cmd[1]?cmd[1]:irc->channel );
	}
	else if( g_strcasecmp( cmd[0], "PART" ) == 0 )
	{
		struct conversation *c;
		
		if( !cmd[1] )
		{
			irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
		}
		else if( g_strcasecmp( cmd[1], irc->channel ) == 0 )
		{
			user_t *u = user_find( irc, irc->nick );
			
			/* Not allowed to leave control channel */
			irc_part( irc, u, irc->channel );
			irc_join( irc, u, irc->channel );
		}
		else if( ( c = conv_findchannel( cmd[1] ) ) )
		{
			user_t *u = user_find( irc, irc->nick );
			
			irc_part( irc, u, c->channel );
			
			if( c->gc && c->gc->prpl )
			{
				c->joined = 0;
				c->gc->prpl->chat_leave( c->gc, c->id );
			}
		}
		else
		{
			irc_reply( irc, 403, "%s :No such channel", cmd[1] );
		}
	}
	else if( g_strcasecmp( cmd[0], "JOIN" ) == 0 )
	{
		if( !cmd[1] )
		{
			irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
		}
		else if( g_strcasecmp( cmd[1], irc->channel ) == 0 )
			; /* Dude, you're already there...
			     RFC doesn't have any reply for that though? */
		else if( cmd[1] )
		{
			if( ( cmd[1][0] == '#' || cmd[1][0] == '&' ) && cmd[1][1] )
			{
				user_t *u = user_find( irc, cmd[1] + 1 );
				
				if( u && u->gc && u->gc->prpl && u->gc->prpl->chat_open )
				{
					irc_reply( irc, 403, "%s :Initializing groupchat in a different channel", cmd[1] );
					
					if( !u->gc->prpl->chat_open( u->gc, u->handle ) )
					{
						irc_usermsg( irc, "Could not open a groupchat with %s, maybe you don't have a connection to him/her yet?", u->nick );
					}
				}
				else
				{
					irc_reply( irc, 403, "%s :Groupchats are not possible with %s", cmd[1], cmd[1]+1 );
				}
			}
			else
			{
				irc_reply( irc, 403, "%s :No such channel", cmd[1] );
			}
		}
	}
	else if( g_strcasecmp( cmd[0], "INVITE" ) == 0 )
	{
		if( cmd[1] && cmd[2] )
			irc_invite( irc, cmd[1], cmd[2] );
		else
			irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
	}
	else if( g_strcasecmp( cmd[0], "PRIVMSG" ) == 0 || g_strcasecmp( cmd[0], "NOTICE" ) == 0 )
	{
		if( !cmd[1] )
		{
			irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
		} 
		else if ( !cmd[2] ) 
		{
			irc_reply( irc, 412, ":No text to send" );
		}
		else if ( irc->nick && g_strcasecmp( cmd[1], irc->nick ) == 0 ) 
		{
			irc_write( irc, ":%s!%s@%s %s %s :%s", irc->nick, irc->user, irc->host, cmd[0], cmd[1], cmd[2] ); 
		}
		else 
		{
			if( g_strcasecmp( cmd[1], irc->channel ) == 0 )
			{
				unsigned int i;
				char *t = set_getstr( irc, "default_target" );
				
				if( g_strcasecmp( t, "last" ) == 0 && irc->last_target )
					cmd[1] = irc->last_target;
				else if( g_strcasecmp( t, "root" ) == 0 )
					cmd[1] = irc->mynick;
				
				for( i = 0; i < strlen( cmd[2] ); i ++ )
				{
					if( cmd[2][i] == ' ' ) break;
					if( cmd[2][i] == ':' || cmd[2][i] == ',' )
					{
						cmd[1] = cmd[2];
						cmd[2] += i;
						*cmd[2] = 0;
						while( *(++cmd[2]) == ' ' );
						break;
					}
				}
				
				irc->is_private = 0;
				
				if( cmd[1] != irc->last_target )
				{
					if( irc->last_target )
						g_free( irc->last_target );
					irc->last_target = g_strdup( cmd[1] );
				}
			}
			else
			{
				irc->is_private = 1;
			}
			irc_send( irc, cmd[1], cmd[2], ( g_strcasecmp( cmd[0], "NOTICE" ) == 0 ) ? IM_FLAG_AWAY : 0 );
		}
	}
	else if( g_strcasecmp( cmd[0], "WHO" ) == 0 )
	{
		irc_who( irc, cmd[1] );
	}
	else if( g_strcasecmp( cmd[0], "USERHOST" ) == 0 )
	{
		user_t *u;
		
		if( !cmd[1] )
		{
			irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
		}
		/* [TV] Usable USERHOST-implementation according to
			RFC1459. Without this, mIRC shows an error
			while connecting, and the used way of rejecting
			breaks standards.
		*/
		
		for( i = 1; cmd[i]; i ++ )
			if( ( u = user_find( irc, cmd[i] ) ) )
			{
				if( u->online && u->away )
					irc_reply( irc, 302, ":%s=-%s@%s", u->nick, u->user, u->host );
				else
					irc_reply( irc, 302, ":%s=+%s@%s", u->nick, u->user, u->host );
			}
	}
	else if( g_strcasecmp( cmd[0], "ISON" ) == 0 )
	{
		user_t *u;
		char buff[IRC_MAX_LINE];
		int lenleft;
		
		buff[0] = '\0';
		
		/* [SH] Leave room for : and \0 */
		lenleft = IRC_MAX_LINE - 2;
		
		for( i = 1; cmd[i]; i ++ )
		{
			if( ( u = user_find( irc, cmd[i] ) ) && u->online )
			{
				/* [SH] Make sure we don't use too much buffer space. */
				lenleft -= strlen( u->nick ) + 1;
				
				if( lenleft < 0 )
				{
					break;
				}
				
				/* [SH] Add the nick to the buffer. Note
				 * that an extra space is always added. Even
				 * if it's the last nick in the list. Who
				 * cares?
				 */
				
				strcat( buff, u->nick );
				strcat( buff, " " );
			}
		}
		
		/* [WvG] Well, maybe someone cares, so why not remove it? */
		if( strlen( buff ) > 0 )
			buff[strlen(buff)-1] = '\0';
		
		/* [SH] By the way, that really *was* WvG talking. */
		/* [WvG] Really? */
		/* [SH] Yeah... But *this* is WvG talking too. ;-P */
		/* [WvG] *sigh* */
		
		irc_reply( irc, 303, ":%s", buff );
	}
	else if( g_strcasecmp( cmd[0], "WATCH" ) == 0 )
	{
		/* Obviously we could also mark a user structure as being
		   watched, but what if the WATCH command is sent right
		   after connecting? The user won't exist yet then... */
		for( i = 1; cmd[i]; i ++ )
		{
			char *nick;
			user_t *u;
			
			if( !cmd[i][0] || !cmd[i][1] )
				break;
			
			nick = g_strdup( cmd[i] + 1 );
			nick_lc( nick );
			
			u = user_find( irc, nick );
			
			if( cmd[i][0] == '+' )
			{
				if( !g_hash_table_lookup( irc->watches, nick ) )
					g_hash_table_insert( irc->watches, nick, nick );
				
				if( u && u->online )
					irc_reply( irc, 604, "%s %s %s %d :%s", u->nick, u->user, u->host, time( NULL ), "is online" );
				else
					irc_reply( irc, 605, "%s %s %s %d :%s", nick, "*", "*", time( NULL ), "is offline" );
			}
			else if( cmd[i][0] == '-' )
			{
				gpointer okey, ovalue;
				
				if( g_hash_table_lookup_extended( irc->watches, nick, &okey, &ovalue ) )
				{
					g_free( okey );
					g_hash_table_remove( irc->watches, okey );
					
					irc_reply( irc, 602, "%s %s %s %d :%s", nick, "*", "*", 0, "Stopped watching" );
				}
			}
		}
	}
	else if( g_strcasecmp( cmd[0], "TOPIC" ) == 0 )
	{
		if( cmd[1] && cmd[2] )
			irc_reply( irc, 482, "%s :Cannot change topic", cmd[1] );
		else if( cmd[1] )
			irc_topic( irc, cmd[1] );
		else
			irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
	}
	else if( g_strcasecmp( cmd[0], "AWAY" ) == 0 )
	{
		irc_away( irc, cmd[1] );
	}
	else if( g_strcasecmp( cmd[0], "WHOIS" ) == 0 )
	{
		if( cmd[1] )
		{
			irc_whois( irc, cmd[1] );
		}
		else
		{
			irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
		}
	}
	else if( g_strcasecmp( cmd[0], "WHOWAS" ) == 0 )
	{
		/* For some reason irssi tries a whowas when whois fails. We can
		   ignore this, but then the user never gets a "user not found"
		   message from irssi which is a bit annoying. So just respond
		   with not-found and irssi users will get better error messages */
		
		if( cmd[1] )
		{
			irc_reply( irc, 406, "%s :Nick does not exist", cmd[1] );
			irc_reply( irc, 369, "%s :End of WHOWAS", cmd[1] );
		}
		else
		{
			irc_reply( irc, 461, "%s :Need more parameters", cmd[0] );
		}
	}
	else if( ( g_strcasecmp( cmd[0], "NICKSERV" ) == 0 ) || ( g_strcasecmp( cmd[0], "NS" ) == 0 ) )
	{
		/* [SH] This aliases the NickServ command to PRIVMSG root */
		/* [TV] This aliases the NS command to PRIVMSG root as well */
		root_command( irc, cmd + 1 );
	}
	else if( g_strcasecmp( cmd[0], "MOTD" ) == 0 )
	{
		irc_motd( irc );
	}
	else if( g_strcasecmp( cmd[0], "PONG" ) == 0 )
	{
		/* We could check the value we get back from the user, but in
		   fact we don't care, we're just happy he's still alive. */
		irc->last_pong = gettime();
		irc->pinging = 0;
	}
	else if( g_strcasecmp( cmd[0], "COMPLETIONS" ) == 0 )
	{
		user_t *u = user_find( irc, irc->mynick );
		help_t *h;
		set_t *s;
		int i;
		
		irc_privmsg( irc, u, "NOTICE", irc->nick, "COMPLETIONS ", "OK" );
		
		for( i = 0; commands[i].command; i ++ )
			irc_privmsg( irc, u, "NOTICE", irc->nick, "COMPLETIONS ", commands[i].command );
		
		for( h = global.help; h; h = h->next )
			irc_privmsg( irc, u, "NOTICE", irc->nick, "COMPLETIONS help ", h->string );
		
		for( s = irc->set; s; s = s->next )
			irc_privmsg( irc, u, "NOTICE", irc->nick, "COMPLETIONS set ", s->key );
		
		irc_privmsg( irc, u, "NOTICE", irc->nick, "COMPLETIONS ", "END" );
	}
	else if( set_getint( irc, "debug" ) )
	{
		irc_usermsg( irc, "\002--- Unknown command:" );
		for( i = 0; cmd[i]; i ++ ) irc_usermsg( irc, "%s", cmd[i] );
		irc_usermsg( irc, "\002--------------------" );
	}
	
	return( 1 );
}

void irc_reply( irc_t *irc, int code, char *format, ... )
{
	char text[IRC_MAX_LINE];
	va_list params;
	
	va_start( params, format );
	g_vsnprintf( text, IRC_MAX_LINE, format, params );
	va_end( params );
	irc_write( irc, ":%s %03d %s %s", irc->myhost, code, irc->nick?irc->nick:"*", text );
	
	return;
}

int irc_usermsg( irc_t *irc, char *format, ... )
{
	char text[1024];
	va_list params;
	char is_private = 0;
	user_t *u;
	
	u = user_find( irc, irc->mynick );
	if( u ) is_private = u->is_private;
	
	va_start( params, format );
	g_vsnprintf( text, sizeof( text ), format, params );
	va_end( params );
	
	return( irc_msgfrom( irc, u->nick, text ) );
}

void irc_write( irc_t *irc, char *format, ... ) 
{
	va_list params;

	va_start( params, format );
	irc_vawrite( irc, format, params );	
	va_end( params );

	return;

}

void irc_vawrite( irc_t *irc, char *format, va_list params )
{
	int size;
	char line[IRC_MAX_LINE];
	
	if( irc->quit )
		return;

	g_vsnprintf( line, IRC_MAX_LINE - 3, format, params );

	strip_newlines( line );
	strcat( line, "\r\n" );

	if( irc->sendbuffer != NULL ) {
		size = strlen( irc->sendbuffer ) + strlen( line );
#ifdef FLOOD_SEND
		if( size > FLOOD_SEND_MAXBUFFER ) {
			/* Die flooder, die! >:) */

			g_free(irc->sendbuffer);
			
			/* We need the \r\n at the start because else we might append our string to a half
			 * sent line. A bit hackish, but it works.
			 */
			irc->sendbuffer = g_strdup( "\r\nERROR :Sendq Exceeded\r\n" );
			irc->quit = 1;
			
			return;
		}
#endif
		irc->sendbuffer = g_renew ( char, irc->sendbuffer, size + 1 );
		strcpy( ( irc->sendbuffer + strlen( irc->sendbuffer ) ), line );
	}
	else 
		irc->sendbuffer = g_strdup(line);	
	
	if( irc->w_watch_source_id == 0 )
	{
		irc->w_watch_source_id = g_io_add_watch( irc->io_channel, G_IO_OUT, bitlbee_io_current_client_write, irc );
	}
	
	return;
}

void irc_write_all( int now, char *format, ... )
{
	va_list params;
	GSList *temp;	
	
	va_start( params, format );
	
	temp = irc_connection_list;
	while( temp != NULL )
	{
		irc_t *irc = temp->data;
		
		if( now )
		{
			g_free( irc->sendbuffer );
			irc->sendbuffer = g_strdup( "\r\n" );
		}
		irc_vawrite( temp->data, format, params );
		if( now )
		{
			bitlbee_io_current_client_write( irc->io_channel, G_IO_OUT, irc );
		}
		temp = temp->next;
	}
	
	va_end( params );
	return;
} 

void irc_names( irc_t *irc, char *channel )
{
	user_t *u = irc->users;
	char *s;
	int control = ( g_strcasecmp( channel, irc->channel ) == 0 );
	struct conversation *c = NULL;
	
	if( !control )
		c = conv_findchannel( channel );
	
	/* RFC's say there is no error reply allowed on NAMES, so when the
	   channel is invalid, just give an empty reply. */
	
	if( control || c ) while( u )
	{
		if( u->online )
		{
			if( u->gc && control )
			{
				if( set_getint( irc, "away_devoice" ) && !u->away )
					s = "+";
				else
					s = "";
				
				irc_reply( irc, 353, "@ %s :%s%s", channel, s, u->nick );
			}
			else if( !u->gc )
			{
				if( strcmp( u->nick, irc->mynick ) == 0 && ( strcmp( set_getstr( irc, "ops" ), "root" ) == 0 || strcmp( set_getstr( irc, "ops" ), "both" ) == 0 ) )
					s = "@";
				else if( strcmp( u->nick, irc->nick ) == 0 && ( strcmp( set_getstr( irc, "ops" ), "user" ) == 0 || strcmp( set_getstr( irc, "ops" ), "both" ) == 0 ) )
					s = "@";
				else
					s = "";
				
				irc_reply( irc, 353, "@ %s :%s%s", channel, s, u->nick );
			}
		}
		
		u = u->next;
	}
	
	/* For non-controlchannel channels (group conversations) only root and
	   you are listed now. Time to show the channel people: */
	if( !control && c )
	{
		GList *l;
		
		for( l = c->in_room; l; l = l->next )
			if( ( u = user_findhandle( c->gc, l->data ) ) )
				irc_reply( irc, 353, "@ %s :%s%s", channel, "", u->nick );
	}
	
	irc_reply( irc, 366, "%s :End of /NAMES list", channel );
}

void irc_who( irc_t *irc, char *channel )
{
	user_t *u = irc->users;
	struct conversation *c;
	GList *l;
	
	if( !channel || *channel == '0' || *channel == '*' || !*channel )
		while( u )
		{
			irc_reply( irc, 352, "%s %s %s %s %s %c :0 %s", u->online ? irc->channel : "*", u->user, u->host, irc->myhost, u->nick, u->online ? ( u->away ? 'G' : 'H' ) : 'G', u->realname );
			u = u->next;
		}
	else if( g_strcasecmp( channel, irc->channel ) == 0 )
		while( u )
		{
			if( u->online )
				irc_reply( irc, 352, "%s %s %s %s %s %c :0 %s", channel, u->user, u->host, irc->myhost, u->nick, u->away ? 'G' : 'H', u->realname );
			u = u->next;
		}
	else if( ( c = conv_findchannel( channel ) ) )
		for( l = c->in_room; l; l = l->next )
		{
			if( ( u = user_findhandle( c->gc, l->data ) ) )
				irc_reply( irc, 352, "%s %s %s %s %s %c :0 %s", channel, u->user, u->host, irc->myhost, u->nick, u->away ? 'G' : 'H', u->realname );
		}
	else if( ( u = user_find( irc, channel ) ) )
		irc_reply( irc, 352, "%s %s %s %s %s %c :0 %s", channel, u->user, u->host, irc->myhost, u->nick, u->online ? ( u->away ? 'G' : 'H' ) : 'G', u->realname );
	
	irc_reply( irc, 315, "%s :End of /WHO list.", channel?channel:"**" );
}

void irc_login( irc_t *irc )
{
	user_t *u;
	
	irc_reply( irc,   1, ":Welcome to the BitlBee gateway, %s", irc->nick );
	irc_reply( irc,   2, ":Host %s is running BitlBee " BITLBEE_VERSION " " ARCH "/" CPU ".", irc->myhost );
	irc_reply( irc,   3, ":%s", IRCD_INFO );
	irc_reply( irc,   4, "%s %s %s %s", irc->myhost, BITLBEE_VERSION, UMODES, CMODES );
	irc_motd( irc );
	irc_umode_set( irc, irc->myhost, "+" UMODE );

	u = user_add( irc, irc->mynick );
	u->host = g_strdup( irc->myhost );
	u->realname = g_strdup( ROOT_FN );
	u->online = 1;
	u->send_handler = root_command_string;
	u->is_private = 0; /* [SH] The channel is root's personal playground. */
	irc_spawn( irc, u );
	
	u = user_add( irc, NS_NICK );
	u->host = g_strdup( irc->myhost );
	u->realname = g_strdup( ROOT_FN );
	u->online = 0;
	u->send_handler = root_command_string;
	u->is_private = 1; /* [SH] NickServ is not in the channel, so should always /query. */
	
	u = user_add( irc, irc->nick );
	u->user = g_strdup( irc->user );
	u->host = g_strdup( irc->host );
	u->realname = g_strdup( irc->realname );
	u->online = 1;
//	u->send_handler = msg_echo;
	irc_spawn( irc, u );
	
	irc_usermsg( irc, "Welcome to the BitlBee gateway!\n\nIf you've never used BitlBee before, please do read the help information using the \x02help\x02 command. Lots of FAQ's are answered there." );
	
	irc->status = USTATUS_LOGGED_IN;
}

void irc_motd( irc_t *irc )
{
	int fd;
	
	fd = open( global.conf->motdfile, O_RDONLY );
	if( fd == -1 )
	{
		irc_reply( irc, 422, ":We don't need MOTDs." );
	}
	else
	{
		char linebuf[80];	/* Max. line length for MOTD's is 79 chars. It's what most IRC networks seem to do. */
		char *add, max;
		int len;
		
		linebuf[79] = len = 0;
		max = sizeof( linebuf ) - 1;
		
		irc_reply( irc, 375, ":- %s Message Of The Day - ", irc->myhost );
		while( read( fd, linebuf + len, 1 ) == 1 )
		{
			if( linebuf[len] == '\n' || len == max )
			{
				linebuf[len] = 0;
				irc_reply( irc, 372, ":- %s", linebuf );
				len = 0;
			}
			else if( linebuf[len] == '%' )
			{
				read( fd, linebuf + len, 1 );
				if( linebuf[len] == 'h' )
					add = irc->myhost;
				else if( linebuf[len] == 'v' )
					add = BITLBEE_VERSION;
				else if( linebuf[len] == 'n' )
					add = irc->nick;
				else
					add = "%";
				
				strncpy( linebuf + len, add, max - len );
				while( linebuf[++len] );
			}
			else if( len < max )
			{
				len ++;
			}
		}
		irc_reply( irc, 376, ":End of MOTD" );
		closesocket( fd );
	}
}

void irc_topic( irc_t *irc, char *channel )
{
	if( g_strcasecmp( channel, irc->channel ) == 0 )
	{
		irc_reply( irc, 332, "%s :%s", channel, CONTROL_TOPIC );
	}
	else
	{
		struct conversation *c = conv_findchannel( channel );
		
		if( c )
			irc_reply( irc, 332, "%s :BitlBee groupchat: \"%s\". Please keep in mind that root-commands won't work here. Have fun!", channel, c->title );
		else
			irc_reply( irc, 331, "%s :No topic for this channel" );
	}
}

void irc_whois( irc_t *irc, char *nick )
{
	user_t *u = user_find( irc, nick );
	
	if( u )
	{
		irc_reply( irc, 311, "%s %s %s * :%s", u->nick, u->user, u->host, u->realname );
		
		if( u->gc )
			irc_reply( irc, 312, "%s %s.%s :%s network", u->nick, u->gc->user->username,
			           *u->gc->user->proto_opt[0] ? u->gc->user->proto_opt[0] : "", proto_name[u->gc->user->protocol] );
		else
			irc_reply( irc, 312, "%s %s :%s", u->nick, irc->myhost, IRCD_INFO );
		
		if( !u->online )
			irc_reply( irc, 301, "%s :%s", u->nick, "User is offline" );
		else if( u->away )
			irc_reply( irc, 301, "%s :%s", u->nick, u->away );
		
		irc_reply( irc, 318, "%s :End of /WHOIS list", nick );
	}
	else
	{
		irc_reply( irc, 401, "%s :Nick does not exist", nick );
	}
}


void irc_umode_set( irc_t *irc, char *who, char *s )
{
	char m[256], st = 1, *t;
	int i;
	
	memset( m, 0, sizeof( m ) );
	
	for( t = irc->umode; *t; t ++ )
		m[(int)*t] = 1;
	
	for( t = s; *t; t ++ )
	{
		if( *t == '+' || *t == '-' )
			st = *t == '+';
		else
			m[(int)*t] = st;
	}
	
	memset( irc->umode, 0, sizeof( irc->umode ) );
	
	for( i = 0; i < 256 && strlen( irc->umode ) < ( sizeof( irc->umode ) - 1 ); i ++ )
		if( m[i] && strchr( UMODES, i ) )
			irc->umode[strlen(irc->umode)] = i;
	
	irc_reply( irc, 221, "+%s", irc->umode );
}

int irc_away( irc_t *irc, char *away )
{
	user_t *u = user_find( irc, irc->nick );
	GSList *c = get_connections();
	
	if( !u ) return( 0 );
	
	if( away && *away )
	{
		int i, j;
		
		/* Copy away string, but skip control chars. Mainly because
		   Jabber really doesn't like them. */
		u->away = g_malloc( strlen( away ) + 1 );
		for( i = j = 0; away[i]; i ++ )
			if( ( u->away[j] = away[i] ) >= ' ' )
				j ++;
		u->away[j] = 0;
		
		irc_reply( irc, 306, ":You're now away: %s", u->away );
		/* irc_umode_set( irc, irc->myhost, "+a" ); */
	}
	else
	{
		if( u->away ) g_free( u->away );
		u->away = NULL;
		/* irc_umode_set( irc, irc->myhost, "-a" ); */
		irc_reply( irc, 305, ":Welcome back" );
	}
	
	while( c )
	{
		if( ((struct gaim_connection *)c->data)->flags & OPT_LOGGED_IN )
			proto_away( c->data, u->away );
		
		c = c->next;
	}
	
	return( 1 );
}

void irc_spawn( irc_t *irc, user_t *u )
{
	irc_join( irc, u, irc->channel );
}

void irc_join( irc_t *irc, user_t *u, char *channel )
{
	char *nick;
	
	if( ( g_strcasecmp( channel, irc->channel ) != 0 ) || user_find( irc, irc->nick ) )
		irc_write( irc, ":%s!%s@%s JOIN :%s", u->nick, u->user, u->host, channel );
	
	if( nick_cmp( u->nick, irc->nick ) == 0 )
	{
		irc_write( irc, ":%s MODE %s +%s", irc->myhost, channel, CMODE );
		irc_names( irc, channel );
		irc_topic( irc, channel );
	}
	
	nick = g_strdup( u->nick );
	nick_lc( nick );
	if( g_hash_table_lookup( irc->watches, nick ) )
	{
		irc_reply( irc, 600, "%s %s %s %d :%s", u->nick, u->user, u->host, time( NULL ), "logged online" );
	}
	g_free( nick );
}

void irc_part( irc_t *irc, user_t *u, char *channel )
{
	irc_write( irc, ":%s!%s@%s PART %s :%s", u->nick, u->user, u->host, channel, "" );
}

void irc_kick( irc_t *irc, user_t *u, char *channel, user_t *kicker )
{
	irc_write( irc, ":%s!%s@%s KICK %s %s :%s", kicker->nick, kicker->user, kicker->host, channel, u->nick, "" );
}

void irc_kill( irc_t *irc, user_t *u )
{
	char *nick;
	
	irc_write( irc, ":%s!%s@%s QUIT :%s", u->nick, u->user, u->host, "Leaving..." );
	
	nick = g_strdup( u->nick );
	nick_lc( nick );
	if( g_hash_table_lookup( irc->watches, nick ) )
	{
		irc_reply( irc, 601, "%s %s %s %d :%s", u->nick, u->user, u->host, time( NULL ), "logged offline" );
	}
	g_free( nick );
}

void irc_invite( irc_t *irc, char *nick, char *channel )
{
	struct conversation *c = conv_findchannel( channel );
	user_t *u = user_find( irc, nick );
	
	if( u && c && ( u->gc == c->gc ) )
		if( c->gc && c->gc->prpl && c->gc->prpl->chat_invite )
		{
			c->gc->prpl->chat_invite( c->gc, c->id, "", u->handle );
			irc_reply( irc, 341, "%s %s", nick, channel );
			return;
		}
	
	irc_reply( irc, 482, "%s :Invite impossible; User/Channel non-existent or incompatible", channel );
}

int irc_send( irc_t *irc, char *nick, char *s, int flags )
{
	struct conversation *c = NULL;
	user_t *u = NULL;
	
	if( *nick == '#' || *nick == '&' )
	{
		if( !( c = conv_findchannel( nick ) ) )
		{
			irc_reply( irc, 403, "%s :Channel does not exist", nick );
			return( 0 );
		}
	}
	else
	{
		u = user_find( irc, nick );
		
		if( !u )
		{
			if( irc->is_private )
				irc_reply( irc, 401, "%s :Nick does not exist", nick );
			else
				irc_usermsg( irc, "Nick `%s' does not exist!", nick );
			return( 0 );
		}
	}
	
	if( *s == 1 && s[strlen(s)-1] == 1 )
	{
		if( g_strncasecmp( s + 1, "ACTION", 6 ) == 0 )
		{
			if( s[7] == ' ' ) s ++;
			s += 3;
			*(s++) = '/';
			*(s++) = 'm';
			*(s++) = 'e';
			*(s++) = ' ';
			s -= 4;
			s[strlen(s)-1] = 0;
		}
		else if( g_strncasecmp( s + 1, "VERSION", 7 ) == 0 )
		{
			u = user_find( irc, irc->mynick );
			irc_privmsg( irc, u, "NOTICE", irc->nick, "", "\001VERSION BitlBee " BITLBEE_VERSION " " ARCH "/" CPU "\001" );
			return( 1 );
		}
		else if( g_strncasecmp( s + 1, "PING", 4 ) == 0 )
		{
			u = user_find( irc, irc->mynick );
			irc_privmsg( irc, u, "NOTICE", irc->nick, "", s );
			return( 1 );
		}
		else if( g_strncasecmp( s + 1, "TYPING", 6 ) == 0 )
		{
			if( u && u->gc && u->gc->prpl->send_typing && strlen( s ) >= 10 )
			{
				time_t current_typing_notice = time( NULL );
				
				if( current_typing_notice - u->last_typing_notice >= 5 )
				{
					u->gc->prpl->send_typing( u->gc, u->handle, s[8] == '1' );
					u->last_typing_notice = current_typing_notice;
				}
			}
			return( 1 );
		}
		else
		{
			irc_usermsg( irc, "Non-ACTION CTCP's aren't supported" );
			return( 0 );
		}
	}
	
	if( u )
	{
		/* For the next message, we probably do have to send new notices... */
		u->last_typing_notice = 0;
		u->is_private = irc->is_private;
		
		if( u->is_private )
		{
			if( !u->online )
				irc_reply( irc, 301, "%s :%s", u->nick, "User is offline" );
			else if( u->away )
				irc_reply( irc, 301, "%s :%s", u->nick, u->away );
		}
		
		if( u->send_handler )
			return( u->send_handler( irc, u, s, flags ) );
	}
	else if( c && c->gc && c->gc->prpl )
	{
		return( serv_send_chat( irc, c->gc, c->id, s ) );
	}
	
	return( 0 );
}

gboolean buddy_send_handler_delayed( gpointer data )
{
	user_t *u = data;
	
	u->sendbuf[u->sendbuf_len-2] = 0; /* Cut off the last newline */
	serv_send_im( u->gc->irc, u, u->sendbuf, u->sendbuf_flags );
	
	g_free( u->sendbuf );
	u->sendbuf = NULL;
	u->sendbuf_len = 0;
	u->sendbuf_timer = 0;
	u->sendbuf_flags = 0;
	
	return( FALSE );
}

int buddy_send_handler( irc_t *irc, user_t *u, char *msg, int flags )
{
	if( !u || !u->gc ) return( 0 );
	
	if( set_getint( irc, "buddy_sendbuffer" ) && set_getint( irc, "buddy_sendbuffer_delay" ) > 0 )
	{
		int delay;
		
		if( u->sendbuf_len > 0 && u->sendbuf_flags != flags)
		{
			//Flush the buffer
			g_source_remove( u->sendbuf_timer );
			buddy_send_handler_delayed( u );
		}

		if( u->sendbuf_len == 0 )
		{
			u->sendbuf_len = strlen( msg ) + 2;
			u->sendbuf = g_new (char, u->sendbuf_len );
			u->sendbuf[0] = 0;
			u->sendbuf_flags = flags;
		}
		else
		{
			u->sendbuf_len += strlen( msg ) + 1;
			u->sendbuf = g_renew ( char, u->sendbuf, u->sendbuf_len );
		}
		
		strcat( u->sendbuf, msg );
		strcat( u->sendbuf, "\n" );
		
		delay = set_getint( irc, "buddy_sendbuffer_delay" );
		if( delay <= 5 )
			delay *= 1000;
		
		if( u->sendbuf_timer > 0 )
			g_source_remove( u->sendbuf_timer );
		u->sendbuf_timer = g_timeout_add( delay, buddy_send_handler_delayed, u );
		
		return( 1 );
	}
	else
	{
		return( serv_send_im( irc, u, msg, flags ) );
	}
}

int irc_privmsg( irc_t *irc, user_t *u, char *type, char *to, char *prefix, char *msg )
{
	char last = 0;
	char *s = msg, *line = msg;
	
	/* The almighty linesplitter .. woohoo!! */
	while( !last )
	{
		if( *s == '\r' && *(s+1) == '\n' )
			*(s++) = 0;
		if( *s == '\n' )
		{
			last = s[1] == 0;
			*s = 0;
		}
		else
		{
			last = s[0] == 0;
		}
		if( *s == 0 )
		{
			if( g_strncasecmp( line, "/me ", 4 ) == 0 && ( !prefix || !*prefix ) && g_strcasecmp( type, "PRIVMSG" ) == 0 )
			{
				irc_write( irc, ":%s!%s@%s %s %s :\001ACTION %s\001", u->nick, u->user, u->host,
				           type, to, line + 4 );
			}
			else
			{
				irc_write( irc, ":%s!%s@%s %s %s :%s%s", u->nick, u->user, u->host,
				           type, to, prefix ? prefix : "", line );
			}
			line = s + 1;
		}
		s ++;
	}
	
	return( 1 );
}

int irc_msgfrom( irc_t *irc, char *nick, char *msg )
{
	user_t *u = user_find( irc, nick );
	static char *prefix = NULL;
	
	if( !u ) return( 0 );
	if( prefix && *prefix ) g_free( prefix );
	
	if( !u->is_private && nick_cmp( u->nick, irc->mynick ) != 0 )
	{
		int len = strlen( irc->nick) + 3;
		prefix = g_new (char, len );
		g_snprintf( prefix, len, "%s%s", irc->nick, set_getstr( irc, "to_char" ) );
		prefix[len-1] = 0;
	}
	else
	{
		prefix = "";
	}
	
	return( irc_privmsg( irc, u, "PRIVMSG", u->is_private ? irc->nick : irc->channel, prefix, msg ) );
}

int irc_noticefrom( irc_t *irc, char *nick, char *msg )
{
	user_t *u = user_find( irc, nick );
	
	if( u )
		return( irc_privmsg( irc, u, "NOTICE", irc->nick, "", msg ) );
	else
		return( 0 );
}

/* Returns 0 if everything seems to be okay, a number >0 when there was a
   timeout. The number returned is the number of seconds we received no
   pongs from the user. When not connected yet, we don't ping but drop the
   connection when the user fails to connect in IRC_LOGIN_TIMEOUT secs. */
static gboolean irc_userping( gpointer _irc )
{
	irc_t *irc = _irc;
	int rv = 0;
	
	if( irc->status < USTATUS_LOGGED_IN )
	{
		if( gettime() > ( irc->last_pong + IRC_LOGIN_TIMEOUT ) )
			rv = gettime() - irc->last_pong;
	}
	else
	{
		if( ( gettime() > ( irc->last_pong + global.conf->ping_interval ) ) && !irc->pinging )
		{
			irc_write( irc, "PING :%s", IRC_PING_STRING );
			irc->pinging = 1;
		}
		else if( gettime() > ( irc->last_pong + global.conf->ping_timeout ) )
		{
			rv = gettime() - irc->last_pong;
		}
	}
	
	if( rv > 0 )
	{
		irc_write( irc, "ERROR :Closing Link: Ping Timeout: %d seconds", rv );
		irc_free( irc );
		return FALSE;
	}
	
	return TRUE;
}
