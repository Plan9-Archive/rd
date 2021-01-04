#include <u.h>
#include <libc.h>
#include <mp.h>
#include <libsec.h>
#include "dat.h"
#include "fns.h"

int
istrusted(uchar* cert, int certlen)
{
	uchar digest[SHA1dlen];
	Thumbprint *table;

	fmtinstall('H', encodefmt);
	if(cert==nil || certlen <= 0) {
		werrstr("server did not provide TLS certificate");
		return 0;
	}
	sha1(cert, certlen, digest, nil);
	table = initThumbprints("/sys/lib/tls/rdp", "/sys/lib/tls/rdp.exclude", "x224");
	if(!table || !okThumbprint(digest, SHA1dlen, table)){
		werrstr("server certificate %.*H not recognized", SHA1dlen, digest);
		return 0;
	}
	freeThumbprints(table);
	return 1;
}

/* lifted from /sys/src/cmd/upas/fs/imap4.c:/^starttls */
int
starttls(Rdp* r)
{
	TLSconn c;
	int fd, sfd;

	fd = r->fd;

	memset(&c, 0, sizeof c);
	sfd = tlsClient(fd, &c);
	if(sfd < 0){
		werrstr("tlsClient: %r");
		return -1;
	}
	if(!istrusted(c.cert, c.certlen)){
		close(sfd);
		return -1;
	}
	/* BUG: free c.cert? */

	close(r->fd);
	r->fd = sfd;
	return sfd;
}
