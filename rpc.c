#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

int	mcsconnect(Rdp*);
int	attachuser(Rdp*);
int	joinchannel(Rdp*,int,int);

int
x224handshake(Rdp* c)
{
	Msg t, r;

	t.type = Xconnect;
	t.negproto = ProtoTLS;
	if(writemsg(c, &t) <= 0)
		return -1;
	if(readmsg(c, &r) <= 0)
		return -1;
	if(r.type != Xconnected){
		werrstr("X.224: protocol botch");
		return -1;
	}
	if(r.negproto&ProtoTLS == 0){
		werrstr("server refused STARTTLS");
		return -1;
	}
	c->sproto = r.negproto;

	if(starttls(c) < 0)
		return -1;

	return 0;
}

int
x224hangup(Rdp* c)
{
	Msg t;

	t.type = Xhangup;
	return writemsg(c, &t);
}

int
mcsconnect(Rdp* c)
{
	Msg t, r;
		
	t.type = Mconnect;
	t.ver = 0x80004;	/* RDP5 */
	t.depth = c->depth;
	t.xsz = c->xsz;
	t.ysz = c->ysz;
	t.sysname = c->local;
	t.sproto = c->sproto;
	t.wantconsole = c->wantconsole;
	t.vctab = c->vc;
	t.nvc = c->nvc;
	if(writemsg(c, &t) <= 0)
		sysfatal("Connect Initial: writemsg: %r");
	if(readmsg(c, &r) <= 0)
		sysfatal("Connect Response: readmsg: %r");
	if(r.type != Mconnected)
		sysfatal("Connect Response: protocol botch");
	if(r.ver < t.ver)
		sysfatal("Connect Response: unsupported RDP protocol version %x", r.ver);

	return 0;
}

void
erectdom(Rdp* c)
{
	Msg t;
	
	t.type = Merectdom;
	if(writemsg(c, &t) <= 0)
		sysfatal("Erect Domain: writemsg: %r");
}

int
attachuser(Rdp* c)
{
	Msg t, r;

	t.type = Mattach;
	if(writemsg(c, &t) <= 0)
		sysfatal("attachuser: writemsg: %r");
	if(readmsg(c, &r) <= 0)
		sysfatal("attachuser: readmsg: %r");
	if(r.type != Mattached)
		sysfatal("attachuser: protocol botch");

	c->mcsuid = r.mcsuid;
	c->userchan = r.mcsuid;
	return 0;
}

int
joinchannel(Rdp* c, int mcsuid, int chanid)
{
	Msg t, r;
	
	t.type = Mjoin;
	t.mcsuid = mcsuid;
	t.chanid = chanid;
	if(writemsg(c, &t) <= 0)
		sysfatal("Channel Join: writemsg: %r");
	if(readmsg(c, &r) <= 0)
		sysfatal("Channel Join: readmsg: %r");
	if(r.type != Mjoined)
		sysfatal("Channel Join: protocol botch");

	/* BUG: ensure the returned and requested chanids match */

	return 0;
}

int
rdphandshake(Rdp* c)
{
	int i, nv;
	Vchan* v;
	Msg r;
	Share u;

	v = c->vc;
	nv = c->nvc;

	if(mcsconnect(c) < 0)
		return -1;
	erectdom(c);
	if(attachuser(c) < 0)
		return -1;

	if(joinchannel(c, c->mcsuid, c->userchan) < 0)
		return -1;
	if(joinchannel(c, c->mcsuid, GLOBALCHAN) < 0)
		return -1;
	for(i = 0; i < nv; i++)
		if(joinchannel(c, c->mcsuid, v[i].mcsid) < 0)
			return -1;

	sendclientinfo(c);
	for(;;){
		if(readmsg(c, &r) <= 0)
			return -1;
		switch(r.type){
		case Mclosing:
			werrstr("Disconnect Provider Ultimatum");
			return -1;
		case Ldone:
			break;
		case Lneedlicense:
		case Lhavechal:
			respondlicense(c, &r);
			break;
		case Aupdate:
			if(r.getshare(&u, r.data, r.ndata) < 0)
				return -1;
			switch(u.type){
			default:
				fprint(2, "handshake: unhandled %d\n", u.type);
				break;
			case ShEinfo:	/* do we really expect this here? */
				c->hupreason = u.err;
				break;
			case ShActivate:
				activate(c, &u);
				return 0;
			}
		}
	}
}

/* 2.2.1.13.1 Server Demand Active PDU */
void
activate(Rdp* c, Share* as)
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

	confirmactive(c);
	finalhandshake(c);

	passinput(c, 0, InputSync, 0, 0, 0);
}

void
deactivate(Rdp* c, Share*)
{
	c->active = 0;
}

void
finalhandshake(Rdp* c)
{
	Msg r;
	Share u;

	assync(c);
	asctl(c, CAcooperate);
	asctl(c, CAreqctl);
	asfontls(c);

	for(;;){
		if(readmsg(c, &r) <= 0)
			sysfatal("activate: readmsg: %r");
		switch(r.type){
		default:
			fprint(2, "activate: unhandled PDU type %d\n", u.type);
			break;
		case Mclosing:
			fprint(2, "disconnecting early");
			return;
		case Aupdate:
			if(r.getshare(&u, r.data, r.ndata) < 0)
				sysfatal("activate: r.getshare: %r");
			switch(u.type){
			default:
				fprint(2, "activate: unhandled ASPDU type %d\n", u.type);
				break;
			case ShSync:
			case ShCtl:
				/* responses to the assync(). asctl() calls above */
				break;
			case ShFmap:
				/* finalized - we're good */
				return;
			}
		}
	}
}

void
sendclientinfo(Rdp* c)
{
	Msg t;

	t.type = Dclientinfo;
	t.mcsuid = c->mcsuid;
	t.dom = c->windom;
	t.user = c->user;
	t.pass = c->passwd;
	t.rshell = c->shell;
	t.rwd = c->rwd;
	t.dologin = (strlen(c->user) > 0);

	if(writemsg(c, &t) <= 0)
		sysfatal("sendclientinfo: %r");
}

void
confirmactive(Rdp* c)
{
	Msg	t;

	t.type = Mactivated;
	t.originid = c->srvchan;
	t.mcsuid = c->userchan;
	t.shareid = c->shareid;
	t.xsz = c->xsz;
	t.ysz = c->ysz;
	t.depth = c->depth;
	if(writemsg(c, &t) <= 0)
		sysfatal("confirmactive: %r");
}

void
respondlicense(Rdp *c, Msg *r)
{
	Msg t;

	switch(r->type){
	default:
		return;
	case Lneedlicense:
		t.type = Lreq;
		t.sysname = c->local;
		t.user = c->user;
		t.originid = c->userchan;
		break;
	case Lhavechal:
			fprint(2, "unhandled Lhavechal\n");
		t.type = Lnolicense;
		t.originid = c->userchan;
		break;
	}

	if(writemsg(c, &t) < 0)
		sysfatal("respondlicense: writemsg failed: %r");
}


void
assync(Rdp *c)
{
	Msg t;

	t.type = Async;
	t.mcsuid = c->srvchan;
	t.originid = c->userchan;
	t.shareid = c->shareid;
	if(writemsg(c, &t) <= 0)
		sysfatal("assync: %r");
}

void
asctl(Rdp* c, int action)
{
	Msg t;

	t.type = Actl;
	t.originid = c->userchan;
	t.shareid = c->shareid;
	t.action = action;
	if(writemsg(c, &t) <= 0)
		sysfatal("asctl: %r");
}

void
asfontls(Rdp* c)
{
	Msg t;

	t.type = Afontls;
	t.originid = c->userchan;
	t.shareid = c->shareid;
	if(writemsg(c, &t) <= 0)
		sysfatal("asfontls: %r");
}

void
passinput(Rdp* c, ulong msec, int typ, int f, int a, int b)
{
	Msg t;

	t.type = Ainput;
	t.originid = c->userchan;
	t.shareid = c->shareid;
	t.msec = msec;
	t.mtype = typ;
	t.flags = f;
	t.iarg[0] = a;
	t.iarg[1] = b;
	if(writemsg(c, &t) <= 0)
		sysfatal("passinput: %r");
}

void
turnupdates(Rdp* c, int allow)
{
	Msg t;

	t.type = Dsupress;
	t.originid = c->userchan;
	t.shareid = c->shareid;
	t.xsz = c->xsz;
	t.ysz = c->ysz;
	t.allow = allow;
	writemsg(c, &t);
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
			deactivate(c, &u);
			break;
		case ShActivate:	// server may initiate capability re-exchange
			activate(c, &u);
			break;
		case ShEinfo:
			c->hupreason = u.err;
			break;
		case ShUorders:
			draworders(c, &u);
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
