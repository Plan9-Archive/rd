#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum /* Rdpnego.type */
{
	Tnego=	1,
	Rnego=	2,
};

typedef	struct	Rdpnego Rdpnego;
struct Rdpnego
{
	int	type;
	int	flags;
	int	proto;
};

static int
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

static int
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
sizesechdr(int secflags)
{
	if(secflags&Scrypt)
		return  12;	// flags[4] mac[8]
	else if(secflags)
		return 4;		// flags[4]
	return 0;
}

uchar*
txprep(uchar* buf, int nb, int ndata, int chanid, int mcsuid, int secflags)
{
	int n, len, shdsize;
	uchar *p, *ep;

	if(chanid==0)
		chanid = GLOBALCHAN;

	shdsize = sizesechdr(secflags);
	len = TPDATAFIXLEN+8+shdsize+ndata;
	if(len>nb){
		werrstr("%s: provided %d, need %d, data %d", Esmall, nb, len, ndata);
		return nil;
	}
	ep = buf+len;

	ndata = len-TPDATAFIXLEN;
	n = mktpdat(buf, nb, ndata);
	if(n < 0)
		sysfatal("mktpdat: %r");
	p = buf+TPDATAFIXLEN;

	ndata -= 8;
	if(putmsdr(p, ep-p, ndata, chanid, mcsuid) < 0)
		sysfatal("putmsdr: %r");
	p += 8;

	if(shdsize > 0)
		PLONG(p, secflags);
	return p + shdsize;
}

int
putmsg(uchar* b, uint nb, Msg* m)
{
	int n, nld, len;
	uchar *p, *ep, *q;
	Rdpnego neg;

	switch(m->type){
	case Xconnect:
		/* 5.4.2.1 Negotiation-Based Approach */
		nld = 25+8;
		n = mktpcr(b, nb, nld);
		if(n < 0)
			return -1;
		p = b+n-nld;
		ep = b+n;
	
		memcpy(p, "Cookie: mstshash=eltons\r\n", 25);
		neg = (Rdpnego){Tnego, 0, m->negproto};
		if(putnego(p+25, ep-p, &neg) != 8){
			werrstr("pnego failed: %r");
			return -1;
		}
		return n;
		
	case Xhangup:
		return mktpdr(b, nb, 0);

	case Mattach:
		n = mktpdat(b, nb, 1);
		if(n < 0)
			return -1;
		b[TPDATAFIXLEN] = (Maur<<2);
		return n;

	case Mjoin:
		n = mktpdat(b, nb, 5);
		if(n < 0)
			return -1;
		p = b+TPDATAFIXLEN;
		p[0] = (Mcjr << 2);
		PSHORTB(p+1, m->mcsuid);
		PSHORTB(p+3, m->chanid);
		return n;

	case Merectdom:
		n = mktpdat(b, nb, 5);
		if(n < 0)
			return -1;
		p = b+TPDATAFIXLEN;
		p[0] = (Medr << 2);
		PSHORTB(p+1, 1);
		PSHORTB(p+3, 1);
		return n;

	case Mconnect:
		/* 2.2.1.3 Client MCS Connect Initial PDU with GCC Conference Create Request */
		nld = sizegccr(m);
		len = mktpdat(b, nb, nld+MCSCIFIXLEN);
		if(len < 0){
			werrstr("mktpdat: %r");
			return -1;
		}
		p = b+TPDATAFIXLEN;
		ep = b+nb;
		n = mkmcsci(p, ep-p, nld);
		if(n != nld+MCSCIFIXLEN){
			werrstr("mkmcsci: %r");
			return -1;
		}
		n = putgccr(p+MCSCIFIXLEN, nld, m);
		if(n != nld){
			werrstr("putgccr: %r");
			return -1;
		}
		return len;

	case Dclientinfo:
		/* 2.2.1.11 Client Info PDU */
		return putclientinfo(b, nb, m);

	case Mactivated:
		/* 2.2.1.13.2 Client Confirm Active PDU */
		return putconfirmactive(b, nb, m);

	case Mvchan:
		nld = m->ndata+8;
		p = txprep(b, nb, nld, m->chanid, m->originid, 0);
		if(p == nil)
			return -1;
		PLONG(p+0, m->len);
		PLONG(p+4, m->flags);
		memcpy(p+8, m->data, m->ndata);
		len = p+nld-b;
		return len;

	case Async:
		/* 2.2.1.14 Client Synchronize PDU */
		nld = 4+SCDSIZE;
		if((p = txprep(b, nb, nld, 0, m->originid, 0)) == nil)
			return -1;
		len = p+nld-b;
		q = putsdh(p, p+nld, nld, ADsync, m->originid, m->shareid);
		PSHORT(q+0, 1);	// sync message type
		PSHORT(q+2, m->mcsuid);	// target MCS userId
		return len;

	case Actl:
		/* 2.2.1.15 Control PDU */
		nld = 8+SCDSIZE;
		if((p = txprep(b, nb, nld, 0, m->originid, 0)) == nil)
			return -1;
		len = p+nld-b;
	
		q = putsdh(p, p+nld, nld, ADctl, m->originid, m->shareid);
		/* action[2] grantId[2] controlId[2] */
		PSHORT(q+0, m->action);
		PSHORT(q+2, 0);
		PLONG(q+4, 0);
		return len;

	case Afontls:
		/* 2.2.1.18 Client Font List PDU */
		nld = 8+SCDSIZE;
		if((p = txprep(b, nb, nld, 0, m->originid, 0)) == nil)
			return -1;
		len = p+nld-b;
	
		q = putsdh(p, p+nld, nld, ADfontlist, m->originid, m->shareid);
		PSHORT(q+0, 0);	// numberFonts
		PSHORT(q+2, 0);	// totalNumFonts
		PSHORT(q+4, 2+1);	// listFlags: 1=first, 2=last
		PSHORT(q+6, 50);	// entrySize
		return len;

	case Ainput:
		/* 2.2.8.1.1.3.1.1 Slow-Path Input Event (TS_INPUT_EVENT) */
		nld = 16+SCDSIZE;
		if((p = txprep(b, nb, nld, 0, m->originid, 0)) == nil)
			return -1;
		len = p+nld-b;
	
		q = putsdh(p, p+nld, nld, ADinput, m->originid, m->shareid);
		PSHORT(q+0, 1);	/* numEvents */
		PSHORT(q+2, 0);
		PLONG(q+4, m->msec);
		PSHORT(q+8, m->mtype);
		PSHORT(q+10, m->flags);
		PSHORT(q+12, m->iarg[0]);
		PSHORT(q+14, m->iarg[1]);
		return len;

	case Lreq:
	case Lnolicense:
		if((nld = sizelicensemsg(m)) < 0)
			return -1;
		if((p = txprep(b, nb, nld, 0, m->originid, Slicensepk)) == nil)
			return -1;
		len = p+nld-b;
		if(putlicensemsg(p, nld, m) != nld){
			werrstr("putlicensemsg: %r");
			return -1;
		}
		return len;

	case Dsupress:
		/* 2.2.11.3 Suppress Output PDU */
		nld = (m->allow?12:4)+SCDSIZE;
		if((p = txprep(b, nb, nld, 0, m->originid, 0)) == nil)
			return -1;
		len = p-b+nld;
	
		q = putsdh(p, p+nld, nld, ADsupress, m->originid, m->shareid);
		q[0] = (m->allow?1:0); 
		memset(q+1, 3, 0);
		if(m->allow){
			PSHORT(q+4, 0);	// left
			PSHORT(q+6, 0);	// top
			PSHORT(q+8, m->xsz-1);	// right
			PSHORT(q+10, m->ysz-1);	// bottom
		}
		return len;

	default:
		werrstr("putmsg: unsupported type");
		return 0;
	}
};

int
getmsg(Msg* m, uchar* b, uint nb)
{
	uchar *p, *ep;
	int type, mtag, btag, mr, secflg, sctlver;
	Rdpnego neg;

	p = b;
	ep = b+nb;

	if(istpkt(p, ep) == 0){
		/*
		 * 2.2.9.1.2 Server Fast-Path Update PDU
		 * enabled with CanFastpath in General Capability Set
		 */
		if(p[0]&(1<<7)){
			werrstr("legacy encryption in a Fast-Path PDU");
			return -1;
		}
		if(p[1]&(1<<7))
			p += 3;
		else
			p += 2;
		m->type = Aupdate;
		m->data = p;
		m->ndata = ep-p;
		m->getshare = getshareF;
		return nb;
	}

	type = tptype(b, b+nb);
	switch(type){
	default:
		werrstr("unknown TPDU type %d", type);
		return -1;
	case ConCfrm:
		/* 5.4.2.1 Negotiation-Based Approach */
		m->type = Xconnected;
		m->negproto = 0;
		if((p = tpdat(p, ep)) != nil && getnego(&neg, p, ep-p))
			if(neg.type == Rnego)
				m->negproto = neg.proto;
		return nb;
	case Data:
		p = tpdat(p, ep);
		if(p+2 > ep){
			werrstr(Eshort);
			return -1;
		}

		/* try ASN.1 PER: DomainMCSPDU are encoded this way */
		mtag = p[0]>>2;
		switch(mtag){
		default:
			werrstr("unknown MCS tag %d", mtag);
			return -1;
		case Mauc:
			m->type = Mattached;
			m->mcsuid = 0;
			mr = p[1];
			if(mr != 0){
				werrstr("Mauc error result: %d", mr);
				return -1;
			}
			if((p[0])&2){
				if(p+4 > ep){
					werrstr(Eshort);
					return -1;
				}
				m->mcsuid = GSHORTB(p+2);
			}
			return nb;
		case Mcjc:
			m->type = Mjoined;
			mr = p[1];
			if(mr != 0){
				werrstr("Mcjc error result: %d", mr);
				return -1;
			}
			return nb;
		case Mdpu:
			m->type = Mclosing;
			return nb;
		case Msdi:
			m->chanid = mcschan(b, b+nb);
			if(m->chanid < 0){
				werrstr("%d: bad MCS channel id", m->chanid);
				return -1;
			}
			p = mcsdat(b, b+nb);
			if(p == nil)
				return -1;
			if(m->chanid != GLOBALCHAN){
				m->type = Mvchan;
				m->len = GLONG(p+0);
				m->flags = GLONG(p+4);
				m->data = p+8;
				m->ndata = ep-p-8;
				if(m->len > 8*1024*1024){
					werrstr("bad length in virtual channel PDU header");
					return -1;
				}
				return nb;
			}
			if(isflowpdu(p,ep)){
				m->type = Aflow;
				return nb;
			}
			secflg = GSHORT(p);
			sctlver = GSHORT(p+2)>>4;
			if(secflg&Slicensepk && sctlver != 1)
				return getlicensemsg(m, p+4, ep-(p+4));
			
			m->type = Aupdate;
			m->data = p;
			m->ndata = ep-p;
			m->getshare = getshareT;
			return nb;
		case 31:
			/* try ANS.1 BER: T.125 ConnectMCSPDU are encoded this way */
			gbtag(p, ep, &btag);
			switch(btag){
			case Mcr:
				return getmcr(m, p, ep-p);
			default:
				werrstr("unknown MCS BER tag %d", btag);
				return -1;
			}
		}
		
	}
};

int
readmsg(Rdp* c, Msg* m)
{
	uchar* buf;
	int fd, n;
	uint nb;

	fd = c->fd;
	buf = c->rbuf;
	nb = sizeof c->rbuf;

	if((n = readpdu(fd, buf, nb)) < 0 || getmsg(m, buf, n) <= 0)
		return -1;

	return n;
}

int
writemsg(Rdp* c, Msg* m)
{
	uchar buf[MAXTPDU];
	int fd, n;

	fd = c->fd;
	if((n = putmsg(buf, sizeof buf, m)) < 0 || write(fd, buf, n) != n)
		return -1;

	return n;
}
