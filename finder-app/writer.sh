#!/bin/bash
if [ $#  -lt 2 ]; then
        echo "no parameters"
        exit 1
fi
writefile=$1
writestr=$2
err1=$(mkdir -p $(dirname $writefile))
err=$(echo $writestr > $writefile | wc -l)
if [ $err -gt 0 ]; then
	echo "can not create file ${writefile}"
	exit 1
fi

