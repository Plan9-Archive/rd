#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

/* 2.2.9.1.1.3.1.2.1 Bitmap Update Data (TS_UPDATE_BITMAP_DATA) */
void
drawimgupdate(Rdp *c, Share* s)
{
	uchar* p, *ep;
	int n, err, nr;
	Rectangle r;
	Imgupd iu;
	int (*loadfunc)(Image*,Rectangle,uchar*,int,uchar*);
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
		if((n = getimgupd(&iu, p, ep-p)) < 0)
			sysfatal("getimgupd: %r");
		if(iu.depth != pad->depth)
			sysfatal("bad image depth");
		r = Rect(iu.x, iu.y, iu.xm+1, iu.ym+1);
		r = rectaddpt(r, screen->r.min);
		loadfunc = (iu.iscompr? loadrle : loadbmp);
		err = loadfunc(pad, r, iu.bytes, iu.nbytes, c->cmap);
		if(err < 0)
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
