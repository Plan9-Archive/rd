#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include "dat.h"
#include "fns.h"

enum {
	MouseMove=	0x0800,
	MouseB1=	0x1000,
	MouseB2=	0x2000,
	MouseB3=	0x4000,
	MouseB4=	0x0280,
	MouseB5=	0x0380,
	MouseBdown=	0x8000,
};

static int mfd = -1;

static void
sendmouse(Rdp* c, Mouse m, int f)
{
	passinput(c, m.msec, InputMouse, f, m.xy.x, m.xy.y);
}

static void
mouseevent(Rdp* c, Mouse m)
{
	static Mouse o;
	int down;
	int chg;

	m.xy = subpt(m.xy, screen->r.min);
	switch(m.buttons){
	case 8:
		sendmouse(c, m, MouseB4|MouseBdown);
		sendmouse(c, m, MouseB4);
		return;
	case 16:
		sendmouse(c, m, MouseB5|MouseBdown);
		sendmouse(c, m, MouseB5);
		return;
	}

	if(!eqpt(m.xy, o.xy))
		sendmouse(c, m, MouseMove);

	chg = m.buttons ^ o.buttons;
	if(chg&1){
		down = (m.buttons&1)? MouseBdown : 0;
		sendmouse(c, m, MouseB1|down);
	}
	if(chg&2){
		down = (m.buttons&2)? MouseBdown : 0;
		sendmouse(c, m, MouseB3|down);
	}
	if(chg&4){
		down = (m.buttons&4)? MouseBdown : 0;
		sendmouse(c, m, MouseB2|down);
	}
	o = m;
}

void
readdevmouse(Rdp* c)
{
	Mouse m;
	char ev[1+4*12];

	if((mfd = open("/dev/mouse", ORDWR)) < 0)
		sysfatal("open /dev/mouse: %r");

	for(;;){
		if(read(mfd, ev, sizeof ev) != sizeof ev)
			sysfatal("mouse eof");
		if(*ev == 'm'){
			m.xy.x = atoi(ev+1+0*12);
			m.xy.y = atoi(ev+1+1*12);
			m.buttons = atoi(ev+1+2*12) & 0x1F;
			m.msec = atoi(ev+1+3*12);
			mouseevent(c, m);
		}else
			eresized(c, 1);
	}
}

void
warpmouse(int x, int y)
{
	if(mfd < 0)
		return;

	fprint(mfd, "m%d %d", x, y);
}
