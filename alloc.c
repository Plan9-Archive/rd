#include <u.h>
#include <libc.h>
#include "dat.h"
#include "fns.h"

void*
emalloc(ulong n)
{
	void *b;

	b = mallocz(n, 1);
	if(b == nil)
		sysfatal("out of memory allocating %lud: %r", n);
	setmalloctag(b, getcallerpc(&n));
	return b;
}

void*
erealloc(void *a, ulong n)
{
	void *b;

	b = realloc(a, n);
	if(b == nil)
		sysfatal("out of memory re-allocating %lud: %r", n);
	setrealloctag(b, getcallerpc(&a));
	return b;
}
