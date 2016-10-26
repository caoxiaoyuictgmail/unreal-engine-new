#!/bin/sh

ZLIB_HTML5=$(pwd)

cd ../../../../HTML5/
	. ./Build_All_HTML5_libs.rc
cd "$ZLIB_HTML5"


cd ..

proj=zlib
makefile=html5/Makefile.HTML5
EMFLAGS="-msse -msse2 -s FULL_ES2=1 -s USE_PTHREADS=1 -Wno-shift-negative-value"

make clean   OPTIMIZATION=-O3 CFLAGS_EXTRA="$EMFLAGS" LIB=${proj}_O3.bc -f ${makefile}
make install OPTIMIZATION=-O3 CFLAGS_EXTRA="$EMFLAGS" LIB=${proj}_O3.bc -f ${makefile}

make clean   OPTIMIZATION=-O2 CFLAGS_EXTRA="$EMFLAGS" LIB=${proj}_O2.bc -f ${makefile}
make install OPTIMIZATION=-O2 CFLAGS_EXTRA="$EMFLAGS" LIB=${proj}_O2.bc -f ${makefile}

make clean   OPTIMIZATION=-Oz CFLAGS_EXTRA="$EMFLAGS" LIB=${proj}_Oz.bc -f ${makefile}
make install OPTIMIZATION=-Oz CFLAGS_EXTRA="$EMFLAGS" LIB=${proj}_Oz.bc -f ${makefile}

make clean   OPTIMIZATION=-O0 CFLAGS_EXTRA="$EMFLAGS" LIB=${proj}.bc -f ${makefile}
make install OPTIMIZATION=-O0 CFLAGS_EXTRA="$EMFLAGS" LIB=${proj}.bc -f ${makefile}

ls -l ../Lib/HTML5

