/*
 * [MS-RDPEGDI]: Graphics Device Interface (GDI) Acceleration Extensions
 * http://msdn.microsoft.com/en-us/library/cc241537.aspx
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

#define	DBG	if(0)

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
	Scopy	= 0xcc,
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

static int	getscrblt(Imgupd*, uchar*, uint, int, int);
static int	getmemblt(Imgupd*, uchar*, uint, int, int);
static int	getimgcache2(Imgupd*, uchar*, uint, int, int);
static int	getcmapcache(Imgupd*, uchar*, uint, int, int);

typedef	struct	Order Order;
struct Order
{
	int	fsize;
	int (*get)(Imgupd*,uchar*,uint,int,int);
};

Order ordtab[NumOrders] = {
	[ScrBlt]= 		{ 1, getscrblt },
	[MemBlt]=	{ 2, getmemblt },
};

Order auxtab[8] = {
	[CacheImage2]=		{ 0, getimgcache2  },
	[CacheCompressed2]= 	{ 0, getimgcache2 },
	[CacheCmap]=			{ 0, getcmapcache },
};

uchar
orderSupport[NumOrders] = 
{
	[CanScrBlt]=   	1,
	[CanMemBlt]=	1,
};

static struct GdiContext
{
	int order;
	Rectangle clipr;
} gc	= {PatBlt};

static	int	cfclipr(Rectangle*,uchar*,int);
static	int	cfpt(Point*,uchar*,int,int,int);

int
getfupd(Imgupd* up, uchar* a, uint nb)
{
	uchar *p, *ep;
	int ctl, fset, fsize;
	int n, size, opt, xorder;

	p = a;
	ep = a+nb;

	fset = 0;
	ctl = *p;
	if(!(ctl&Standard))
		goto ErrNstd;
	if(ctl&Secondary){
		if(p+6>ep)
			sysfatal("draworders: %s", Eshort);
		size = ((short)GSHORT(p+1))+13;
		if(size < 0 || p+size > ep)
			sysfatal("draworders: size: %s", Eshort);
		opt = GSHORT(p+3);
		xorder = p[5];
		if(xorder >= nelem(auxtab) || auxtab[xorder].get == nil){
			fprint(2, "egdi: unsupported secondary order %d\n", xorder);
			p += size;
			return p-a;
		}

		auxtab[xorder].get(up, p, size, xorder, opt);
		p += size;
		return p-a;
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
		p += cfclipr(&gc.clipr, p, ep-p);

	if(ordtab[gc.order].get == nil)
		goto ErrNotsup;
	n = ordtab[gc.order].get(up, p, ep-p, ctl, fset);
	p += n;
	return p-a;
ErrNstd:
	fprint(2, "egdi: non-standard order (GDI+ or out of sync)\n");
	return p-a;
ErrFsize:
	fprint(2, "egdi: bad field encoding bytes count for order %d\n", gc.order);
	return p-a;
ErrNotsup:
	fprint(2, "egdi: unsupported order %d\n", gc.order);
	return p-a;
}

static int
cfpt(Point* p, uchar* a, int nb, int isdelta, int fset)
{
	int n;

	n = 0;
	if(isdelta){
		if(fset&1<<0)
			n++;
		if(fset&1<<1)
			n++;
	}else{
		if(fset&1<<0)
			n += 2;
		if(fset&1<<1)
			n += 2;
	}
	if(n>nb)
		sysfatal("cfpt: %s", Eshort);

	if(isdelta){
		if(fset&1<<0)
			p->x += (signed char)*a++;
		if(fset&1<<1)
			p->y += (signed char)*a;
	}else{
		if(fset&1<<0){
			p->x = GSHORT(a);
			a += 2;
		};
		if(fset&1<<1)
			p->y = GSHORT(a);
	}
	return n;
}

static int
cfrect(Rectangle* pr, uchar* p, int nb, int isdelta, int fset){
	int n, m;

	pr->max = subpt(pr->max, pr->min);
	n = cfpt(&pr->min, p, nb, isdelta, fset);
	m = cfpt(&pr->max, p+n, nb-n, isdelta, fset>>2);
	pr->max = addpt(pr->max, pr->min);
	return n+m;
}

static int
cfclipr(Rectangle* pr, uchar* a, int nb)
{
	int bctl;
	uchar *p, *ep;

	p = a;
	ep = a+nb;

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
		sysfatal("cfclipr: %s", Eshort);
	return p-a;
}

/* 2.2.2.2.1.1.2.7 ScrBlt (SCRBLT_ORDER) */
static int
getscrblt(Imgupd* up, uchar* a, uint nb, int ctl, int fset)
{
	int n;
	uchar *p, *ep;
	Rectangle r;
	static	Rectangle wr;
	static	Point wp;
	static	int rop3;

DBG	fprint(2, "getscrblt...");
	p = a;
	ep = a+nb;

	n = cfrect(&wr, p, ep-p, ctl&Diff, fset);
	p += n;
	if(fset&1<<4){
		if(ep-p<1)
			sysfatal(Eshort);
		rop3 = *p++;
	}
	n = cfpt(&wp, p, ep-p, ctl&Diff, fset>>5);
	p += n;

	r = wr;
	if(ctl&Clipped)
		rectclip(&r, gc.clipr);

	if(rop3 != Scopy)
		fprint(2, "scrblt: rop3 %#hhux is not supported\n", rop3);

	up->type = Uscrblt;
	up->x = r.min.x;
	up->y = r.min.y;
	up->xsz = Dx(r);
	up->ysz = Dy(r);
	up->sx = wp.x;
	up->sy = wp.y;

	return p-a;
}

static int
getmemblt(Imgupd* up, uchar* a, uint nb, int ctl, int fset)
{
	static int cid;	/* cacheId */
	static int coff;	/* cacheIndex */
	static int rop3;
	static Rectangle r;
	static Point pt;
	int n;
	uchar *p, *ep;
DBG	fprint(2, "getmemblt...");

	p = a;
	ep = a+nb;

	if(fset&1<<0){
		cid = GSHORT(p);
		p += 2;
	}
	n = cfrect(&r, p, ep-p, ctl&Diff, fset>>1);
	p += n;
	if(fset&1<<5)
		rop3 = *p++;
	n = cfpt(&pt, p, ep-p, ctl&Diff, fset>>6);
	p += n;
	if(fset&1<<8){
		coff = GSHORT(p);
		p += 2;
	}
	if(p>ep)
		sysfatal("getmemblt: %s", Eshort);

	cid &= Bits8;

	up->type = Umemblt;
	up->cid = cid;
	up->coff = coff;
	up->x = r.min.x;
	up->y = r.min.y;
	up->xm = r.max.x-1;
	up->ym = r.max.y-1;
	up->xsz = Dx(r);
	up->ysz = Dy(r);
	up->sx = pt.x;
	up->sy = pt.y;
	up->clipped = 0;
	if(ctl&Clipped){
		up->clipped = 1;
		up->cx = gc.clipr.min.x;
		up->cy = gc.clipr.min.y;
		up->cxsz = Dx(gc.clipr);
		up->cysz = Dy(gc.clipr);
	}

	return p-a;
}

/* 2.2.2.2.1.2.3 Cache Bitmap - Revision 2 (CACHE_BITMAP_REV2_ORDER) */
static int
getimgcache2(Imgupd* up, uchar* a, uint nb, int xorder, int opt)
{
	uchar *p, *ep;
	int cid;	/* cacheId */
	int coff;	/* cacheIndex */
	int n, g;
	int size;

DBG	fprint(2, "getimgcache2...");
	p = a;
	ep = a+nb;

	up->iscompr = (xorder==CacheCompressed2);

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
	up->xsz = g;
	if(opt&1)
		up->ysz = g;
	else{
		g = *p++;
		if(g&1<<7)
			g = ((g&Bits7)<<8) | *p++;
		up->ysz = g;
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

	if(up->iscompr && !(opt&1<<3)){
		p += 8;	// bitmapComprHdr
		size -= 8;
	}
	if(p+size > ep)
		sysfatal("cacheimage2: size: %s", Eshort);

	up->type = Uicache;
	up->cid = cid;
	up->coff = coff;
	up->nbytes = size;
	up->bytes = p;
	return size;
}

/* 2.2.2.2.1.2.4 Cache Color Table (CACHE_COLOR_TABLE_ORDER) */
static int
getcmapcache(Imgupd* up, uchar* a, uint nb, int xorder, int opt)
{
	int cid, n;
DBG	fprint(2, "getcmapcache...");
	USED(xorder);
	USED(opt);
	
	cid = a[6];
	n = GSHORT(a+7);
	if(n != 256)
		sysfatal("cachecmap: %d != 256", n);
	if(9+4*256 > nb)
		sysfatal(Eshort);
	up->type = Umcache;
	up->cid = cid;
	up->bytes = a+9;
	up->nbytes = 4*256;
	return 9+4*256;
}

