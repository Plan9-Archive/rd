/* Based on: T.128 Multipoint application sharing */
#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

static const char	srcDesc[] = "Plan 9";	/* sourceDescriptor (T.128 section 8.4.1) */

enum
{
	Bits4=		0x0F,

	/* 2.2.9.1.1.3.1.2.2 Bitmap Data */
	Bcompress=	1,
	Pcompress=	0x20,

	/* 2.2.8.1.1.1.1 Share Control Header */
	PDUactivate	= 1,	/* Demand Active PDU (section 2.2.1.13.1) */
	PDUactivated	= 3,	/* Confirm Active PDU (section 2.2.1.13.2) */
	PDUdeactivate	= 6,	/* Deactivate All PDU (section 2.2.3.1) */
	PDUdata		= 7,	/* Data PDU */

	/* 2.2.9.1.1.4 Server Pointer Update PDU (TS_POINTER_PDU) */
	PDUcursorwarp=	3,

	/* 2.2.1.11.1.1 Info Packet (TS_INFO_PACKET) */
	InfMouse=	1<<0,
	InfNoctlaltdel=	1<<1,
	InfAutologon=	1<<3,
	InfUnicode=	1<<4,
	InfMaxshell=	1<<5,
	InfCompress=	1<<7,
	InfWinkey=	1<<8,
	Compress64k=	1<<9,	// RDP 5.0 bulk compression (3.1.8.4.2)

	PerfNoDrag=	1<<1,
	PerfNoAnim=	1<<2,
	PerfNoTheming=	1<<3,
	PerfNoCursorset=	1<<6,
	PerfFontAA=	1<<7,
	
	/* 2.2.9.1.1.3.1 Slow-Path Graphics Update (TS_GRAPHICS_UPDATE) */
	UpdOrders=	0, 		
 	UpdBitmap=	1,	/* section 2.2.9.1.1.3.1.2 */
	UpdCmap=	2,	/* section 2.2.9.1.1.3.1.1 */

	/* 2.2.9.1.2.1 Fast-Path Update (TS_FP_UPDATE) */
	FUpdOrders=	0,
	FUpdBitmap=	1,
	FUpdCmap=	2,
	FUpdWarp=	8,

	/* 2.2.7.1.1 General Capability Set (TS_GENERAL_CAPABILITYSET) */
	NoBitcomphdr	= 0x0400,
};


/* 2.2.9.1.2.1 Fast-Path Update (TS_FP_UPDATE)
 *
 * updateHeader[1] compressionFlags[1]? size[2] updateData[*]
 */
int
getshareF(Share* as, uchar* a, uint nbytes)
{
	int hd, nb, cflags, ulen;
	uchar *p,  *ep, *q;

	as->type = 0;
	if(nbytes < 3){
		werrstr(Eshort);
		return -1;
	}

	p = a;
	ep = a+nbytes;

	hd = *p++;
	if(hd&(1<<7))
		cflags = *p++;
	else
		cflags = 0;
	if(p+2 > ep){
		werrstr(Eshort);
		return -1;
	}
	nb = GSHORT(p);
	p += 2;
	q = p+nb;
	if(cflags&Pcompress){
		if(p+nb > ep){
			werrstr("bad length %d in Fast-Path PDU header", nb);
			return -1;
		}
		if((p = uncomp(p, nb, cflags, &ulen)) == nil){
			werrstr("fast-path packet de-compression failed: %r cflags=%#x", cflags);
			return -1;
		}
		ep = p+ulen;
	}

	switch(hd&Bits4){
	case FUpdOrders:
		if(p+2>ep){
			werrstr(Eshort);
			return -1;
		}
		as->type = ShUorders;
		as->nr = GSHORT(p);
		as->data = p+2;
		as->ndata = ep-(p+2);
		break;
	case FUpdBitmap:
		if(p+4>ep){
			werrstr(Eshort);
			return -1;
		}
		as->type = ShUimg;
		as->nr = GSHORT(p+2);
		as->data = p+4;
		as->ndata = ep-(p+4);
		break;
	case FUpdCmap:
		as->type = ShUcmap;
		as->data = p;
		as->ndata = ep-p;
		break;
	case FUpdWarp:
		if(p+4>ep){
			werrstr(Eshort);
			return -1;
		}
		as->type = ShUwarp;
		as->x = GSHORT(p+0);
		as->y = GSHORT(p+2);
		break;
	}
	return q-a;
}

/* 
 * T.128 ASPDU
 *
 * 2.2.8.1.1.1.1 Share Control Header
 * https://msdn.microsoft.com/en-us/library/cc240576.aspx
 * 
 * totalLength[2] pduType[2] pduSource[2]
 */
int
getshareT(Share* as, uchar* p, uint nb)
{
	int len, nsrc, type;
	int pduType2, ctype, mtype, uptype, clen, ulen, ulenr;
	uchar *ep;

	as->type = 0;
	if(nb < 6){
		werrstr(Eshort);
		return -1;
	}
	len = GSHORT(p);
	if(len < SCHSIZE || len > nb){
		werrstr("bad length in Share Control PDU header");
		return -1;
	}
	type = GSHORT(p+2)&Bits4;
	as->source = GSHORT(p+4);

	switch(type){
	case PDUactivate:
		/*
		 * 2.2.1.13.1 Server Demand Active PDU
		 * http://msdn.microsoft.com/en-us/library/cc240484.aspx
		 */
		as->type = ShActivate;
		if(len<14){
			werrstr(Eshort);
			return -1;
		}
		as->shareid = GLONG(p+6);
		nsrc = GSHORT(p+10);
		as->ndata = GSHORT(p+12);
		if(len<14+nsrc+as->ndata){
			werrstr(Eshort);
			return -1;
		}
		as->data = p+14+nsrc;
		as->ncap = GSHORT(p+14+nsrc);
		break;
	case PDUdeactivate:
		as->type = ShDeactivate;
		break;
	case PDUdata:
		/*
		 * 2.2.8.1.1.1.2 Share Data Header (TS_SHAREDATAHEADER)
		 * http://msdn.microsoft.com/en-us/library/cc240577.aspx
		 *
		 * shareId[4] pad1[1] streamId[1] uncomprLen[2]
		 * pduType2[1] comprType[1] comprLen[2]
		 */
		ep = p+nb;
	
		if(nb < 18){
			werrstr("%s (%ud<18)", Eshort, nb);
			return -1;
		}
	
		ulen = GSHORT(p+12);
		pduType2 = p[14];
		ctype = p[15];
		clen = GSHORT(p+16) - SCDSIZE;
		p += 18;
	
		if(ctype&(1<<5)){
			if(p+clen > ep){
				werrstr(Eshort);
				return -1;
			}
			if((p = uncomp(p, clen, ctype, &ulenr)) == nil){
				werrstr("decompression failed: %r");
				return -1;
			}
			if(ulen != ulenr+SCDSIZE){
				werrstr("bad length after decompression");
				return -1;
			}
			ep = p+ulenr;
		}
	
		switch (pduType2){
		case ADsync:
			as->type = ShSync;
			break;
		case ADctl:
			as->type = ShCtl;
			break;
		case ADfontmap:	/* denotes completion of the connection sequence */
			as->type = ShFmap;
			break;
		case ADerrx:
			/* 2.2.5.1.1 Set Error Info PDU Data (TS_SET_ERROR_INFO_PDU) */
			if(p+4 > ep){
				werrstr("ADdraw: %s", Eshort);
				return -1;
			}
			as->type = ShEinfo;
			as->err = GLONG(p);
			break;
		case ADdraw:
			/* 2.2.9.1.1.3.1 Slow-Path Graphics Update (TS_GRAPHICS_UPDATE) */
			/* not when Fast-Path is in use */
			if(p+2 > ep){
				werrstr("ADdraw: %s", Eshort);
				return -1;
			}
			uptype = GSHORT(p);
			switch(uptype){
			case UpdOrders:
				if(p+8 > ep){
					werrstr("ShUorders: %s", Eshort);
					return -1;
				}
				as->type = ShUorders;
				as->nr = GSHORT(p+4);
				as->data = p+8;
				as->ndata = ep-(p+8);
				break;
			case UpdBitmap:
				if(p+4 > ep){
					werrstr("ShUimg: %s", Eshort);
					return -1;
				}
				as->type = ShUimg;
				as->nr = GSHORT(p+2);
				as->data = p+4;
				as->ndata = ep-(p+4);
				break;
			case UpdCmap:
				as->type = ShUcmap;
				as->data = p;
				as->ndata = ep-p;
				break;
			}
			break;
		case ADcursor:
			/* 2.2.9.1.1.4 Server Pointer Update PDU (TS_POINTER_PDU) */
			if(p+2 > ep){
				werrstr(Eshort);
				return -1;
			}
			mtype = GSHORT(p);
			if(mtype == PDUcursorwarp){
				if(p+8 > ep){
					werrstr(Eshort);
					return -1;
				}
				as->type = ShUwarp;
				as->x = GSHORT(p+4);
				as->y = GSHORT(p+6);
				break;
			}
		}
		break;
	}
	return len;
}

/* 2.2.9.1.1.3.1.2.2 Bitmap Data (TS_BITMAP_DATA) */
int
getimgupd(Imgupd* iu, uchar* a, uint nb)
{
	uchar *p, *ep, *s;
	int opt, len;

	p = a;
	ep = a+nb;
	if(nb < 18){
		werrstr(Eshort);
		return -1;
	}
	iu->type = Ubitmap;
	iu->x = GSHORT(p+0);
	iu->y = GSHORT(p+2);
	iu->xm = GSHORT(p+4);
	iu->ym = GSHORT(p+6);
	iu->xsz = GSHORT(p+8);
	iu->ysz = GSHORT(p+10);
	iu->depth = GSHORT(p+12);
	opt = GSHORT(p+14);
	len = GSHORT(p+16);
	p += 18;
	s = p+len;
	if(s > ep){
		werrstr(Eshort);
		return -1;
	}
	iu->iscompr = (opt&Bcompress);
	if(opt&Bcompress && !(opt&NoBitcomphdr))
		p += 8;
	iu->bytes = p;
	iu->nbytes = s-p;
	return s-a;
}

/* T.128 FlowPDU */
int
isflowpdu(uchar* p, uchar* ep)
{
	int marker;

	if(p+2 > ep){
		werrstr(Eshort);
		return -1;
	}
	marker = GSHORT(p);
	return marker == 0x8000;
}

/* 2.2.1.13.2 Client Confirm Active PDU */
int
putconfirmactive(uchar* b, uint nb, Msg* m)
{
	uchar *p, *q, *ep;
	int n, nsrc, capsize, ndata, pdusize;
	int userchan, shareid, originid;
	Caps caps;

	assert(m->type == Mactivated);
	userchan = m->mcsuid;
	shareid = m->shareid;
	originid = m->originid;

	caps.depth = m->depth;
	caps.xsz = m->xsz;
	caps.ysz = m->ysz;
	
	nsrc = sizeof(srcDesc);
	capsize = sizecaps(&caps);
	ndata = 16+nsrc+capsize;

	p = txprep(b, nb, ndata, 0, userchan, 0);
	if(p == nil)
		sysfatal("buffer not prepared: %r");
	ep = p+ndata;
	pdusize = ep-b;
	q = p;

	/* 2.2.8.1.1.1.1 Share Control Header */
	/* totalLength[2] pduType[2] PDUSource[2] */
	PSHORT(p+0, ndata);
	PSHORT(p+2, PDUactivated | (1<<4));
	PSHORT(p+4, userchan);

	/* shareId[4] originatorId[2] sdlen[2] caplen[2] srcdesc[sdlen] ncap[2] pad[2] */
	PLONG(p+6, shareid);
	PSHORT(p+10, originid);
	PSHORT(p+12, nsrc);
	PSHORT(p+14, capsize);
	memcpy(p+16, srcDesc, nsrc);
	p += nsrc+16;
	if((n = putcaps(p, ep-p, &caps)) < 0)
		sysfatal("putcaps: %r");
	p += n;
	assert(p-ndata == q);
	return pdusize;
}

/* 2.2.1.11 Client Info PDU */
int
putclientinfo(uchar* b, uint nb, Msg* m)
{
	uchar *p, *q;
	int ndata, usize;
	int opt, perfopt;
	int ndom, nusr, npw, nsh, nwd;
	uchar *wdom, *wusr, *wpw, *wsh, *wwd;
		char *dom, *usr, *pw, *sh, *wd;
		int dologin;

	assert(m->type == Dclientinfo);
	dom = m->dom;
	usr = m->user;
	pw = m->pass;
	sh = m->rshell;
	wd = m->rwd;
	dologin = m->dologin;

	ndom = strlen(dom)+1;
	nusr = strlen(usr)+1;
	npw = strlen(pw)+1;
	nsh = strlen(sh)+1;
	nwd = strlen(wd)+1;
	wdom = emalloc(4*ndom);
	wusr = emalloc(4*nusr);
	wpw = emalloc(4*npw);
	wsh = emalloc(4*nsh);
	wwd = emalloc(4*nwd);
	ndom = toutf16(wdom, 4*ndom, dom, ndom);
	nusr = toutf16(wusr, 4*nusr, usr, nusr);
	npw = toutf16(wpw, 4*npw, pw, npw);
	nsh = toutf16(wsh, 4*nsh, sh, nsh);
	nwd = toutf16(wwd, 4*nwd, wd, nwd);

	ndata = 18+ndom+nusr+npw+nsh+nwd+188;
	
	opt = 0
		| InfMouse
		| InfUnicode
		| InfNoctlaltdel
		| InfMaxshell
		| InfWinkey
		| InfCompress
		| Compress64k
		;
	if(dologin)
		opt |= InfAutologon;
	perfopt = 0
		| PerfNoDrag
		| PerfNoAnim
		| PerfNoCursorset
		| PerfNoTheming
		;

	p = txprep(b, nb, ndata, 0, m->mcsuid, Sinfopk);
	if(p == nil)
		return -1;
	usize = p+ndata-b;
	q = p;

	PLONG(q+0, 0);	// codePage; langId when opt&InfUnicode
	PLONG(q+4, opt);
	PSHORT(q+8, ndom-2);
	PSHORT(q+10, nusr-2);
	PSHORT(q+12, npw-2);
	PSHORT(q+14, nsh-2);
	PSHORT(q+16, nwd-2);
	q += 18;
	memcpy(q, wdom, ndom);
	q += ndom;
	memcpy(q, wusr, nusr);
	q += nusr;
	memcpy(q, wpw, npw);
	q += npw;
	memcpy(q, wsh, nsh);
	q += nsh;
	memcpy(q, wwd, nwd);
	q += nwd;

	PSHORT(q+0, 2);	// cbClientAddress 
	PSHORT(q+2, 0);	// clientAddress
	PSHORT(q+4, 2);	// cbClientDir
	PSHORT(q+6, 0);	// clientDir
	memset(q+8, 172, 0);	// clientTimeZone
	PLONG(q+180, 0);	// clientSessionId
	PLONG(q+184, perfopt);	// performanceFlags 
	q += 188;

	assert(q == p+ndata);

	free(wdom);
	free(wusr);
	free(wpw);
	free(wsh);
	free(wwd);

	return usize;
}

/* Share-Data Header (2.2.8.1.1.1.2 Share Data Header) */
uchar*
putsdh(uchar* p, uchar* ep, int ndata, int pduType2, int originid, int shareid)
{
	if(p+18>ep)
		sysfatal(Eshort);
	PSHORT(p+0, ndata);
	PSHORT(p+2, (PDUdata | (1<<4)));
	PSHORT(p+4, originid);
	PLONG(p+6, shareid);
	p[10] = 0;	// pad1
	p[11] = 1;	// streamId: 1=Low, 2=Medium, 4=High
	PSHORT(p+12, ndata);
	p[14] = pduType2;
	p[15] = 0; // compressedType 
	PSHORT(p+16, 0); // compressedLength 
	return p+18;
}
