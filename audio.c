/* [MS-RDPEA]: Remote Desktop Protocol: Audio Output Virtual Channel Extension */
#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

/*
BUG requires RDPDR support...

[MS-RDPEA] "6 Appendix A: Product Behavior":
<1> Section 2.1: In Windows, the client advertises the static virtual
channel named "RDPDR", as defined in [MS-RDPEFS].  If that channel is
not advertised, the server will not issue any communication on the
"RDPSND" channel.  Not supported on Windows XP and Windows Server
2003.
*/

static char	rdpsnd[]				= "RDPSND";

enum	/* Audiomsg.type */
{
	Awav=	2,	/* WaveInfo PDU */
	Awavack=	5,	/* Wave Confirm PDU */
	Aprobe=	6,	/* Training PDU or Training Confirm PDU */
	Afmt=	7,	/* Client/Server Audio Formats and Version PDU */
		/* 2.2.1 RDPSND PDU Header (SNDPROLOG) */
};
typedef	struct	Audiomsg Audiomsg;
struct Audiomsg
{
	uint	type;
	uint	nfmt;	/* Afmt */
	uint	ack;	/* Afmt */
	uint	ver;	/* Afmt */
	uchar	*data;	/* Afmt */
	uint	ndata;	/* Afmt */
};
static	int	getaudiomsg(Audiomsg*,uchar*,int);
static	int	putaudiomsg(uchar*,int, Audiomsg*);

enum
{
	FmtPCM=	0x01,	/* WAVE_FORMAT_PCM */
	FmtMP3=	0x55,	/* WAVE_FORMAT_MPEGLAYER3 */
		/* RFC2361 Appendix A */
};


void
audiovcfn(Rdp* c, uchar* a, uint nb)
{
	Audiomsg r;

	USED(c);
fprint(2, " A ");
	if(getaudiomsg(&r, a, nb) < 0){
		fprint(2, "audio: %r\n");
		return;
	}
fprint(2, " a%ud ", r.type);
}


/*
 * 2.2.1 RDPSND PDU Header (SNDPROLOG)
 *	msgtype[1] pad[1] bodysize[2]
 *
 * 2.2.2.1 Server Audio Formats and Version PDU
 *	hdr[4] flags[4] vol[4] pitch[4] dgport[2] nfmt[2] ack[1] ver[2] pad[1] nfmt*(afmt[*])
 *
 * 2.2.2.1.1 Audio Format (AUDIO_FORMAT)
 *	ftag[2] nchan[2] samphz[4] avgbytehz[4] blocksz[2] sampbitsz[2] ndata[2] data[ndata]
 */

static int
getaudiomsg(Audiomsg* m, uchar* a, int nb)
{
	ulong len;

	if(nb < 4){
		werrstr(Eshort);
		return -1;
	}
	
	m->type = *a;
	len = GSHORT(a+2);
	switch(m->type){
	case Afmt:
		if(len < 20 || nb < len+4){
			werrstr(Eshort);
			return -1;
		}
		m->nfmt = GSHORT(a+18);
		m->ack = a[20];
		m->ver = GSHORT(a+21);
		m->data = a+24;
		m->ndata = len-20;
		return len+4;
	}
	werrstr("unrecognized audio msg type");
	return -1;
}

