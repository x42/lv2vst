#!/bin/sh

DROBILLAD=$HOME/src/drobillad/
LV2GIT=$HOME/src/lv2/

TOP=`pwd`

test -d $TOP/include || exit
test -d $TOP/lib/lilv || exit

for lib in lilv serd sord sratom; do
	for file in `find lib/$lib -type f`; do
		find ${DROBILLAD}${lib} -type f -name `basename $file` -exec cp -v {} $file \;
	done
	for file in `find include/$lib -type f`; do
		find ${DROBILLAD}${lib} -type f -name `basename $file` -exec cp -v {} $file \;
	done
done

exit ## DRY-RUN

cd $LV2GIT
./waf configure --prefix=/tmp/lv2stack --no-plugins --copy-headers
./waf build install
rsync -a --delete /tmp/lv2stack/lib/lv2/ $TOP/include/lv2/
