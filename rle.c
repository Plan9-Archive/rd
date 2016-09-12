/*
 * 2.2.9.1.1.3.1.2.4 RLE Compressed Bitmap Stream (RLE_BITMAP_STREAM)
 *	http://msdn.microsoft.com/en-us/library/cc240895.aspx
 * 3.1.9 Interleaved RLE-Based Bitmap Compression
 *	http://msdn.microsoft.com/en-us/library/dd240593.aspx
*/
#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

enum
{
	Bits4=	15,
	Bits5=	31,
};

enum
{
	DWhite		= 0xFFFFFFFF,
};

enum
{
	Bg,
	Fg,
	FgS,
	Dith,
	Fill,
	Mix,
	MixS,
	Lit,
	Spec,
	Mix3,
	Mix5,
	Wpix,
	Bpix,
};

enum
{
	Std= 0,
	Ext= 1,
};

static int
decode[2][16] =
{
[Std]={
		Bg, 	Bg, 	Fg, 	Fg,	Mix,	Mix,	Fill,	Fill,
		Lit,	Lit,	-1, 	-1,	FgS,	MixS,	Dith,	-1,
	},
[Ext]={
		Bg, 	Fg, 	Mix,	Fill,	Lit,	-1, 	FgS,	MixS,
		Dith,	Mix3,	Mix5,	-1, 	-1, 	Wpix,	Bpix,	-1,
	}
};

static void*
memfill(void *a1, ulong n1, void *a2, ulong n2)
{
	char *s1, *s2, *e1, *e2;

	if((long)n1 < 0 || (long)n2 <= 0)
		abort();
	s1 = a1;
	s2 = a2;
	e1 = s1+n1;
	e2 = s2+n2;
	while(s1 < e1){
		*s1++ = *s2++;
		if(s2 >= e2)
			s2 = a2;
	}
	return a1;
}

static void*
memxor(void *a1, void *a2, ulong n)
{
	char *s1, *s2;

	if((long)n < 0)
		abort();
	s1 = a1;
	s2 = a2;
	while(n > 0){
		*s1++ ^= *s2++;
		n--;
	}
	return a1;
}

uchar*
unrle(uchar* d, int nd, uchar* s, int ns, int bpl, int pixelsize)
{
	int t, hdr, code, bits, len, wasline1, wasbg;
	uchar pen[4], dpen[8], wpen[4], *p, *ep, *wp, *we;
	uint sreg;

	p = s;
	ep = s+ns;
	wp = d;
	we = d+nd;
	wasbg = 0;
	wasline1 = 1;
	PLONG(pen, DWhite);
	PLONG(wpen, DWhite);

	while(p < ep){
		hdr = *p++;
		code = hdr>>4;
		if(code != Bits4){
			t = decode[Std][code];
			if(code>>2 == 3)
				bits = Bits4;
			else
				bits = Bits5;
			len = hdr&bits;
			if(t==Mix || t==MixS){
				if(len == 0)
					len = 1+*p++;
				else
					len *= 8;
			}else{
				if(len == 0)
					len = 1+bits+*p++;
			}
		}else{
			code = hdr&Bits4;
			if(code < 9){
				len = GSHORT(p);
				p += 2;
			}else
				len = 0;
			t = decode[Ext][code];
		}
		len *= pixelsize;
		if(wp+len > we){
		   Overrun:
			werrstr("unrle: output buffer is %uld bytes short", wp+len-we);
			return nil;
		}

		if(t != Bg)
			wasbg = 0;
		if(wasline1 && wp-bpl >= d){
			wasline1 = 0;
			wasbg = 0;
		}

		switch(t){
		default:
			werrstr("unrle: bad decode");
			return nil;
		case Lit:
			memmove(wp, p, len);
			p += len;
			break;
		case Bg:
			if(wasbg){
				memmove(wp, pen, pixelsize);
				if(wp-bpl >= d)
					memxor(wp, wp-bpl, pixelsize);
				wp += pixelsize;
				len -= pixelsize;
			}
			if(wp-bpl >= d){
				while(len>bpl){
					memmove(wp, wp-bpl, bpl);
					wp += bpl;
					len -= bpl;
				}
				memmove(wp, wp-bpl, len);
			}else
				memset(wp, len, 0);
			wasbg = 1;
			break;
		case FgS:
			memmove(pen, p, pixelsize);
			p += pixelsize;
			/* fall through */
		case Fg:
			memfill(wp, len, pen, pixelsize);
			if(wp-bpl >= d)
				memxor(wp, wp-bpl, len);
			break;
		case Mix3:
			sreg = 3;
			bits = 8;
			len = 8*pixelsize;
			goto case_MixAll;
		case Mix5:
			sreg = 5;
			bits = 8;
			len = 8*pixelsize;
			goto case_MixAll;
		case MixS:
			memmove(pen, p, pixelsize);
			p += pixelsize;
			/* fall through */
		case Mix:
			sreg = 0;
			bits = 0;
			/* fall through */
		case_MixAll:
			if(wp+len > we)
				goto Overrun;
			while(len > 0){
				if(bits == 0){
					sreg = *p++;
					bits = 8;
				}
				if(sreg&1){
					memmove(wp, pen, pixelsize);
					if(wp-bpl >= d)
						memxor(wp, wp-bpl, pixelsize);
				}else{
					if(wp-bpl >= d)
						memmove(wp, wp-bpl, pixelsize);
					else
						memset(wp, pixelsize, 0);
				}
				wp += pixelsize;
				len -= pixelsize;
				sreg >>= 1;
				bits--;
			}
			break;
		case Fill:
			memmove(dpen, p, pixelsize);
			p += pixelsize;
			memfill(wp, len, dpen, pixelsize);
			break;
		case Dith:
			len *= 2;
			if(wp+len > we)
				goto Overrun;
			memmove(dpen, p, pixelsize);
			memmove(dpen+pixelsize, p+pixelsize, pixelsize);
			p += 2*pixelsize;
			memfill(wp, len, dpen, 2*pixelsize);
			break;
		case Wpix:
			len = pixelsize;
			if(wp+len > we)
				goto Overrun;
			memmove(wp, wpen, pixelsize);
			break;
		case Bpix:
			len = pixelsize;
			if(wp+len > we)
				goto Overrun;
			memset(wp, pixelsize, 0);
			break;
		}
		wp += len;
	}
	return wp;
}
