#!/bin/sh

# ISCC currently crashes with a fake "Out of memory" when running
# under wine. This wrapper loops up to MAX times until ISCC finished
# successfully.

CNT=0
MAX=5

EOUT=isccerr.$$
WINE_VERSION=`wine --version|sed -e 's/wine-//'|tr . 0`
BADWINE=
if [ $WINE_VERSION -lt 103021 ] ; then
    BADWINE=-dBADWINE=1
fi
while test $CNT -lt $MAX ; do
     "" "$@" $BADWINE 2>$EOUT
    R=$?
    case $R in
        0)
        rm -f $EOUT
        exit 0;
        ;;
        *)
        cat $EOUT
        OMM=`grep "Out of memory" $EOUT`
        rm -f $EOUT
        test -z "$OMM" && exit $R
        echo "ISCC got out of memory, retrying ..."
        sleep 2
        CNT=`expr $CNT + 1`
        ;;
    esac
done
