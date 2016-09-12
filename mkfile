</$objtype/mkfile
<$PLAN9/src/mkhdr

TARG=rd
BIN=/$objtype/bin

HFILES=fns.h dat.h
OFILES=\
	alloc.$O\
	audio.$O\
	cap.$O\
	draw.$O\
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
	msg.$O\
	rd.$O\
	rpc.$O\
	tls.$O\
	utf16.$O\
	vchan.$O\
	wsys.$O\
	x224.$O\

THREADOFILES=${OFILES:rd.$O=rd-thread.$O}
CLEANFILES=$O.thread

</sys/src/cmd/mkone
<$PLAN9/src/mkone

$TARG: mkfile

default:V: $O.thread
all:V: $O.thread

$O.thread:	$THREADOFILES $LIB
	$LD $LDFLAGS -o $target $prereq
