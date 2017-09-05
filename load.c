#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

/* like loadimage(2) but reverses scanline order and translates per cmap */
int
loadbmp(Image *i, Rectangle r, uchar *data, int ndata, uchar *cmap)
{
	int n, bpl;
	uchar *a;

	bpl = bytesperline(r, i->depth);
	n = bpl*Dy(r);
	if(n > ndata){
		werrstr("loadbmp: insufficient data");
		return -1;
	}

	if(i->depth == 8)
		for(a = data; a <data+ndata; a++)
			*a = cmap[*a];

	/* BUG: padding ignored

	2.2.9.1.1.3.1.2.2 Bitmap Data (TS_BITMAP_DATA):

		❝Uncompressed bitmap data is formatted as a bottom-up,
		left-to-right series of pixels.  Each pixel is a whole
		number of bytes.  Each row contains a multiple of four
		bytes (including up to three bytes of padding, as
		necessary).❞

	*/

	n = bpl;
	while(r.max.y > r.min.y){
		a = bufimage(i->display, 21+n);
		if(a == nil){
			werrstr("bufimage failed");
			return -1;
		}
		a[0] = 'y';
		iputl(a+1, i->id);
		iputl(a+5, r.min.x);
		iputl(a+9, r.max.y-1);
		iputl(a+13, r.max.x);
		iputl(a+17, r.max.y);
		memmove(a+21, data, n);
		ndata += n;
		data += n;
		r.max.y--;
	}
	if(flushimage(i->display, 0) < 0)
		return -1;
	return ndata;
}

int
loadrle(Image *i, Rectangle r, uchar *data, int ndata, uchar *cmap)
{
	int nb, bpl;
	uchar *buf;

	bpl = bytesperline(r, i->depth);
	nb = bpl*Dy(r);
	buf = emalloc(nb);

	if(unrle(buf, nb, data, ndata, bpl, bpl/Dx(r)) == nil){
		werrstr("loadrle: decompression failed");
		free(buf);
		return -1;
	}
	if(loadbmp(i, r, buf, nb, cmap) < 0){
		werrstr("loadrle: r=%R i->r=%R: %r", r, i->r);
		free(buf);
		return -1;
	}
	free(buf);
	return nb;
}

void
loadcmap(Rdp* c, Share* as)
{
	int i, n;
	uchar *p,  *ep, *cmap;

	if(as->type != ShUcmap){
		fprint(2, "loadcmap: bad share type");
		return;
	}
	p = as->data;
	ep = as->data + as->ndata;
	cmap = c->cmap;

	n = igets(p+4);
	p += 8;
	if(n > sizeof(c->cmap)){
		fprint(2, "loadcmap: data too big");
		return;
	}
	if(p+3*n > ep)
		sysfatal(Eshort);
	for(i = 0; i<n; p+=3)
		cmap[i++] = rgb2cmap(p[0], p[1], p[2]);
}
