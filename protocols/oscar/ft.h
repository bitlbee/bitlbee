#ifndef __OSCAR_FT_H__
#define __OSCAR_FT_H__

#define AIM_CB_FAM_OFT 0xfffe /* OFT/Rvous */

/*
 * OFT Services
 *
 * See non-SNAC note below.
 */
#define AIM_CB_OFT_DIRECTIMCONNECTREQ 0x0001/* connect request -- actually an OSCAR CAP*/
#define AIM_CB_OFT_DIRECTIMINCOMING 0x0002
#define AIM_CB_OFT_DIRECTIMDISCONNECT 0x0003
#define AIM_CB_OFT_DIRECTIMTYPING 0x0004
#define AIM_CB_OFT_DIRECTIMINITIATE 0x0005

#define AIM_CB_OFT_GETFILECONNECTREQ 0x0006 /* connect request -- actually an OSCAR CAP*/
#define AIM_CB_OFT_GETFILELISTINGREQ 0x0007 /* OFT listing.txt request */
#define AIM_CB_OFT_GETFILEFILEREQ 0x0008    /* received file request */
#define AIM_CB_OFT_GETFILEFILESEND 0x0009   /* received file request confirm -- send data */
#define AIM_CB_OFT_GETFILECOMPLETE 0x000a   /* received file send complete*/
#define AIM_CB_OFT_GETFILEINITIATE 0x000b   /* request for file get acknowledge */
#define AIM_CB_OFT_GETFILEDISCONNECT 0x000c   /* OFT connection disconnected.*/
#define AIM_CB_OFT_GETFILELISTING 0x000d   /* OFT listing.txt received.*/
#define AIM_CB_OFT_GETFILERECEIVE 0x000e   /* OFT file incoming.*/
#define AIM_CB_OFT_GETFILELISTINGRXCONFIRM 0x000f
#define AIM_CB_OFT_GETFILESTATE4 0x0010

#define AIM_CB_OFT_SENDFILEDISCONNECT 0x0020   /* OFT connection disconnected.*/

struct aim_fileheader_t {
#if 0
	char  magic[4];		/* 0 */
	short hdrlen; 		/* 4 */
	short hdrtype;		/* 6 */
#endif
	char  bcookie[8];       /* 8 */
	short encrypt;          /* 16 */
	short compress;         /* 18 */
	short totfiles;         /* 20 */
	short filesleft;        /* 22 */
	short totparts;         /* 24 */
	short partsleft;        /* 26 */
	long  totsize;          /* 28 */
	long  size;             /* 32 */
	long  modtime;          /* 36 */
	long  checksum;         /* 40 */
	long  rfrcsum;          /* 44 */
	long  rfsize;           /* 48 */
	long  cretime;          /* 52 */
	long  rfcsum;           /* 56 */
	long  nrecvd;           /* 60 */
	long  recvcsum;         /* 64 */
	char  idstring[32];     /* 68 */
	char  flags;            /* 100 */
	char  lnameoffset;      /* 101 */
	char  lsizeoffset;      /* 102 */
	char  dummy[69];        /* 103 */
	char  macfileinfo[16];  /* 172 */
	short nencode;          /* 188 */
	short nlanguage;        /* 190 */
	char  name[64];         /* 192 */
				/* 256 */
};



struct aim_filetransfer_priv {
	char sn[MAXSNLEN];
	guint8 cookie[8];
	char ip[30];
	int state;
	struct aim_fileheader_t fh;
};

#define AIM_CB_FAM_OFT 0xfffe /* OFT/Rvous */

#endif /* __OSCAR_FT_H__ */
