</$objtype/mkfile

TARG=rd
BIN=/$objtype/bin

HFILES=fns.h dat.h
CLEANFILES= x509.c
OFILES=\
	cap.$O\
	eclip.$O\
	egdi.$O\
	ele.$O\
	kbd.$O\
	rle.$O\
	load.$O\
	mcs.$O\
	mouse.$O\
	mpas.$O\
	mppc.$O\
	rd.$O\
	utf16.$O\
	vchan.$O\
	wsys.$O\
	x224.$O\

</sys/src/cmd/mkone

x509.c:	/sys/src/libsec/port/x509.c
	sed '
	/^	ALG_sha1WithRSAEncryption,/a\
		ALG_sha256WithRSAEncryption,\
		ALG_shaWithRSASignatureOiw,
	/^static Ints7 oid_sha1WithRSAEncryption =/a\
	static Ints7 oid_sha256WithRSAEncryption ={7, 1, 2, 840, 113549, 1, 1, 11 };\
	static Ints7 oid_shaWithRSASignatureOiw ={6, 1, 3, 14, 3, 2, 15 };
	/^	\(Ints\*\)\&oid_sha1WithRSAEncryption,/a\
		(Ints*)&oid_sha256WithRSAEncryption,\
		(Ints*)&oid_shaWithRSASignatureOiw,
	/^static DigestFun digestalg/ s/sha1,/sha1, sha2_256, sha1,/
	' $prereq > $target

$TARG: mkfile
