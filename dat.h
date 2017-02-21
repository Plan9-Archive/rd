typedef	struct	Rdp Rdp;
typedef	struct	Vchan Vchan;
typedef	struct	Msg Msg;
typedef	struct	Share Share;
typedef	struct	Caps Caps;
typedef	struct	Imgupd Imgupd;

#define	GSHORT(p)	((p)[0]|((p)[1]<<8))
#define	GSHORTB(p)	((p)[0]<<8|((p)[1]))
#define	GLONG(p) 	((p)[0]|((p)[1]<<8)|((p)[2]<<16)|((p)[3]<<24))
#define	GLONGB(p)	(((p)[0]<<24)|((p)[1]<<16)|((p)[2]<<8)|(p)[3])
#define	PSHORT(p,v)	(p)[0]=(uchar)(v);(p)[1]=(uchar)((v)>>8)
#define	PSHORTB(p,v)	(p)[0]=(uchar)((v)>>8);(p)[1]=(uchar)(v)
#define	PLONG(p,v)	\
	(p)[0]=(uchar)(v);(p)[1]=(uchar)((v)>>8);\
	(p)[2]=(uchar)((v)>>16);(p)[3]=(uchar)((v)>>24)
#define	PLONGB(p,v)	\
	(p)[0]=(uchar)((v)>>24);(p)[1]=(uchar)((v)>>16);\
	(p)[2]=(uchar)((v)>>8);(p)[3]=(uchar)(v)

#define	MIN(x,y)		(((x) < (y)) ? (x) : (y))

enum
{
	MAXTPDU=	16386,	/* max TPDU size */
};

struct Rdp
{
	int		fd;			/* connection I/O descriptor */
	long		sproto;		/* magic to bounce back to server */
	char		*label;		/* window label */
	char		*local;		/* local system name */
	char		*user;		/* user name for auto logon  */
	char		*windom;		/* domain for auto logon */
	char		*passwd;		/* password for auto logon (sic) */
	char		*shell;		/* remote shell override */
	char		*rwd;		/* remote working directory */
	int		xsz;			/* rfb dimensions */
	int		ysz;			/* rfb dimensions */
	int		depth;		/* rfb color depth */
	int		hupreason;	/* hangup reason as server explains */
	int		mcsuid;		/* MCS [T.122] userId */
	int		userchan;		/* MCS user channelId */
	int		srvchan;		/* MCS server channel ID */
	int		shareid;		/* share ID - [T128] section 8.4.2 */
	int		active;		/* T.128 action state */
	int		wantconsole;	/* attach to the console sesstion */
	Vchan	*vc;			/* static virtual channels table */
	uint		nvc;			/* number of vctab entries */
	uchar	cmap[256];	/* rfb color map for depths ≤ 8 */
	uchar	rbuf[MAXTPDU];	/* read buffer */	
};
int	starttls(Rdp*);
int	rdphandshake(Rdp*);
int	x224handshake(Rdp*);
int	x224hangup(Rdp*);
void	sendclientinfo(Rdp*);
void	confirmactive(Rdp*);
void	assync(Rdp*);
void	asctl(Rdp*,int);
void	asfontls(Rdp*);
void	act(Rdp*,ulong,int,int,int,int);
void	turnupdates(Rdp*, int);
void	erectdom(Rdp*);
void	readnet(Rdp*);
void	clipannounce(Rdp*);
void	clipvcfn(Rdp*, uchar*,uint);
void	audiovcfn(Rdp*, uchar*,uint);
void	pollsnarf(Rdp*);

void	initscreen(Rdp*);
void	readkbd(Rdp*);
void	sendkbd(Rdp*, Rune);
void	readdevmouse(Rdp*);
void	eresized(Rdp*, int);

struct Vchan
{
	int		mcsid;		/* MCS channelId */
	int		flags;
	char		name[8];
	uchar	*buf;			/* defragmentation buffer */
	int		nb;			/* sizeof buf */
	int		pos;			/* next fragment offset */ 
	void		(*fn)(Rdp*,uchar*,uint);
};
void		initvc(Rdp*);
int		sendvc(Rdp*,char*,uchar*,int);

struct Caps
{
	int	general;
	int	canrefresh;
	int	cansupress;

	int	bitmap;
	int	depth;
	int	xsz;
	int	ysz;
};
int	getcaps(Caps*,uchar*,uint);
int	sizecaps(Caps*);
int	putcaps(uchar*,uint,Caps*);

enum /* Msg.type */
{
	Xconnect=1, 	/* C: X.224 connect request */
	Xconnected,	/* S: X.224 connection confirm */
	Xhangup,  	/* C: X.224 disconnection request */
	Mattach,   	/* C: MCS (T.125) Attach User Request */
	Mattached,	/* S: MCS Attach User Confirm */
	Mjoin,	/* C: MCS Channel Join Request */
	Mjoined,		/* S: MCS Channel Join Confirm */
	Merectdom,	/* C: MCS Erect Domain Request */
	Mconnect,	/* C: MCS Connect Initial + GCC Confce Create Req */
	Mconnected,	/* S: MCS Connect Response + GCC Confce Create Resp */
	Mactivated,	/* C: MCS Confirm Active */
	Mclosing,		/* S: Disconnect Provider Ultimatum */
	Mvchan,		/* S,C: MCS virtual channel data, raw */
	Async,		/* C: MPAS 2.2.1.14 Client Synchronize PDU */
	Actl,			/* C: MPAS 2.2.1.15 Control PDU */
	Afontls,		/* C: MPAS 2.2.1.18 Client Font List PDU */
	Ainput,      	/* C: MPAS (T.128) Input Event */
	Aupdate,		/* S: T.128 ASPDU or a "Fast-Path" RDP PDU */
	Aflow,		/* S: T.128 Flow PDU */
	Dclientinfo,	/* C: RDP 2.2.1.11 Client Info */
	Dsupress,		/* C: RDP 2.2.11.3 Suppress Output PDU */
	Lneedlicense,	/* S: Licensing PDU */
	Lreq,			/* C: Licensing PDU */
	Lhavechal,	/* S: Licensing PDU */
	Lnolicense,	/* C: Licensing PDU */
	Ldone,		/* S: Licensing PDU */
};

enum /* Msg.negproto - for msg.c x224.c */
{
	ProtoTLS= 	1,
	ProtoCSSP=	2,
	ProtoUAUTH=	8,
};

struct Msg {
	int	type;
	int	negproto;	/* Xconnect, Xconnected */
	int	mcsuid;	/* Mattached, Mjoin, Mactivated, Async & more */
	int	chanid;	/* Mjoin, Mvchan */
	int	originid;	/* Mactivated, Async, Actl, Afontls, Ainput, Dsupress */
	int	shareid;	/* Mactivated, Async, Actl, Afontls, Ainput, Dsupress */
	int	ver;	/* Mconnect, Mconnected */
	int	xsz;	/* Mconnect, Dsupress, Mactivated */
	int	ysz;	/* Mconnect, Dsupress, Mactivated */
	int	depth;	/* Mconnect, Dsupress, Mactivated */
	char	*sysname;	/* Mconnect, Dclientinfo, Lreq */
	int	sproto;	/* Mconnect */
	int	wantconsole;	/* Mconnect */
	uint	nvc;	/* Mconnect */
	Vchan	*vctab;	/* Mconnect */
	char	*dom;	/* Dclientinfo*/
	char	*user;	/* Dclientinfo, Lreq */
	char	*pass;	/* Dclientinfo */
	char	*rshell;	/* Dclientinfo */
	char	*rwd;	/* Dclientinfo */
	int	dologin;	/* Dclientinfo */
	int	mtype;	/* Ainput */
	ulong	msec;	/* Ainput */
	ulong	flags;	/* Ainput, Mvchan */
	int	iarg[2];	/* Ainput */
	int	action;	/* Actl */
	int	allow;	/* Dsupress */
	uchar*	data;	/* Mvchan, Aupdate */
	uint	ndata;	/* Mvchan, Aupdate */
	uint	len;	/* Mvchan */
	int	(*getshare)(Share*, uchar*, uint);	/* Aupdate */
};

typedef	int	Msgget(Msg*,uchar*,uint);
typedef	int	Msgput(uchar*,uint,Msg*);

Msgget	getmsg;
Msgput	putmsg;
int	readmsg(Rdp*,Msg*);
int	writemsg(Rdp*,Msg*);

int	sizegccr(Msg*);
Msgget	getmcr;
Msgput	putgccr;
Msgput	putclientinfo;
Msgput	putconfirmactive;

int	sizelicensemsg(Msg*);
Msgget	getlicensemsg;
Msgput	putlicensemsg;
void 	respondlicense(Rdp*,Msg*);
void	apply(Rdp* c, Msg* m);

enum /* Share.type */
{
	ShActivate=	1,
	ShDeactivate,
	ShUorders,
	ShUimg,
	ShUcmap,
	ShUwarp,
	ShSync,
	ShCtl,
	ShFmap,
	ShEinfo,
};

struct Share
{
	int	type;
	int	source;
	int	shareid;
	int	ncap;
	int	nr;
	int	x;
	int	y;
	int	err;
	uchar* data;
	uint	ndata;
};
int	getshareT(Share*, uchar*, uint);	/* T.128 ASPDU updates */
int	getshareF(Share*, uchar*, uint);	/* RDP Fast-Path updates */

void	activate(Rdp*,Share*);
void	deactivate(Rdp*,Share*);
void	finalhandshake(Rdp*);
void	drawimgupdate(Rdp*,Share*);
void	loadcmap(Rdp*,Share*);

enum /* Imgupd.type */
{
	Ubitmap,
	Uscrblt,
	Umemblt,
	Uicache,
	Umcache,
};
struct Imgupd
{
	int	type;
	int	x;
	int	y;
	int	xm;
	int	ym;
	int	xsz;
	int	ysz;
	int	depth;
	int	iscompr;
	int	cid;
	int	coff;
	int	sx;
	int	sy;
	int	clipped;
	int	cx;
	int	cy;
	int	cxsz;
	int	cysz;
	int	nbytes;
	uchar*	bytes;
};
int	getimgupd(Imgupd*, uchar*, uint);
int	getfupd(Imgupd*, uchar*, uint);

enum 
{
	/* 2.2.8.1.1.2.1 Basic (TS_SECURITY_HEADER) */
	Scrypt			= 0x0008,
	Sinfopk			= 0x0040,
	Slicensepk		= 0x0080,

	/* 2.2.8.1.1.3.1.1 Slow-Path Input Event (TS_INPUT_EVENT) */
	InputSync=	0,
	InputKeycode=	4,
	InputUnicode=	5,
	InputMouse=	0x8001,

	/* 2.2.1.15.1 Control PDU Data */
	CAreqctl=	1,
	CAcooperate=	4,

	GLOBALCHAN=	1003,	/* MCS global channel id */

	TPKTFIXLEN=		4,
	TPDATAFIXLEN=	(TPKTFIXLEN+3),
	MCSCIFIXLEN=		(18+3*2+24*4),

	SECHSIZE=	4,
	SCHSIZE=		6,
	SCDSIZE=		SCHSIZE+4+4+2*2,

	NumOrders=		32,
};

extern uchar	orderSupport[NumOrders];

extern char	Eshort[];
extern char	Ebignum[];
extern char	Esmall[];


enum /* X.224 PDU codes */
{
	ConReq=		0xE0, 		/* connection request */
	ConCfrm=	0xD0,		/* connection confirm */
	HupReq=		0x80,		/* disconnection request */
	Data=		0xF0,		/* data */
	Err=			0x70,		/* error */
};

enum /* ASN.1 tag numbers for MCS types */
{
	Mci=		101,		/* Connect Initial */
	Mcr=	102,		/* Connect Response */
	Medr=	1,		/* Erect Domain Request */
	Maur=	10,		/* Attach User Request */
	Mauc=	11,		/* Attach User Confirm */
	Mcjr=	14,		/* Channel Join Request */
	Mcjc=	15,		/* Channel Join Confirm */
	Msdr=	25,		/* Send Data Request */
	Msdi=	26,		/* Send Data Indication */
	Mdpu=	8,		/* Disconnect Provider Ultimatum */
};

enum /* 2.2.8.1.1.1.2 Share Data Header (TS_SHAREDATAHEADER) */
{
	ADdraw=	2,
	ADctl=	20,
	ADcursor=	27,
	ADinput=		28,
	ADsync=	31,
	ADrefresh=	33,
	ADsupress=	35,
	ADfontlist=	39,
	ADfontmap=	40,
	ADerrx=	47,
};
