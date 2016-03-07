#include <u.h>
#include <libc.h>
#include <draw.h>
#include "dat.h"
#include "fns.h"

/* 2.2.7.1.1 General Capability Set (TS_GENERAL_CAPABILITYSET) */
void
scangencaps(uchar* p, uchar* ep)
{
	int extraFlags, canrefresh, cansupress;

	if(p+22>ep)
		sysfatal(Eshort);
	extraFlags  = GSHORT(p+14);
	USED(extraFlags);
	canrefresh = p[22];
	cansupress = p[23];
	if(!canrefresh)
		sysfatal("server lacks support for Refresh Rect PDU");
	if(!cansupress)
		sysfatal("server lacks support for Suppress Output PDU");
}

/* 2.2.7.1.2 Bitmap Capability Set (TS_BITMAP_CAPABILITYSET) */
void
scanbitcaps(uchar* p, uchar* ep)
{
	int w, h, depth;

	if(p+16> ep)
		sysfatal(Eshort);
	depth = GSHORT(p+4);
	w = GSHORT(p+12);
	h = GSHORT(p+14);

	if(depth != rd.depth){
		rd.depth = depth;
		switch(depth){
		case 8:
			rd.chan = CMAP8;
			break;
		case 15:
			rd.chan = RGB15;
			break;
		case 16:
			rd.chan = RGB16;
			break;
		case 24:
			rd.chan = RGB24;
			break;
		case 32:
			rd.chan = XRGB32;
			break;
		default:
			sysfatal("Unsupported server color depth: %uhd\n", depth);
		}
	}
	if(w != rd.dim.x || h != rd.dim.y){
		rd.dim.x = w;
		rd.dim.y = h;
		rd.dim.x = (rd.dim.x + 3) & ~3;	/* ensure width divides by 4 */
	}
}

/* 2.2.7.1.1 General Capability Set (TS_GENERAL_CAPABILITYSET) */
uchar*
putgencaps(uchar *p, uchar *ep)
{
	int extraFlags;

	extraFlags = 0
		| CanFastpath
		| NoBitcomphdr
		| CanLongcred
	;

	if(p+24>ep)
		sysfatal(Eshort);
	PSHORT(p+0, CapGeneral);
	PSHORT(p+2, GENCAPSIZE);
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
	return p+24;
}


/* 2.2.7.1.2 Bitmap Capability Set (TS_BITMAP_CAPABILITYSET) */
uchar*
putbitcaps(uchar *p, uchar *ep)
{
	if(p+30>ep)
		sysfatal(Eshort);
	PSHORT(p+0, CapBitmap);
	PSHORT(p+2, BITCAPSIZE);
	PSHORT(p+4, rd.depth);	// preferredBitsPerPixel
	PSHORT(p+6, 1);	// receive1BitPerPixel
	PSHORT(p+8, 1);	// receive4BitsPerPixel
	PSHORT(p+10, 1);	// receive8BitsPerPixel
	PSHORT(p+12, rd.dim.x);	// desktopWidth
	PSHORT(p+14, rd.dim.y);	// desktopHeight
	PSHORT(p+16, 0);	// pad2octets 
	PSHORT(p+18, 1);	// desktopResizeFlag 
	PSHORT(p+20, 1);	// bitmapCompressionFlag 
	PSHORT(p+22, 0);	// highColorFlags 
	PSHORT(p+24, 1);	// drawingFlags 
	PSHORT(p+26, 1);	// multipleRectangleSupport
	PSHORT(p+26, 0);	// pad2octetsB
	return p+30;
}

/* 2.2.7.1.3 Order Capability Set (TS_ORDER_CAPABILITYSET) */
uchar*
putordcaps(uchar *p, uchar *ep)
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
	if(p+88>ep)
		sysfatal(Eshort);
	PSHORT(p+0, CapOrder);
	PSHORT(p+2, ORDCAPSIZE);
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
	return p+88;
}

/* 2.2.7.1.4 Bitmap Cache Capability Set (TS_BITMAPCACHE_CAPABILITYSET) */
/* 2.2.7.1.4.2 Revision 2 (TS_BITMAPCACHE_CAPABILITYSET_REV2) */
uchar*
putbc2caps(uchar *p, uchar *ep)
{
	if(p+40>ep)
		sysfatal(Eshort);
	PSHORT(p+0, CapBitcache2);
	PSHORT(p+2, BCACAPSIZE);
	PSHORT(p+4, 0);	// CacheFlags (2 bytes):  
	p[6] = 0;	// pad2
	p[7] = 3;	// NumCellCaches
	PLONG(p+8, 120);	// BitmapCache0CellInfo
	PLONG(p+12, 120);	// BitmapCache1CellInfo
	PLONG(p+16, 336);	// BitmapCache2CellInfo
	PLONG(p+20, 0);	// BitmapCache3CellInfo
	PLONG(p+24, 0);	// BitmapCache4CellInfo
	memset(p+28, 12, 0); // Pad3
	return p+40;
}

/* 2.2.7.1.5 Pointer Capability Set (TS_POINTER_CAPABILITYSET) */
uchar*
putptrcaps(uchar *p, uchar *ep)
{
	if(p+8>ep)
		sysfatal(Eshort);
	PSHORT(p+0, CapPointer);
	PSHORT(p+2, PTRCAPSIZE);
	PSHORT(p+4, 0);	// colorPointerFlag  
	PSHORT(p+6, 20);	// colorPointerCacheSize 
	return p+8;
}

/* 2.2.7.1.6 Input Capability Set (TS_INPUT_CAPABILITYSET) */
uchar*
putinpcaps(uchar *p, uchar *ep)
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

	if(p+88>ep)
		sysfatal(Eshort);
	PSHORT(p+0, CapInput);
	PSHORT(p+2, INPCAPSIZE);
	PSHORT(p+4, inputFlags);	// inputFlags
	PSHORT(p+6, 0);	// pad2octetsA

	// the below SHOULD be the same as in the Client Core Data (section 2.2.1.3.2).
	PLONG(p+8, 0x409);	// keyboardLayout
	PLONG(p+12, 4);	// keyboardType: IBM enhanced (101- or 102-key)
	PLONG(p+16, 0);	// keyboardSubType
	PLONG(p+20, 12);	// keyboardFunctionKey
	memset(p+24, 64, 0);	// imeFileName
	return p+88;
}

/* 2.2.7.1.8 Glyph Cache Capability Set (TS_GLYPHCACHE_CAPABILITYSET) */
uchar*
putglycaps(uchar* p, uchar* ep)
{
	enum {
		GLYPH_SUPPORT_NONE= 0,
	};

	if(p+52>ep)
		sysfatal(Eshort);
	PSHORT(p+0, CapGlyph);
	PSHORT(p+2, GLYCAPSIZE);
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
	return p+52;
}

/* 2.2.7.1.11 Sound Capability Set (TS_SOUND_CAPABILITYSET) */
uchar*
putsndcaps(uchar* p, uchar* ep)
{
	if(p+8>ep)
		sysfatal(Eshort);
	PSHORT(p+0, CapSound);
	PSHORT(p+2, SNDCAPSIZE);
	PSHORT(p+4, 0);	// soundFlags
	PSHORT(p+6, 0);	// pad2octetsA
	return p+8;
}
