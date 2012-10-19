#ifndef __OSCAR_ICQ_H__
#define __OSCAR_ICQ_H__

#define AIM_CB_FAM_ICQ 0x0015

/*
 * SNAC Family: ICQ
 *
 * Most of these are actually special.
 */ 
#define AIM_CB_ICQ_ERROR 0x0001
#define AIM_CB_ICQ_OFFLINEMSG 0x00f0
#define AIM_CB_ICQ_OFFLINEMSGCOMPLETE 0x00f1
#define AIM_CB_ICQ_SIMPLEINFO 0x00f2
#define AIM_CB_ICQ_INFO 0x00f2 /* just transitional */
#define AIM_CB_ICQ_DEFAULT 0xffff

struct aim_icq_offlinemsg {
	guint32 sender;
	guint16 year;
	guint8 month, day, hour, minute;
	guint16 type;
	char *msg;
};

struct aim_icq_simpleinfo {
	guint32 uin;
	char *nick;
	char *first;
	char *last;
	char *email;
};

struct aim_icq_info {
        gushort reqid;

        /* simple */
        guint32 uin;

        /* general and "home" information (0x00c8) */
        char *nick;
        char *first;
        char *last;
        char *email;
        char *homecity;
        char *homestate;
        char *homephone;
        char *homefax;
        char *homeaddr;
        char *mobile;
        char *homezip;
        gushort homecountry;
/*      guchar timezone;
        guchar hideemail; */

        /* personal (0x00dc) */
        guchar age;
        guchar unknown;
        guchar gender;
        char *personalwebpage;
        gushort birthyear;
        guchar birthmonth;
        guchar birthday;
        guchar language1;
        guchar language2;
        guchar language3;

        /* work (0x00d2) */
        char *workcity;
        char *workstate;
        char *workphone;
        char *workfax;
        char *workaddr;
        char *workzip;
        gushort workcountry;
        char *workcompany;
        char *workdivision;
        char *workposition;
        char *workwebpage;

        /* additional personal information (0x00e6) */
        char *info;

        /* email (0x00eb) */
        gushort numaddresses;
        char **email2;

        /* we keep track of these in a linked list because we're 1337 */
        struct aim_icq_info *next;
};


int aim_icq_reqofflinemsgs(aim_session_t *sess);
int aim_icq_ackofflinemsgs(aim_session_t *sess);
int aim_icq_getallinfo(aim_session_t *sess, const char *uin);

#endif /* __OSCAR_ICQ_H__ */
