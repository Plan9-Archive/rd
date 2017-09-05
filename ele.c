/*
 * [MS-RDPELE] 2.2.2 Licensing PDU (TS_LICENSING_PDU)
 * http://msdn.microsoft.com/en-us/library/cc241913.aspx
 *
 * 2.2.1.12.1.1 Licensing Preamble [MS-RDPBCGR]
 * 2.2.1.12.1.2 Licensing Binary Blob [MS-RDPBCGR]
 * 2.2.2.2 Client New License Request [MS-RDPELE]
 */
#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

enum
{
	RandomSize=	32,

	PreambleV3=	3,	/* RDP 5.0+ */
	KeyExRSA=	1,

	SNeedLicense=	1,
	SHaveChal=	2,
	SHaveLicense=	3,
	SNeedRenew=	4,
 	CNeedLicense=	0x13,
	Notify=	0xFF,

	Brandom=	2,
	Berror=	4,
	Bcuser=	15,
	Bchost=	16,

	ErrCNoLicense=	2,

	TotalAbort=	1,
	NoTransition=	2,
};

int
getlicensemsg(Msg* m, uchar* b, uint nb)
{
	uint type;

	if(nb < 1)
		sysfatal(Eshort);

	/* type[1] flags[1] size[2] */
	type = b[0];
	switch(type){
	default:
		werrstr("unhandled license packet %ud", type);
		m->type = 0;
		return -1;
	case SNeedLicense:
		m->type = Lneedlicense;
		break;
	case SHaveChal:
		m->type = Lhavechal;
		break;
	case SNeedRenew:
	case SHaveLicense:
	case Notify:
		m->type = Ldone;
		break;
	}
	return nb;
}

int
sizelicensemsg(Msg* m)
{
	int usersize, hostsize;

	switch(m->type){
	default:
		werrstr("sizelicensemsg: bad message type");
		return -1;
	case Lreq:
		usersize = strlen(m->user)+1;
		hostsize = strlen(m->sysname)+1;
		return 24+usersize+hostsize+RandomSize+48;
		break;
	case Lnolicense:
		return 16;
	}
}

int
putlicensemsg(uchar* buf, uint nb, Msg* m)
{
	uchar *p, *ep;
	int ndata, usersize, hostsize;
	int errcode, newstate;

	p = buf;
	ep = buf+nb;
	ndata = nb;

	switch(m->type){
	default:
		werrstr("putlicensemsg: bad message type");
		return -1;
	case Lreq:	
		usersize = strlen(m->user)+1;
		hostsize = strlen(m->sysname)+1;

		/* 
		 * type[1] flags[1] size[2] kexalg[4] platfid[4] crandom[32]
		 * premaster[blob] cuser[blob] chost[blob]
		 * (blob := type[2] len[2] data[len])
		 */
		p[0] = CNeedLicense;
		p[1] = PreambleV3;
		iputs(p+2, ndata);
		iputl(p+4, KeyExRSA);
		iputl(p+8, 0);
		memset(p+12, RandomSize, 0);
		p += 12+RandomSize;
	
		iputs(p+0, Brandom);
		iputs(p+2, 48);
		memset(p+4, 48, 0);
		p += 4+48;

		iputs(p+0, Bcuser);
		iputs(p+2, usersize);
		memcpy(p+4, m->user, usersize);
		p+= 4+usersize;
	
		iputs(p+0, Bchost);
		iputs(p+2, hostsize);
		memcpy(p+4, m->sysname, hostsize);
		p+= 4+hostsize;
		break;

	case Lnolicense:
		errcode = ErrCNoLicense;
		newstate = TotalAbort;

		/* type[1] flags[1] size[2] errcode[4] newstate[4] blob.type[2] blob.len[2] ... */
		p[0] = Notify;
		p[1] = PreambleV3;
		iputs(p+2, ndata);
		iputl(p+4, errcode);
		iputl(p+8, newstate);
		iputs(p+12, Berror);
		iputs(p+14, 0);
		break;
	}
	assert(p == ep);
	return p-buf;
}

