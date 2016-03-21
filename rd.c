#include <u.h>
#include <libc.h>
#include <auth.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

Rdp rd = {
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
startmouseproc(void)
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
	readdevmouse(&rd);
	exits("mouse eof");
	return 0;
}

static int
startkbdproc(void)
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
	readkbd(&rd);
	exits("kbd eof");
	return 0;
}

static int
startsnarfproc(void)
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
	pollsnarf(&rd);
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
	int doauth, fd;
	char *server, *addr, *keyspec;
	UserPasswd *creds;

	rd.local = getenv("sysname");
	rd.user = getenv("user");

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
		rd.label = strdup(EARGF(usage()));
		break;
	case 'd':
		rd.windom = strdup(EARGF(usage()));
		break;
	case 's':
		rd.shell = strdup(EARGF(usage()));
		break;
	case 'c':
		rd.rwd = strdup(EARGF(usage()));
		break;
	case 'n':
		rd.local = estrdup(EARGF(usage()));
		break;
	case 'a':
		rd.depth = atol(EARGF(usage()));
		break;
	case '0':
		rd.wantconsole = 1;
		break;
	default:
		usage();
	} ARGEND

	if (argc != 1)
		usage();

	server = argv[0];
	if(rd.local == nil)
		sysfatal("set $sysname or use -n\n");
	if(rd.user == nil)
		sysfatal("set $user");
	if (rd.label == nil)
		rd.label = smprint("rd %s", server);

	if(doauth){
		creds = auth_getuserpasswd(auth_getkey, "proto=pass service=rdp %s", keyspec);
		if(creds == nil)
			fprint(2, "factotum: %r\n");
		else {
			rd.user = creds->user;
			rd.passwd = creds->passwd;
		}
	}else
		rd.user = "";
	initvc(&rd);

	addr = netmkaddr(server, "tcp", "3389");
	fd = dial(addr, nil, nil, nil);
	if(fd < 0)
		sysfatal("dial %s: %r", addr);
	rd.fd = fd;
	if(x224connect(&rd) < 0)
		sysfatal("connect: %r");

	if(initdraw(drawerror, nil, rd.label) < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	unlockdisplay(display);

	rd.ysz = Dy(screen->r);
	rd.xsz = (Dx(screen->r) +3) & ~3;

	if(rdphandshake(&rd) < 0)
		sysfatal("handshake: %r");

	atexit(atexitkiller);
	atexitkill(getpid());
	atexitkill(startmouseproc());
	atexitkill(startkbdproc());
	atexitkill(startsnarfproc());

	readnet(&rd);

	x224disconnect(&rd);

	if(!rd.active)
		exits(nil);
	if(rd.hupreason)
		sysfatal("disconnect reason code %d", rd.hupreason);
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
		case ShDeactivate:
			deactivating(c, &u);
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
		}
	}
}

/* 2.2.1.13.1 Server Demand Active PDU */
void
activating(Rdp* c, Share* as)
{
	Caps rcaps;

	if(getcaps(&rcaps, as->data, as->ndata) < 0)
		sysfatal("getcaps: %r");
	if(!rcaps.canrefresh)
		sysfatal("server can not Refresh Rect PDU");
	if(!rcaps.cansupress)
		sysfatal("server can not Suppress Output PDU");
	if(!rcaps.bitmap)
		sysfatal("server concealed their Bitmap Capabilities");

	switch(rcaps.depth){
	default:	sysfatal("Unsupported server color depth: %uhd\n", rcaps.depth);
	case 8:	c->chan = CMAP8; break;
	case 15:	c->chan = RGB15; break;
	case 16:	c->chan = RGB16; break;
	case 24:	c->chan = RGB24; break;
	case 32:	c->chan = XRGB32; break;
	}
	c->depth = rcaps.depth;
	c->xsz = rcaps.xsz;
	c->ysz = rcaps.ysz;
	c->srvchan = as->source;
	c->shareid = as->shareid;
	c->active = 1;
}

void
deactivating(Rdp* c, Share*)
{
	c->active = 0;
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
