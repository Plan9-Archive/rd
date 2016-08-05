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
	Rectangle r, rs, d;
	Imgupd iu;
	static Image* img;

	assert(s->type == ShUimg);
	p = s->data;
	ep = s->data + s->ndata;
	nr = s->nrect;

	rs = rectaddpt(Rpt(ZP, Pt(c->xsz, c->ysz)), screen->r.min);

	if(display->locking)
		lockdisplay(display);

	if(img==nil || !eqrect(img->r, rs)){
		if(img != nil)
			freeimage(img);
		img = allocimage(display, rs, c->chan, 0, DNofill);
		if(img == nil)
			sysfatal("drawimgupdate: %r");
	}

	while(p<ep && nr>0){
		/* 2.2.9.1.1.3.1.2.2 Bitmap Data (TS_BITMAP_DATA) */
		if((n = getimgupd(&iu, p, ep-p)) < 0)
			sysfatal("getimgupd: %r");
		if(iu.depth != img->depth)
			sysfatal("bad image depth");

		d.min = Pt(iu.x, iu.y);
		d.max = Pt(iu.xm+1, iu.ym+1);
		r.min = ZP;
		r.max = Pt(iu.xsz, iu.ysz);
		r = rectaddpt(r, img->r.min);

		err = (iu.iscompr? loadrle : loadbmp)(img, r, iu.bytes, iu.nbytes, c->cmap);
		if(err < 0)
			sysfatal("%r");
		draw(screen, rectaddpt(d, screen->r.min), img, nil, img->r.min);
		p += n;
		nr--;
	}
	flushimage(display, 1);
	if(display->locking)
		unlockdisplay(display);
}
