#!/bin/sh
# GDAL specific script to extract exported libtiff symbols that can be renamed
# to keep them internal to GDAL as much as possible

gcc ./*.c -fPIC -shared -o libtiff.so -I. -I../../../port  -DPIXARLOG_SUPPORT -DZIP_SUPPORT -DOJPEG_SUPPORT -DLZMA_SUPPORT -DZSTD_SUPPORT ${ZSTD_INCLUDE}

OUT_FILE=gdal_libtiff_symbol_rename.h

rm $OUT_FILE 2>/dev/null

echo "/* This is a generated file by dump_symbols.h. *DO NOT EDIT MANUALLY !* */" >> $OUT_FILE

symbol_list=$(objdump -t libtiff.so  | grep .text | awk '{print $6}' | grep -v .text | grep -v TIFFInit | grep -v __do_global | grep -v __bss_start | grep -v _edata | grep -v _end | grep -v _fini | grep -v _init | grep -v call_gmon_start | grep -v CPL_IGNORE_RET_VAL_INT | grep -v register_tm_clones | sort)
for symbol in $symbol_list
do
    echo "#define $symbol gdal_$symbol" >> $OUT_FILE
done

rodata_symbol_list=$(objdump -t libtiff.so  | grep "\\.rodata" |  awk '{print $6}' | grep -v "\\.")
for symbol in $rodata_symbol_list
do
    echo "#define $symbol gdal_$symbol" >> $OUT_FILE
done

data_symbol_list=$(objdump -t libtiff.so  | grep "\\.data"  | grep -v __dso_handle | grep -v __TMC_END__ |  awk '{print $6}' | grep -v "\\.")
for symbol in $data_symbol_list
do
    echo "#define $symbol gdal_$symbol" >> $OUT_FILE
done

bss_symbol_list=$(objdump -t libtiff.so  | grep "\\.bss" |  awk '{print $6}' | grep -v "\\.")
for symbol in $bss_symbol_list
do
    echo "#define $symbol gdal_$symbol" >> $OUT_FILE
done

rm libtiff.so

cat <<EOF > $OUT_FILE

#define DISABLE_CHECK_TIFFSWABMACROS

EOF

{
    # Was excluded by grep -v TIFFInit
    echo "#define TIFFInitDumpMode gdal_TIFFInitDumpMode"

    # Pasted and adapted from tif_codec.c
    echo "#define TIFFReInitJPEG_12 gdal_TIFFReInitJPEG_12"
    echo "#define TIFFJPEGIsFullStripRequired_12 gdal_TIFFJPEGIsFullStripRequired_12"
    echo "#ifdef LZW_SUPPORT"
    echo "#define TIFFInitLZW gdal_TIFFInitLZW"
    echo "#endif"
    echo "#ifdef PACKBITS_SUPPORT"
    echo "#define TIFFInitPackBits gdal_TIFFInitPackBits"
    echo "#endif"
    echo "#ifdef THUNDER_SUPPORT"
    echo "#define TIFFInitThunderScan gdal_TIFFInitThunderScan"
    echo "#endif"
    echo "#ifdef NEXT_SUPPORT"
    echo "#define TIFFInitNeXT gdal_TIFFInitNeXT"
    echo "#endif"
    echo "#ifdef JPEG_SUPPORT"
    echo "#define TIFFInitJPEG gdal_TIFFInitJPEG"
    # Manually added
    echo "#define TIFFInitJPEG_12 gdal_TIFFInitJPEG_12"
    echo "#endif"
    echo "#ifdef OJPEG_SUPPORT"
    echo "#define TIFFInitOJPEG gdal_TIFFInitOJPEG"
    echo "#endif"
    echo "#ifdef CCITT_SUPPORT"
    echo "#define TIFFInitCCITTRLE gdal_TIFFInitCCITTRLE"
    echo "#define TIFFInitCCITTRLEW gdal_TIFFInitCCITTRLEW"
    echo "#define TIFFInitCCITTFax3 gdal_TIFFInitCCITTFax3"
    echo "#define TIFFInitCCITTFax4 gdal_TIFFInitCCITTFax4"
    echo "#endif"
    echo "#ifdef JBIG_SUPPORT"
    echo "#define TIFFInitJBIG gdal_TIFFInitJBIG"
    echo "#endif"
    echo "#ifdef ZIP_SUPPORT"
    echo "#define TIFFInitZIP gdal_TIFFInitZIP"
    echo "#endif"
    echo "#ifdef PIXARLOG_SUPPORT"
    echo "#define TIFFInitPixarLog gdal_TIFFInitPixarLog"
    echo "#endif"
    echo "#ifdef LOGLUV_SUPPORT"
    echo "#define TIFFInitSGILog gdal_TIFFInitSGILog"
    echo "#endif"
    echo "#ifdef LZMA_SUPPORT"
    echo "#define TIFFInitLZMA gdal_TIFFInitLZMA"
    echo "#endif"
} >> $OUT_FILE
