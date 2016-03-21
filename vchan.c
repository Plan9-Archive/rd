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
	ChanInited=	0x80000000,	/* 2.2.1.3.4.1 Channel Definition Structure */
	ChanShowproto=	0x00200000,
	
	ChanChunkLen=	1600,	/* 2.2.6.1 Virtual Channel PDU */

	CFfirst=	0x01,	/* 2.2.6.1.1 Channel PDU Header */
	CFlast=	0x02,
	CFshowproto=	0x10,
};

static
Vchan vctab[] =
{
	{
		.mcsid = GLOBALCHAN + 1,	/* iota */
		.name = "CLIPRDR",
		.fn = clipvcfn,
		.flags = ChanInited | ChanShowproto,
	},
};
static
uint nvc = nelem(vctab);

void
initvc(Rdp* c)
{
	c->vc = vctab;
	c->nvc = nvc;
}

static Vchan*
lookupvc(int mcsid)
{
	int i;
	for(i=0; i<nvc; i++)
	if(vctab[i].mcsid == mcsid)
		return &vctab[i];
	return nil;
}

static Vchan*
namevc(char* name)
{
	int i;
	for(i=0; i<nvc; i++)
	if(strcmp(vctab[i].name, name) == 0)
		return &vctab[i];
	return nil;
}

void
scanvcdata(Rdp* c, Msg* m)
{
	Vchan* vc;
	int n;
	
	vc = lookupvc(m->chanid);
	if(vc == nil)
		return;

	if(m->flags&CFfirst)
		vc->pos = 0;

	vc->buf = erealloc(vc->buf, m->len);
	vc->nb = m->len;
	n = vc->nb - vc->pos;
	if(n > m->ndata)
		n = m->ndata;
	memcpy(vc->buf+vc->pos, m->data, n);
	vc->pos += n;

	if(m->flags&CFlast){
		vc->fn(c, vc->buf, vc->nb);
		free(vc->buf);
		vc->buf = nil;
		vc->nb = 0;
	}
}

int
sendvc(Rdp* c, char* cname, uchar* a, int n)
{
	int sofar, chunk;
	Vchan* vc;
	Msg t;
	
	if(n < 0)
		return -1;

	vc = namevc(cname);
	if(vc == nil){
		werrstr("%s: no such vchannel", cname);
		return -1;
	}
	if(vc->mcsid < 0)
		return -1;

	t.type = Mvcdata;
	t.originid = c->userchan;
	t.chanid = vc->mcsid;
	t.flags = CFfirst | CFshowproto;
	t.len = n;
	t.data = a;

	for(sofar=0; sofar<n; sofar += chunk){
		chunk = n-sofar;
		if(chunk > ChanChunkLen)
			chunk = ChanChunkLen;
		else
			t.flags |= CFlast;

		t.data = a+sofar;
		t.ndata = chunk;
		writemsg(c, &t);

		t.flags &= ~CFfirst;
	}
	return n;
}

