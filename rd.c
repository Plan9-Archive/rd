#include <u.h>
#include <libc.h>
#include <auth.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Rdp conn = {
	.fd = -1,
	.depth = 16,
	.windom = "",
	.passwd = "",
	.shell = "",
	.rwd = "",
};

char Eshort[]=	"short data";
char Esmall[]=	"buffer too small";
char Ebignum[]=	"number too big";

static void
usage(void)
{
	fprint(2, "usage: rd [-0A] [-T title] [-a depth] [-c wdir] [-d dom] [-k keyspec] [-n term] [-s shell] [net!]server[!port]\n");
	exits("usage");
}

long
writen(int fd, void* buf, long nbytes)
{
	long n, sofar;

	sofar = 0;
	while(sofar < nbytes){
		n = write(fd, buf, nbytes-sofar);
		if(n <= 0)
			break;
		sofar += n;
	}
	return sofar;
}

static int
startmouseproc(Rdp* c)
{
	int mpid;

	switch(mpid = rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		break;
	default:
		return mpid;
	}
	atexit(atexitkiller);
	readdevmouse(c);
	exits("mouse eof");
	return 0;
}

static int
startkbdproc(Rdp* c)
{
	int pid;
	switch(pid = rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		break;
	default:
		return pid;
	}
	atexit(atexitkiller);
	readkbd(c);
	exits("kbd eof");
	return 0;
}

static int
startsnarfproc(Rdp* c)
{
	int pid;

	switch(pid = rfork(RFPROC|RFMEM)){
	case -1:
		sysfatal("rfork: %r");
	case 0:
		break;
	default:
		return pid;
	}
	atexit(atexitkiller);
	initsnarf();
	pollsnarf(c);
	exits("snarf eof");
	return 0;
}

static int killpid[32];
static int nkillpid;

void
atexitkiller(void)
{
	int i, pid;

	pid = getpid();
	for(i=0; i<nkillpid; i++)
		if(pid != killpid[i])
			postnote(PNPROC, killpid[i], "kill");
}
void
atexitkill(int pid)
{
	killpid[nkillpid++] = pid;
}

void
main(int argc, char *argv[])
{
	int doauth;
	char *server, *addr, *keyspec, *label;
	UserPasswd *creds;
	Rdp* c;

	c = &conn;

	keyspec = "";
	doauth = 1;

	ARGBEGIN {
	case 'A':
		doauth = 0;
		break;
	case 'k':
		keyspec = EARGF(usage());
		break;
	case 'T':
		label = strdup(EARGF(usage()));
		break;
	case 'd':
		c->windom = strdup(EARGF(usage()));
		break;
	case 's':
		c->shell = strdup(EARGF(usage()));
		break;
	case 'c':
		c->rwd = strdup(EARGF(usage()));
		break;
	case 'a':
		c->depth = atol(EARGF(usage()));
		break;
	case '0':
		c->wantconsole = 1;
		break;
	default:
		usage();
	} ARGEND

	if (argc != 1)
		usage();

	server = argv[0];

	c->local = getenv("sysname");
	c->user = getenv("user");
	if(c->local == nil)
		sysfatal("set $sysname or use -n\n");
	if(c->user == nil)
		sysfatal("set $user");
	if(doauth){
		creds = auth_getuserpasswd(auth_getkey, "proto=pass service=rdp %s", keyspec);
		if(creds == nil)
			fprint(2, "factotum: %r\n");
		else {
			c->user = creds->user;
			c->passwd = creds->passwd;
		}
	}else
		c->user = "";

	initvc(c);

	addr = netmkaddr(server, "tcp", "3389");
	c->fd = dial(addr, nil, nil, nil);
	if(c->fd < 0)
		sysfatal("dial %s: %r", addr);
	if(x224handshake(c) < 0)
		sysfatal("X.224 handshake: %r");

	if(label == nil)
		label = smprint("rd %s", server);
	if(initdraw(drawerror, nil, label) < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	unlockdisplay(display);

	c->ysz = Dy(screen->r);
	c->xsz = (Dx(screen->r) +3) & ~3;

	if(rdphandshake(c) < 0)
		sysfatal("handshake: %r");

	atexit(atexitkiller);
	atexitkill(getpid());
	atexitkill(startmouseproc(c));
	atexitkill(startkbdproc(c));
	atexitkill(startsnarfproc(c));

	readnet(c);

	x224hangup(c);
	if(!c->active)
		exits(nil);
	if(c->hupreason)
		sysfatal("disconnect reason code %d", c->hupreason);
	sysfatal("hangup");
}

void
readnet(Rdp* c)
{
	Msg r;

	for(;;){
		if(readmsg(c, &r) <= 0)
			return;

		switch(r.type){
		case Mclosing:
			return;
		case Mvcdata:
			scanvcdata(c, &r);
			break;
		case Aupdate:
			scanupdates(c, &r);
			break;
		case 0:
			fprint(2, "unsupported PDU\n");
			break;
		default:
			fprint(2, "r.type %d is not expected\n", r.type);
		}
	}
}

void
scanupdates(Rdp* c, Msg* m)
{
	int n;
	uchar *p, *ep;
	Share u;

	p = m->data;
	ep = m->data + m->ndata;

	for(; p < ep; p += n){
		n = m->getshare(&u, p, ep-p);
		if(n < 0)
			sysfatal("scanupdates: %r");

		switch(u.type){
		default:
			if(u.type != 0)
				fprint(2, "scanupdates: unhandled %d\n", u.type);
			break;
		case ShDeactivate:
			deactivating(c, &u);
			break;
		case ShActivate:	// server may engage capability re-exchange
			activating(c, &u);
			break;
		case ShEinfo:
			c->hupreason = u.err;
			break;
		case ShUorders:
			scanorders(c, &u);
			break;
		case ShUimg:
			scanimgupdate(c, &u);
			break;
		case ShUcmap:
			scancmap(c, &u);
			break;
		case ShUwarp:
			warpmouse(u.x, u.y);
			break;
		case Aflow:
			break;
		}
	}
}

/* 2.2.9.1.1.3.1.2.1 Bitmap Update Data (TS_UPDATE_BITMAP_DATA) */
void
scanimgupdate(Rdp *c, Share* as)
{
	uchar* p, *ep;
	int n, err, nr;
	static Image* img;
	Rectangle r, rs, d;
	Imgupd iu;

	assert(as->type == ShUimg);
	p = as->data;
	ep = as->data + as->ndata;
	nr = as->nrect;

	rs = rectaddpt(Rpt(ZP, Pt(c->xsz, c->ysz)), screen->r.min);

	lockdisplay(display);

	if(img==nil || !eqrect(img->r, rs)){
		if(img != nil)
			freeimage(img);
		img = allocimage(display, rs, c->chan, 0, DNofill);
		if(img == nil)
			sysfatal("%r");
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
	unlockdisplay(display);
}

void
scancmap(Rdp* c, Share* as)
{
	int i, n;
	uchar *p,  *ep, *cmap;

	p = as->data;
	ep = as->data + as->ndata;
	cmap = c->cmap;

	n = GSHORT(p+4);
	p += 8;
	if(n > sizeof(c->cmap)){
		fprint(2, "scancmap: data too big");
		return;
	}
	if(p+3*n > ep)
		sysfatal(Eshort);
	for(i = 0; i<n; p+=3)
		cmap[i++] = rgb2cmap(p[0], p[1], p[2]);
}

void*
emalloc(ulong n)
{
	void *b;

	b = mallocz(n, 1);
	if(b == nil)
		sysfatal("out of memory allocating %lud: %r", n);
	setmalloctag(b, getcallerpc(&n));
	return b;
}

void*
erealloc(void *a, ulong n)
{
	void *b;

	b = realloc(a, n);
	if(b == nil)
		sysfatal("out of memory re-allocating %lud: %r", n);
	setrealloctag(b, getcallerpc(&a));
	return b;
}

char*
estrdup(char *s)
{
	char *b;

	b = strdup(s);
	if(b == nil)
		sysfatal("strdup: %r");
	setmalloctag(b, getcallerpc(&s));
	return b;
}
