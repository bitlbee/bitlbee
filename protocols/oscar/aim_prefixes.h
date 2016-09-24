/* Automatically generated prefixes to avoid symbol conflicts with libpurple.
 * Because sometimes people compile both oscar and purple and things break.
 * This is awful but i know no better solution other than not running libpurple in the same process.
 *
 * Made with the following commands:
 *
 * nm --defined-only oscar_mod.o | awk '{print $3}' | sort | uniq > bee_syms
 * nm --defined-only liboscar.so | awk '{print $3}' | sort | uniq > purple_syms
 * comm -12 bee_syms purple_syms | sed 's/^.*$/#define & b_&/' > aim_prefixes.h
 */

#define admin_modfirst b_admin_modfirst
#define aim_admin_changepasswd b_aim_admin_changepasswd
#define aim_admin_getinfo b_aim_admin_getinfo
#define aim_admin_reqconfirm b_aim_admin_reqconfirm
#define aim_admin_setemail b_aim_admin_setemail
#define aim_admin_setnick b_aim_admin_setnick
#define aim_bos_reqrights b_aim_bos_reqrights
#define aim_cachecookie b_aim_cachecookie
#define aim_cachesnac b_aim_cachesnac
#define aim_callhandler b_aim_callhandler
#define aim_caps b_aim_caps
#define aim_chat_join b_aim_chat_join
#define aim_chatnav_createroom b_aim_chatnav_createroom
#define aim_chatnav_reqrights b_aim_chatnav_reqrights
#define aim_chat_readroominfo b_aim_chat_readroominfo
#define aim_chat_send_im b_aim_chat_send_im
#define aim_checkcookie b_aim_checkcookie
#define aim_cleansnacs b_aim_cleansnacs
#define aim_cookie_free b_aim_cookie_free
#define aim__findmodule b_aim__findmodule
#define aim__findmodulebygroup b_aim__findmodulebygroup
#define aim_genericreq_l b_aim_genericreq_l
#define aim_genericreq_n b_aim_genericreq_n
#define aim_genericreq_n_snacid b_aim_genericreq_n_snacid
#define aim_icq_freeinfo b_aim_icq_freeinfo
#define aim_icq_getallinfo b_aim_icq_getallinfo
#define aim_im_sendmtn b_aim_im_sendmtn
#define aim_initsnachash b_aim_initsnachash
#define aim_mkcookie b_aim_mkcookie
#define aim_newsnac b_aim_newsnac
#define aim_putsnac b_aim_putsnac
#define aim__registermodule b_aim__registermodule
#define aim_remsnac b_aim_remsnac
#define aim_request_login b_aim_request_login
#define aim_send_login b_aim_send_login
#define aim__shutdownmodules b_aim__shutdownmodules
#define aim_ssi_enable b_aim_ssi_enable
#define aim_ssi_freelist b_aim_ssi_freelist
#define aim_ssi_getpermdeny b_aim_ssi_getpermdeny
#define aim_ssi_itemlist_add b_aim_ssi_itemlist_add
#define aim_ssi_itemlist_find b_aim_ssi_itemlist_find
#define aim_ssi_itemlist_finditem b_aim_ssi_itemlist_finditem
#define aim_ssi_itemlist_rebuildgroup b_aim_ssi_itemlist_rebuildgroup
#define aim_ssi_modbegin b_aim_ssi_modbegin
#define aim_ssi_modend b_aim_ssi_modend
#define aim_ssi_movebuddy b_aim_ssi_movebuddy
#define aim_ssi_reqrights b_aim_ssi_reqrights
#define aim_uncachecookie b_aim_uncachecookie
#define auth_modfirst b_auth_modfirst
#define bos_modfirst b_bos_modfirst
#define buddylist_modfirst b_buddylist_modfirst
#define chat_modfirst b_chat_modfirst
#define chatnav_modfirst b_chatnav_modfirst
#define extract_name b_extract_name
#define icq_modfirst b_icq_modfirst
#define incomingim b_incomingim
#define incomingim_ch2_chat_free b_incomingim_ch2_chat_free
#define incomingim_ch2_icqserverrelay_free b_incomingim_ch2_icqserverrelay_free
#define locate_modfirst b_locate_modfirst
#define misc_modfirst b_misc_modfirst
#define msgerrreason b_msgerrreason
#define msg_modfirst b_msg_modfirst
#define oscar_add_buddy b_oscar_add_buddy
#define oscar_add_deny b_oscar_add_deny
#define oscar_add_permit b_oscar_add_permit
#define oscar_chat_invite b_oscar_chat_invite
#define oscar_chat_kill b_oscar_chat_kill
#define oscar_chat_leave b_oscar_chat_leave
#define oscar_encoding_to_utf8 b_oscar_encoding_to_utf8
#define oscar_get_info b_oscar_get_info
#define oscar_init b_oscar_init
#define oscar_keepalive b_oscar_keepalive
#define oscar_login b_oscar_login
#define oscar_rem_deny b_oscar_rem_deny
#define oscar_remove_buddy b_oscar_remove_buddy
#define oscar_rem_permit b_oscar_rem_permit
#define oscar_send_typing b_oscar_send_typing
#define search_modfirst b_search_modfirst
#define snachandler b_snachandler
#define ssi_modfirst b_ssi_modfirst
#define ssi_shutdown b_ssi_shutdown
#define stats_modfirst b_stats_modfirst
