#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mp.h>
#include <libsec.h>
#include "dat.h"
#include "fns.h"

enum
{
	/* X.224 PDU codes */
	ConReq=		0xE0, 		/* connection request */
	ConCfrm=	0xD0,		/* connection confirm */
	HupReq=		0x80,		/* disconnection request */
	Data=		0xF0,		/* data */
	Err=			0x70,		/* error */

	/* Rdpnego.type */
	Tnego=	1,
	Rnego=	2,

	/* Rdpnego.proto */
	ProtoTLS= 	1,
	ProtoCSSP=	2,
	ProtoUAUTH=	8,
};

struct Rdpnego
{
	int	type;
	int	flags;
	int	proto;
};
int	getnego(Rdpnego*, uchar*, uint);
int	putnego(uchar*, uint, Rdpnego*);

/*
 * examine a packet header at between p and ep
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
tpdutype(uchar* p, uchar* ep)
{
	if(p+5 >= ep){
		werrstr(Eshort);
		return -1;
	}
	return p[5];
}

int
isdatatpdu(uchar* p, uchar* ep)
{
	return (tpdutype(p,ep) == Data);
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
tpdupayload(uchar* p, uchar* ep)
{
	uchar* q;

	if(istpkt(p, ep) == 0){
		werrstr("Fast-Path Update PDU is not expected");
		return nil;
	}
	if(tpdutype(p,ep) == Data)
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


int
putnego(uchar* b, uint nb, Rdpnego* m)
{
	int len;

	len = 8;
	if(nb < 8){
		werrstr(Esmall);
		return -1;
	}
	b[0] = m->type;
	b[1] = m->flags;
	PSHORT(b+2, len);
	PLONG(b+4, m->proto);

	return len;
}

int
getnego(Rdpnego* m, uchar* b, uint nb)
{
	int len;

	if(nb < 8){
		werrstr(Eshort);
		return -1;
	}
	m->type = b[0];
	m->flags = b[1];
	len = GSHORT(b+2);
	m->proto = GLONG(b+4);
	if(len != 8){
		werrstr("bad length in RDP Nego Response");
		return -1;
	}
	return len;
}

int
istrusted(uchar* cert, int certlen)
{
	uchar digest[SHA1dlen];
	Thumbprint *table;

	if(cert==nil || certlen <= 0) {
		werrstr("server did not provide TLS certificate");
		return 0;
	}
	sha1(cert, certlen, digest, nil);
	table = initThumbprints("/sys/lib/tls/rdp", "/sys/lib/tls/rdp.exclude");
	if(!table || !okThumbprint(digest, table)){
		werrstr("server certificate %.*H not recognized", SHA1dlen, digest);
		return 0;
	}
	freeThumbprints(table);
	return 1;
}

/* lifted from /sys/src/cmd/upas/fs/imap4.c:/^starttls */
int
starttls(void)
{
	TLSconn conn;
	int sfd;

	fmtinstall('H', encodefmt);
	memset(&conn, 0, sizeof conn);
	sfd = tlsClient(rd.fd, &conn);
	if(sfd < 0){
		werrstr("tlsClient: %r");
		return -1;
	}
	if(!istrusted(conn.cert, conn.certlen)){
		close(sfd);
		return -1;
	}

	close(rd.fd);
	rd.fd = sfd;

	return sfd;
}

/* 5.4.2.1 Negotiation-Based Approach */
int
x224connect(int fd)
{
	int n, ndata;
	uchar buf[4+7+25+8], *p, *ep;
	Rdpnego t, r;

	ndata = 25+8;
	n = mktpcr(buf, sizeof buf, ndata);
	if(n < 0)
		return -1;

	p = buf+n-ndata;
	ep = buf+n;

	memcpy(p, "Cookie: mstshash=eltons\r\n", 25);
	p += 25;

	t = (Rdpnego){Tnego, 0, ProtoTLS};
	if(putnego(p, ep-p, &t) != 8){
		werrstr("pnego failed: %r");
		return -1;
	}
	if(writen(fd, buf, n) != n)
		return -1;

	n = readpdu(fd, buf, sizeof buf);
	if(n < 6){
		werrstr("X.224: ConCfrm: %r");
		return -1;
	}
	ep = buf+n;

	if(!istpkt(buf, ep) || tpdutype(buf, ep) != ConCfrm){
		werrstr("X.224: protocol botch");
		return -1;
	}
	if((p = tpdupayload(buf, ep)) == nil)
		return -1;
	if(getnego(&r, p, ep-p) < 0 || r.type != Rnego || !(r.proto&ProtoTLS)){
		werrstr("server refused STARTTLS");
		return -1;
	}

	fd = starttls();
	if(fd < 0)
		return -1;

	rd.sproto = r.proto;

	return fd;
}

int
x224disconnect(int fd)
{
	int n, m;
	uchar buf[12];

	n = mktpdr(buf, sizeof buf, 0);
	m = write(fd, buf, n);
	return m;
}
