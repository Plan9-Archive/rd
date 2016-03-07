/* /lib/rfc/rfc2781, also translates \n to \r\n */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum
{
	Bits10 = 0x03FF,
	Bits16 = 0xFFFF,
	Bits20 = 0x0FFFFF,
	HHalfZoneS = 0xD800,	HHalfZoneE = 0xDBFF,
	LHalfZoneS = 0xDC00,	LHalfZoneE = 0xDFFF,
};

int
toutf16(uchar* buf, int nb, char* s, int ns)
{
	uchar *b, *eb;
	char *es;
	Rune r;

	b = buf;
	eb = b+nb;
	es = s+ns;

	while(s < es){
		if(*s == '\n'){
			if(b+2 > eb)
				break;
			PSHORT(b, '\r');
			b+=2;
		}
		s += chartorune(&r, s);
		if(b+2 > eb)
			break;
		if(r <= Bits16){
			PSHORT(b, r);
			b+=2;
			continue;
		} 
		r -= Bits16+1;
		if(r > Bits20){
			PSHORT(b, Runeerror);
			b+=2;
			continue;
		}
		if(b+4 > eb)
			break;
		PSHORT(b+0, HHalfZoneS | (r >> 10));
		PSHORT(b+2, LHalfZoneS | (r & Bits10));
		b+=4;
	}
	return b-buf;
}

int
fromutf16(char* str, int ns, uchar* ws, int nw)
{
	char *s, *es, buf[UTFmax];
	uchar *q, *eq;
	ushort w1, w2;
	Rune r;
	int n;

	s = str;
	es = str + ns;
	q = ws;
	eq = ws + nw;

	while(q+2 <= eq){
		w1 = GSHORT(q);
		q += 2;
		if(w1<HHalfZoneS || w1>LHalfZoneE){
			r = w1;
			goto Convert;
		}
		if(w1>HHalfZoneE){
			r = Runeerror;
			goto Convert;
		}
		if(q+2 > eq){
			r = Runeerror;
			goto Convert;
		}
		w2 = GSHORT(q);
		q += 2;
		if(w2<LHalfZoneS || w2>LHalfZoneE){
			r = Runeerror;
			goto Convert;
		}
		r = (w1 & Bits10)<<10 | (w2 & Bits10) + Bits16 + 1;
	Convert:
		n = runetochar(buf, &r);
		if(buf[0] == '\r')
			continue;
		if(s+n > es)
			break;
		memmove(s, buf, n);
		s += n;
	}
	return s-str;
}

