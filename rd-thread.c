#include <u.h>
#include <libc.h>
#include <draw.h>
#include <thread.h>
#include <keyboard.h>
#include <mouse.h>
#include <cursor.h>
#include <auth.h>
#include "dat.h"
#include "fns.h"

#define	STACK	8192

Rdp conn = {
	.fd = -1,
	.depth = 16,
	.windom = "",
	.passwd = "",
	.shell = "",
	.rwd = "",
};
Mousectl		*mousectl;
Keyboardctl	*keyboardctl;

char Eshort[]=	"short data";
char Esmall[]=	"buffer too small";
char Ebignum[]=	"number too big";

void	sendmouse(Rdp* c, Mouse m);

static void	keyboardthread(void*);
static void	mousethread(void*);
static void	snarfthread(void*);

static void
usage(void)
{
	fprint(2, "usage: rd [-0A] [-T title] [-a depth] [-c wdir] [-d dom] [-k keyspec] [-n term] [-s shell] [net!]server[!port]\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char *argv[])
{
	int doauth;
	char *server, *addr, *keyspec;
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
		c->label = strdup(EARGF(usage()));
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
	if(c->label == nil)
		c->label = smprint("rd %s", server);

	initvc(c);

	addr = netmkaddr(server, "tcp", "3389");
	c->fd = dial(addr, nil, nil, nil);
	if(c->fd < 0)
		sysfatal("dial %s: %r", addr);
	if(x224handshake(c) < 0)
		sysfatal("X.224 handshake: %r");
	initscreen(c);
	if(rdphandshake(c) < 0)
		sysfatal("handshake: %r");

	mousectl = initmouse(nil, screen);
	if(mousectl == nil){
		fprint(2, "rd: can't initialize mouse: %r\n");
		exits("mouse");
	}
	keyboardctl = initkeyboard(nil);
	if(keyboardctl == nil){
		fprint(2, "rd: can't initialize keyboard: %r\n");
		exits("keyboard");
	}

	proccreate(keyboardthread, c, STACK);
	proccreate(mousethread, c, STACK);
	proccreate(snarfthread, c, STACK);

	threadsetname("mainthread");
	readnet(c);

	x224hangup(c);
	if(!c->active)
		threadexitsall(nil);
	if(c->hupreason)
		sysfatal("disconnect reason code %d", c->hupreason);
	sysfatal("hangup");
}

void
initscreen(Rdp* c)
{
	if(initdraw(drawerror, nil, c->label) < 0)
		sysfatal("initdraw: %r");
	display->locking = 1;
	unlockdisplay(display);
	c->ysz = Dy(screen->r);
	c->xsz = (Dx(screen->r) +3) & ~3;
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
		case Mvchan:
			scanvc(c, &r);
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
			drawimgupdate(c, &u);
			break;
		case ShUcmap:
			loadcmap(c, &u);
			break;
		case ShUwarp:
			warpmouse(u.x, u.y);
			break;
		case Aflow:
			break;
		}
	}
}

void
drawimgupdate(Rdp *c, Share* as)
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

static void
keyboardthread(void* v)
{
	Rune r;
	Rdp* c;

	c = v;
	threadsetname("keyboardthread");
	for(;;){
		recv(keyboardctl->c, &r);
		sendkbd(c, r);
	}
}

static void
mousethread(void* v)
{
	Rdp* c;
	enum { MResize, MMouse, NMALT };
	static Alt alts[NMALT+1];

	c = v;
	threadsetname("mousethread");
	alts[MResize].c = mousectl->resizec;
	alts[MResize].v = nil;
	alts[MResize].op = CHANRCV;
	alts[MMouse].c = mousectl->c;
	alts[MMouse].v = &mousectl->Mouse;
	alts[MMouse].op = CHANRCV;

	for(;;){
		switch(alt(alts)){
		case MResize:
			eresized(c, 1);
			break;
		case MMouse:
			sendmouse(c, mousectl->Mouse);
			break;
		}
	}
}

void
warpmouse(int x, int y)
{
	moveto(mousectl, Pt(x, y));
}

static void
snarfthread(void* v)
{
	Rdp* c;

	c = v;
	threadsetname("snarfthread");
	initsnarf();
	pollsnarf(c);
	threadexits("snarf eof");
}
