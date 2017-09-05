#include <u.h>
#include <libc.h>
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

enum
{
	Bits5	= 0x1F,
	Bits7 = 0x7F,
};

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

uchar*
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

uchar*
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
	case 2:	*plen = nhgets(p); break;
	case 3:	*plen = (nhgets(p)<<8)|p[2]; break;
	case 4:	*plen = nhgetl(p); break;
	}
	return p+c;
}

void
pbshort(uchar* p, int v)
{
	p[0]=2;
	p[1]=2;
	hnputs(p+2,v);
}

int
mcstype(uchar* p, uchar* ep)
{
	if(tptype(p,ep) != Data){
		werrstr("not an X.224 Data TPDU");
		return -1;
	}
	p = tpdat(p, ep);
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
mcschan(uchar *p, uchar* ep)
{
	if(mcstype(p,ep) != Msdi){
		werrstr("not an MCS Send Data Indication: %r");
		return -1;
	}
	if((p = tpdat(p, ep)) == nil)
		return -1;
	if(p+5 > ep){
		werrstr(Eshort);
		return -1;
	}
	return nhgets(p+3);
}

uchar*
mcsdat(uchar *p, uchar* ep)
{
	if(mcstype(p,ep) != Msdi){
		werrstr("not an MCS Send Data Indication: %r");
		return nil;
	}
	if((p = tpdat(p, ep)) == nil)
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

	hnputs(p, 0x7f65);	/* Connect-Initial tag */
	p[2] = 0x82;		/* length in next 2 bytes  */
	hnputs(p+3, ndata+MCSCIFIXLEN-2*2-1);
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
	hnputs(p+2, ndata);
	/* userData should follow */

	return MCSCIFIXLEN+ndata;
}

/* GCC Conference Create Request  [T.124 section 8.7] in ASN.1 PER [X.691] */
int
sizegccr(Msg* m)
{
	int size, nv;

	nv = m->nvc;
	size = 9+14+216+12+12 + 8+12*nv;
	return size;	// should agree with the below
}


static uchar t124IdentifierKeyOid[7] = {0, 5, 0, 20, 124, 0, 1};

int
putgccr(uchar* buf, uint nb, Msg* m)
{
	int i;
	uchar *p, *ep;
	long gccsize, earlyCapabilityFlags;
	int ver, depth, width, height, sproto, wantconsole;
	char* sysname;
	Vchan *v;
	int nv;

	p = buf;
	ep = buf+nb;
	gccsize = sizegccr(m)-9;
	if(p+gccsize+9 > ep){
		werrstr(Eshort);
		return -1;
	}

	ver = m->ver;
	depth = m->depth;
	width = m->xsz;
	height = m->ysz;
	sysname = m->sysname;
	sproto = m->sproto;
	wantconsole = m->wantconsole;
	v = m->vctab;
	nv = m->nvc;

	earlyCapabilityFlags = CanErrinfo;
	if(depth == 32)
		earlyCapabilityFlags |= Want32bpp;

	memcpy(p, t124IdentifierKeyOid, 7);

	// connectPDU as a PER octet string
	hnputs(p+7, (gccsize | 0x8000));	// connectPDU length
	hnputs(p+9, 8);		// ConferenceCreateRequest
	hnputs(p+11, 16);
	p[13] = 0;
	iputs(p+14, 0xC001);	// userData key: h221NonStandard
	p[16] = 0;
	memcpy(p+17, "Duca", 4);	// H.221 nonstandard key (3.2.5.3.3)
	hnputs(p+21, ((gccsize-14) | 0x8000));		// userData length
	p += 23;

	// 2.2.1.3.2 Client Core Data
	iputs(p+0, ClientCore);
	iputs(p+2, 216);	// length of the data block
	iputl(p+4, ver);	// rdpVersion: RDP5=0x00080004
	iputs(p+8, width);	// desktopWidth ≤ 4096
	iputs(p+10, height);	// desktopHeight ≤ 2048
	iputs(p+12, 0xCA01);	// colorDepth=8bpp, ignored
	iputs(p+14, 0xAA03);	// SASSequence
	iputl(p+16, 0x409);	// keyboardLayout=us
	iputl(p+20, 2600); 	// clientBuild
	memset(p+24, 32, 0);	// clientName[32]
	toutf16(p+24, 32, sysname, strlen(sysname)+1);
	iputs(p+54, 0);		// zero-terminateclientName
	iputl(p+56, 4);	// keyboardType: 4="IBM enhanced (101-key or 102-key)"
	iputl(p+60, 0);	// keyboardSubType
	iputl(p+64, 12);	// keyboardFunctionKey
	memset(p+68, 64, 0);	// imeFileName[64]
	iputs(p+132, 0xCA01);	// postBeta2ColorDepth=8bpp, ignored
	iputs(p+134, 1);	// clientProductId
	iputl(p+136, 0);	// serialNumber
	iputs(p+140, MIN(depth, 24));	// highColorDepth: 4, 8, 15, 16, 24 bpp.
	iputs(p+142, 1+2+4+8);	// supportedColorDepths: 1=24, 2=16, 4=15, 8=32 bpp
	iputs(p+144, earlyCapabilityFlags);	// earlyCapabilityFlags 
	memset(p+146, 64, 0);	// clientDigProductId[64]
	p[210] = 7;	// connectionType: 7=autodetect
	p[211] = 0;	// pad1octet
	iputl(p+212, sproto);	// serverSelectedProtocol
	p += 216;
	
	// 2.2.1.3.3 Client Security Data
	iputs(p+0, ClientSec);
	iputs(p+2, 12);	// length of the data block
	iputl(p+4, 0); 	// (legacy) encryptionMethods
	iputl(p+8, 0); 	// extEncryptionMethods
	p += 12;

	// 2.2.1.3.5 Client Cluster Data		*optional*
	iputs(p+0, ClientCluster);
	iputs(p+2, 12);	// length of the data block
	iputl(p+4, (wantconsole? 11 : 9));	// Flags
	iputl(p+8, 0);		// RedirectedSessionID
	p += 12;

	// 2.2.1.3.4 Client Network Data 	*optional*
	// type[2] len[2] nchan[4] nchan*(name[8] options[4])
	iputs(p+0, ClientNet);
	iputs(p+2, 8+12*nv);
	iputl(p+4, nv);
	for(i=0; i<nv; i++){
		memcpy(p+8+12*i+0, v[i].name, 8);
		hnputl(p+8+12*i+8, v[i].flags);
	}
	p += 8+12*nv;

	return p-buf;
}

int
getmcr(Msg* m, uchar* b, uint nb)
{
	int len, tag, r, utype, ulen;
	uchar *p, *ep;

	p = b;
	ep = b+nb;

	m->type = Mconnected;

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
		utype = igets(p+0);
		ulen = igets(p+2);
		switch(utype){
		case SrvCore:		/* 2.2.1.4.2 Server Core Data */
			m->ver = igetl(p+4);
			break;
		/* BUG: exract channel IDs from SrvNet */
		}
		p += ulen;
	}
	return p-b;
}


/* MCS Send Data Request */
int
putmsdr(uchar* p, int nb, int ndata, int chanid, int mcsuid)
{
	if(nb < 8){
		werrstr(Esmall);
		return -1;
	}
	
	p[0] = (Msdr<<2);
	hnputs(p+1, mcsuid);
	hnputs(p+3, chanid);
	p[5] = 0x70;
	hnputs(p+6, ndata|0x8000);
	return 8;
}
