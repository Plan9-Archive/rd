#include <u.h>
#include <fns.h>

short
igets(uchar* p)
{
	return p[0] | (p[1]<<8);
}
	
long
igetl(uchar* p)
{
 	return p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
}

void
iputs(uchar* p, short v)
{
	p[0]=(uchar)v;
	p[1]=(uchar)(v>>8);
}

void
iputl(uchar* p, long v)
{
	p[0]=(uchar)v;
	p[1]=(uchar)(v>>8);
	p[2]=(uchar)(v>>16);
	p[3]=(uchar)(v>>24);
}

short
nhgets(uchar* p)
{
	return (p[0]<<8) | p[1];
}

long
nhgetl(uchar* p)
{
	return (p[0]<<24) | (p[1]<<16) | (p[2]<<8) | p[3];
}

void
hnputs(uchar* p, short v)
{
	p[0]=(uchar)(v>>8);
	p[1]=(uchar)v;
}

void
hnputl(uchar* p, long v)
{
	p[0]=(uchar)(v>>24);
	p[1]=(uchar)(v>>16);
	p[2]=(uchar)(v>>8);
	p[3]=(uchar)v;
}
