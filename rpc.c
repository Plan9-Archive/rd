#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

int	mcsconnect(Rdp*);
int	attachuser(Rdp*);
int	joinchannel(Rdp*,int,int);

int
x224connect(Rdp* c)
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

	if(starttls(c) < 0)
		return -1;

	c->sproto = r.negproto;
	return 0;
}

int
x224disconnect(Rdp* c)
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
	int i;
	int mcsuid, userchan;
	Vchan* v;
	int nv;
	Msg r;
	Share u;

	v = c->vc;
	nv = c->nvc;

	if(mcsconnect(c) < 0)
		return -1;
	erectdom(c);
	if(attachuser(c) < 0)
		return -1;

	mcsuid = c->mcsuid;
	userchan = c->userchan;

	if(joinchannel(c, mcsuid, userchan) < 0)
		return -1;
	if(joinchannel(c, mcsuid, GLOBALCHAN) < 0)
		return -1;
	for(i = 0; i < nv; i++)
		if(joinchannel(c, mcsuid, v[i].mcsid) < 0)
			return -1;

	sendclientinfo(c);
	for(;;){
		if(readmsg(c, &r) <= 0)
			return -1;
		switch(r.type){
		case Mclosing:
			werrstr("Disconnect Provider Ultimatum");
			return -1;
		case Aflow:
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
			case ShEinfo:
				c->hupreason = u.err;
				break;
			case ShActivate:
				activating(c, &u);
				confirmactive(c);
				passinput(c, 0, InputSync, 0, 0, 0);
				assync(c);
				asctl(c, CAcooperate);
				asctl(c, CAreqctl);
				asfontls(c);
				break;
			case ShDeactivate:
				deactivating(c, &u);
				break;
			case ShSync:
			case ShCtl:
				break;
			case ShFmap:
				return 0;
				break;
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
passinput(Rdp* c, ulong msec, int mtype, int iflags, int iarg1, int iarg2)
{
	Msg t;

	t.type = Ainput;
	t.originid = c->userchan;
	t.shareid = c->shareid;
	t.mtype = mtype;
	t.msec = msec;
	t.flags = iflags;
	t.iarg1 = iarg1;
	t.iarg2 = iarg2;
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


