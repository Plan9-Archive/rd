#include <u.h>
#include <libc.h>
#include <keyboard.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"


enum
{
	Kbrk=	Spec|0x61,
	KeyEx= 	0x0100,
	KeyUp=	0x8000,
};

enum
{
	Sext=	(1<<16),
	Sesc=	1,
	Sbspace=	14,
	Stab=	15,
	Sq=	16,
	Sw=	17,
	Se=	18,
	Sr=	19,
	St=	20,
	Sy=	21,
	Su=	22,
	Si=	23,
	So=	24,
	Sp=	25,
	Srbrace=	27,
	Sret=	28,
	Slctrl=	29,
	Sa=	30,
	Ss=	31,
	Sd=	32 ,
	Sf=	33,
	Sg=	34,
	Sh=	35,
	Sj=	36,
	Sk=	37,
	Sl=	38,
	Slshift=	42,
	Sbslash=	43,
	Sz=	44,
	Sx=	45,
	Sc=	46,
	Sv=	47,
	Sb=	48,
	Sn=	49,
	Sm=	50,
	Speriod=	52,
	Sslash=	53,
	Sprint=	Sext|55,
	SF1=	59,
	SF11=	87,
	SF12=	88,
	Shome=	199,
	Sup=	200,
	Spgup=	201,
	Sleft=	203,
	Sright=	205,
	Sdown=	208,
	Spgdown=	209,
	Send=	207,
	Sins=	210,
	Sdel=	211,
};

struct {
	uchar sc, mod;
} rune2scan[] = {
	Sdel, Slshift,
	Sa,  Slctrl,
	Sb,  Slctrl,
	Sc,  Slctrl,
	Sd,  Slctrl,
	Se,  Slctrl,
	Sf,  Slctrl,
	Sg,  Slctrl,
	Sbspace,  0,
	Stab,    0,
	Sret,  0,
	Sk,  Slctrl,
	Sl,  Slctrl,
	Sm,    Slctrl,
	Sn,  Slctrl,
	So,  Slctrl,
	Sp,  Slctrl,
	Sq,  Slctrl,
	Sr,  Slctrl,
	Ss,  Slctrl,
	St,  Slctrl,
	Su,  Slctrl,
	Sv,  Slctrl,
	Sw,  Slctrl,
	Sx,  Slctrl,
	Sy,  Slctrl,
	Sz,  Slctrl,
	Sesc, 0,
	Sbslash, Slctrl,
	Srbrace, Slctrl,
	Speriod, Slctrl,
	Sslash,  Slctrl,
};

void
kbdsendscan(int sc, int mod)
{
	long msec;
	int f;

	f = 0;
	if(sc&Sext)
		f = KeyEx;
	sc &= ~Sext;

	msec = time(nil);
	if(mod != 0)
		passinput(msec, InputKeycode, 0, mod, 0);
	passinput(msec, InputKeycode, f|0, sc, 0);
	passinput(msec, InputKeycode, f|KeyUp, sc, 0);
	if(mod != 0)
		passinput(msec, InputKeycode, KeyUp, mod, 0);
}

void
kbdsendrune(Rune r)
{
	long msec;

	msec = time(nil);
	passinput(msec, InputUnicode, 0, r, 0);
	passinput(msec, InputUnicode, KeyUp, r, 0);
}

void
readkbd(void)
{
	char buf[256], k[10];
	int ctlfd, fd, kr, kn, w;
	uchar mod, sc;
	Rune r;

	if((fd = open("/dev/cons", OREAD)) < 0)
		sysfatal("open %s: %r", buf);
	if((ctlfd = open("/dev/consctl", OWRITE)) < 0)
		sysfatal("open %s: %r", buf);
	write(ctlfd, "rawon", 5);

	kn = 0;
	for(;;){
		while(!fullrune(k, kn)){
			kr = read(fd, k+kn, sizeof k - kn);
			if(kr <= 0)
				sysfatal("bad read from kbd");
			kn += kr;
		}
		w = chartorune(&r, k);
		kn -= w;
		memmove(k, &k[w], kn);

		if(r < nelem(rune2scan)){
			sc = rune2scan[r].sc;
			mod = rune2scan[r].mod;
			kbdsendscan(sc, mod);
			continue;
		}

		switch(r){
		case Kins:
			kbdsendscan(Sins, 0);
			break;
		case Kdel:
			kbdsendscan(Sdel, 0);
			break;
		case Khome:
			kbdsendscan(Shome, 0);
			break;
		case Kend:
			kbdsendscan(Send, 0);
			break;
		case Kpgup:
			kbdsendscan(Spgup, 0);
			break;
		case Kpgdown:
			kbdsendscan(Spgdown, 0);
			break;
		case Kup:
			kbdsendscan(Sup, 0);
			break;
		case Kdown:
			kbdsendscan(Sdown,0 );
			break;
		case Kleft:
			kbdsendscan(Sleft, 0);
			break;
		case Kright:
			kbdsendscan(Sright, 0);
			break;
		case Kbrk:
			exits("interrupt");
			break;
		case Kprint:
			kbdsendscan(Sprint, 0);
			break;
		case KF|1:
		case KF|2:
		case KF|3:
		case KF|4:
		case KF|5:
		case KF|6:
		case KF|7:
		case KF|8:
		case KF|9:
		case KF|10:
			kbdsendscan(SF1+r-(KF|1), 0);
			break;
		case KF|11:
			kbdsendscan(SF11, 0);
			break;
		case KF|12:
			kbdsendscan(SF12, 0);
			break;
		default:
			kbdsendrune(r);
			break;
		}
	}
}
