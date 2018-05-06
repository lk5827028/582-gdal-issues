#!/usr/bin/env python
###############################################################################
# $Id$
#
# Project:  GDAL Python samples
# Purpose:  Script to produce a shaded relief image from elevation data
# Author:   Andrey Kiselev, dron@remotesensing.org
#
###############################################################################
# Copyright (c) 2003, Andrey Kiselev <dron@remotesensing.org>
# Copyright (c) 2009, Even Rouault <even dot rouault at mines-paris dot org>
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
# OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
###############################################################################

import math
import sys

try:
    import numpy as Numeric
    Numeric.arrayrange = Numeric.arange
except ImportError:
    import Numeric

try:
    from osgeo import gdal_array as gdalnumeric
    from osgeo import gdal
    gdal.TermProgress = gdal.TermProgress_nocb
except ImportError:
    import gdalnumeric

# =============================================================================


def Usage():
    print('Usage: rel.py -lsrcaz azimuth -lsrcel elevation [-elstep step]')
    print('       [-dx xsize] [-dy ysize] [-b band] [-ot type] infile outfile')
    print('Produce a shaded relief image from elevation data')
    print('')
    print('  -lsrcaz azimuth   Azimuth angle of the diffuse light source (0..360 degrees)')
    print('  -lsrcel elevation Elevation angle of the diffuse light source (0..180 degrees)')
    print('  -elstep step      Elevation change corresponding to a change of one grey level')
    print('                    (default 1)')
    print('  -dx xsize         X and Y dimensions (in meters) of one pixel on the ground')
    print('  -dy ysize         (taken from the geotransform matrix by default)')
    print('  -r range	       Dynamic range for output image (default 255)')
    print('  -b band	       Select a band number to convert (default 1)')
    print('  -ot type	       Data type of the output dataset')
    print('                    (Byte/Int16/UInt16/UInt32/Int32/Float32/Float64/')
    print('                     CInt16/CInt32/CFloat32/CFloat64, default is Byte)')
    print('  infile	       Name of the input file')
    print('  outfile	       Name of the output file')
    print('')
    sys.exit(1)

# =============================================================================

# =============================================================================


def ParseType(type):
    if type == 'Byte':
        return gdal.GDT_Byte
    if type == 'Int16':
        return gdal.GDT_Int16
    if type == 'UInt16':
        return gdal.GDT_UInt16
    if type == 'Int32':
        return gdal.GDT_Int32
    if type == 'UInt32':
        return gdal.GDT_UInt32
    if type == 'Float32':
        return gdal.GDT_Float32
    if type == 'Float64':
        return gdal.GDT_Float64
    if type == 'CInt16':
        return gdal.GDT_CInt16
    if type == 'CInt32':
        return gdal.GDT_CInt32
    if type == 'CFloat32':
        return gdal.GDT_CFloat32
    if type == 'CFloat64':
        return gdal.GDT_CFloat64
    return gdal.GDT_Byte
# =============================================================================


infile = None
outfile = None
iBand = 1	    # The first band will be converted by default
frmt = 'GTiff'
type = gdal.GDT_Byte

lsrcaz = None
lsrcel = None
elstep = 1.0
xsize = None
ysize = None
dyn_range = 255.0

# Parse command line arguments.
i = 1
while i < len(sys.argv):
    arg = sys.argv[i]

    if arg == '-b':
        i += 1
        iBand = int(sys.argv[i])

    elif arg == '-ot':
        i += 1
        type = ParseType(sys.argv[i])

    elif arg == '-lsrcaz':
        i += 1
        lsrcaz = float(sys.argv[i])

    elif arg == '-lsrcel':
        i += 1
        lsrcel = float(sys.argv[i])

    elif arg == '-elstep':
        i += 1
        elstep = float(sys.argv[i])

    elif arg == '-dx':
        i += 1
        xsize = float(sys.argv[i])

    elif arg == '-dy':
        i += 1
        ysize = float(sys.argv[i])

    elif arg == '-r':
        i += 1
        dyn_range = float(sys.argv[i])

    elif infile is None:
        infile = arg

    elif outfile is None:
        outfile = arg

    else:
        Usage()

    i += 1

if infile is None:
    Usage()
if outfile is None:
    Usage()
if lsrcaz is None:
    Usage()
if lsrcel is None:
    Usage()

# translate angles from degrees to radians
lsrcaz = lsrcaz / 180.0 * math.pi
lsrcel = lsrcel / 180.0 * math.pi

lx = -math.sin(lsrcaz) * math.cos(lsrcel)
ly = math.cos(lsrcaz) * math.cos(lsrcel)
lz = math.sin(lsrcel)
lxyz = math.sqrt(lx**2 + ly**2 + lz**2)

indataset = gdal.Open(infile, gdal.GA_ReadOnly)
if indataset is None:
    print('Cannot open', infile)
    sys.exit(2)

if indataset.RasterXSize < 3 or indataset.RasterYSize < 3:
    print('Input image is too small to process, minimum size is 3x3')
    sys.exit(3)

out_driver = gdal.GetDriverByName(frmt)
outdataset = out_driver.Create(outfile, indataset.RasterXSize, indataset.RasterYSize, indataset.RasterCount, type)
outband = outdataset.GetRasterBand(1)

geotransform = indataset.GetGeoTransform()
projection = indataset.GetProjection()

if xsize is None:
    xsize = abs(geotransform[1])
if ysize is None:
    ysize = abs(geotransform[5])

inband = indataset.GetRasterBand(iBand)
if inband is None:
    print('Cannot load band', iBand, 'from the', infile)
    sys.exit(2)

numtype = gdalnumeric.GDALTypeCodeToNumericTypeCode(type)
outline = Numeric.empty((1, inband.XSize), numtype)

prev = inband.ReadAsArray(0, 0, inband.XSize, 1, inband.XSize, 1)[0]
outband.WriteArray(outline, 0, 0)
gdal.TermProgress(0.0)

cur = inband.ReadAsArray(0, 1, inband.XSize, 1, inband.XSize, 1)[0]
outband.WriteArray(outline, 0, inband.YSize - 1)
gdal.TermProgress(1.0 / inband.YSize)

dx = 2 * xsize
dy = 2 * ysize

for i in range(1, inband.YSize - 1):
    next_ = inband.ReadAsArray(0, i + 1, inband.XSize, 1, inband.XSize, 1)[0]
    dzx = (cur[0:-2] - cur[2:]) * elstep
    dzy = (prev[1:-1] - next_[1:-1]) * elstep
    nx = -dy * dzx
    ny = dx * dzy
    nz = dx * dy
    nxyz = nx * nx + ny * ny + nz * nz
    nlxyz = nx * lx + ny * ly + nz * lz
    cosine = dyn_range * (nlxyz / (lxyz * Numeric.sqrt(nxyz)))
    cosine = Numeric.clip(cosine, 0.0, dyn_range)
    outline[0, 1:-1] = cosine.astype(numtype)
    outband.WriteArray(outline, 0, i)

    prev = cur
    cur = next_

    # Display progress report on terminal
    gdal.TermProgress(float(i + 1) / (inband.YSize - 1))

outdataset.SetGeoTransform(geotransform)
outdataset.SetProjection(projection)
