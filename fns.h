/* load.c */
int		loadbmp(Image*,Rectangle,uchar*,int,uchar*);
int		loadrle(Image*,Rectangle,uchar*,int,uchar*);

/* mcs.c */
int		mcschan(uchar*,uchar*);
int		mcstype(uchar*,uchar*);
int		ismcshangup(uchar*,uchar*);
uchar*	mcsdat(uchar*,uchar*);
int		mkmcsci(uchar*, int, int);
int		putmsdr(uchar*,int,int,int,int);

/* mpas.c */
int		isflowpdu(uchar*,uchar*);
int		sizesechdr(int);
uchar*	txprep(uchar*,int,int,int,int,int);

/* snarf.c */
void		initsnarf(void);
char*	getsnarf(int*);
void		putsnarf(char*,int);

/* mouse.c */
void		warpmouse(int,int);

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
int		tptype(uchar*,uchar*);
int		istpdat(uchar*,uchar*);
uchar*	tpdat(uchar*,uchar*);

/* rd.c */
void		atexitkiller(void);
void		atexitkill(int pid);
void*	emalloc(ulong);
void*	erealloc(void*,ulong);
char*	estrdup(char*);
long		writen(int,void*,long);

uchar*	gblen(uchar*,uchar*,int*);
uchar*	gbtag(uchar*,uchar*,int*);
void		pbshort(uchar*,int);

uchar*	putsdh(uchar*,uchar*,int,int,int,int);
