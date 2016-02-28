#define BILBEE_CORE
#include "bitlbee.h"
#include <mysql.h>
#include <stdarg.h>

/* MySQL config goes in /etc/bitlbee/my.cnf */
static char *b_mysql_config = ETCDIR "/my.cnf";

static MYSQL* b_mysql_connection(void)
{
	MYSQL *con = mysql_init(NULL);
	if (!con)
		return NULL;
	if (mysql_options(con, MYSQL_READ_DEFAULT_FILE, b_mysql_config) != 0) {
		log_message(LOGLVL_WARNING, "Setting mysql option failed: %s", mysql_error(con));
		mysql_close(con);
		return NULL;
	}
	if (!mysql_real_connect(con, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
		log_message(LOGLVL_WARNING, "Database connection failed: %s", mysql_error(con));
		mysql_close(con);
		return NULL;
	}
	return con;
}

static void b_mysql_init(void)
{
	MYSQL *con = b_mysql_connection();
	if (!con)
		log_message(LOGLVL_WARNING, "Unable to access database, configuration won't be saved.");
	else
		mysql_close(con);
}

/* Possibly the worst sprintf implementation possible. What we actually need is
 * a sprintf wrapper that lets you process all the arguments before passing
 * them on to the actual printf. Since there's no way to do that, we now have a
 * very limited vasprintf that only supports the %s specifier (and probably
 * causes horrible crashes if you use other specifiers. Fortunately, this is
 * enough. 
 *
 * We pass the each of the format's arguments through mysql_real_escape_string.
 * All arguments processed this way are either table names or WHERE parameters,
 * so this is safe. We then combine the arguments with the format string by
 * splitting the format string, splicing in the arguments and joining the
 * result.
 *
 * If only there were an sprintf that accepted an array of parameters :(
 */
static char *b_mysql_format(MYSQL *con, const char *query, va_list args)
{
	char **items, **combined, *raw, *escaped, *stmt;
	int i, len;

	items = g_strsplit(query, "%s", -1);
	len = g_strv_length(items);
	combined = g_new0(char *, len * 2);

	for(i=0; i<len-1; i++) {
		combined[i*2] = items[i];
		raw = va_arg(args, char *);
		escaped = g_malloc(strlen(raw)*2+1);
		mysql_real_escape_string(con, escaped, raw, strlen(raw));
		combined[i*2+1] = escaped;
	}
	combined[(len-1)*2] = items[len-1];

	stmt = g_strjoinv(NULL, combined); 
	g_strfreev(combined);
	g_free(items);

	return stmt;
}

static int b_mysql_query(MYSQL *con, const char *query, ...) __attribute__((format (printf, 2,3)));
static int b_mysql_query(MYSQL *con, const char *query, ...)
{
	va_list args;
	char *stmt;
	int ret;

	va_start(args, query);
	stmt = b_mysql_format(con, query, args);
	va_end(args);

	ret = mysql_real_query(con, stmt, strlen(stmt));
	if (ret || *mysql_error(con))
		log_message(LOGLVL_WARNING, "MySQL query \"%s\" failed during query (%d): %s", stmt, ret, mysql_error(con));
	g_free(stmt);
	return ret;
}

static MYSQL_RES *b_mysql_select(MYSQL *con, const char *query, ...) __attribute__((format (printf, 2,3)));
static MYSQL_RES *b_mysql_select(MYSQL *con, const char *query, ...) {
	MYSQL_RES *res;
	va_list args;
	char *stmt;
	int ret;

	va_start(args, query);
	stmt = b_mysql_format(con, query, args);
	va_end(args);

	ret = mysql_real_query(con, stmt, strlen(stmt));
	if (ret || *mysql_error(con)) {
		log_message(LOGLVL_WARNING, "MySQL query \"%s\" failed during query (%d): %s", stmt, ret, mysql_error(con));
		g_free(stmt);
		return NULL;
	}

	res = mysql_store_result(con);
	if (!res) {
		log_message(LOGLVL_WARNING, "MySQL query \"%s\" failed during store (%d): %s", stmt, ret, mysql_error(con));
		g_free(stmt);
		return NULL;
	}

	g_free(stmt);
	return res;
}

static storage_status_t b_mysql_check_pass_real(irc_t *irc, MYSQL *con, const char *nick, const char *password) {
	MYSQL_ROW row;
	MYSQL_RES *res;
	storage_status_t ret;

	if (!(res = b_mysql_select(con, "SELECT `password`, `auth_backend` FROM `users` WHERE `login`='%s'", nick))) {
		return STORAGE_OTHER_ERROR;
	}

	if (mysql_num_rows(res) != 1) {
		mysql_free_result(res);
		return STORAGE_NO_SUCH_USER;
	}

	row = mysql_fetch_row(res);
	if (row[1] && *row[1]) {
		ret = auth_check_pass(row[1], nick, password);
		if ((ret == STORAGE_OK) && irc)
			irc->auth_backend = g_strdup(row[1]);
	} else if ((ret = password_verify(password, row[0]))) {
		ret = (ret == -1) ? STORAGE_OTHER_ERROR : STORAGE_INVALID_PASSWORD;
	} else {
		ret = STORAGE_OK;
	}
	mysql_free_result(res);
	return ret;
}

static storage_status_t b_mysql_check_pass(const char *nickname, const char *password) {
	MYSQL *con = b_mysql_connection();
	storage_status_t ret;
	if (!con) {
		log_message(LOGLVL_WARNING, "Unable to access database, configuration for %s not saved.", nickname);
		return STORAGE_OTHER_ERROR;
	}
	ret = b_mysql_check_pass_real(NULL, con, nickname, password);
	mysql_close(con);
	return ret;
}

static storage_status_t b_mysql_load_settings(irc_t *irc, MYSQL *con) {
	MYSQL_ROW row;
	MYSQL_RES *res;
	struct set *setting;

	if (!(res = b_mysql_select(con, "SELECT setting, value, locked FROM settings WHERE `login`='%s'", irc->user->nick)))
		return STORAGE_OTHER_ERROR;

	while ((row = mysql_fetch_row(res))) {
		set_setstr(&irc->b->set, row[0], row[1]);
		if (row[2][0] == '1') {
			setting = set_find(&irc->b->set, row[0]);
			setting->flags |= SET_LOCKED;
		}
	}
	mysql_free_result(res);
	return STORAGE_OK;
}

static storage_status_t b_mysql_load_buddies(irc_t *irc, MYSQL *con, account_t *acc) {
	MYSQL_RES *res;
	MYSQL_ROW row;

	if (!(res = b_mysql_select(con, "SELECT handle, nick FROM buddies WHERE `login`='%s' AND `account`='%s'", irc->user->nick, acc->user)))
		return STORAGE_OTHER_ERROR;

	while ((row = mysql_fetch_row(res)))
		nick_set_raw(acc, row[0], row[1]);
	mysql_free_result(res);
	return STORAGE_OK;
}

static storage_status_t b_mysql_load_account_settings(irc_t *irc, MYSQL *con, account_t *acc) {
	MYSQL_ROW row;
	MYSQL_RES *res;
	set_t *setting;

	if (!(res = b_mysql_select(con, "SELECT setting, value, locked FROM account_settings WHERE `login`='%s' AND `account`='%s'", irc->user->nick, acc->user)))
		return STORAGE_OTHER_ERROR;

	while ((row = mysql_fetch_row(res))) {
		setting = set_find(&acc->set, row[0]);
		if (setting && setting->flags & ACC_SET_ONLINE_ONLY)
			continue;
		set_setstr(&acc->set, row[0], row[1]);
		if (row[2][0] == '1')
			setting->flags |= SET_LOCKED;
	}
	mysql_free_result(res);

	return b_mysql_load_buddies(irc, con, acc);
}

static storage_status_t b_mysql_load_accounts(irc_t *irc, MYSQL *con, const char *irc_pass) {
	MYSQL_ROW row;
	MYSQL_RES *res;
	account_t *acc;
	struct prpl *prpl;
	storage_status_t ret = STORAGE_OK;
	char *password;
	int pass_len;

	if (!(res = b_mysql_select(con, "SELECT account, protocol, password, tag, server, auto_connect, locked FROM accounts WHERE `login`='%s'", irc->user->nick)))
		return STORAGE_OTHER_ERROR;

	while ((row = mysql_fetch_row(res))) {
		prpl = find_protocol(row[1]);
		if (irc->auth_backend)
			pass_len = password_decode(row[2], &password);
		else
			pass_len = password_decrypt(row[2], irc_pass, &password);
		if (pass_len < 0) {
			g_free(password);
			mysql_free_result(res);
			return STORAGE_OTHER_ERROR;
		}
		acc = account_add(irc->b, prpl, row[0], password);
		g_free(password);

		set_setstr(&acc->set, "tag", row[3]);
		set_setstr(&acc->set, "server", row[4]);
		set_setstr(&acc->set, "auto_connect", row[5][0] == '0' ? "true" : "false");
		if (row[6][0] == '1')
			acc->flags |= ACC_FLAG_LOCKED;

		ret = b_mysql_load_account_settings(irc, con, acc);
		if (ret != STORAGE_OK) {
			mysql_free_result(res);
			return ret;
		}
	}

	mysql_free_result(res);
	return ret;
}


static storage_status_t b_mysql_load_channels(irc_t *irc, MYSQL *con) {
	MYSQL_ROW row;
	MYSQL_RES *res;
	irc_channel_t *ic;
	set_t *setting;

	if (!(res = b_mysql_select(con, "SELECT channel, setting, value, locked FROM channel_settings WHERE `login`='%s'", irc->user->nick)))
		return STORAGE_OTHER_ERROR;

	while ((row = mysql_fetch_row(res))) {
		ic = irc_channel_by_name(irc, row[0]);
		if (!ic)
			ic = irc_channel_new(irc, row[0]);
		set_setstr(&ic->set, row[1], row[2]);
		if (row[3][0] == '1') {
			setting = set_find(&ic->set, row[1]);
			setting->flags |= SET_LOCKED;
		}
	}
	mysql_free_result(res);
	return STORAGE_OK;
}

static storage_status_t b_mysql_load(irc_t *irc, const char *password) {
	MYSQL *con;
	storage_status_t ret;

	con = b_mysql_connection();
	if (!con) {
		log_message(LOGLVL_WARNING, "Unable to access database, configuration for %s not saved.", irc->user->nick);
		return STORAGE_OTHER_ERROR;
	}

	ret = b_mysql_check_pass_real(irc, con, irc->user->nick, password);
	if (!ret) ret = b_mysql_load_settings(irc, con);
	if (!ret) ret = b_mysql_load_accounts(irc, con, password);
	if (!ret) ret = b_mysql_load_channels(irc, con);
	mysql_close(con);
	return ret;
}

#define locked(setting) (((setting)->flags & SET_LOCKED) ? "1" : "0")
static storage_status_t b_mysql_save_settings(irc_t *irc, MYSQL *con) {
	set_t *set;
	char *pass_buf = "";
	if(!irc->auth_backend)
		password_hash(irc->password, &pass_buf);
	if (b_mysql_query(con, "INSERT INTO `users` SET `login`='%s', `password`='%s', `auth_backend`='%s'", 
	                        irc->user->nick, pass_buf, irc->auth_backend ? irc->auth_backend : ""))
		return STORAGE_OTHER_ERROR;
	for(set = irc->b->set; set; set=set->next) {
		if (!set->value || (set->flags & SET_NOSAVE))
			continue;
		if (b_mysql_query(con, "INSERT INTO `settings` SET `login`='%s', `setting`='%s', `value`='%s', `locked`=%s", 
					irc->user->nick, set->key, set->value, locked(set)))
			return STORAGE_OTHER_ERROR;
	}
	return STORAGE_OK;
}

static storage_status_t b_mysql_save_accounts(irc_t *irc, MYSQL *con) {
	account_t *acc;
	int ret;
	set_t *set;
	GHashTableIter iter;
	char *handle, *nick;
	char *password;

	for(acc=irc->b->accounts; acc; acc=acc->next) {
		if(irc->auth_backend) {
			/* If we don't "own" the password, it may change without us
			 * knowing, so we cannot encrypt the data, as we then may not be
			 * able to decrypt it */
			password_encode(acc->pass, &password);
		} else {
			password_encrypt(acc->pass, irc->password, &password);
		}
		ret = b_mysql_query(con, 
		      "INSERT INTO `accounts` SET `login`='%s', `account`='%s', `protocol`='%s', "
		      "`password`='%s', `tag`='%s', `server`='%s', `auto_connect`=%s, locked=%s",
		      irc->user->nick, acc->user, acc->prpl->name, password, acc->tag, acc->server ? acc->server : "",
		      acc->auto_connect ? "1": "0", (acc->flags & ACC_FLAG_LOCKED) ? "1" : "0");
		if (ret)
			return STORAGE_OTHER_ERROR;

		for(set = acc->set; set; set=set->next) {
			if (!set->value || (set->flags & SET_NOSAVE))
				continue;
			if (b_mysql_query(con,
			    "INSERT INTO `account_settings` SET `login`='%s', `account`='%s', `setting`='%s', `value`='%s', `locked`=%s",
			    irc->user->nick, acc->user, set->key, set->value, locked(set)))
				return STORAGE_OTHER_ERROR;
		}

		g_hash_table_iter_init(&iter, acc->nicks);
		while (g_hash_table_iter_next(&iter, (void**)&handle, (void**)&nick)) {
			if (b_mysql_query(con, "INSERT INTO `buddies` SET `login`='%s', `account`='%s', `handle`='%s', `nick`='%s'",
						irc->user->nick, acc->user, handle, nick))
				return STORAGE_OTHER_ERROR;
		}
	}
	return STORAGE_OK;
}

static storage_status_t b_mysql_save_channels(irc_t *irc, MYSQL *con) {
	GSList *l;
	irc_channel_t *ic;
	set_t *set;

	for (l=irc->channels; l; l=l->next) {
		ic = l->data;

		if (ic->flags & IRC_CHANNEL_TEMP)
			continue;

		for(set = ic->set; set; set=set->next) {
			if (!set->value || (set->flags & SET_NOSAVE))
				continue;
			if (b_mysql_query(con,
			    "INSERT INTO `channel_settings` SET `login`='%s', `channel`='%s', `setting`='%s', `value`='%s', `locked`=%s",
			    irc->user->nick, ic->name, set->key, set->value, locked(set)))
				return STORAGE_OTHER_ERROR;
		}
	}
	return STORAGE_OK;
}

static storage_status_t b_mysql_remove_real(MYSQL *con, const char *nick) {
	char *tables[7] = {"users", "accounts", "settings", "account_settings", "channel_settings", "buddies", NULL};
	int i;

	for(i=0; tables[i]; i++) {
		if (b_mysql_query(con, "DELETE FROM `%s` WHERE `login`='%s'", tables[i], nick))
			return STORAGE_OTHER_ERROR;
	}

	return STORAGE_OK;
}

static storage_status_t b_mysql_remove(const char *nick, const char *password) {
	MYSQL *con;
	storage_status_t ret;

	con = b_mysql_connection();
	if (!con) {
		log_message(LOGLVL_WARNING, "Unable to access database, configuration for %s not removed.", nick);
		return STORAGE_OTHER_ERROR;
	}
	ret = b_mysql_check_pass_real(NULL, con, nick, password);
	if (!ret) ret = b_mysql_remove_real(con, nick);

	mysql_close(con);
	return ret;
}

static storage_status_t b_mysql_save(irc_t *irc, int overwrite) {
	MYSQL *con = b_mysql_connection();
	MYSQL_RES *res;
	int ret;

	if (!con) {
		log_message(LOGLVL_WARNING, "Unable to access database, configuration for %s not saved.", irc->user->nick);
		return STORAGE_OTHER_ERROR;
	}

	if (!overwrite) {
		if (!(res = b_mysql_select(con, "SELECT 1 FROM `users` WHERE `login`='%s'", irc->user->nick))) {
			mysql_close(con);
			return STORAGE_OTHER_ERROR;
		}
		ret = mysql_num_rows(res);
		mysql_free_result(res);
		if (ret) {
			mysql_close(con);
			return STORAGE_ALREADY_EXISTS;
		}
	}

	if (b_mysql_query(con, "START TRANSACTION"))
		return STORAGE_OTHER_ERROR;

	ret = b_mysql_remove_real(con, irc->user->nick);
	if (!ret) ret = b_mysql_save_settings(irc, con);
	if (!ret) ret = b_mysql_save_accounts(irc, con);
	if (!ret) ret = b_mysql_save_channels(irc, con);

	if (ret != STORAGE_OK)
		b_mysql_query(con, "ROLLBACK");
	else if (b_mysql_query(con, "COMMIT"))
		ret = STORAGE_OTHER_ERROR;

	mysql_close(con);
	return ret;
}

storage_t storage_mysql = {
	.name = "mysql",
	.init = b_mysql_init,
	.check_pass = b_mysql_check_pass,
	.remove = b_mysql_remove,
	.load = b_mysql_load,
	.save = b_mysql_save
};
