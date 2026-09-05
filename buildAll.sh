:
# ====================================================================================
# pico-outrun build all script
# Builds picoOutRun for every supported hardware configuration, in Release.
# Binaries are copied to the releases folder.
#
# RP2350 only - every config is built with -2. HW_CONFIG 3 and 4 are RP2040-only
# boards and are not supported (Cannonball does not fit in 264 KB of SRAM);
# HW_CONFIG 11 is deprecated.
#
# HSTX boards:   2 5 8 13 14
# PicoDVI boards: 1 6 7 9 10 12
# The choice is automatic - see GPIOHSTX* in pico_shared/BoardConfigs.cmake.
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
HWCONFIGS="1 2 5 6 7 8 9 10 12 13 14"
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
