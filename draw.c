#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

/* 2.2.9.1.1.3.1.2.1 Bitmap Update Data (TS_UPDATE_BITMAP_DATA) */
void
drawimgupdate(Rdp *c, Share* s)
{
	int (*loadfn)(Image*,Rectangle,uchar*,int,uchar*);
	uchar* p, *ep;
	int n, nr;
	Rectangle r;
	Imgupd u;
	static Image* pad;

	assert(s->type == ShUimg);
	p = s->data;
	ep = s->data + s->ndata;
	nr = s->nrect;

	if(display->locking)
		lockdisplay(display);
	if(pad==nil || eqrect(pad->r, screen->r) != 0){
		freeimage(pad);
		pad = allocimage(display, screen->r, c->chan, 0, DNofill);
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

		r = Rect(u.x, u.y, u.xm+1, u.ym+1);
		r = rectaddpt(r, screen->r.min);

		if(loadfn(pad, r, u.bytes, u.nbytes, c->cmap) < 0)
			sysfatal("%r");
		draw(screen, r, pad, nil, r.min);
		p += n;
		nr--;
	}
	if(p != ep)
		fprint(2, "drawimgupdate: out of sync\n");
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