#!/bin/sh

echo $# $1
if [ "$#" != "1" ]; then
	exit 0;
fi

echo "deploying..."
cp utils/libclover_utils.so $1/src/swift/clover/lib
cp utils/*.h $1/src/swift/clover/include

