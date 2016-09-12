#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>	/* for screen->r and geometry */
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

static void
sendmouse1(Rdp* c, Mouse m, int f)
{
	passinput(c, m.msec, InputMouse, f, m.xy.x, m.xy.y);
}

void
sendmouse(Rdp* c, Mouse m)
{
	static Mouse o;
	int down;
	int chg;

	m.xy = subpt(m.xy, screen->r.min);
	switch(m.buttons){
	case 8:
		sendmouse1(c, m, MouseB4|MouseBdown);
		sendmouse1(c, m, MouseB4);
		return;
	case 16:
		sendmouse1(c, m, MouseB5|MouseBdown);
		sendmouse1(c, m, MouseB5);
		return;
	}

	if(!eqpt(m.xy, o.xy))
		sendmouse1(c, m, MouseMove);

	chg = m.buttons ^ o.buttons;
	if(chg&1){
		down = (m.buttons&1)? MouseBdown : 0;
		sendmouse1(c, m, MouseB1|down);
	}
	if(chg&2){
		down = (m.buttons&2)? MouseBdown : 0;
		sendmouse1(c, m, MouseB3|down);
	}
	if(chg&4){
		down = (m.buttons&4)? MouseBdown : 0;
		sendmouse1(c, m, MouseB2|down);
	}
	o = m;
}
