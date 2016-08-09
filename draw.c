#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static Image*	pad;
static Image*	icache[3][600];

static	void	drawupd(Rdp*,Imgupd*);
static	void	padresize(Rdp*);

/* 2.2.9.1.1.3.1.2.1 Bitmap Update Data (TS_UPDATE_BITMAP_DATA) */
void
drawimgupdate(Rdp *c, Share* s)
{
	uchar* p, *ep;
	int n, nr;
	Imgupd u;

	assert(s->type == ShUimg);
	p = s->data;
	ep = s->data + s->ndata;
	nr = s->nr;

	if(display->locking)
		lockdisplay(display);

	while(p<ep && nr>0){
		if((n = getimgupd(&u, p, ep-p)) < 0)
			sysfatal("getimgupd: %r");
		drawupd(c, &u);
		p += n;
		nr--;
	}
	flushimage(display, 1);
	if(display->locking)
		unlockdisplay(display);
}

/* 2.2.2.2 Fast-Path Orders Update (TS_FP_UPDATE_ORDERS) */
void
draworders(Rdp* c, Share* as)
{
	int n, count;
	uchar *p, *ep;
	Imgupd u;

	count = as->nr;
	p = as->data;
	ep = as->data + as->ndata;

	if(display->locking)
		lockdisplay(display);
	while(count> 0 && p<ep){
		n = getfupd(&u, p, ep-p);
		drawupd(c, &u);
		p += n;
		count--;
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

void
drawmemimg(Rdp*, Imgupd* iu)
{
	Image* img;
	Rectangle r;
	Point pt;

	/* called with display locked */

	if(iu->cid >= nelem(icache) || iu->coff >= nelem(*icache)){
		fprint(2, "drawmemimg: bad cache spec [%d %d]\n", iu->cid, iu->coff);
		return;
	}
	img = icache[iu->cid][iu->coff];
	if(img == nil){
		fprint(2, "drawmemimg: empty cache entry cid %d coff %d\n", iu->cid, iu->coff);
		return;
	}

	r = Rect(iu->x, iu->y, iu->xm+1, iu->ym+1);
	r = rectaddpt(r, screen->r.min);
	pt = Pt(iu->sx, iu->sy);
	draw(screen, r, img, nil, pt);
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
memblt(Rdp* c, Imgupd* up)
{
	if(up->clipped)
		replclipr(screen, screen->repl, rectaddpt(up->clipr, screen->r.min));
	drawmemimg(c, up);
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
drawupd(Rdp* c, Imgupd* up)
{
	switch(up->type){
	case Ubitmap:	imgupd(c, up); break;
	case Uscrblt:	scrblt(c, up); break;
	case Umemblt:	memblt(c, up); break;
	case Uicache:	cacheimage2(c, up); break;
	case Umcache:	cachecmap(c, up); break;
	}
}
