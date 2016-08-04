#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

/*
 * examine a packet header
 * returns 1 if it's a TPKT-encapsulated TPDU (T.123 clause 8; RFC 1006)
 * returns 0 if not - likely a Fast-Path Update PDU ([MS-RDPBCGR] 5.3.8 and 5.4.4)
 */
int
istpkt(uchar* p, uchar* ep)
{
	int magic;

	if(p+1>ep){
		werrstr(Eshort);
		return -1;
	}

	magic = p[0];
	return (magic == 3);
}

int
tptype(uchar* p, uchar* ep)
{
	if(p+5 >= ep){
		werrstr(Eshort);
		return -1;
	}
	return p[5];
}

/*
 * read a PDU: either TPKT-encapsulated TPDU or Fast-Path Update PDU
 */
int
readpdu(int fd, uchar *buf, uint nbuf)
{
	int n, len;
	uchar *p;

	p = buf;

	n = readn(fd, p, TPKTFIXLEN);
	if(n != TPKTFIXLEN){
		werrstr("short read: %r");
		return -1;
	}

	switch(istpkt(p, p+n)){
	case -1:
		return -1;
	case 0:
		/* Fast-Path Update PDU */
		len = p[1];
		if(len&(1<<7))
			len = ((len^(1<<7))<<8) | p[2];
		break;
	default:
		/* TPKT-encapsulated TPDU */
		len = GSHORTB(p+2);
	}
	
	if(len <= n || len > nbuf){
		werrstr("bad length in PDU header: %d", len);
		return -1;
	}

	n += readn(fd, p+n, len-n);
	if(n != len)
		return -1;
	return n;
}

uchar*
tpdat(uchar* p, uchar* ep)
{
	uchar* q;

	if(istpkt(p, ep) == 0){
		werrstr("Fast-Path Update PDU is not expected");
		return nil;
	}
	if(tptype(p,ep) == Data)
		q = p+7;
	else
		q = p+11;
	if(q > ep){
		werrstr(Eshort);
		return nil;
	}
	return q;
}

/* connect request */
int
mktpcr(uchar* buf, int nbuf, int ndata)
{
	int size;
	uchar *p;

	p = buf;
	size = TPKTFIXLEN+7+ndata;
	if(size > nbuf){
		werrstr(Esmall);
		return -1;
	}

	/* TPKT header: version[1] unused[1] len[2] */
	p[0] = 0x03;
	p[1] = 0;
	PSHORTB(p+2, size);

	/* ConReq: hdlen[1] type[1] dstref[2] srcref[2] class[1] */
	p[4+0] = 7-1+ndata;
	p[4+1] = ConReq;
	PSHORTB(p+4+2, 0);
	PSHORTB(p+4+4, 0);
	p[4+6] = 0;

	return size;
}

/* data transfer */
int
mktpdat(uchar* buf, int nbuf, int ndata)
{
	int size;
	uchar *p;

	p = buf;
	size = TPDATAFIXLEN+ndata;
	if(size > nbuf){
		werrstr("buffer too small: provided %d need %d", nbuf, size);
		return -1;
	}

	/* TPKT header: version[1] unused[1] len[2] */
	p[0] = 0x03;
	p[1] = 0;
	PSHORTB(p+2, size);

	/* TPDU: hdlen[1] type[1] seqno[1] */
	p[4] = 2;
	p[5] = Data;
	p[6] = (1<<7);	/* seqno (0 in Class 0) + EOT mark (1<<7) */

	return size;
}

/* disconnection request */
int
mktpdr(uchar* buf, int nbuf, int ndata)
{
	int size;
	uchar *p;

	p = buf;
	size = TPDATAFIXLEN+ndata;
	if(size > nbuf){
		werrstr("buffer too small");
		return -1;
	}

	/* TPKT header: version[1] unused[1] len[2] */
	p[0] = 0x03;
	p[1] = 0;
	PSHORTB(p+2, size);

	/* HupReq: hdlen[1] type[1] seqno[1] */
	p[4] = 2;
	p[5] = HupReq;
	p[6] = (1<<7);		/* seqno (0 in Class 0) + EOT mark (1<<7) */
	return size;
}
