/*
 * depending on CanMemBlt servers will only do
 * either loadmeming+drawmemimg or drawimgupdate.
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static Image*	icache[3][600];

/* 2.2.9.1.1.3.1.2.1 Bitmap Update Data (TS_UPDATE_BITMAP_DATA) */
void
drawimgupdate(Rdp *c, Share* s)
{
	int (*loadfn)(Image*,Rectangle,uchar*,int,uchar*);
	uchar* p, *ep;
	int n, nr;
	Rectangle r, rs;
	Imgupd u;
	static Image* pad;

	assert(s->type == ShUimg);
	p = s->data;
	ep = s->data + s->ndata;
	nr = s->nrect;

	if(display->locking)
		lockdisplay(display);

	rs = rectaddpt(Rpt(ZP, Pt(c->xsz+4, c->ysz+4)), screen->r.min);
	if(pad==nil || eqrect(pad->r, rs) == 0){
		freeimage(pad);
		pad = allocimage(display, rs, c->chan, 0, DNofill);
		if(pad==nil)
			sysfatal("drawimgupdate: %r");
	}
	while(p<ep && nr>0){
		if((n = getimgupd(&u, p, ep-p)) < 0)
			sysfatal("getimgupd: %r");
		if(u.depth != pad->depth)
			sysfatal("bad image depth");

		loadfn = loadbmp;
		if(u.iscompr)
			loadfn = loadrle;

		r = rectaddpt(Rect(u.x, u.y, u.x+u.xsz, u.y+u.ysz), screen->r.min);
		if(loadfn(pad, r, u.bytes, u.nbytes, c->cmap) < 0)
			sysfatal("drawimgupdate: %r");

		r = rectaddpt(Rect(u.x, u.y, u.xm+1, u.ym+1), screen->r.min);
		draw(screen, r, pad, nil, r.min);
		p += n;
		nr--;
	}
//	if(p != ep)
//		fprint(2, "drawimgupdate: out of sync: %d bytes left\n", (int)(ep-p));
	flushimage(display, 1);
	if(display->locking)
		unlockdisplay(display);
}

void
scroll(Display* d, Rectangle r, Rectangle sr)
{
	if(d && d->locking)
		lockdisplay(d);
	if(d)
		draw(d->screenimage, r, d->screenimage, nil, sr.min);
	if(d && d->locking)
		unlockdisplay(d);
}

void
loadmemimg(Rdp* c, Imgupd* iu)
{
	int (*loadfn)(Image*,Rectangle,uchar*,int,uchar*);
	Image* img;
	Rectangle r;

	loadfn = loadbmp;
	if(iu->iscompr)
		loadfn = loadrle;

	r = Rect(0, 0, iu->xsz, iu->ysz);

	if(iu->cid >= nelem(icache) || iu->coff >= nelem(*icache))
		sysfatal("cacheimage2: bad cache spec [%d %d]", iu->cid, iu->coff);

	img = icache[iu->cid][iu->coff];
	if(img==nil || eqrect(img->r, r)==0){
		freeimage(img);
		img = allocimage(display, r, c->chan, 0, DNofill);
		if(img == nil)
			sysfatal("cacheimage2: %r");
		icache[iu->cid][iu->coff] = img;
	}

	if(loadfn(img, r, iu->bytes, iu->nbytes, c->cmap) < 0)
		sysfatal("loadmemimg: %r");
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
