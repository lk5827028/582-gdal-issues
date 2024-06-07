#!/usr/bin/env python
###############################################################################
# $Id$
#
# Project:  GDAL/OGR Test Suite
# Purpose:  Helper functions for testing CLI utilities
# Author:   Even Rouault <even dot rouault @ spatialys.com>
#
###############################################################################
# Copyright (c) 2008-2010, Even Rouault <even dot rouault at spatialys.com>
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

import os

###############################################################################
#


def get_cli_utility_path(cli_utility_name):
    build_dir = os.path.dirname(os.path.dirname(os.path.dirname(__file__)))
    return os.path.join(build_dir, "apps", cli_utility_name)


###############################################################################
#


def get_gdalinfo_path():
    return get_cli_utility_path("gdalinfo")


###############################################################################
#


def get_gdalmdiminfo_path():
    return get_cli_utility_path("gdalmdiminfo")


###############################################################################
#


def get_gdalmanage_path():
    return get_cli_utility_path("gdalmanage")


###############################################################################
#


def get_gdal_translate_path():
    return get_cli_utility_path("gdal_translate")


###############################################################################
#


def get_gdalmdimtranslate_path():
    return get_cli_utility_path("gdalmdimtranslate")


###############################################################################
#


def get_gdalwarp_path():
    return get_cli_utility_path("gdalwarp")


###############################################################################
#


def get_gdaladdo_path():
    return get_cli_utility_path("gdaladdo")


###############################################################################
#


def get_gdaltransform_path():
    return get_cli_utility_path("gdaltransform")


###############################################################################
#


def get_gdaltindex_path():
    return get_cli_utility_path("gdaltindex")


###############################################################################
#


def get_gdal_grid_path():
    return get_cli_utility_path("gdal_grid")


###############################################################################
#


def get_ogrinfo_path():
    return get_cli_utility_path("ogrinfo")


###############################################################################
#


def get_ogr2ogr_path():
    return get_cli_utility_path("ogr2ogr")


###############################################################################
#


def get_ogrtindex_path():
    return get_cli_utility_path("ogrtindex")


###############################################################################
#


def get_ogrlineref_path():
    return get_cli_utility_path("ogrlineref")


###############################################################################
#


def get_gdalbuildvrt_path():
    return get_cli_utility_path("gdalbuildvrt")


###############################################################################
#


def get_gdal_contour_path():
    return get_cli_utility_path("gdal_contour")


###############################################################################
#


def get_gdaldem_path():
    return get_cli_utility_path("gdaldem")


###############################################################################
#


def get_gdal_rasterize_path():
    return get_cli_utility_path("gdal_rasterize")


###############################################################################
#


def get_nearblack_path():
    return get_cli_utility_path("nearblack")


###############################################################################
#


def get_test_ogrsf_path():
    return get_cli_utility_path("test_ogrsf")


###############################################################################
#


def get_gdallocationinfo_path():
    return get_cli_utility_path("gdallocationinfo")


###############################################################################
#


def get_gdalsrsinfo_path():
    return get_cli_utility_path("gdalsrsinfo")


###############################################################################
#


def get_gnmmanage_path():
    return get_cli_utility_path("gnmmanage")


###############################################################################
#


def get_gnmanalyse_path():
    return get_cli_utility_path("gnmanalyse")


###############################################################################
#


def get_gdal_viewshed_path():
    return get_cli_utility_path("gdal_viewshed")


###############################################################################
#


def get_gdal_footprint_path():
    return get_cli_utility_path("gdal_footprint")
