#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

enum /* 2.2.7 Capability Sets; T.128 */
{
	CapGeneral=	1,
	CapBitmap=	2,
	CapOrder=	3,
	CapPointer=	8,
	CapBitcache2=	19,
	CapInput=	13,
	CapSound=	12,
	CapGlyph=	16,

	/* 2.2.7.1.1 General Capability Set (TS_GENERAL_CAPABILITYSET) */
	CanFastpath=	0x0001,
	NoBitcomphdr=	0x0400,
	CanLongcred=	0x0004,
};

static int	putncap(uchar*,uint,Caps*);
static int	putbitcaps(uchar*,uint,Caps*);
static int	putgencaps(uchar*,uint,Caps*);
static int	putordcaps(uchar*,uint,Caps*);
static int	putbc2caps(uchar*,uint,Caps*);
static int	putptrcaps(uchar*,uint,Caps*);
static int	putinpcaps(uchar*,uint,Caps*);
static int	putsndcaps(uchar*,uint,Caps*);
static int	putglycaps(uchar*,uint,Caps*);

static
struct {
	int	size;
	int	(*putcap)(uchar*,uint,Caps*);
} ctab[]=
{
	{ 4,	putncap },
	{ 24,	putgencaps },
	{ 30,	putbitcaps },
	{ 88,	putordcaps },
	{ 40,	putbc2caps },
	{ 8,	putptrcaps },
	{ 88,	putinpcaps },
	{ 8,	putsndcaps },
	{ 52,	putglycaps },
};

int
getcaps(Caps* caps, uchar* a, uint nb)
{
	int ncap, type, len;
	uchar *p, *ep;
	int extraFlags;

	p = a;
	ep = p+nb;
	memset(caps, sizeof(*caps), 0);

	ncap = GSHORT(p);
	p += 4;
	for(; ncap>0 && p+4<ep; ncap--){
		type = GSHORT(p+0);
		len = GSHORT(p+2);
		if(p+len > ep){
			werrstr("bad length in server's capability set");
			return -1;
		}
		switch(type){
		case CapGeneral:
			/* 2.2.7.1.1 General Capability Set (TS_GENERAL_CAPABILITYSET) */
			if(len < 24){
				werrstr(Eshort);
				return -1;
			}
			caps->general = 1;
			extraFlags  = GSHORT(p+14);
			caps->canrefresh = p[22];
			caps->cansupress = p[23];
			USED(extraFlags);
			break;
		case CapBitmap:
			/* 2.2.7.1.2 Bitmap Capability Set (TS_BITMAP_CAPABILITYSET) */
			if(len < 16){
				werrstr(Eshort);
				return -1;
			}
			caps->bitmap = 1;
			caps->depth = GSHORT(p+4);
			caps->xsz = GSHORT(p+12);
			caps->ysz = GSHORT(p+14);
			break;
		}
		p += len;
	}
	return p-a;
}

int
sizecaps(Caps*)
{
	int i, n;

	n = 0;
	for(i = 0; i < nelem(ctab); i++)
		n += ctab[i].size;
	return n;
}

int
putcaps(uchar* a, uint nb, Caps* caps)
{
	uchar *p, *ep;
	int i, n;

	p = a;
	ep = a+nb;

	for(i = 0; i < nelem(ctab); i++){
		n = ctab[i].putcap(p, ep-p, caps);
		if(n < 0)
			return -1;
		p += n;
	}
	return p-a;
}

static int
putncap(uchar *p, uint nb, Caps*)
{
	int ncap;
	
	ncap = 8;
	
	if(nb<4){
		werrstr(Eshort);
		return -1;
	}
	PSHORT(p, ncap);
	PSHORT(p+2, 0);
	return 4;
}

/* 2.2.7.1.1 General Capability Set (TS_GENERAL_CAPABILITYSET) */
static int
putgencaps(uchar *p, uint nb, Caps*)
{
	int extraFlags;
	
	extraFlags = 0
		| CanFastpath
		| NoBitcomphdr
		| CanLongcred
		;
	if(nb<24){
		werrstr(Eshort);
		return -1;
	}
	PSHORT(p+0, CapGeneral);
	PSHORT(p+2, 24);	// size
	PSHORT(p+4, 0);	// OSMAJORTYPE_UNSPECIFIED
	PSHORT(p+6, 0);	// OSMINORTYPE_UNSPECIFIED
	PSHORT(p+8, 0x200);	// TS_CAPS_PROTOCOLVERSION
	PSHORT(p+12, 0);	// generalCompressionTypes
	PSHORT(p+14, extraFlags);
	PSHORT(p+16, 0);	// updateCapabilityFlag
	PSHORT(p+18, 0);	// remoteUnshareFlag 
	PSHORT(p+20, 0);	// generalCompressionLevel
	p[22] = 0;  	// refreshRectSupport - server only
	p[23] = 0;  	// suppressOutputSupport - server only
	return 24;
}


/* 2.2.7.1.2 Bitmap Capability Set (TS_BITMAP_CAPABILITYSET) */
static int
putbitcaps(uchar *p, uint nb, Caps* caps)
{
	if(nb < 30){
		werrstr(Eshort);
		return -1;
	}
	PSHORT(p+0, CapBitmap);
	PSHORT(p+2, 30);	// size
	PSHORT(p+4, caps->depth);	// preferredBitsPerPixel
	PSHORT(p+6, 1);	// receive1BitPerPixel
	PSHORT(p+8, 1);	// receive4BitsPerPixel
	PSHORT(p+10, 1);	// receive8BitsPerPixel
	PSHORT(p+12, caps->xsz);	// desktopWidth
	PSHORT(p+14, caps->ysz);	// desktopHeight
	PSHORT(p+16, 0);	// pad2octets 
	PSHORT(p+18, 1);	// desktopResizeFlag 
	PSHORT(p+20, 1);	// bitmapCompressionFlag 
	PSHORT(p+22, 0);	// highColorFlags 
	PSHORT(p+24, 1);	// drawingFlags 
	PSHORT(p+26, 1);	// multipleRectangleSupport
	PSHORT(p+26, 0);	// pad2octetsB
	return 30;
}

/* 2.2.7.1.3 Order Capability Set (TS_ORDER_CAPABILITYSET) */
static int
putordcaps(uchar *p, uint nb, Caps*)
{
	ushort orderFlags;
	enum
	{
		NEGOTIATEORDERSUPPORT=		0x02,
		ZEROBOUNDSDELTASSUPPORT=	0x08,
		COLORINDEXSUPPORT=			0x20,
		SOLIDPATTERNBRUSHONLY=		0x40,
	};
	
	orderFlags = 0
		| NEGOTIATEORDERSUPPORT
		| ZEROBOUNDSDELTASSUPPORT
		| COLORINDEXSUPPORT
		| SOLIDPATTERNBRUSHONLY
		;
	if(nb<88){
		werrstr(Eshort);
		return -1;
	}
	PSHORT(p+0, CapOrder);
	PSHORT(p+2, 88);	// size
	memset(p+4, 16, 0);	// terminalDescriptor
	PLONG(p+20, 0);	// pad4octetsA 
	PSHORT(p+24, 1);	// desktopSaveXGranularity 
	PSHORT(p+26, 20);	// desktopSaveYGranularity 
	PSHORT(p+28, 0);	// pad2octetsA
	PSHORT(p+30, 1);	// maximumOrderLevel 
	PSHORT(p+32, 0);	// numberFonts 
	PSHORT(p+34, orderFlags);
	memcpy(p+36, orderSupport, 32);
	PSHORT(p+68, 0x6a1);	// textFlags
	PSHORT(p+70, 0);	// orderSupportExFlags
	PLONG(p+72, 0);	// pad4octetsB
	PLONG(p+76, 480*480);	// desktopSaveSize
	PSHORT(p+80, 0);	// pad2octetsC
	PSHORT(p+82, 0);	// pad2octetsD
	PSHORT(p+84, 0xe4);	// textANSICodePage
	PSHORT(p+86, 0x04);	// pad2octetsE
	return 88;
}

/* 2.2.7.1.4 Bitmap Cache Capability Set (TS_BITMAPCACHE_CAPABILITYSET) */
/* 2.2.7.1.4.2 Revision 2 (TS_BITMAPCACHE_CAPABILITYSET_REV2) */
static int
putbc2caps(uchar *p, uint nb, Caps*)
{
	if(nb<40){
		werrstr(Eshort);
		return -1;
	}
	PSHORT(p+0, CapBitcache2);
	PSHORT(p+2, 40);	// size
	PSHORT(p+4, 0);	// CacheFlags (2 bytes):  
	p[6] = 0;	// pad2
	p[7] = 3;	// NumCellCaches
	PLONG(p+8, 120);	// BitmapCache0CellInfo
	PLONG(p+12, 120);	// BitmapCache1CellInfo
	PLONG(p+16, 336);	// BitmapCache2CellInfo
	PLONG(p+20, 0);	// BitmapCache3CellInfo
	PLONG(p+24, 0);	// BitmapCache4CellInfo
	memset(p+28, 12, 0); // Pad3
	return 40;
}

/* 2.2.7.1.5 Pointer Capability Set (TS_POINTER_CAPABILITYSET) */
static int
putptrcaps(uchar *p, uint nb, Caps*)
{
	if(nb<8){
		werrstr(Eshort);
		return -1;
	}
	PSHORT(p+0, CapPointer);
	PSHORT(p+2, 8);	// size
	PSHORT(p+4, 0);	// colorPointerFlag  
	PSHORT(p+6, 20);	// colorPointerCacheSize 
	return 8;
}

/* 2.2.7.1.6 Input Capability Set (TS_INPUT_CAPABILITYSET) */
static int
putinpcaps(uchar *p, uint nb, Caps*)
{
	long inputFlags;
	enum
	{
		INPUT_FLAG_SCANCODES=		0x0001,
		INPUT_FLAG_MOUSEX=			0x0004,
		INPUT_FLAG_FASTPATH_INPUT=	0x0008,
		INPUT_FLAG_UNICODE=			0x0010,
		INPUT_FLAG_FASTPATH_INPUT2=	0x0020,
	};

	inputFlags = 0
		| INPUT_FLAG_SCANCODES
		| INPUT_FLAG_UNICODE
		;
	if(nb<88){
		werrstr(Eshort);
		return -1;
	}
	PSHORT(p+0, CapInput);
	PSHORT(p+2, 88);	// size
	PSHORT(p+4, inputFlags);	// inputFlags
	PSHORT(p+6, 0);	// pad2octetsA

	// the below SHOULD be the same as in the Client Core Data (section 2.2.1.3.2).
	PLONG(p+8, 0x409);	// keyboardLayout
	PLONG(p+12, 4);	// keyboardType: IBM enhanced (101- or 102-key)
	PLONG(p+16, 0);	// keyboardSubType
	PLONG(p+20, 12);	// keyboardFunctionKey
	memset(p+24, 64, 0);	// imeFileName
	return 88;
}

/* 2.2.7.1.8 Glyph Cache Capability Set (TS_GLYPHCACHE_CAPABILITYSET) */
static int
putglycaps(uchar *p, uint nb, Caps*)
{
	enum {
		GLYPH_SUPPORT_NONE= 0,
	};

	if(nb<52){
		werrstr(Eshort);
		return -1;
	}
	PSHORT(p+0, CapGlyph);
	PSHORT(p+2, 52);	// size
	PLONG(p+4, 0x0400fe);	// GlyphCache 0
	PLONG(p+8, 0x0400fe);	// GlyphCache 1
	PLONG(p+12, 0x0800fe);	// GlyphCache 2
	PLONG(p+16, 0x0800fe);	// GlyphCache 3
	PLONG(p+20, 0x1000fe);	// GlyphCache 4
	PLONG(p+24, 0x2000fe);	// GlyphCache 5
	PLONG(p+28, 0x4000fe);	// GlyphCache 6
	PLONG(p+32, 0x8000fe);	// GlyphCache 7
	PLONG(p+36, 0x10000fe);	// GlyphCache 8
	PLONG(p+40, 0x8000040);	// GlyphCache 9
	PLONG(p+44, 0x01000100);	// FragCache 
	PSHORT(p+48, GLYPH_SUPPORT_NONE);	// GlyphSupportLevel
	PSHORT(p+50, 0);	// pad2octets 
	return 52;
}

/* 2.2.7.1.11 Sound Capability Set (TS_SOUND_CAPABILITYSET) */
static int
putsndcaps(uchar *p, uint nb, Caps*)
{
	if(nb<8){
		werrstr(Eshort);
		return -1;
	}
	PSHORT(p+0, CapSound);
	PSHORT(p+2, 8);	// size
	PSHORT(p+4, 0);	// soundFlags
	PSHORT(p+6, 0);	// pad2octetsA
	return 8;
}
