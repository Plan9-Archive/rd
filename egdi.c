/*
 * [MS-RDPEGDI]: Graphics Device Interface (GDI) Acceleration Extensions
 * http://msdn.microsoft.com/en-us/library/cc241537.aspx
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum
{
	Bits2=	3,
	Bits3=	7,
	Bits6=	63,
	Bits7=	127,
	Bits8=	255,
};
enum /* 2.2.2.2.1 Drawing Order (DRAWING_ORDER) */
{
	Standard=		1<<0,
	Secondary=	1<<1,
};
enum /* 2.2.2.2.1.1.2 Primary Drawing Order (PRIMARY_DRAWING_ORDER) */
{
	Clipped=		1<<2,
	NewOrder= 	1<<3,
	Diff=			1<<4,
	SameClipping=	1<<5,
	ZeroFieldBit0=	1<<6,
	ZeroFieldBit1=	1<<7,
};
enum
{
	ROP2_COPY	= 0xcc,
};
enum
{
	/* orderSupport indices for capability negotiation */
	CanDstBlt = 0,
	CanPatBlt,				/* also OpaqueRect */
	CanScrBlt,
	CanMemBlt = 3,
	CanMem3Blt,
	CanDrawNineGrid = 7,
	CanLineTo,
	CanMultiDrawNineGrid,
	CanSaveBitmap = 0x0B,
	CanMultiDstBlt = 0x0F,
	CanMultiPatBlt = 0x10,
	CanMultiScrBlt,
	CanMultiOpaqueRect,
	CanFastIndex,
	CanPolygonSC,		/* also PolygonCB */
	CanPolygonCB,		/* also PolygonCB */
	CanPolyline,
	CanFastGlyph = 0x18,
	CanEllipseSC,			/* also EllipseCB */
	CanEllipseCB,			/* also EllipseSC */
	CanGlyphIndex,
};
enum
{
	/* 2.2.2.2.1.1.2 Primary Drawing Order (PRIMARY_DRAWING_ORDER) */
	PatBlt=1,
	ScrBlt=2,
	OpaqueRect=10,
	MemBlt=13,
	MultiOpaqueRect=18,
};
enum
{
	/* 2.2.2.2.1.2.1.1 Secondary Drawing Order Header */
	CacheImage = 0,
	CacheCmap,
	CacheCompressed,
	CacheGlyph,
	CacheImage2,
	CacheCompressed2,
	CacheBrush = 7,
	CacheCompressed3,
};

typedef	struct	Order Order;
struct Order
{
	int	fsize;
	uchar* (*fn)(Rdp*, uchar*,uchar*,int,int);
};

static	uchar*	scrblt(Rdp*, uchar*,uchar*,int,int);
static	uchar*	memblt(Rdp*, uchar*,uchar*,int,int);
static	uchar*	cacheimage2(Rdp*, uchar*,uchar*,int,int);
static	uchar*	cacheimage(Rdp*, uchar*,uchar*,int,int);
static	uchar*	cachecmap(Rdp*, uchar*,uchar*,int,int);

Order ordtab[NumOrders] = {
	[ScrBlt]= 		{ 1, scrblt },
	[MemBlt]=	{ 2, memblt },
	[PatBlt]= 		{ 2, nil },
	[OpaqueRect]=	{ 1, nil },
	[MultiOpaqueRect]=	{ 2, nil },
};

Order auxtab[8] = {
	[CacheImage2]=		{ 0, cacheimage2 },
	[CacheCompressed2]= 	{ 0, cacheimage2 },
	[CacheImage]=			{ 0, cacheimage },
	[CacheCompressed]= 	{ 0, cacheimage },
	[CacheCmap]=			{ 0, cachecmap },
};

uchar
orderSupport[NumOrders] = 
{
	[CanScrBlt]   	1,
	[CanMemBlt]	1,
//	[CanMultiOpaqueRect] 1,
//	[CanPatBlt]   	1,	/* and OpaqueRect */
};

static struct GdiContext
{
	int order;
	Rectangle clipr;
} gc	= {PatBlt};

static Image*	imgcache[3][600];

static	uchar*	getclipr(Rectangle*,uchar*,uchar*);
static	uchar*	getpt(Point*,uchar*,uchar*,int,int);

/* 2.2.2.2 Fast-Path Orders Update (TS_FP_UPDATE_ORDERS) */
void
scanorders(Rdp* c, Share* as)
{
	int count;
	uchar *p, *ep;
	int ctl, fmask, fsize;
	int size, opt, xorder;

	count = as->nord;
	p = as->data;
	ep = as->data + as->ndata;

	while(count-- > 0 && p<ep){	
		fmask = 0;
		ctl = *p;
		if(!(ctl&Standard))
			goto ErrNstd;	// GDI+ or out of sync
		if(ctl&Secondary){
			if(p+6>ep)
				sysfatal("scanorders: %s", Eshort);
			size = ((short)GSHORT(p+1))+13;
			if(size < 0 || p+size > ep)
				sysfatal("scanorders: size: %s", Eshort);
			opt = GSHORT(p+3);
			xorder = p[5];
			if(xorder >= nelem(auxtab) || auxtab[xorder].fn == nil){
				fprint(2, "egdi: unsupported secondary order %d\n", xorder);
				p += size;
				continue;
			}

			auxtab[xorder].fn(c, p, p+size, xorder, opt);
			p += size;
			continue;
		}
		p++;
		if(ctl&NewOrder){
			gc.order = *p++;
			if(gc.order >= NumOrders)		// paranoia
				gc.order = PatBlt;
		}
		fsize = ordtab[gc.order].fsize - ((ctl>>6)&Bits2);
		switch(fsize){
		default:
			goto ErrFsize;
		case 3:
			fmask = p[0]|(p[1]<<8)|(p[2]<<16);
			break;
		case 2:
			fmask = GSHORT(p);
			break;
		case 1:
			fmask = p[0];
		case 0:
			break;
		}
		p += fsize;

		if(ctl&Clipped && !(ctl&SameClipping))
			p = getclipr(&gc.clipr, p, ep);

		if(ordtab[gc.order].fn == nil)
			goto ErrNotsup;
		p = ordtab[gc.order].fn(c, p, ep, ctl, fmask);
		if(p == nil)
			break;
	}
	if(display->locking)
		lockdisplay(display);
	flushimage(display, 1);
	if(display->locking)
		unlockdisplay(display);
	return;

ErrNstd:
	fprint(2, "egdi: non-standard order\n");
	return;
ErrFsize:
	fprint(2, "egdi: bad field encoding bytes count for order %d\n", gc.order);
	return;
ErrNotsup:
	fprint(2, "egdi: unsupported order %d\n", gc.order);
	return;
}

static uchar*
getclipr(Rectangle* pr, uchar* p, uchar* ep)
{
	int bctl;

	bctl = *p++;
	if(bctl&1<<4)
		pr->min.x += (char)*p++;
	else if(bctl&1<<0){
		pr->min.x = GSHORT(p);
		p += 2;
	}
	if(bctl&1<<5)
		pr->min.y += (char)*p++;
	else if(bctl&1<<1){
		pr->min.y = GSHORT(p);
		p += 2;
	}
	if(bctl&1<<6)
		pr->max.x += (char)*p++;
	else if(bctl&1<<2){
		pr->max.x = GSHORT(p)+1;
		p += 2;
	}
	if(bctl&1<<7)
		pr->max.y += (char)*p++;
	else if(bctl&1<<3){
		pr->max.y = GSHORT(p)+1;
		p += 2;
	}
	if(p>ep)
		sysfatal("getclipr: %s", Eshort);
	return p;
}

static uchar*
getpt(Point* pp, uchar* s, uchar *es,  int ctl, int fmask)
{
	Point p;

	p = *pp;
	if(ctl&Diff){
		if(fmask&1<<0)
			p.x += (char)*s++;
		if(fmask&1<<1)
			p.y += (char)*s++;
	}else{
		if(fmask&1<<0){
			p.x = GSHORT(s);
			s += 2;
		};
		if(fmask&1<<1){
			p.y = GSHORT(s);
			s += 2;
		};
	}
	if(s > es)
		sysfatal("getpt: %s", Eshort);
	*pp = p;
	return s;
}

static uchar*
getoffrect(Rectangle* pr, uchar* p, uchar* ep, int ctl, int fmask){
	Rectangle r;

	r = *pr;
	r.max = subpt(r.max, r.min);
	p = getpt(&r.min, p, ep, ctl, fmask);
	p = getpt(&r.max, p, ep, ctl, fmask>>2);
	r.max = addpt(r.max, r.min);
	*pr = r;
	return p;
}

/* 2.2.2.2.1.1.2.7 ScrBlt (SCRBLT_ORDER) */
static uchar*
scrblt(Rdp*, uchar* p, uchar* ep, int ctl, int fmask)
{
	static Rectangle r;
	static Point pt;
	static int rop3;
	Rectangle rs;
	Point ps;

	p = getoffrect(&r, p, ep, ctl, fmask);
	if(fmask&1<<4)
		rop3 = *p++;
	p = getpt(&pt, p, ep, ctl, fmask>>5);

	if(rop3 != ROP2_COPY){
		fprint(2, "scrblt: rop3 %#hhux is not supported\n", rop3);
		return p;
	}
	rs = r;
	if(ctl&Clipped)
		rectclip(&rs, gc.clipr);	// not replclipr: need to clip dst only
	rs = rectaddpt(rs, screen->r.min);
	ps = addpt(pt, screen->r.min);

	if(display->locking)
		lockdisplay(display);
	draw(screen, rs, screen, nil, ps);
	if(display->locking)
		unlockdisplay(display);

	return p;
}

/* 2.2.2.2.1.1.2.9 MemBlt (MEMBLT_ORDER) */
static uchar*
memblt(Rdp*, uchar* p, uchar* ep, int ctl, int fmask)
{
	static int cid;	/* cacheId */
	static int coff;	/* cacheIndex */
	static int rop3;
	static Rectangle r;
	static Point pt;
	Image* img;

	if(fmask&1<<0){
		cid = GSHORT(p);
		p += 2;
	}
	p = getoffrect(&r, p, ep, ctl, fmask>>1);
	if(fmask&1<<5)
		rop3 = *p++;
	p = getpt(&pt, p, ep, ctl, fmask>>6);
	if(fmask&1<<8){
		coff = GSHORT(p);
		p += 2;
	}
	if(p>ep)
		sysfatal("memblt: %s", Eshort);

	cid &= Bits8;
	if(cid >= nelem(imgcache) || coff >= nelem(*imgcache)){
		fprint(2, "memblt: bad image cache spec [%d %d]\n", cid, coff);
		return p;
	}
	img = imgcache[cid][coff];
	if(img == nil){
		fprint(2, "memblt: empty cache entry cid %d coff %d\n", cid, coff);
		return p;
	}

	if(display->locking)
		lockdisplay(display);
	if(ctl&Clipped)
		replclipr(screen, screen->repl, rectaddpt(gc.clipr, screen->r.min));
	draw(screen, rectaddpt(r, screen->r.min), img, nil, pt);
	if(ctl&Clipped)
		replclipr(screen, screen->repl, screen->r);
	if(display->locking)
		unlockdisplay(display);

	return p;
}

static Image*
pickimage(int cid, int coff, Rectangle r, ulong chan)
{
	Image* img;

	if(cid >= nelem(imgcache) || coff >= nelem(*imgcache)){
		werrstr("bad image cache spec [%d %d]", cid, coff);
		return nil;
	}

	img = imgcache[cid][coff];
	if(img==nil || !eqrect(img->r, r)){
		if(img != nil)
			freeimage(img);
		img = allocimage(display, r, chan, 0, DNofill);
		if(img == nil)
			return nil;
		imgcache[cid][coff] = img;
	}
	return img;
}

/* 2.2.2.2.1.2.2 Cache Bitmap - Revision 1 (CACHE_BITMAP_ORDER) */
static uchar*
cacheimage(Rdp* c, uchar* p, uchar* ep, int xorder, int opt)
{
	int cid;	/* cacheId */
	int coff;	/* cacheIndex */
	int chan;
	int zip;
	int err;
	int size;
	Image* img;
	Point d;	/* width, height */
	Rectangle r;

	if(p+15 >= ep)
		sysfatal("cacheimage: %s", Eshort);
	cid = p[6];
	d.x = p[8];
	d.y = p[9];
	size = GSHORT(p+11);
	coff = GSHORT(p+13);
	r.min = ZP;
	r.max = d;
	chan = c->chan;
	zip = (xorder==CacheCompressed);

	if(zip)
	if(opt&1<<10){
		p += 8;	// bitmapComprHdr
		size -= 8;
	}
	if(p+size > ep)
		sysfatal("cacheimage: size: %s", Eshort);
	if((img = pickimage(cid, coff, r, chan)) == nil)
		sysfatal("cacheimage: pickimage: %r");
	err = (zip? loadrle : loadbmp)(img, r, p, size, c->cmap);
	if(err < 0)
		sysfatal("%r");
	return p+size;
}

/* 2.2.2.2.1.2.3 Cache Bitmap - Revision 2 (CACHE_BITMAP_REV2_ORDER) */
static uchar*
cacheimage2(Rdp* c, uchar* p,uchar* ep, int xorder, int opt)
{
	int n, g;
	int zip;
	int cid;	/* cacheId */
	int coff;	/* cacheIndex */
	int chan;
	int size;
	int err;
	Image* img;
	Point d;
	Rectangle r;

	if(p+9 >= ep)
		sysfatal("cacheimage2: %s", Eshort);
	p += 6;

	chan = c->chan;
	zip = (xorder==CacheCompressed2);
	cid = opt&Bits3;
	opt >>= 7;

	if(opt&1<<1)
		p += 8;	// persistent cache key
	g = *p++;
	if(g&1<<7)
		g = ((g&Bits7)<<8) | *p++;
	d.x = g;
	if(opt&1)
		d.y = d.x;
	else{
		g = *p++;
		if(g&1<<7)
			g = ((g&Bits7)<<8) | *p++;
		d.y = g;
	}
	r.min = ZP;
	r.max = d;

	g = *p++;
	n = g>>6;
	g &= Bits6;
	switch(n){
	default:	sysfatal("cacheimage2: integer too big");
	case 3:	g = (g<<8) | *p++;
	case 2:	g = (g<<8) | *p++;
	case 1:	g = (g<<8) | *p++;
	}
	size = g;

	g = *p++;
	if(g&1<<7)
		g = ((g&Bits7)<<8) | *p++;
	coff = g;

	if(zip&& !(opt&1<<3)){
		p += 8;	// bitmapComprHdr
		size -= 8;
	}
	if(p+size > ep)
		sysfatal("cacheimage2: size: %s", Eshort);
	if((img = pickimage(cid, coff, r, chan)) == nil)
		sysfatal("cacheimage2: pickimage: %r");
	err = (zip? loadrle : loadbmp)(img, r, p, size, c->cmap);
	if(err < 0)
		sysfatal("%r");
	return p+size;
}

/* 2.2.2.2.1.2.4 Cache Color Table (CACHE_COLOR_TABLE_ORDER) */
static uchar*
cachecmap(Rdp*, uchar* p,uchar* ep, int, int)
{
	int cid, n;
	
	cid = p[6];
	n = GSHORT(p+7);
	if(n != 256){
		fprint(2, "cachecmap: %d != 256\n", n);
		return nil;
	}
	if(p+9+4*256>ep){
		werrstr(Eshort);
		return nil;
	}
	/* fixed cmap here */
	USED(cid);
	return p+9+4*256;
}
