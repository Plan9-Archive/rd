/*
 * [MS-RDPECLIP]: Remote Desktop Protocol: Clipboard Virtual Channel Extension
 * http://msdn.microsoft.com/en-us/library/cc241066.aspx
 * 
 * Standard Clipboard Formats
 * http://msdn.microsoft.com/en-us/library/windows/desktop/ff729168(v=vs.85).aspx
 */
#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

static char	cliprdr[]				= "CLIPRDR";
static void		cliprequest(uint);

enum
{
	CFunicode	= 13,
};

enum
{
	FlagOk			= (1<<0),
	FlagErr			= (1<<1),
};

enum
{
	ClipReady			= 1,
	ClipAnnounce		= 2,
	ClipNoted			= 3,
	ClipReq			= 4,
	ClipResp			= 5,
};

typedef	struct	Clipmsg Clipmsg;
struct Clipmsg
{
	uint		type;
	uint		flags;
	uint		fmtid;
	uchar	*data;
	uint		ndata;
};

static	int	clipputmsg(Clipmsg*,uchar*,int);
static	int	clipgetmsg(Clipmsg*,uchar*,int);

static	void	clipattached(Clipmsg*);
static	void	clipnoted(Clipmsg*);
static	void	cliprequested(Clipmsg*);
static	void	clipprovided(Clipmsg*);

static	void	(*clipcall[])(Clipmsg*) =
{
	[ClipReady]=		clipattached,
	[ClipAnnounce]=	clipnoted,
	[ClipReq]=		cliprequested,
	[ClipResp]=		clipprovided,
};

void
clipvcfn(uchar* p, uchar* ep)
{
	Clipmsg tx;

	if(clipgetmsg(&tx, p, ep-p) < 0)
		return;
	if(tx.flags&FlagErr)
		return;
	if(tx.type >= nelem(clipcall))
		return;
	if(clipcall[tx.type] == nil)
		return;
	clipcall[tx.type](&tx);
}

void
clipannounce(void)
{
	Clipmsg r;
	uchar a[44];
	int n;

	r.type = ClipAnnounce;
	r.flags = 0;
	r.fmtid = CFunicode;
	n = clipputmsg(&r, a, sizeof(a));
	if(sendvc(cliprdr, a, n) < 0)
		fprint(2, "clipannounce: %r\n");
}

static void
cliprequest(uint fmtid)
{
	Clipmsg r;
	uchar a[12];
	int n;

	r.type = ClipReq;
	r.flags = 0;
	r.fmtid = fmtid;
	n = clipputmsg(&r, a, sizeof(a));
	if(sendvc(cliprdr, a, n) < 0)
		fprint(2, "cliprequest: %r\n");
}

static void
clipattached(Clipmsg*)
{
	clipannounce();
}

static void
clipnoted(Clipmsg *m)
{
	Clipmsg r;
	uchar a[8];
	int n;

	if(m->fmtid)
		cliprequest(m->fmtid);

	r.type = ClipNoted;
	r.flags = FlagOk;
	n = clipputmsg(&r, a, sizeof(a));
	if(sendvc(cliprdr, a, n) < 0)
		fprint(2, "clipnoted: %r\n");
}

static void
cliprequested(Clipmsg *m)
{
	Clipmsg r;
	char* s;
	uchar *b;
	int n, ns, nb;

	b = emalloc(8);
	nb = 0;

	r.type = ClipResp;
	r.flags = FlagOk;
	if(m->fmtid != CFunicode){
		r.flags = FlagErr;
		goto Respond;
	}

	s = getsnarf(&ns);
	if(s == nil)
		goto Respond;
	nb = ns*4;
	b = erealloc(b, nb+8);
	nb = toutf16(b+8, nb, s, ns);
	free(s);
  Respond:
	r.data = b+8;
	r.ndata = nb;
	n = clipputmsg(&r, b, nb+8);
	if(sendvc(cliprdr, b, n) < 0)
		fprint(2, "cliprequested: %r\n");
	free(b);
}

static void
clipprovided(Clipmsg *m)
{
	char *s;
	int n, ns;

	ns = m->ndata*UTFmax/2;
	s = emalloc(ns);
	n = fromutf16(s, ns, m->data, m->ndata);
	putsnarf(s, n);
	free(s);
	return;
}

static int
clipputmsg(Clipmsg *m, uchar *a, int n)
{
	if(n < 8){
		werrstr(Esmall);
		return -1;
	}
	PSHORT(a+0, m->type);
	PSHORT(a+2, m->flags);
	switch(m->type){
	case ClipAnnounce:
		m->data = a+8;
		m->ndata = 4+32;
		if(8 + m->ndata > n){
			werrstr(Esmall);
			return -1;
		}
		PLONG(a+8, m->fmtid);
		memset(a+12, 0, 32);	/* fmt name - who cares? */
		break;
	case ClipReq:
		m->data = a+8;
		m->ndata = 4;
		if(8 + m->ndata > n){
			werrstr(Esmall);
			return -1;
		}
		PLONG(a+8, m->fmtid);
		break;
	case ClipNoted:
		m->ndata = 0;
		m->data = a+8;
		break;
	}
	if(8 + m->ndata > n){
		werrstr(Esmall);
		return -1;
	}
	PLONG(a+4, m->ndata);
	memcpy(a+8, m->data, m->ndata);
	return 8+m->ndata;
}

static int
clipgetmsg(Clipmsg *m, uchar *p, int n)
{
	uint len, fmtid;
	uchar *ep;

	if(8 > n){
		werrstr(Eshort);
		return -1;
	}
	m->type = GSHORT(p);
	m->flags = GSHORT(p+2);
	len = GLONG(p+4);
	if(8+len > n){
		werrstr(Eshort);
		return -1;
	}
	m->ndata = len;
	m->data = p+8;

	switch(m->type){
	case ClipReq:
		if(len < 4){
			werrstr(Eshort);
			return -1;
		}
		m->fmtid = GLONG(m->data);
		break;
	case ClipAnnounce:
		m->fmtid = 0;
		p += 8;
		ep = p+len;
		while(p < ep){
			fmtid = GLONG(p);
			if(fmtid == CFunicode){
				m->fmtid = fmtid;
				break;
			}
			p += 4+32*1;
		}
		break;
	}
	return 8+len;
}
