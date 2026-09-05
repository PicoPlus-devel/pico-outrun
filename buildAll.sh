:
# ====================================================================================
# pico-outrun build all script
# Builds picoOutRun for every supported hardware configuration, in Release.
# Binaries are copied to the releases folder.
#
# Only the four HSTX boards with PSRAM are supported: 2, 8, 13 and 14. Every
# config is built with -2 (RP2350 - the build refuses RP2040).
#
# PSRAM is mandatory: the engine's big buffers go there through Frens::f_malloc,
# so a board without it reports "This board has no PSRAM" and stops.
#
# HSTX is mandatory too: the bit-banged PicoDVI path ties the system clock to the
# pixel clock and cannot exceed 324 MHz, where the engine is too slow, and it puts
# the sound chain back on core0. The remaining board configs still build, but they
# are not supported - see README.md.
# ====================================================================================
cd `dirname $0` || exit 1
[ -d releases ] && rm -rf releases
mkdir releases || exit 1
# check picotool exists in path
if ! command -v picotool &> /dev/null
then
	echo "picotool could not be found"
	echo "Please install picotool from https://github.com/raspberrypi/picotool.git"
	exit 1
fi
HWCONFIGS="2 8 13 14"
for HWCONFIG in $HWCONFIGS
do
	./bld.sh -c $HWCONFIG -2 || exit 1
done
if [ -z "$(ls -A releases)" ]; then
	echo "No UF2 files found in releases folder"
	exit 1
fi
for UF2 in releases/*.uf2
do
	ls -l $UF2
	picotool info $UF2
	echo " "
done
