/* cap.c */
void		scangencaps(uchar*,uchar*);
void		scanbitcaps(uchar*,uchar*);
uchar*	putgencaps(uchar*,uchar*);
uchar*	putbitcaps(uchar*,uchar*);
uchar*	putordcaps(uchar*,uchar*);
uchar*	putbc2caps(uchar*,uchar*);
uchar*	putptrcaps(uchar*,uchar*);
uchar*	putinpcaps(uchar*,uchar*);
uchar*	putsndcaps(uchar*,uchar*);
uchar*	putglycaps(uchar*,uchar*);

/* load.c */
int		loadbmp(Image*,Rectangle,uchar*,int);
int		loadrle(Image*,Rectangle,uchar*,int);

/* mcs.c */
int		mcschanid(uchar*,uchar*);
int		mcstype(uchar*,uchar*);
int		ismcshangup(uchar*,uchar*);
uchar*	mcspayload(uchar*,uchar*);
int		mkmcsci(uchar*, int, int);
int		mkmcssdr(uchar*,int,int,int);
int		mcsconnect(int);
void		erectdom(int);
int		attachuser(int);
int		joinchannel(int,int);

/* mpas.c */
int		rdphandshake(int);
void		readnet(int);
int		isflowpdu(uchar*,uchar*);
void		scanaspdu(uchar*, uchar*);
void		scandatapdu(uchar*,uchar*);
void		activating(uchar*,uchar*);
void		passinput(ulong,int,int,int,int);
void		turnupdates(int);
int		sizesechdr(int);
uchar*	prebuf(uchar*,int,int,int,int);

/* draw.c */
void		eresized(int);

/* eclip.c */
void		clipannounce(void);
void		clipvcfn(uchar*,uchar*);

/* egdi.c */
void		scanorders(uchar*,uchar*,int);

/* ele.c */
void		scanlicensepdu(uchar*,uchar*);

/* snarf.c */
void		initsnarf(void);
void		pollsnarf(void);
char*	getsnarf(int*);
void		putsnarf(char*,int);

/* mouse.c */
void		readdevmouse(void);
void		warpmouse(int,int);

/* kbd.c */
void		readkbd(void);

/* sec.c */

/* mppc.c */
uchar*	uncomp(uchar*,int,int,int*);

/* rle.c */
uchar*	unrle(uchar*,int, uchar*,int,int,int);

/* utf16.c */
int		fromutf16(char*,int,uchar*,int);
int		toutf16(uchar*,int,char*,int);

/* x224.c */
int		mktpdat(uchar*,int,int);
int		readpdu(int,uchar*,uint);
int		mktpcr(uchar*,int,int);
int		mktpdr(uchar*,int,int);
int		istpkt(uchar*,uchar*);
int		tpdutype(uchar*,uchar*);
int		isdatatpdu(uchar*,uchar*);
uchar*	tpdupayload(uchar*,uchar*);
int		x224connect(int);
int		x224disconnect(int);
int		starttls(void);

/* rd.c */
void		atexitkiller(void);
void		atexitkill(int pid);
void*	emalloc(ulong);
void*	erealloc(void*,ulong);
char*	estrdup(char*);
long		writen(int,void*,long);

