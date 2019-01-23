#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static Image*	pad;
static Image*	icache[3][600];

static	void	padresize(Rdp*);

static	void	drawupd1(Rdp*,Imgupd*);

extern	/* load.c */
	int	loadbmp(Image*,Rectangle,uchar*,int,uchar*);
	int	loadrle(Image*,Rectangle,uchar*,int,uchar*);

static int
depth2chan(int depth)
{
	int chan;

	switch(depth){
	default:	sysfatal("Unsupported remote color depth: %uhd\n", depth);
	case 8:	chan = CMAP8; break;
	case 15:	chan = RGB15; break;
	case 16:	chan = RGB16; break;
	case 24:	chan = RGB24; break;
	case 32:	chan = XRGB32; break;
	}
	return chan;
}

void
initscreen(Rdp* c)
{
	if(initdraw(drawerror, nil, c->label) < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	unlockdisplay(display);
	c->ysz = Dy(screen->r);
	c->xsz = (Dx(screen->r) +3) & ~3;
}

void
drawimgupdate(Rdp *c, Share* s)
{
	int (*getupd)(Imgupd*, uchar*, uint);
	uchar* p, *ep;
	int n, nr;
	Imgupd u;
	
	switch(s->type){
	default:	sysfatal("drawimgupdate: bad s->type");
	case ShUimg:	getupd = getimgupd; break;
	case ShUorders:	getupd = getfupd; break;
	}

	p = s->data;
	ep = s->data + s->ndata;
	nr = s->nr;

	if(display->locking)
		lockdisplay(display);
	while(p<ep && nr>0){
		if((n = getupd(&u, p, ep-p)) < 0)
			sysfatal("getimgupd: %r");
		drawupd1(c, &u);
		p += n;
		nr--;
	}
	flushimage(display, 1);
	if(display->locking)
		unlockdisplay(display);
}

static void
padresize(Rdp* c)
{
	Rectangle rs;
	int chan;

	rs = rectaddpt(Rpt(ZP, Pt(c->xsz+4, c->ysz+4)), screen->r.min);
	if(pad==nil || eqrect(pad->r, rs) == 0){
		chan = depth2chan(c->depth);
		freeimage(pad);
		pad = allocimage(display, rs, chan, 0, DNofill);
		if(pad==nil)
			sysfatal("drawimgupdate: %r");
	}
}

static void
imgupd(Rdp* c, Imgupd* up)
{
	Rectangle r;
	int (*loadfn)(Image*,Rectangle,uchar*,int,uchar*);

	padresize(c);	// BUG call elsewhere - whenever c->xsz changes

	if(up->depth != pad->depth)
		sysfatal("bad image depth");

	loadfn = loadbmp;
	if(up->compressed)
		loadfn = loadrle;

	r = rectaddpt(Rect(up->x, up->y, up->x+up->xsz, up->y+up->ysz), screen->r.min);
	if(loadfn(pad, r, up->bytes, up->nbytes, c->cmap) < 0)
		sysfatal("drawimgupdate: %r");

	r = rectaddpt(Rect(up->x, up->y, up->xm+1, up->ym+1), screen->r.min);
	draw(screen, r, pad, nil, r.min);
}

static void
scrblt(Rdp* c, Imgupd* up)
{
	Rectangle r;
	Point p;

	USED(c);
	r = rectaddpt(Rect(up->x, up->y, up->x+up->xsz, up->y+up->ysz), screen->r.min);
	p = addpt(Pt(up->sx, up->sy), screen->r.min);
	draw(screen, r, screen, nil, p);
}

static void
memblt(Rdp* c, Imgupd* up)
{
	Image* img;
	Rectangle clipr, r;
	Point pt;

	USED(c);
	if(up->cid >= nelem(icache) || up->coff >= nelem(*icache)){
		fprint(2, "drawmemimg: bad cache spec [%d %d]\n", up->cid, up->coff);
		return;
	}
	img = icache[up->cid][up->coff];
	if(img == nil){
		fprint(2, "drawmemimg: empty cache entry cid %d coff %d\n", up->cid, up->coff);
		return;
	}

	if(up->clip){
		clipr = Rect(up->cx, up->cy, up->cx+up->cxsz, up->cy+up->cysz);
		replclipr(screen, screen->repl, rectaddpt(clipr, screen->r.min));
	}

	r = Rect(up->x, up->y, up->xm+1, up->ym+1);
	r = rectaddpt(r, screen->r.min);
	pt = Pt(up->sx, up->sy);
	draw(screen, r, img, nil, pt);

	if(up->clip)
		replclipr(screen, screen->repl, screen->r);
}

static void
cacheimg(Rdp* c, Imgupd* u)
{
	int (*loadfn)(Image*,Rectangle,uchar*,int,uchar*);
	int chan;
	Image* img;
	Rectangle r;

	loadfn = loadbmp;
	if(u->compressed)
		loadfn = loadrle;

	r = Rect(0, 0, u->xsz, u->ysz);

	if(u->cid >= nelem(icache) || u->coff >= nelem(*icache))
		sysfatal("cacheimg: bad cache spec [%d %d]", u->cid, u->coff);

	img = icache[u->cid][u->coff];
	if(img==nil || eqrect(img->r, r)==0){
		chan = depth2chan(c->depth);
		freeimage(img);
		img = allocimage(display, r, chan, 0, DNofill);
		if(img == nil)
			sysfatal("cacheimg: %r");
		icache[u->cid][u->coff] = img;
	}

	if(loadfn(img, r, u->bytes, u->nbytes, c->cmap) < 0)
		sysfatal("loadmemimg: %r");
}

static void
cachecmap(Rdp* c, Imgupd* up)
{
	/* BUG: who cares? */
	USED(c);
	USED(up);
}

static void
drawupd1(Rdp* c, Imgupd* up)
{
	switch(up->type){
	case Ubitmap:	imgupd(c, up); break;
	case Uscrblt:	scrblt(c, up); break;
	case Umemblt:	memblt(c, up); break;
	case Ucacheimg:	cacheimg(c, up); break;
	case Umcache:	cachecmap(c, up); break;
	}
}
