/*
 * [MS-RDPBCGR] 3.1.5.2 Static Virtual Channels
 * http://msdn.microsoft.com/en-us/library/cc240926.aspx
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum
{
	/* 2.2.1.3.4.1 Channel Definition Structure */
	ChanInited		= 0x80000000,
	ChanShowproto	= 0x00200000,

	/* 2.2.6.1 Virtual Channel PDU */
	ChanChunkLen			= 1600,

	/* 2.2.6.1.1 Channel PDU Header */
	CFfirst=	0x01,
	CFlast=	0x02,
	CFshowproto=	0x10,
};

Vchan vctab[] =
{
	{
		.mcsid = GLOBALCHAN + 1,	/* iota */
		.name = "CLIPRDR",
		.fn = clipvcfn,
		.flags = ChanInited | ChanShowproto,
	},
};
uint nvc = nelem(vctab);

Vchan*
lookupvc(int mcsid)
{
	int i;
	for(i=0; i<nvc; i++)
	if(vctab[i].mcsid == mcsid)
		return &vctab[i];
	return nil;
}

Vchan*
namevc(char* name)
{
	int i;
	for(i=0; i<nvc; i++)
	if(strcmp(vctab[i].name, name) == 0)
		return &vctab[i];
	return nil;
}

void
scanvcpdu(uchar *p, uchar* ep, int chanid)
{
	int flags, len, rem;
	uchar *q, *eq;
	Vchan* vc;

	vc = lookupvc(chanid);
	if(vc == nil)
		return;

	len = GLONG(p+0);
	flags = GLONG(p+4);
	p += 8;
	if(len < 0 || len > 8*1024*1024){
		werrstr("bad length in virtual channel PDU header");
		fprint(2, "scanvcpdu: %r\n");
		return;
	}
	if(flags&CFfirst){
		vc->defragging = 1;
		vc->pos = 0;
	}

	if(!vc->defragging){
		vc->fn(p, ep);
		return;
	}

	vc->buf = erealloc(vc->buf, len);
	vc->nb = len;
	q = vc->buf + vc->pos;
	eq = vc->buf + len;
	rem = ep-p;
	if(rem > eq-q)
		rem = eq-q;
	memcpy(q, p, rem);
	vc->pos += rem;
	if(flags&CFlast){
		q = vc->buf;
		vc->fn(q, eq);
		free(vc->buf);
		vc->buf = nil;
		vc->nb = 0;
		vc->defragging = 0;
	}
}

int
sendvc(char* cname, uchar* a, int n)
{
	int sofar, chunk;
	int flags;
	int nb, len, ndata;
	uchar buf[40+ChanChunkLen];
	uchar *p, *q;
	Vchan* vc;
	int chanid;
	
	vc = namevc(cname);
	if(vc == nil){
		werrstr("%s: no such vchannel", cname);
		return -1;
	}
	chanid = vc->mcsid;
	if(chanid < 0)
		return -1;
	if(n < 0)
		return -1;

	p = a;
	nb = sizeof(buf);
	flags = CFfirst | CFshowproto;

	for(sofar=0; sofar<n; sofar += chunk){
		chunk = n-sofar;
		if(chunk > ChanChunkLen)
			chunk = ChanChunkLen;
		else
			flags |= CFlast;
		ndata = chunk+8;
		q = prebuf(buf, nb, ndata, chanid, 0);
		if(q == nil)
			return -1;
		PLONG(q+0, n);
		PLONG(q+4, flags);
		memcpy(q+8, p+sofar, chunk);
		len = q-buf+ndata;
		writen(rd.fd, buf, len);
		flags &= ~CFfirst;
	}
	return n;
}
