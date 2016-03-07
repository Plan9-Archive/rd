/*
 * Subset of: T.128 Multipoint application sharing
 * 
 * 2.2.8.1.1.1.1 Share Control Header (TS_SHARECONTROLHEADER)
 * http://msdn.microsoft.com/en-us/library/cc240576.aspx
 *
 * totalLen[2] pduType[2] PDUSource[2] 
 *
 * 2.2.9.1.1.3 says there may be many of these.
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"
#define DBG if(0)
//#define DBG


uchar	cmap[256];

static const char	srcDesc[] = "Plan 9";	/* sourceDescriptor (T.128 section 8.4.1) */

static void		scanfastpath(uchar*,uchar*);
static void		scancursorpdu(uchar*, uchar*);
static void		scangraphpdu(uchar*,uchar*);
static void		scanimgupdate(uchar*,uchar*);
static void		scanpalette(uchar*,uchar*);
static void		scansrvcapset(uchar*,uchar*);
static void		sendclientinfo(void);
static void		confirmactive(void);
static uchar*	putsdh(uchar*,uchar*,int,int);
static void		assync(void);
static void		asctl(int);
static void		asfontls(void);

enum
{
	Bits4=		0x0F,
	SECHSIZE=	4,
	SCHSIZE=		6,
	SCDSIZE=		SCHSIZE+4+4+2*2,
};

enum /* 2.2.8.1.1.1.1 Share Control Header (TS_SHARECONTROLHEADER) */
{
	PDUTYPE_DEMANDACTIVEPDU	= 1,	/* Demand Active PDU (section 2.2.1.13.1) */
	PDUTYPE_CONFIRMACTIVEPDU	= 3,	/* Confirm Active PDU (section 2.2.1.13.2) */
	PDUTYPE_DEACTIVATEALLPDU	= 6,	/* Deactivate All PDU (section 2.2.3.1) */
	PDUTYPE_DATAPDU				= 7,	/* Data PDU */
	PDUTYPE_SERVER_REDIR_PKT		= 10,	/* Redirection PDU (section 2.2.13.3.1). */
};

enum /* 2.2.8.1.1.1.2 Share Data Header (TS_SHAREDATAHEADER) */
{
	PDUTYPE2_UPDATE=	2,
	PDUTYPE2_CONTROL=	20,
	PDUTYPE2_POINTER=	27,
	PDUTYPE2_INPUT=		28,
	PDUTYPE2_SYNCHRONIZE=	31,
	PDUTYPE2_REFRESH_RECT=	33,
	PDUTYPE2_SUPPRESS_OUTPUT=	35,
	PDUTYPE2_FONTLIST=	39,
	PDUTYPE2_FONTMAP=	40,
	PDUTYPE2_SET_ERROR_INFO_PDU=	47,
};

enum /* 2.2.9.1.1.4 Server Pointer Update PDU (TS_POINTER_PDU) */
{
	TS_PTRMSGTYPE_SYSTEM=	1,
	TS_PTRMSGTYPE_POSITION=	3,
	TS_PTRMSGTYPE_COLOR=	6,
	TS_PTRMSGTYPE_CACHED=	7,
	TS_PTRMSGTYPE_POINTER=	8,
};

enum /* 2.2.9.1.1.3.1.2.2 Bitmap Data */
{
	Bcompress=	1,
	Pcompress=	0x20,
};

enum /* 2.2.1.15.1 Control PDU Data */
{
	CTRLACTION_REQUEST_CONTROL= 	1,
	CTRLACTION_GRANTED_CONTROL=	2,
	CTRLACTION_DETACH=				3,
	CTRLACTION_COOPERATE=			4,
};

enum /* 2.2.1.11.1.1 Info Packet (TS_INFO_PACKET) */
{
	INFO_MOUSE=			0x1,
	INFO_DISABLECTRLALTDEL=	0x2,
	INFO_AUTOLOGON=	0x8,
	INFO_UNICODE=		0x10,
	INFO_MAXIMIZESHELL=	0x20,
	INFO_COMPRESSION=	0x80,
	CompressionTypeMask=	0x1E00,
		PACKET_COMPR_TYPE_8K=	0<<9,	// RDP 4.0 bulk compression ≡ MPPC
		PACKET_COMPR_TYPE_64K=	1<<9,	// RDP 5.0 bulk compression (3.1.8.4.2)
		PACKET_COMPR_TYPE_RDP6=	2<<9,	// RDP 6.0 bulk compression
		PACKET_COMPR_TYPE_RDP61= 3<<9,	// RDP 6.1 bulk compression
	INFO_ENABLEWINDOWSKEY=		0x100,
	INFO_REMOTECONSOLEAUDIO=	0x2000,
	INFO_FORCE_ENCRYPTED_CS_PDU=	0x4000,
	INFO_RAIL=				0x8000,
	INFO_LOGONERRORS=		0x10000,
	INFO_MOUSE_HAS_WHEEL=	0x20000,
	INFO_NOAUDIOPLAYBACK=	0x80000,
	INFO_VIDEO_DISABLE=	0x400000,

	PERF_DISABLE_WALLPAPER=	1<<0,
	PERF_DISABLE_FULLWINDOWDRAG=	1<<1,
	PERF_DISABLE_MENUANIMATIONS=	1<<2,
	PERF_DISABLE_THEMING=	1<<3,
	PERF_DISABLE_CURSOR_SHADOW=		1<<5,
	PERF_DISABLE_CURSORSETTINGS=		1<<6,
	PERF_ENABLE_FONT_SMOOTHING=	1<<7,
};

enum
{
	UPDATETYPE_ORDERS		= 0, 		/* [MS-RDPEGDI] section 2.2.2.2 */
 	UPDATETYPE_BITMAP		= 1,		/* section 2.2.9.1.1.3.1.2 */
	UPDATETYPE_PALETTE		= 2,		/* section 2.2.9.1.1.3.1.1 */
	UPDATETYPE_SYNCHRONIZE	= 3,		/* section 2.2.9.1.1.3.1.3 */
};

enum /* 2.2.9.1.2.1 Fast-Path Update (TS_FP_UPDATE) */
{
	FASTPATH_UPDATETYPE_ORDERS		= 0,	/* [MS-RDPEGDI] section 2.2.2.2 */
	FASTPATH_UPDATETYPE_BITMAP		= 1,
	FASTPATH_UPDATETYPE_PALETTE		= 2,
	FASTPATH_UPDATETYPE_SYNCHRONIZE	= 3,
	FASTPATH_UPDATETYPE_SURFCMDS	= 4,
	FASTPATH_UPDATETYPE_PTR_NULL	= 5,
	FASTPATH_UPDATETYPE_PTR_DEFAULT	= 6,
	FASTPATH_UPDATETYPE_PTR_POSITION	= 8,
	FASTPATH_UPDATETYPE_COLOR		= 9,
	FASTPATH_UPDATETYPE_CACHED		= 10,
	FASTPATH_UPDATETYPE_POINTER		= 11,

};

int
rdphandshake(int)
{
	int i;

	if(mcsconnect(rd.fd) < 0)
		return -1;
	erectdom(rd.fd);
	if(attachuser(rd.fd) < 0)
		return -1;
	if(joinchannel(rd.fd, rd.userchan) < 0)
		return -1;
	if(joinchannel(rd.fd, GLOBALCHAN) < 0)
		return -1;
	for(i = 0; i < nvc; i++)
		if(joinchannel(rd.fd, vctab[i].mcsid) < 0)
			return -1;

	sendclientinfo();
	return rd.fd;
}

void
readnet(int fd)
{
	int chanid, len, flags;
	uchar *p, *ep, buf[MAXTPDU];
	
	for(;;){
		len = readpdu(fd, buf, sizeof(buf));
		if(len <= 0){
			if(rd.active && !rd.hupreason)
				fprint(2, "readpdu: %r\n");
			return;
		}
		p = buf;
		ep = buf+len;
	
		if(istpkt(p,ep) == 0){
			scanfastpath(p, ep);
			continue;
		}
		if(ismcshangup(p,ep)){
			werrstr("Disconnect Provider Ultimatum");
			return;
		}

		chanid = mcschanid(p,ep);
		if(chanid < 0)
			sysfatal("mcschanid: %r");

		p = mcspayload(p, ep);

		flags = GSHORT(p);
		if(!rd.licensed && flags&Slicensepk){
			/*
			 * 2.2.8.1.1.2.1 Basic (TS_SECURITY_HEADER)
			 * http://msdn.microsoft.com/en-us/library/cc240579.aspx
			 */
			p += SECHSIZE;
			if(flags&Slicensepk){
				scanlicensepdu(p, ep);
				continue;
			}
			if(flags&Scrypt)
				sysfatal("legacy encryption of a Slow-Path PDU");
		}

		if(chanid != GLOBALCHAN){
			scanvcpdu(p, ep, chanid);
			continue;
		}

		if(isflowpdu(p,ep))
			continue;

		scanaspdu(p,ep);
	}
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

/* 2.2.9.1.2 Server Fast-Path Update PDU
 * enabled with CanFastpath in General Capability Set
 */
static void
scanfastpath(uchar *p, uchar* ep)
{
	int hd, nb, nord, cflags, ulen, x, y, enc;
	uchar *q, *eq;

	enc = p[0]&(1<<7);
	if(enc)
		sysfatal("legacy encryption in a Fast-Path PDU");
	if(p[1]&(1<<7))
		p += 3;
	else
		p += 2;

	eq = ep;
	while(p+3 < ep){
		/* updateHeader[1] compressionFlags[1]? size[2] updateData[*] */
		hd = *p++;
		if(hd&(1<<7))
			cflags = *p++;
		else
			cflags = 0;
		if(p+2 > ep)
			sysfatal(Eshort);
		nb = GSHORT(p);
		p += 2;
		q = p+nb;

		if(cflags&Pcompress){
			if(p+nb > ep)
				sysfatal(Eshort);
			if((p = uncomp(p, nb, cflags, &ulen)) == nil)
				sysfatal("fast-path packet de-compression failed: %r cflags=%#x", cflags);
			ep = p+ulen;
		}

		switch(hd&Bits4){
		case FASTPATH_UPDATETYPE_ORDERS:
			nord = GSHORT(p);
			scanorders(p+2, ep, nord);
			break;
		case FASTPATH_UPDATETYPE_BITMAP:
			scanimgupdate(p, ep);
			break;
		case FASTPATH_UPDATETYPE_PALETTE:
			scanpalette(p, ep);
			break;
		case FASTPATH_UPDATETYPE_PTR_POSITION:
			x = GSHORT(p+0);
			y = GSHORT(p+2);
			warpmouse(x, y);
			break;
		}

		p = q;
		ep = eq;
	}

	lockdisplay(display);
	flushimage(display, 1);
	unlockdisplay(display);
}

/* T.128 ASPDU */
void
scanaspdu(uchar* p, uchar* ep)
{
	int pdutype, len;

	while(p+SCHSIZE <= ep){
		len = GSHORT(p);
		if(len < SCHSIZE || p+len > ep)
			sysfatal("bad length in Share Control PDU header");
	
		pdutype = GSHORT(p+2)&Bits4;

		switch(pdutype){
		case PDUTYPE_DEMANDACTIVEPDU:
			activating(p+SCHSIZE, p+len);
			rd.active = 1;
			break;
		case PDUTYPE_DATAPDU:
			scandatapdu(p+SCHSIZE, p+len);
			break;
		case PDUTYPE_DEACTIVATEALLPDU:
			rd.active = 0;
			break;
		}
		p += len;
	}
}

/*
 * 2.2.8.1.1.1.2 Share Data Header (TS_SHAREDATAHEADER)
 * http://msdn.microsoft.com/en-us/library/cc240577.aspx
 *
 * shareId[4] pad1[1] streamId[1] uncomprLen[2]
 * pduType2[1] comprType[1] comprLen[2]
 */
void
scandatapdu(uchar *p, uchar* ep)
{
	int pduType2, ctype, clen, ulen, ulenr;

	ulen = GSHORT(p+6);
	pduType2 = p[8];
	ctype = p[9];
	clen = GSHORT(p+10);
	p += 12;

	if(ctype&(1<<5)){
		clen -= SCDSIZE;
		if(p+clen > ep)
			sysfatal(Eshort);
		if((p = uncomp(p, clen, ctype, &ulenr)) == nil)
			sysfatal("decompression failed: %r");
		if(ulen != ulenr+SCDSIZE)
			sysfatal("bad length after decompression");
		ep = p+ulenr;
	}

	switch (pduType2){
	case PDUTYPE2_SYNCHRONIZE:
	case PDUTYPE2_CONTROL:
	case PDUTYPE2_FONTMAP:	/* denotes completion of the connection sequence */
		break;
	case PDUTYPE2_SET_ERROR_INFO_PDU:
		/* 2.2.5.1.1 Set Error Info PDU Data (TS_SET_ERROR_INFO_PDU) */
		rd.hupreason = GLONG(p);
		break;
	case PDUTYPE2_UPDATE:
		scangraphpdu(p, ep);
		break;
	case PDUTYPE2_POINTER:
		scancursorpdu(p, ep);
		break;
	}
}

/* 2.2.9.1.1.3.1 Slow-Path Graphics Update (TS_GRAPHICS_UPDATE) */
static void
scangraphpdu(uchar *p, uchar *ep)
{
	int uptype, nord;

	if(p+2 > ep)
		sysfatal(Eshort);

	uptype = GSHORT(p);
	switch(uptype){
	case UPDATETYPE_ORDERS:
		if(p+8 > ep)
			sysfatal(Eshort);
		nord = GSHORT(p+4);
		scanorders(p+8, ep, nord);
		break;
	case UPDATETYPE_BITMAP:
		scanimgupdate(p, ep);
		break;
	case UPDATETYPE_PALETTE:
		scanpalette(p, ep);
		break;
	}
	lockdisplay(display);
	flushimage(display, 1);
	unlockdisplay(display);
}

/* 2.2.9.1.1.4 Server Pointer Update PDU (TS_POINTER_PDU) */
static void
scancursorpdu(uchar* p, uchar* ep)
{
	int type, x, y;

	if(p+2 > ep)
		sysfatal(Eshort);
	type = GSHORT(p);
	switch(type){
	case TS_PTRMSGTYPE_POSITION:
		if(p+8 > ep)
			sysfatal(Eshort);
		x = GSHORT(p+4);
		y = GSHORT(p+6);
		warpmouse(x, y);
		break;
	}
}

/* 2.2.9.1.1.3.1.2.1 Bitmap Update Data (TS_UPDATE_BITMAP_DATA) */
static void
scanimgupdate(uchar* p, uchar* ep)
{
	uchar *s;
	int err, nr, len, depth, chan, opt;
	static Image* img;
	Rectangle r, rs, d;

	if(p+4 > ep)
		sysfatal(Eshort);
	chan = rd.chan;
	rs = rectaddpt(Rpt(ZP, rd.dim), screen->r.min);
	nr = GSHORT(p+2);
	p += 4;

	lockdisplay(display);

	if(img==nil || !eqrect(img->r, rs)){
		if(img != nil)
			freeimage(img);
		img = allocimage(display, rs, chan, 0, DNofill);
		if(img == nil)
			sysfatal("%r");
	}

	for(; nr>0 && p+18<ep; nr--){
		/* 2.2.9.1.1.3.1.2.2 Bitmap Data (TS_BITMAP_DATA) */
		d.min.x =	GSHORT(p+0);
		d.min.y =	GSHORT(p+2);
		d.max.x =	GSHORT(p+4) + 1;
		d.max.y =	GSHORT(p+6) + 1;
		r.min = ZP;
		r.max.x =	GSHORT(p+8);
		r.max.y =	GSHORT(p+10);
		depth = 	GSHORT(p+12);
		opt = 	GSHORT(p+14);
		len =   	GSHORT(p+16);
		p +=    	18;
		s = p+len;
		if(s > ep)
			sysfatal(Eshort);
		if(depth != img->depth)
			sysfatal("bad image depth");
		r = rectaddpt(r, img->r.min);

		if(opt&Bcompress)
		if(!(opt&NoBitcomphdr))
			p += 8;

		err = (opt&Bcompress? loadrle : loadbmp)(img, r, p, s-p);
		if(err < 0)
			sysfatal("%r");
		draw(screen, rectaddpt(d, screen->r.min), img, nil, img->r.min);
		p = s;
	}
	unlockdisplay(display);
}

static void
scanpalette(uchar* p, uchar* ep)
{
	int i, n;

	n = GSHORT(p+4);
	p += 8;
	if(n > sizeof(cmap)){
		fprint(2, "scanpalette: palette too big");
		return;
	}
	if(p+3*n > ep)
		sysfatal(Eshort);
	for(i = 0; i<n; i++, p+=3)
		cmap[i] = rgb2cmap(p[0], p[1], p[2]);
}

static void
scansrvcapset(uchar *p, uchar *ep)
{
	int ncap, type, len;

	ncap = GSHORT(p);
	p += 4;
	for(; ncap>0 && p+4<ep; ncap--){
		type = GSHORT(p+0);
		len = GSHORT(p+2);
		if(p+len > ep)
			sysfatal("bad length in server's capability set");
		switch (type){
		case CapGeneral:
			scangencaps(p, p+len);
			break;
		case CapBitmap:
			scanbitcaps(p, p+len);
			break;
		}
		p += len;
	}
}

/*
 * 2.2.1.13.1 Server Demand Active PDU
 * http://msdn.microsoft.com/en-us/library/cc240484.aspx
 */
void
activating(uchar* p, uchar* ep)
{
	int nsrc, ncaps;

	rd.shareid = GLONG(p);
	nsrc = GSHORT(p+4);
	ncaps = GSHORT(p+6);
	p += 8+nsrc;
	if(p+ncaps >= ep){
		werrstr(Eshort);
		return;
	}
	scansrvcapset(p, p+ncaps);
	confirmactive();
	// server accepts input since this point
	passinput(0, InputSync, 0, 0, 0);

	assync();
	asctl(CTRLACTION_COOPERATE);
	asctl(CTRLACTION_REQUEST_CONTROL);
	asfontls();	// unleashes the artist
}

/* 2.2.1.13.2 Client Confirm Active PDU */
static void
confirmactive(void)
{
	int ncap, nsrc, capsize, calen, pdusize;
	uchar buf[512], *p, *q, *ep;

	ncap = 8;
	nsrc = sizeof(srcDesc);
	capsize = 0
		+ GENCAPSIZE
		+ BITCAPSIZE
		+ ORDCAPSIZE
		+ BCACAPSIZE
		+ PTRCAPSIZE
		+ INPCAPSIZE
		+ SNDCAPSIZE
		+ GLYCAPSIZE
		;
	calen = 20+nsrc+capsize;

	p = prebuf(buf, sizeof(buf), calen, 0, 0);
	if(p == nil)
		sysfatal("buffer not prepared: %r");
	ep = p+calen;
	pdusize = ep-buf;
	q = p;

	/* 2.2.8.1.1.1.1 Share Control Header */
	/* totalLength[2] pduType[2] PDUSource[2] */
	PSHORT(p+0, calen);
	PSHORT(p+2, PDUTYPE_CONFIRMACTIVEPDU | (1<<4));
	PSHORT(p+4, rd.userchan);

	/* shareId[4] originatorId[2] sdlen[2] caplen[2] srcdesc[sdlen] ncap[2] pad[2] */
	PLONG(p+6, rd.shareid);
	PSHORT(p+10, SRVCHAN);
	PSHORT(p+12, nsrc);
	PSHORT(p+14, capsize+4);
	memcpy(p+16, srcDesc, nsrc);
	PSHORT(p+16+nsrc, ncap);
	PSHORT(p+18+nsrc, 0);
	p += nsrc+20;
	p = putgencaps(p, ep);
	p = putbitcaps(p, ep);
	p = putordcaps(p, ep);
	p = putbc2caps(p, ep);
	p = putptrcaps(p, ep);
	p = putinpcaps(p, ep);
	p = putsndcaps(p, ep);
	p = putglycaps(p, ep);
	assert(p-calen == q);

	writen(rd.fd, buf, pdusize);
}

/* 2.2.1.11 Client Info PDU */
static void
sendclientinfo(void)
{
	uchar a[1024], *p, *q;
	int ndata, secflags, usize;
	int opt, perfopt;
	int ndom, nusr, npw, nsh, nwd;
	uchar *wdom, *wusr, *wpw, *wsh, *wwd;

	ndom = strlen(rd.windom)+1;
	nusr = strlen(rd.user)+1;
	npw = strlen(rd.passwd)+1;
	nsh = strlen(rd.shell)+1;
	nwd = strlen(rd.rwd)+1;
	wdom = emalloc(4*ndom);
	wusr = emalloc(4*nusr);
	wpw = emalloc(4*npw);
	wsh = emalloc(4*nsh);
	wwd = emalloc(4*nwd);
	ndom = toutf16(wdom, 4*ndom, rd.windom, ndom);
	nusr = toutf16(wusr, 4*nusr, rd.user, nusr);
	npw = toutf16(wpw, 4*npw, rd.passwd, npw);
	nsh = toutf16(wsh, 4*nsh, rd.shell, nsh);
	nwd = toutf16(wwd, 4*nwd, rd.rwd, nwd);

	ndata = 18+ndom+nusr+npw+nsh+nwd+188;
	
	opt = 0
	| INFO_MOUSE
	| INFO_UNICODE
	| INFO_DISABLECTRLALTDEL
	| INFO_MAXIMIZESHELL
	| INFO_ENABLEWINDOWSKEY
	| INFO_FORCE_ENCRYPTED_CS_PDU
	| INFO_COMPRESSION
	| PACKET_COMPR_TYPE_8K
	| PACKET_COMPR_TYPE_64K
	;
	perfopt = 0
	| PERF_DISABLE_FULLWINDOWDRAG
	| PERF_DISABLE_MENUANIMATIONS
	| PERF_DISABLE_CURSORSETTINGS
	| PERF_DISABLE_THEMING
	;
	if(rd.autologon)
		opt |= INFO_AUTOLOGON;

	secflags = Sinfopk;
	p = prebuf(a, sizeof(a), ndata, 0, secflags);
	if(p == nil)
		sysfatal("sendclientinfo: %r");
	usize = p+ndata-a;
	q = p;

	PLONG(q+0, 0);	// codePage; langId when opt&INFO_UNICODE
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

	writen(rd.fd, a, usize);
	
}

/* Share-Data Header (2.2.8.1.1.1.2 Share Data Header) */
static uchar*
putsdh(uchar* p, uchar* ep, int ndata, int pduType2)
{
	if(p+18>ep)
		sysfatal(Eshort);
	PSHORT(p+0, ndata);
	PSHORT(p+2, (PDUTYPE_DATAPDU | 0x10));
	PSHORT(p+4, rd.userchan);
	PLONG(p+6, rd.shareid);
	p[10] = 0;
	p[11] = 1;
	PSHORT(p+12, ndata);	// rdesktop used to put ndata-14...
	p[14] = pduType2;
	p[15] = 0; // ctype
	PSHORT(p+16, 0); // clen
	return p+18;
}

/* 2.2.1.14 Client Synchronize PDU */
static void
assync(void)
{
	uchar a[64], *p, *q;
	int ndata, usize; 

	ndata = 4+SCDSIZE;
	p = prebuf(a, sizeof(a), ndata, 0, 0);
	if(p == nil)
		sysfatal("buffer not prepared: %r");
	usize = p+ndata-a;

	q = putsdh(p, p+ndata, ndata, PDUTYPE2_SYNCHRONIZE);
	PSHORT(q+0, 1);
	PSHORT(q+2, 1002);	// target MCS userId
	writen(rd.fd, a, usize);
}

/* 2.2.1.15.1 Control PDU Data (TS_CONTROL_PDU) */
static void
asctl(int action)
{
	uchar a[64], *p, *q;
	int ndata, usize; 

	ndata = 8+SCDSIZE;
	p = prebuf(a, sizeof(a), ndata, 0, 0);
	if(p == nil)
		sysfatal("buffer not prepared: %r");
	usize = p+ndata-a;

	q = putsdh(p, p+ndata, ndata, PDUTYPE2_CONTROL);
	PSHORT(q+0, action);
	PSHORT(q+2, 0);	// grantId[2]
	PLONG(q+4, 0); 	// controlId[2]
	writen(rd.fd, a, usize);
}

/* 2.2.1.18 Client Font List PDU */
static void
asfontls(void)
{
	uchar a[64], *p, *q;
	int ndata, usize; 

	ndata = 8+SCDSIZE;
	p = prebuf(a, sizeof(a), ndata, 0, 0);
	if(p == nil)
		sysfatal("buffer not prepared: %r");
	usize = p+ndata-a;

	q = putsdh(p, p+ndata, ndata, PDUTYPE2_FONTLIST);
	PSHORT(q+0, 0);	// numberFonts
	PSHORT(q+2, 0);	// totalNumFonts
	PSHORT(q+4, 2+1);	// listFlags: 1=first, 2=last
	PSHORT(q+6, 50);	// entrySize

	writen(rd.fd, a, usize);
}

/* 2.2.8.1.1.3.1.1 Slow-Path Input Event (TS_INPUT_EVENT) */
void
passinput(ulong msec, int mtype, int iflags, int iarg1, int iarg2)
{
	uchar a[64], *p, *q;
	int ndata, usize; 

	ndata = 16+SCDSIZE;
	p = prebuf(a, sizeof(a), ndata, 0, 0);
	if(p == nil)
		sysfatal("buffer not prepared: %r");
	usize = p+ndata-a;

	q = putsdh(p, p+ndata, ndata, PDUTYPE2_INPUT);
	PSHORT(q+0, 1); // numEvents
	PSHORT(q+2, 0);
	// 2.2.8.1.1.3.1.1 Slow-Path Input Event
	PLONG(q+4, msec);
	PSHORT(q+8, mtype);
	// slowPathInputData[*]
	PSHORT(q+10, iflags);
	PSHORT(q+12, iarg1);
	PSHORT(q+14, iarg2);

	writen(rd.fd, a, usize);
}

/* 2.2.11.3.1 Suppress Output PDU Data (TS_SUPPRESS_OUTPUT_PDU) */
void
turnupdates(int allow)
{
	uchar a[64], *p, *q;
	int ndata, usize; 

	ndata = (allow?12:4)+SCDSIZE;
	p = prebuf(a, sizeof(a), ndata, 0, 0);
	if(p == nil)
		sysfatal("buffer not prepared: %r");
	usize = p+ndata-a;

	q = putsdh(p, p+ndata, ndata, PDUTYPE2_SUPPRESS_OUTPUT);
	q[0] = (allow?1:0); 
	memset(q+1, 3, 0);
	if(allow){
		PSHORT(q+4, 0);	// left
		PSHORT(q+6, 0);	// top
		PSHORT(q+8, rd.dim.x-1);	// right
		PSHORT(q+10, rd.dim.y-1);	// bottom
	}
	writen(rd.fd, a, usize);
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
prebuf(uchar* buf, int nb, int ndata, int chanid, int secflags)
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
	if(mkmcssdr(p, ep-p, ndata, chanid) < 0)
		sysfatal("mkmcssdr: %r");
	p += 8;

	if(shdsize > 0)
		PLONG(p, secflags);
	return p + shdsize;
}

