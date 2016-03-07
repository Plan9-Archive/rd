/* T.122 MCS, T.124 Generic Conference Control, T.125 MCS protocol  */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum
{
	/* ASN.1 Universal tags */
	TagBool		= 1,
	TagInt		= 2,
	TagOctetString	= 4,
	TagEnum		= 10,
	TagSeq		= 16,		/* also TagSeq OF */

	/* ASN.1 tag numbers for MCS types */
	Mci=		101,		/* Connect Initial */
	Mcr=	102,		/* Connect Response */
	Medr=	1,		/* Erect Domain Request */
	Maur=	10,		/* Attach User Request */
	Mauc=	11,		/* Attach User Confirm */
	Mcjr=	14,		/* Channel Join Request */
	Mcjc=	15,		/* Channel Join Confirm */
	Msdr=	25,		/* Send Data Request */
	Msdi=	26,		/* Send Data Indication */
	Mdpu=	8,		/* Disconnect Provider Ultimatum */

	Musrchanbase=	1001,

	/* 2.2.1.3 Client MCS Connect Initial PDU with GCC Conference Create Request */
	ClientCore=		0xC001,
	ClientCluster=		0xC004,
	ClientSec=		0xC002,
	ClientNet=		0xC003,

	CanErrinfo=	1,
	Want32bpp=	2,

	/* 2.2.1.3.1 User Data Header (TS_UD_HEADER) */
	SrvCore				= 0x0C01,

};

int	mkgcccr(uchar*,int);
int	sizegcccr(void);

enum
{
	Bits5	= 0x1F,
	Bits7 = 0x7F,
};

static	uchar*	gblen(uchar*,uchar*,int*);
static	uchar*	gbtag(uchar*,uchar*,int*);
static	void		pbshort(uchar*,int);

static uchar*
gbuint7(uchar *p, uchar* ep, int* pv)
{
	uint u,v,go;

	v = 0;
	go = 1;
	while(go){
		if(p >= ep){
			werrstr(Eshort);
			return nil;
		}
		u = *p;
		v = (v<<7) | (u&Bits7);
		if(v&(Bits7<<24)){
			werrstr(Ebignum);
			return nil;
		}
		go = u&(1<<7);
		p++;
	}
	*pv = v;
	return p;	
}

static uchar*
gbtag(uchar *p, uchar* ep, int* ptag)
{
	if(p >= ep){
		werrstr(Eshort);
		return nil;
	}
	*ptag = p[0] & Bits5;
	p += 1;
	if(*ptag == Bits5)
		p = gbuint7(p, ep, ptag);
	return p;
}

static uchar*
gblen(uchar *p, uchar* ep, int* plen)
{
	int c,v;

	if(p >= ep){
		werrstr(Eshort);
		return nil;
	}

	v = *p++;
	if(v < (1<<7)){
		*plen = v;
		return p;
	}
	c = v&Bits7;
	if(p+c >= ep){
		werrstr(Eshort);
		return nil;
	}
	switch(c){
	default:	werrstr(Ebignum); return nil;
	case 0:	*plen = 0; break;
	case 1:	*plen = p[0]; break;
	case 2:	*plen = GSHORTB(p); break;
	case 3:	*plen = (GSHORTB(p)<<8)|p[2]; break;
	case 4:	*plen = GLONGB(p); break;
	}
	return p+c;
}

static void
pbshort(uchar* p, int v)
{
	p[0]=2;
	p[1]=2;
	PSHORTB(p+2,v);
}

int
mcstype(uchar* p, uchar* ep)
{
	if(!isdatatpdu(p,ep)){
		werrstr("not an X.224 Data TPDU");
		return -1;
	}
	p = tpdupayload(p, ep);
	if(p == nil)
		return -1;
	if(p >= ep){
		werrstr(Eshort);
		return -1;
	}
	return p[0]>>2;
}

int
ismcshangup(uchar* p, uchar* ep)
{
	return (mcstype(p,ep) == Mdpu);
}

int
mcschanid(uchar *p, uchar* ep)
{
	if(mcstype(p,ep) != Msdi){
		werrstr("not an MCS Send Data Indication: %r");
		return -1;
	}
	if((p = tpdupayload(p, ep)) == nil)
		return -1;
	if(p+5 > ep){
		werrstr(Eshort);
		return -1;
	}
	return GSHORTB(p+3);
}

uchar*
mcspayload(uchar *p, uchar* ep)
{
	if(mcstype(p,ep) != Msdi){
		werrstr("not an MCS Send Data Indication: %r");
		return nil;
	}
	if((p = tpdupayload(p, ep)) == nil)
		return nil;

	if(p+6 > ep){
		werrstr(Eshort);
		return nil;
	}
	if(p[6] & 0x80)
		p += 8;
	else
		p += 7;
	if(p > ep){
		werrstr(Eshort);
		return nil;
	}
	return p;
}

/* MCS Send Data Request */
int
mkmcssdr(uchar* p, int nb, int ndata, int chanid)
{
	if(nb < 8){
		werrstr(Esmall);
		return -1;
	}
	
	p[0] = (Msdr<<2);
	PSHORTB(p+1, rd.mcsuid);
	PSHORTB(p+3, chanid);
	p[5] = 0x70;
	PSHORTB(p+6, ndata|0x8000);
	return 8;
}

/* 2.2.1.3 Client MCS Connect Initial PDU with GCC Conference Create Request */
int
mkmcsci(uchar* buf, int nbuf, int ndata)
{
	uchar* p;
	p = buf;
	if(nbuf < ndata+MCSCIFIXLEN){
		werrstr("buffer too small");
		return -1;
	}

	PSHORTB(p, 0x7f65);	/* Connect-Initial tag */
	p[2] = 0x82;		/* length in next 2 bytes  */
	PSHORTB(p+3, ndata+MCSCIFIXLEN-2*2-1);
	p += 5;

	/* BER callingDomainSelector */
	p[0] = TagOctetString;
	p[1] = 1;		/* len */
	p[2] = 1;		
	p += 3;
	/* BER calledDomainSelector */
	p[0] = TagOctetString;
	p[1] = 1;		/* len */
	p[2] = 1;		
	p += 3;
	/* BER upwardFlag */
	p[0] = TagBool;
	p[1] = 1;		/* len */
	p[2] = 0xff;	
	p += 3;

	/* BER MCS DomainParamaters: targetParameters */
	p[0] = 0x30;			/* tag */
	p[1] = 8*4;	/* len */
	pbshort(p+2, 34);	/* maxChannelIds */
	pbshort(p+6, 2); 	/* maxUserIds */
	pbshort(p+10, 0);	/* maxTokenIds */
	pbshort(p+14, 1);	/* maxPriorities */
	pbshort(p+18, 0);	/* minThroughput */
	pbshort(p+22, 1);	/* maxHeight (of a MCS provider) */
	pbshort(p+26, 65535);	/* maxMCSPDUsize */
	pbshort(p+30, 2);	/* (MCS) protocolVersion */
	p += 34;

	/* BER MCS DomainParamaters: minimumParameters */
	p[0] = 0x30;			/* tag */
	p[1] = 8*4;	/* len */
	pbshort(p+2, 1);	/* maxChannelIds */
	pbshort(p+6, 1); 	/* maxUserIds */
	pbshort(p+10, 1);	/* maxTokenIds */
	pbshort(p+14, 1);	/* maxPriorities */
	pbshort(p+18, 0);	/* minThroughput */
	pbshort(p+22, 1);	/* maxHeight (of a MCS provider) */
	pbshort(p+26, 1056);	/* maxMCSPDUsize */
	pbshort(p+30, 2);	/* (MCS) protocolVersion */
	p += 34;

	/* BER MCS DomainParamaters: maximumParameters */
	p[0] = 0x30;			/* tag */
	p[1] = 8*4;	/* len */
	pbshort(p+2, 65535);	/* maxChannelIds */
	pbshort(p+6, 65535); 	/* maxUserIds */
	pbshort(p+10, 65535);	/* maxTokenIds */
	pbshort(p+14, 1);	/* maxPriorities */
	pbshort(p+18, 0);	/* minThroughput */
	pbshort(p+22, 1);	/* maxHeight (of a MCS provider) */
	pbshort(p+26, 65535);	/* maxMCSPDUsize */
	pbshort(p+30, 2);	/* (MCS) protocolVersion */
	p += 34;

	/* BER userData */
	p[0] = TagOctetString;
	p[1] = 0x82;			/* length in next 2 bytes  */
	PSHORTB(p+2, ndata);
	/* userData should follow */

	return MCSCIFIXLEN+ndata;
}

/* GCC Conference Create Request  [T.124 section 8.7] in ASN.1 PER [X.691] */
int
sizegcccr(void)
{
	int size;
	size = 9+14+216+12+12 + 8+12*nvc;
	return size;	// should agree with the below
}

int
mkgcccr(uchar* buf, int nb)
{
	int i;
	uchar *p, *ep;
	long gccsize, earlyCapabilityFlags;

	p = buf;
	ep = buf+nb;
	gccsize = sizegcccr()-9;
	if(p+gccsize+9 > ep){
		werrstr(Eshort);
		return -1;
	}

	earlyCapabilityFlags = CanErrinfo;
	if(rd.depth == 32)
		earlyCapabilityFlags |= Want32bpp;

	// t124IdentifierKey: 0.0.20.124.0.1
	p[0] = 0;
	p[1] = 5;
	p[2] = 0;
	p[3] = 20;
	p[4] = 124;
	p[5] = 0;
	p[6] = 1;

	// connectPDU as a PER octet string
	PSHORTB(p+7, (gccsize | 0x8000));	// connectPDU length
	PSHORTB(p+9, 8);		// ConferenceCreateRequest
	PSHORTB(p+11, 16);
	p[13] = 0;
	PSHORT(p+14, 0xC001);	// userData key: h221NonStandard. Yes, in LE.
	p[16] = 0;
	memcpy(p+17, "Duca", 4);	// H.221 nonstandard key (as mandated in 3.2.5.3.3)
	PSHORTB(p+21, ((gccsize-14) | 0x8000));		// userData length
	p += 23;

	// 2.2.1.3.2 Client Core Data
	PSHORT(p+0, ClientCore);
	PSHORT(p+2, 216);	// length of the data block
	PLONG(p+4, 0x00080004);	// rdpVersion: RDP5=0x00080004
	PSHORT(p+8, rd.dim.x);	// desktopWidth ≤ 4096
	PSHORT(p+10, rd.dim.y);	// desktopHeight ≤ 2048
	PSHORT(p+12, 0xCA01);	// colorDepth=8bpp, ignored
	PSHORT(p+14, 0xAA03);	// SASSequence
	PLONG(p+16, 0x409);	// keyboardLayout=us
	PLONG(p+20, 2600); 	// clientBuild
	toutf16(p+24, 32, rd.local, strlen(rd.local));	// clientName[32]
	PSHORT(p+54, 0);		// zero-terminateclientName
	PLONG(p+56, 4);	// keyboardType: 4="IBM enhanced (101-key or 102-key)"
	PLONG(p+60, 0);	// keyboardSubType
	PLONG(p+64, 12);	// keyboardFunctionKey
	memset(p+68, 64, 0);	// imeFileName[64]
	PSHORT(p+132, 0xCA01);	// postBeta2ColorDepth=8bpp, ignored
	PSHORT(p+134, 1);	// clientProductId
	PLONG(p+136, 0);	// serialNumber
	PSHORT(p+140, MIN(rd.depth, 24));	// highColorDepth: 4, 8, 15, 16, 24 bpp.
	PSHORT(p+142, 1+2+4+8);	// supportedColorDepths: 1=24, 2=16, 4=15, 8=32 bpp
	PSHORT(p+144, earlyCapabilityFlags);	// earlyCapabilityFlags 
	memset(p+146, 64, 0);	// clientDigProductId[64]
	p[210] = 7;	// connectionType: 7=autodetect
	p[211] = 0;	// pad1octet
	PLONG(p+212, rd.sproto);	// serverSelectedProtocol
	p += 216;
	
	// 2.2.1.3.3 Client Security Data
	PSHORT(p+0, ClientSec);
	PSHORT(p+2, 12);	// length of the data block
	PLONG(p+4, 0); 	// (legacy) encryptionMethods
	PLONG(p+8, 0); 	// extEncryptionMethods
	p += 12;

	// 2.2.1.3.5 Client Cluster Data		*optional*
	PSHORT(p+0, ClientCluster);
	PSHORT(p+2, 12);	// length of the data block
	PLONG(p+4, (rd.wantconsole? 11 : 9));	// Flags
	PLONG(p+8, 0);		// RedirectedSessionID
	p += 12;

	// 2.2.1.3.4 Client Network Data 	*optional*
	// type[2] len[2] nchan[4] nchan*(name[8] options[4])
	PSHORT(p+0, ClientNet);
	PSHORT(p+2, 8+12*nvc);
	PLONG(p+4, nvc);
	for(i=0; i<nvc; i++){
		memcpy(p+8+12*i+0, vctab[i].name, 8);
		PLONGB(p+8+12*i+8, vctab[i].flags);
	}
	p += 8+12*nvc;

	return p-buf;
}

void
erectdom(int fd)
{
	uchar buf[20], *p;
	int len, nb;

	p = buf;
	nb = sizeof(buf);
	len = mktpdat(buf, nb, 5);
	if(len < 0)
		sysfatal("mktpdat: %r");
	p += TPDATAFIXLEN;
	
	p[0] = (Medr<<2);
	PSHORTB(p+1, 1);
	PSHORTB(p+3, 1);	
	if(writen(fd, buf, len) != len)
		sysfatal("Erect Domain: write: %r");
}

int
attachuser(int fd)
{
	int len, tag, r, nb;
	uchar buf[20], *p, *ep;

	nb = sizeof(buf);
	len = mktpdat(buf, nb, 1);
	if(len < 0)
		sysfatal("mktpdat: %r");
	buf[TPDATAFIXLEN] = (Maur<<2);
	if(writen(fd, buf, len) != len)
		sysfatal("Attach User: write: %r");

	len = readpdu(fd, buf, nb);
	if(len <= 0)
		sysfatal("readpdu: %r");
	p = buf;
	ep = buf+len;
	if(!isdatatpdu(p,ep))
		sysfatal("MCS: expected Data TPDU\n");
	p = tpdupayload(p, ep);
	if(p+2 > ep)
		sysfatal(Eshort);

	tag = p[0]>>2;
	r = p[1];
	if(tag != Mauc)
		sysfatal("expected tag %d (Mauc), got %d", Mauc, tag);
	if(r != 0)
		sysfatal("Mauc error result: %d", r);
	if((p[0])&2){
		if(p+4 > ep)
			sysfatal(Eshort);
		rd.mcsuid = GSHORTB(p+2);
		rd.userchan = rd.mcsuid+Musrchanbase;
	}
	return r;
}

int
joinchannel(int fd, int chanid)
{
	uchar buf[32], *p, *ep;
	int tag, len, r, nb;

	p = buf;
	nb = sizeof(buf);
	len = mktpdat(buf, nb, 5);
	if(len < 0)
		sysfatal("mktpdat: %r");
	p += TPDATAFIXLEN;
	p[0] = (Mcjr << 2);
	PSHORTB(p+1, rd.mcsuid);
	PSHORTB(p+3, chanid);
	if(writen(fd, buf, len) != len)
		sysfatal("Channel Join: write: %r");

	len = readpdu(fd, buf, nb);
	if(len <= 0)
		sysfatal("readpdu: %r");
	p = buf;
	ep = buf+len;
	if(!isdatatpdu(p,ep))
		sysfatal("MCS: expected Data TPDU\n");
	p = tpdupayload(p, ep);
	if(p+2 > ep)
		sysfatal(Eshort);

	tag = p[0]>>2;
	r = p[1];
	if(tag != Mcjc)
		sysfatal("expected tag %d (Mcjc), got %d", Mcjc, tag);
	if(r != 0)
		sysfatal("Mcjc error result: %d", r);

	return r;

}

int
mcsconnect(int fd)
{
	uchar buf[MAXTPDU], *p, *ep;
	int n, ndata, nb, len, tag, r, ver, utype, ulen;

	/* 2.2.1.3 Client MCS Connect Initial PDU with GCC Conference Create Request */
	nb = sizeof(buf);
	ndata = sizegcccr();
	len = mktpdat(buf, nb, ndata+MCSCIFIXLEN);
	if(len < 0)
		sysfatal("mktpdat: %r");
	p = buf+TPDATAFIXLEN;
	ep = buf+nb;
	n = mkmcsci(p, ep-p, ndata);
	if(n != ndata+MCSCIFIXLEN)
		sysfatal("mkmcsci: %r");
	n = mkgcccr(p+MCSCIFIXLEN, ndata);
	if(n != ndata)
		sysfatal("mkgcccr: %r");
	if(writen(fd, buf, len) != len)
		sysfatal("TPDUDT: write: %r");

	/* 2.2.1.4 Server MCS Connect Response PDU with GCC Conference Create Response */
	len = readpdu(fd, buf, nb);
	if(len <= 0){
		werrstr("read MCS Connect Response PDU: %r");
		return -1;
	}
	p = buf;
	ep = buf+len;

	if(!isdatatpdu(p,ep)){
		werrstr("MCS: expected Data TPDU\n");
		return -1;
	}
	p = tpdupayload(p, ep);

	/* at MCS Connect-Response ASN.1 BER-encoded structure */
	if((p = gbtag(p, ep, &tag)) == nil || tag != Mcr || (p = gblen(p, ep, &len)) == nil)
		return -1;
	
	/* result */
	if((p = gbtag(p, ep, &tag)) == nil || tag != TagEnum
		|| (p = gblen(p, ep, &len)) == nil || len < 0 || p+len > ep)
		return -1;
	r = p[0];
	if(r != 0){
		werrstr("MCS Connect-Response: %d", r);
		return -1;
	}
	p += len;

	/* calledConnectId */
	if((p = gbtag(p, ep, &tag)) == nil || tag != TagInt
		|| (p = gblen(p, ep, &len)) == nil || len < 0 || p+len > ep)
		return -1;
	p += len;

	/* domainParamaters */
	if((p = gbtag(p, ep, &tag)) == nil || tag != TagSeq
		|| (p = gblen(p, ep, &len)) == nil || len < 0 || p+len > ep)
		return -1;
	p += len;

	/* Mcr userData */
	if((p = gbtag(p, ep, &tag)) == nil || tag != TagOctetString
		|| (p = gblen(p, ep, &len)) == nil || len < 0 || p+len > ep)
		return -1;

	/* GCC ConferenceCreateResponse [T.124] sect 8.7 */
	if(p[21]&(1<<7))
		p += 23;
	else
		p += 22;

	while(p<ep){
		/* 2.2.1.3.1 User Data Header (TS_UD_HEADER) */
		utype = GSHORT(p+0);
		ulen = GSHORT(p+2);
		switch(utype){
		case SrvCore:		/* 2.2.1.4.2 Server Core Data */
			ver = GLONG(p+4);
			assert(ver >= 0x00080004);
			break;
		}
		p += ulen;
	}

	return r;
}
