#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static Image*	pad;
static Image*	icache[3][600];

static	void	padresize(Rdp*);

static	void	drawupd1(Rdp*,Imgupd*);


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

	rs = rectaddpt(Rpt(ZP, Pt(c->xsz+4, c->ysz+4)), screen->r.min);
	if(pad==nil || eqrect(pad->r, rs) == 0){
		freeimage(pad);
		pad = allocimage(display, rs, c->chan, 0, DNofill);
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
	if(up->iscompr)
		loadfn = loadrle;

	r = rectaddpt(Rect(up->x, up->y, up->x+up->xsz, up->y+up->ysz), screen->r.min);
	if(loadfn(pad, r, up->bytes, up->nbytes, c->cmap) < 0)
		sysfatal("drawimgupdate: %r");

	r = rectaddpt(Rect(up->x, up->y, up->xm+1, up->ym+1), screen->r.min);
	draw(screen, r, pad, nil, r.min);
}

static void
scrblt(Rdp*, Imgupd* up)
{
	Rectangle r;
	Point p;

	r = rectaddpt(Rect(up->x, up->y, up->x+up->xsz, up->y+up->ysz), screen->r.min);
	p = addpt(Pt(up->sx, up->sy), screen->r.min);
	draw(screen, r, screen, nil, p);
}

static void
memblt(Rdp*, Imgupd* up)
{
	Image* img;
	Rectangle clipr, r;
	Point pt;

	if(up->cid >= nelem(icache) || up->coff >= nelem(*icache)){
		fprint(2, "drawmemimg: bad cache spec [%d %d]\n", up->cid, up->coff);
		return;
	}
	img = icache[up->cid][up->coff];
	if(img == nil){
		fprint(2, "drawmemimg: empty cache entry cid %d coff %d\n", up->cid, up->coff);
		return;
	}

	if(up->clipped){
		clipr = Rect(up->cx, up->cy, up->cx+up->cxsz, up->cy+up->cysz);
		replclipr(screen, screen->repl, rectaddpt(clipr, screen->r.min));
	}

	r = Rect(up->x, up->y, up->xm+1, up->ym+1);
	r = rectaddpt(r, screen->r.min);
	pt = Pt(up->sx, up->sy);
	draw(screen, r, img, nil, pt);

	if(up->clipped)
		replclipr(screen, screen->repl, screen->r);
}

static void
cacheimage2(Rdp* c, Imgupd* up)
{
	int (*loadfn)(Image*,Rectangle,uchar*,int,uchar*);
	Image* img;
	Rectangle r;

	loadfn = loadbmp;
	if(up->iscompr)
		loadfn = loadrle;

	r = Rect(0, 0, up->xsz, up->ysz);

	if(up->cid >= nelem(icache) || up->coff >= nelem(*icache))
		sysfatal("cacheimage2: bad cache spec [%d %d]", up->cid, up->coff);

	img = icache[up->cid][up->coff];
	if(img==nil || eqrect(img->r, r)==0){
		freeimage(img);
		img = allocimage(display, r, c->chan, 0, DNofill);
		if(img == nil)
			sysfatal("cacheimage2: %r");
		icache[up->cid][up->coff] = img;
	}

	if(loadfn(img, r, up->bytes, up->nbytes, c->cmap) < 0)
		sysfatal("loadmemimg: %r");
}

static void
cachecmap(Rdp*, Imgupd*)
{
	/* BUG: who cares? */
}

static void
drawupd1(Rdp* c, Imgupd* up)
{
	switch(up->type){
	case Ubitmap:	imgupd(c, up); break;
	case Uscrblt:	scrblt(c, up); break;
	case Umemblt:	memblt(c, up); break;
	case Uicache:	cacheimage2(c, up); break;
	case Umcache:	cachecmap(c, up); break;
	}
}
