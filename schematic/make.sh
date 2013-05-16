#!/bin/bash

boards=(interface pt2322 tda8425 msgeq7 ds1804 power encoder volume test)
ver="v_1_0"
exts=(pdf svg png ps)

for board in ${boards[*]}
do
    ./create_gerbers.sh ${board}_board/${board}.brd build/gerbers/${board}/${board}_${ver}
    ./fix_drd.sh build/gerbers/${board}/${board}_${ver}.drd
    for ext in ${exts[*]}
    do
	./create_images.sh build/gerbers/${board}/${board}_${ver}.sol build/${ext}/${board}_${ver} ${ext}
    done
    cp ${board}_board/${board}.pdf build/pdf/${board}_${ver}_schematic.pdf
done
