#include <u.h>
#include <libc.h>
#include <draw.h>
#include <mouse.h>
#include "dat.h"
#include "fns.h"

enum {
	MOUSE_FLAG_MOVE	= 0x0800,
	MOUSE_FLAG_BUTTON1	= 0x1000,
	MOUSE_FLAG_BUTTON2	= 0x2000,
	MOUSE_FLAG_BUTTON3	= 0x4000,
	MOUSE_FLAG_BUTTON4	= 0x0280,
	MOUSE_FLAG_BUTTON5	= 0x0380,
	MOUSE_FLAG_DOWN	= 0x8000,
};

static int mfd = -1;

static void
sendmouse(Mouse m, int flags)
{
	passinput(m.msec, InputMouse, flags, m.xy.x, m.xy.y);
}

static void
mouseevent(Mouse m)
{
	ushort flags;
	int chg;
	static Mouse o;

	switch(m.buttons){
	case 8:
		sendmouse(m, MOUSE_FLAG_BUTTON4|MOUSE_FLAG_DOWN);
		sendmouse(m, MOUSE_FLAG_BUTTON4);
		return;
	case 16:
		sendmouse(m, MOUSE_FLAG_BUTTON5|MOUSE_FLAG_DOWN);
		sendmouse(m, MOUSE_FLAG_BUTTON5);
		return;
	}

	if(!eqpt(m.xy, o.xy))
		sendmouse(m, MOUSE_FLAG_MOVE);

	chg = m.buttons ^ o.buttons;
	if(chg&1){
		flags = MOUSE_FLAG_BUTTON1;
		if(m.buttons&1)
			flags |= MOUSE_FLAG_DOWN;
		sendmouse(m, flags);
	}
	if(chg&2){
		flags = MOUSE_FLAG_BUTTON3;
		if(m.buttons&2)
			flags |= MOUSE_FLAG_DOWN;
		sendmouse(m, flags);
	}
	if(chg&4){
		flags = MOUSE_FLAG_BUTTON2;
		if(m.buttons&4)
			flags |= MOUSE_FLAG_DOWN;
		sendmouse(m, flags);
	}
	o = m;
}

void
readdevmouse(void)
{
	Mouse m;
	char ev[1+4*12];

	if((mfd = open("/dev/mouse", ORDWR)) < 0)
		sysfatal("open /dev/mouse: %r");

	for(;;){
		if(read(mfd, ev, sizeof ev) != sizeof ev)
			sysfatal("mouse eof");
		if(*ev == 'm'){
				m.xy.x = atoi(ev+1);
				m.xy.y = atoi(ev+1+12);
				m.buttons = atoi(ev+1+2*12) & 0x1F;
				m.msec = atoi(ev+1+3*12);
				m.xy = subpt(m.xy, screen->r.min);
				mouseevent(m);
		}else
			eresized(1);
	}
}

void
warpmouse(int x, int y)
{
	if(mfd < 0)
		return;

	fprint(mfd, "m%d %d", x, y);
}
