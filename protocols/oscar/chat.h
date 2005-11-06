#ifndef __OSCAR_CHAT_H__
#define __OSCAR_CHAT_H__

#define AIM_CB_FAM_CHT 0x000e /* Chat */

/*
 * SNAC Family: Chat Services
 */ 
#define AIM_CB_CHT_ERROR 0x0001
#define AIM_CB_CHT_ROOMINFOUPDATE 0x0002
#define AIM_CB_CHT_USERJOIN 0x0003
#define AIM_CB_CHT_USERLEAVE 0x0004
#define AIM_CB_CHT_OUTGOINGMSG 0x0005
#define AIM_CB_CHT_INCOMINGMSG 0x0006
#define AIM_CB_CHT_DEFAULT 0xffff

#endif /* __OSCAR_CHAT_H__ */
