#!/usr/bin/env python
###############################################################################
# $Id$
#
# Project:  GDAL/OGR Test Suite
# Purpose:  Test gdal_polygonize.py script
# Author:   Frank Warmerdam <warmerdam@pobox.com>
#
###############################################################################
# Copyright (c) 2008, Frank Warmerdam <warmerdam@pobox.com>
# Copyright (c) 2010, Even Rouault <even dot rouault at mines-paris dot org>
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
import sys

sys.path.append('../pymod')

import gdaltest
import ogrtest
import test_py_scripts

from osgeo import ogr

###############################################################################
# Test a fairly simple case, with nodata masking.


def test_gdal_polygonize_1():

    script_path = test_py_scripts.get_py_script('gdal_polygonize')
    if script_path is None:
        return 'skip'

    # Create a OGR datasource to put results in.
    shp_drv = ogr.GetDriverByName('ESRI Shapefile')
    try:
        os.stat('tmp/poly.shp')
        shp_drv.DeleteDataSource('tmp/poly.shp')
    except OSError:
        pass

    shp_ds = shp_drv.CreateDataSource('tmp/poly.shp')

    shp_layer = shp_ds.CreateLayer('poly', None, ogr.wkbPolygon)

    fd = ogr.FieldDefn('DN', ogr.OFTInteger)
    shp_layer.CreateField(fd)

    shp_ds.Destroy()

    # run the algorithm.
    test_py_scripts.run_py_script(script_path, 'gdal_polygonize', '../alg/data/polygonize_in.grd tmp poly DN')

    # Confirm we get the set of expected features in the output layer.

    shp_ds = ogr.Open('tmp')
    shp_lyr = shp_ds.GetLayerByName('poly')

    expected_feature_number = 13
    if shp_lyr.GetFeatureCount() != expected_feature_number:
        gdaltest.post_reason('GetFeatureCount() returned %d instead of %d' % (shp_lyr.GetFeatureCount(), expected_feature_number))
        return 'fail'

    expect = [107, 123, 115, 115, 140, 148, 123, 140, 156,
              100, 101, 102, 103]

    tr = ogrtest.check_features_against_list(shp_lyr, 'DN', expect)

    # check at least one geometry.
    if tr:
        shp_lyr.SetAttributeFilter('dn = 156')
        feat_read = shp_lyr.GetNextFeature()
        if ogrtest.check_feature_geometry(feat_read, 'POLYGON ((440720 3751200,440900 3751200,440900 3751020,440720 3751020,440720 3751200),(440780 3751140,440780 3751080,440840 3751080,440840 3751140,440780 3751140))') != 0:
            tr = 0
        feat_read.Destroy()

    shp_ds.Destroy()
    # Reload drv because of side effects of run_py_script()
    shp_drv = ogr.GetDriverByName('ESRI Shapefile')
    shp_drv.DeleteDataSource('tmp/poly.shp')

    return 'success' if tr else 'fail'

###############################################################################
# Test a simple case without masking.


def test_gdal_polygonize_2():

    script_path = test_py_scripts.get_py_script('gdal_polygonize')
    if script_path is None:
        return 'skip'

    shp_drv = ogr.GetDriverByName('ESRI Shapefile')
    try:
        os.stat('tmp/out.shp')
        shp_drv.DeleteDataSource('tmp/out.shp')
    except OSError:
        pass

    # run the algorithm.
    test_py_scripts.run_py_script(script_path, 'gdal_polygonize', '-b 1 -f "ESRI Shapefile" -q -nomask ../alg/data/polygonize_in.grd tmp')

    # Confirm we get the set of expected features in the output layer.
    shp_ds = ogr.Open('tmp')
    shp_lyr = shp_ds.GetLayerByName('out')

    expected_feature_number = 17
    if shp_lyr.GetFeatureCount() != expected_feature_number:
        gdaltest.post_reason('GetFeatureCount() returned %d instead of %d' % (shp_lyr.GetFeatureCount(), expected_feature_number))
        return 'fail'

    expect = [107, 123, 115, 132, 115, 132, 140, 132, 148, 123, 140,
              132, 156, 100, 101, 102, 103]

    tr = ogrtest.check_features_against_list(shp_lyr, 'DN', expect)

    shp_ds.Destroy()
    # Reload drv because of side effects of run_py_script()
    shp_drv = ogr.GetDriverByName('ESRI Shapefile')
    shp_drv.DeleteDataSource('tmp/out.shp')

    return 'success' if tr else 'fail'


def test_gdal_polygonize_3():

    script_path = test_py_scripts.get_py_script('gdal_polygonize')
    if script_path is None:
        return 'skip'

    drv = ogr.GetDriverByName('GPKG')
    if drv is None:
        return 'skip'
    try:
        os.stat('tmp/out.gpkg')
        drv.DeleteDataSource('tmp/out.gpkg')
    except OSError:
        pass

    # run the algorithm.
    test_py_scripts.run_py_script(script_path, 'gdal_polygonize', '-b 1 -f "GPKG" -q -nomask ../alg/data/polygonize_in.grd tmp/out.gpkg')

    # Confirm we get the set of expected features in the output layer.
    gpkg_ds = ogr.Open('tmp/out.gpkg')
    gpkg_lyr = gpkg_ds.GetLayerByName('out')
    geom_type = gpkg_lyr.GetGeomType()
    geom_is_polygon = geom_type in (ogr.wkbPolygon, ogr.wkbMultiPolygon)

    gpkg_ds.Destroy()
    # Reload drv because of side effects of run_py_script()
    drv = ogr.GetDriverByName('GPKG')
    drv.DeleteDataSource('tmp/out.gpkg')

    if geom_is_polygon:
        return 'success'
    gdaltest.post_reason('GetGeomType() returned %d instead of %d or %d (ogr.wkbPolygon or ogr.wkbMultiPolygon)' % (geom_type, ogr.wkbPolygon, ogr.wkbMultiPolygon))
    return 'fail'

###############################################################################
# Test -b mask


def test_gdal_polygonize_4():

    script_path = test_py_scripts.get_py_script('gdal_polygonize')
    if script_path is None:
        return 'skip'

    # Test mask syntax
    test_py_scripts.run_py_script(script_path, 'gdal_polygonize', '-q -f GML -b mask ../gcore/data/byte.tif tmp/out.gml')

    content = open('tmp/out.gml', 'rt').read()

    os.unlink('tmp/out.gml')

    if content.find('<ogr:geometryProperty><gml:Polygon srsName="EPSG:26711"><gml:outerBoundaryIs><gml:LinearRing><gml:coordinates>440720,3751320 440720,3750120 441920,3750120 441920,3751320 440720,3751320</gml:coordinates></gml:LinearRing></gml:outerBoundaryIs></gml:Polygon></ogr:geometryProperty>') < 0:
        gdaltest.post_reason('fail')
        print(content)
        return 'fail'

    # Test mask,1 syntax
    test_py_scripts.run_py_script(script_path, 'gdal_polygonize', '-q -f GML -b mask,1 ../gcore/data/byte.tif tmp/out.gml')

    content = open('tmp/out.gml', 'rt').read()

    os.unlink('tmp/out.gml')

    if content.find('<ogr:geometryProperty><gml:Polygon srsName="EPSG:26711"><gml:outerBoundaryIs><gml:LinearRing><gml:coordinates>440720,3751320 440720,3750120 441920,3750120 441920,3751320 440720,3751320</gml:coordinates></gml:LinearRing></gml:outerBoundaryIs></gml:Polygon></ogr:geometryProperty>') < 0:
        gdaltest.post_reason('fail')
        print(content)
        return 'fail'

    return 'success'


gdaltest_list = [
    test_gdal_polygonize_1,
    test_gdal_polygonize_2,
    test_gdal_polygonize_3,
    test_gdal_polygonize_4
]

if __name__ == '__main__':

    gdaltest.setup_run('test_gdal_polygonize')

    gdaltest.run_tests(gdaltest_list)

    gdaltest.summarize()
