#!/bin/tcsh

pushd ./src
make
popd

# Branch prediction select 
#   0: gshare
#   1: bimodal
#   2: tage
#set sel = 0
#set sel = 1
set sel = 2

set list = ( \
  164.gzip \
  175.vpr \
  176.gcc \
  181.mcf \
  186.crafty \
  197.parser \
  201.compress \
  202.jess \
  205.raytrace \
  209.db \
  213.javac \
  222.mpegaudio \
  227.mtrt \
  228.jack \
  252.eon \
  253.perlbmk \
  254.gap \
  255.vortex \
  256.bzip2 \
  300.twolf \
)

rm -f log.txt
touch log.txt
foreach l ($list)
  echo $l
  echo $l >> log.txt
  ./run ./traces/$l $sel >> log.txt

  echo "\n" >> log.txt
end
