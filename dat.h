typedef	struct	Rdp Rdp;
typedef	struct	Vchan Vchan;
typedef	struct	Rdpnego Rdpnego;

#define	GSHORT(p)	((p)[0]|((p)[1]<<8))
#define	GSHORTB(p)	((p)[0]<<8|((p)[1]))
#define	GLONG(p) 	((p)[0]|((p)[1]<<8)|((p)[2]<<16)|((p)[3]<<24))
#define	GLONGB(p)	(((p)[0]<<24)|((p)[1]<<16)|((p)[2]<<8)|(p)[3])
#define	PSHORT(p,v)	(p)[0]=(v);(p)[1]=(v)>>8
#define	PSHORTB(p,v)	(p)[0]=(v)>>8;(p)[1]=(v)
#define	PLONG(p,v)	(p)[0]=(v);(p)[1]=(v)>>8;(p)[2]=(v)>>16;(p)[3]=(v)>>24
#define	PLONGB(p,v)	(p)[0]=(v)>>24;(p)[1]=(v)>>16;(p)[2]=(v)>>8;(p)[3]=(v)

#define	MIN(x,y)		(((x) < (y)) ? (x) : (y))

struct Rdp
{
	int		fd;			/* connection I/O descriptor */
	long		sproto;		/* server-selected security protocol to replay back */
	int		autologon;	/* enable auto logon */
	char		*label;		/* window label */
	char		*local;		/* local system name */
	char		*user;		/* user name */
	char		*windom;		/* domain for auto logon */
	char		*passwd;		/* password for auto logon (sic) */
	char		*shell;		/* remote shell override */
	char		*rwd;		/* remote working directory */
	ulong	chan;		/* remote graphics channel descriptor */
	int		depth;		/* color depth as exposed by the protocol */
	int		hupreason;	/* hangup reason as server explains */
	int		mcsuid;		/* MCS [T.122] userId */
	int		userchan;		/* MCS user channelId */
	int		shareid;		/* share ID - [T128] section 8.4.2 */
	int		licensed;		/* licensing sub-protocol completion */
	int		active;		/* T.128 action state */
	int		wantconsole;	/* attach to the console sesstion */
	Point		dim;			/* rfb size */
};

struct Vchan
{
	int		mcsid;		/* MCS channelId */
	int		flags;
	char		name[8];
	int		defragging;
	uchar*	buf;			/* defragmentation buffer */
	int		nb;			/* sizeof buf */
	int		pos;			/* next fragment offset */ 
	void		(*fn)(uchar*,uchar*);
};
Vchan*	lookupvc(int);
Vchan*	namevc(char*);
int		sendvc(char*,uchar*,int);
void		scanvcpdu(uchar*,uchar*,int);

enum 
{
	/* 2.2.7 Capability Sets; T.128 */
	CapGeneral=	1,
	CapBitmap=	2,
	CapOrder=	3,
	CapPointer=	8,
	CapBitcache2=	19,
	CapInput=	13,
	CapSound=	12,
	CapGlyph=	16,
	GENCAPSIZE=	24,
	BITCAPSIZE=	30,
	ORDCAPSIZE=	88,
	BCACAPSIZE=	40,
	PTRCAPSIZE=	8,
	INPCAPSIZE=	88,
	SNDCAPSIZE=	8,
	GLYCAPSIZE=	52,

	/* 2.2.7.1.1 General Capability Set (TS_GENERAL_CAPABILITYSET) */
	CanFastpath	= 0x0001,
	NoBitcomphdr	= 0x0400,
	CanLongcred	= 0x0004,

	/* 2.2.8.1.1.2.1 Basic (TS_SECURITY_HEADER) */
	Scrypt			= 0x0008,
	Sinfopk			= 0x0040,
	Slicensepk		= 0x0080,

	/* 2.2.8.1.1.3.1.1 Slow-Path Input Event (TS_INPUT_EVENT) */
	InputSync=	0,
	InputKeycode=	4,
	InputUnicode=	5,
	InputMouse=	0x8001,

	TPKTFIXLEN=		4,
	TPDATAFIXLEN=	(TPKTFIXLEN+3),
	MCSCIFIXLEN=		(18+3*2+24*4),

	MAXTPDU=	16386,	/* max TPDU size */
	SRVCHAN=	1002,	/* server channel ID */
	GLOBALCHAN=	1003,	/* MCS global channel's id */
	NumOrders=		32,
};

extern Rdp	rd;
extern uchar	orderSupport[NumOrders];
extern uchar	cmap[256];	/* 8bpp translation table */

extern Vchan	vctab[];		/* static virtual channels table */
extern uint	nvc;			/* number of vctab entries */

extern char	Eshort[];
extern char	Ebignum[];
extern char	Esmall[];
