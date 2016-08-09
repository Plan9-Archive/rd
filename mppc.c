/*
 * lifted from /sys/src/cmd/ip/ppp/mppc.c (RFC 2118)
 * plus the RDP 5.0 64K history (3.1.8.4.2 RDP 5.0)
 */
#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

#define DBG if(0)
//#define DBG

enum
{
	MaxHistorySize=	64*1024,

	Preset=			0x80,	/* reset history */
	Pfront=			0x40,	/* move packet to front of history */
	Pcompress=		0x20,	/* packet is compressed */
	Pbulk64=			0x01,	/* RPD5 bulk compression (64K history) */
};

enum
{
	Bits4=	0xf,
	Bits5=	0x1f,
	Bits6=	0x3f,
	Bits7=	0x7f,
	Bits8=	0xff,
	Bits11=	0x7ff,
	Bits13=	0x1fff,
	Bits16=	0xffff,
};

enum
{
	Lit7,		/* seven bit literal */
	Lit8,		/* eight bit literal */
	Off6,		/* six bit offset */
	Off8,		/* eight bit offset */
	Off11,		/* eleven bit offset (64K history) */
	Off13,		/* thirteen bit offset (8K history) */
	Off16,		/* sixteen bit offset (64K history) */
};

/* decode first four bits (8K history) */
static int decode8[16]=
{
	Lit7, Lit7, Lit7, Lit7,
	Lit7, Lit7, Lit7, Lit7,
	Lit8, Lit8, Lit8, Lit8,	
	Off13, Off13, Off8, Off6,
};

/* decode first five bits (64K history) */
static int decode64[32]=
{
	Lit7, Lit7, Lit7, Lit7,
	Lit7, Lit7, Lit7, Lit7,
	Lit7,	Lit7, Lit7, Lit7,
	Lit7, Lit7, Lit7, Lit7,
	Lit8, Lit8, Lit8, Lit8,	
	Lit8, Lit8, Lit8, Lit8,	
	Off16, Off16, Off16, Off16,
	Off11, Off11, Off8, Off6,
};

typedef struct Uncstate Uncstate;
struct Uncstate
{
	uchar	his[MaxHistorySize];
	int	indx;		/* current indx in history */
	int	size;		/* current history size */
};

static Uncstate uncstate;


#define NEXTBYTE	sreg = (sreg<<8) | *p++; n--; bits += 8
uchar*
uncomp(uchar* buf, int nbytes, int flags, int* psize)
{
	int n, bits, off, len, ones, t;
	int *decode, lookbits, lookmask, maxhis, maxones;
	ulong sreg;
	uchar *p, c, *hp, *hs, *he, *hq;
	Uncstate *s;

	s = &uncstate;
	p = buf;
	n = nbytes;

	if(flags&Pbulk64){
		maxhis = 64*1024;
		maxones = 14;
		decode = decode64;
		lookbits = 5;
		lookmask = Bits5;
	}else{
		maxhis = 8*1024;
		maxones = 11;
		decode = decode8;
		lookbits = 4;
		lookmask = Bits4;
	}

	if(flags&Preset){
		s->indx = 0;
		s->size = 0;
		memset(s->his, maxhis, 0);
	}
	if(flags&Pfront){
		s->indx = 0;
DBG		fprint(2, "mppc: front flag set\n"); 
	}
	if(!(flags&Pcompress)){
		*psize = n;
		return buf;
	}

	bits = 0;
	sreg = 0;
	hs = s->his;			/* history start */
	hp = hs+s->indx;		/* write pointer in history */
	he = hs+maxhis;		/* history end */
	for(;;){
		if(bits<lookbits){
			if(n==0) goto Done;
			NEXTBYTE;
		}
		t = decode[(sreg>>(bits-lookbits))&lookmask];
		switch(t){
		default:
			sysfatal("mppc: bad decode %d!", t);
		case Lit7:
			bits -= 1;
			if(bits<7){
				if(n==0) goto Done;
				NEXTBYTE;
			}
			c = (sreg>>(bits-7))&Bits7;
			bits -= 7;
			if(hp >= he) goto His;
			*hp++ = c;
			continue;
		case Lit8:
			bits -= 2;
			if(bits<7) {
				if(n==0) goto Eof;
				NEXTBYTE;
			}
			c = 0x80 | ((sreg>>(bits-7))&Bits7);
			bits -= 7;
			if(hp >= he) goto His;
			*hp++ = c;
			continue;
		case Off6:
			bits -= lookbits;
			if(bits<6){
				if(n==0) goto Eof;
				NEXTBYTE;
			}
			off = (sreg>>(bits-6))&Bits6;
			bits -= 6;
			break;
		case Off8:
			bits -= lookbits;
			if(bits<8){
				if(n==0) goto Eof;
				NEXTBYTE;
			}
			off = ((sreg>>(bits-8))&Bits8)+64;
			bits -= 8;
			break;
		case Off13:	/* (8K history) */
			bits -= 3;
			while(bits<13){
				if(n==0) goto Eof;
				NEXTBYTE;
			}
			off = ((sreg>>(bits-13))&Bits13)+320;
			bits -= 13;
			break;
		case Off11:	/* (64K history) */
			bits -= 4;
			while(bits<11){
				if(n==0) goto Eof;
				NEXTBYTE;
			}
			off = ((sreg>>(bits-11))&Bits11)+320;
			bits -= 11;
			break;
		case Off16:	/* (64K history) */
			bits -= 3;
			while(bits<16){
				if(n==0) goto Eof;
				NEXTBYTE;
			}
			off = ((sreg>>(bits-16))&Bits16)+2368;
			bits -= 16;
			break;
		}
		for(ones=0;;ones++) {
			if(bits == 0) {
				if(n==0) goto Eof;
				NEXTBYTE;
			}
			bits--;
			if(!(sreg&(1<<bits)))
				break;
		}
		if(ones>maxones){
			werrstr("bad length %d\n", ones);
			return nil;
		}
		if(ones == 0) {
			len = 3;
		} else {
			ones++;
			while(bits<ones) {
				if(n==0) goto Eof;
				NEXTBYTE;
			}
			len = (1<<ones) | ((sreg>>(bits-ones))&((1<<ones)-1));
			bits -= ones;
		}

		hq = hp-off;
		if(hq < hs) {
			hq += maxhis;
			if(hq-hs+len > s->size){
//				goto His;
fprint(2, "mppc: reference past valid history\n");
			}
		}
		if(hp+len > he)
			goto His;
		while(len) {
			*hp++ = *hq++;
			len--;
		}
	}

Done:
	hq = hs+s->indx;
	len = hp-hq;
DBG fprint(2, "mppc: len %d bits = %d n=%d\n", len, bits, n);

	s->indx += len;
	if(s->indx > s->size)
		s->size = s->indx;

	*psize = len;
	return hq;

Eof:
	werrstr("unexpected end of data");
	return nil;
His:
	werrstr("bad history reference");
	return nil;
}
