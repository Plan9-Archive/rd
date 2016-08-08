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
static	uchar*	cachecmap(Rdp*, uchar*,uchar*,int,int);

Order ordtab[NumOrders] = {
	[ScrBlt]= 		{ 1, scrblt },
	[MemBlt]=	{ 2, memblt },
};

Order auxtab[8] = {
	[CacheImage2]=		{ 0, cacheimage2 },
	[CacheCompressed2]= 	{ 0, cacheimage2 },
	[CacheCmap]=			{ 0, cachecmap },
};

uchar
orderSupport[NumOrders] = 
{
	[CanScrBlt]   	1,
	[CanMemBlt]	1,
};

static struct GdiContext
{
	int order;
	Rectangle clipr;
} gc	= {PatBlt};

static	uchar*	getclipr(Rectangle*,uchar*,uchar*);
static	uchar*	getpt(Point*,uchar*,uchar*,int,int);

/* 2.2.2.2 Fast-Path Orders Update (TS_FP_UPDATE_ORDERS) */
void
scanorders(Rdp* c, Share* as)
{
	int count;
	uchar *p, *ep;
	int ctl, fset, fsize;
	int size, opt, xorder;

	count = as->nord;
	p = as->data;
	ep = as->data + as->ndata;

	while(count-- > 0 && p<ep){	
		fset = 0;
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
			fset = p[0]|(p[1]<<8)|(p[2]<<16);
			break;
		case 2:
			fset = GSHORT(p);
			break;
		case 1:
			fset = p[0];
		case 0:
			break;
		}
		p += fsize;

		if(ctl&Clipped && !(ctl&SameClipping))
			p = getclipr(&gc.clipr, p, ep);

		if(ordtab[gc.order].fn == nil)
			goto ErrNotsup;
		p = ordtab[gc.order].fn(c, p, ep, ctl, fset);
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
getpt(Point* pp, uchar* s, uchar *es,  int ctl, int fset)
{
	Point p;

	p = *pp;
	if(ctl&Diff){
		if(fset&1<<0)
			p.x += (char)*s++;
		if(fset&1<<1)
			p.y += (char)*s++;
	}else{
		if(fset&1<<0){
			p.x = GSHORT(s);
			s += 2;
		};
		if(fset&1<<1){
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
getoffrect(Rectangle* pr, uchar* p, uchar* ep, int ctl, int fset){
	Rectangle r;

	r = *pr;
	r.max = subpt(r.max, r.min);
	p = getpt(&r.min, p, ep, ctl, fset);
	p = getpt(&r.max, p, ep, ctl, fset>>2);
	r.max = addpt(r.max, r.min);
	*pr = r;
	return p;
}

/* 2.2.2.2.1.1.2.7 ScrBlt (SCRBLT_ORDER) */
static uchar*
scrblt(Rdp*, uchar* p, uchar* ep, int ctl, int fset)
{
	static	Rectangle wr;
	static	Point wp;
	static	int rop3;
	Rectangle r, sr;

	p = getoffrect(&wr, p, ep, ctl, fset);
	if(fset&1<<4)
		rop3 = *p++;
	p = getpt(&wp, p, ep, ctl, fset>>5);
	if(ctl&Clipped)
		rectclip(&wr, gc.clipr);

	if(rop3 != ROP2_COPY){
		fprint(2, "scrblt: rop3 %#hhux is not supported\n", rop3);
		return p;
	}

	r = rectaddpt(wr, screen->r.min);
	sr = rectaddpt(Rpt(wp, Pt(Dx(r), Dy(r))), screen->r.min);
	scroll(display, r, sr);
	return p;
}

uchar*
getmemblt(Imgupd* iu, uchar* p, uchar* ep, int ctl, int fset)
{
	static int cid;	/* cacheId */
	static int coff;	/* cacheIndex */
	static int rop3;
	static Rectangle r;
	static Point pt;

	if(fset&1<<0){
		cid = GSHORT(p);
		p += 2;
	}
	p = getoffrect(&r, p, ep, ctl, fset>>1);
	if(fset&1<<5)
		rop3 = *p++;
	p = getpt(&pt, p, ep, ctl, fset>>6);
	if(fset&1<<8){
		coff = GSHORT(p);
		p += 2;
	}
	if(p>ep)
		sysfatal("getmemblt: %s", Eshort);

	cid &= Bits8;

	iu->cid = cid;
	iu->coff = coff;
	iu->x = r.min.x;
	iu->y = r.min.y;
	iu->xm = r.max.x-1;
	iu->ym = r.max.y-1;
	iu->xsz = Dx(r);
	iu->ysz = Dy(r);
	iu->sx = pt.x;
	iu->sy = pt.y;
	return p;
}

/* 2.2.2.2.1.1.2.9 MemBlt (MEMBLT_ORDER) */
static uchar*
memblt(Rdp* c, uchar* p, uchar* ep, int ctl, int fset)
{
	Imgupd u;

	p = getmemblt(&u, p, ep, ctl, fset);

	if(display->locking)
		lockdisplay(display);
	if(ctl&Clipped)
		replclipr(screen, screen->repl, rectaddpt(gc.clipr, screen->r.min));
	drawmemimg(c, &u);
	if(ctl&Clipped)
		replclipr(screen, screen->repl, screen->r);
	if(display->locking)
		unlockdisplay(display);

	return p;
}

int
getimgcache2(Imgupd* iu, uchar* a, uint nb, int xorder, int opt)
{
	uchar *p, *ep;
	int cid;	/* cacheId */
	int coff;	/* cacheIndex */
	int n, g;
	int size;

	p = a;
	ep = a+nb;

	iu->iscompr = (xorder==CacheCompressed2);

	cid = opt&Bits3;
	opt >>= 7;

	if(p+9 >= ep)
		sysfatal("cacheimage2: %s", Eshort);

	p += 6;
	if(opt&1<<1)
		p += 8;	// persistent cache key
	g = *p++;
	if(g&1<<7)
		g = ((g&Bits7)<<8) | *p++;
	iu->xsz = g;
	if(opt&1)
		iu->ysz = g;
	else{
		g = *p++;
		if(g&1<<7)
			g = ((g&Bits7)<<8) | *p++;
		iu->ysz = g;
	}

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


	if(iu->iscompr && !(opt&1<<3)){
		p += 8;	// bitmapComprHdr
		size -= 8;
	}
	if(p+size > ep)
		sysfatal("cacheimage2: size: %s", Eshort);
	iu->cid = cid;
	iu->coff = coff;
	iu->nbytes = size;
	iu->bytes = p;

	return size;
}

/* 2.2.2.2.1.2.3 Cache Bitmap - Revision 2 (CACHE_BITMAP_REV2_ORDER) */
static uchar*
cacheimage2(Rdp* c, uchar* p, uchar* ep, int xorder, int opt)
{
	int size;
	Imgupd iupd, *iu;

	iu = &iupd;

	size = getimgcache2(iu, p, ep-p, xorder, opt);
	loadmemimg(c, iu);

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
