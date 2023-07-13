#!/bin/bash
if [ $#  -lt 2 ]; then
	echo "no parameters"
	exit 1
fi
filesdir=$1
searchstr=$2
if [ ! -d "${filesdir}" ]; then
	echo "directory ${filesdir} is not valid"
	exit 1
fi
numfind=$(grep -Ril "${searchstr}" "${filesdir}" | wc -l)
numfile=$(find "${filesdir}" -type f | wc -l)
echo "The number of files are ${numfile} and the number of matching lines are ${numfind}"

