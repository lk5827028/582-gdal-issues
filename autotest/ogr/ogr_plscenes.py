#!/usr/bin/env pytest
# -*- coding: utf-8 -*-
###############################################################################
# $Id$
#
# Project:  GDAL/OGR Test Suite
# Purpose:  PlanetLabs scene driver test suite.
# Author:   Even Rouault, even dot rouault at spatialys.com
#
###############################################################################
# Copyright (c) 2015, Planet Labs
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


from osgeo import ogr
from osgeo import gdal

import gdaltest
import pytest


pytestmark = pytest.mark.require_driver('PLScenes')


###############################################################################
# Test Data V1 API catalog listing with a single catalog


def test_ogr_plscenes_data_v1_catalog_no_paging():
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types', '{ "item_types": [ { "id": "PSScene3Band" } ] }')
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    ds = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1', 'API_KEY=foo'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds is not None
    with gdaltest.error_handler():
        assert ds.GetLayerByName('non_existing') is None
    assert ds.GetLayerByName('PSScene3Band') is not None
    assert ds.GetLayerCount() == 1
    with gdaltest.error_handler():
        assert ds.GetLayerByName('non_existing') is None

    gdal.Unlink('/vsimem/data_v1/item-types')

###############################################################################
# Test Data V1 API catalog listing with catalog paging


def test_ogr_plscenes_data_v1_catalog_paging():
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types', '{"_links": { "_next" : "/vsimem/data_v1/item-types/page_2"}, "item_types": [ { "id": "PSScene3Band" } ] }')
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types/page_2', '{ "item_types": [ { "id": "PSScene4Band" } ] }')
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    ds = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1', 'API_KEY=foo'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds is not None
    with gdaltest.error_handler():
        assert ds.GetLayerByName('non_existing') is None
    assert ds.GetLayerByName('PSScene3Band') is not None
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types/PSScene4Band', '{ "id": "PSScene4Band"} }')
    assert ds.GetLayerByName('PSScene4Band') is not None
    assert ds.GetLayerCount() == 2
    assert ds.GetLayerByName('PSScene4Band') is not None
    with gdaltest.error_handler():
        assert ds.GetLayerByName('non_existing') is None

    gdal.Unlink('/vsimem/data_v1/item-types')
    gdal.Unlink('/vsimem/data_v1/item-types/page_2')
    gdal.Unlink('/vsimem/data_v1/item-types/PSScene4Band')

###############################################################################
# Test Data V1 API


def test_ogr_plscenes_data_v1_nominal():
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types',
                           """{ "item_types": [
    {"display_description" : "display_description",
     "display_name" : "display_name",
     "id": "PSOrthoTile"}
]}""")
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    ds = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1', 'API_KEY=foo'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds is not None

    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    ds = gdal.OpenEx('PLScenes:version=data_v1,api_key=foo,FOLLOW_LINKS=YES', gdal.OF_VECTOR)
    gdal.SetConfigOption('PL_URL', None)
    assert ds is not None

    lyr = ds.GetLayer(0)
    assert lyr.GetName() == 'PSOrthoTile'
    assert (lyr.TestCapability(ogr.OLCFastFeatureCount) == 1 and \
       lyr.TestCapability(ogr.OLCStringsAsUTF8) == 1 and \
       lyr.TestCapability(ogr.OLCRandomRead) == 0)
    # Different serialization depending on libjson versions
    gdal.FileFromMemBuffer("""/vsimem/data_v1/stats&POSTFIELDS={"interval":"year","item_types":["PSOrthoTile"],"filter":{"type":"AndFilter","config":[{"type":"RangeFilter","field_name":"cloud_cover","config":{"gte":0.000000}}]}}""", """{ "buckets": [ { "count": 1 }, { "count": 1} ] }""")
    gdal.FileFromMemBuffer("""/vsimem/data_v1/stats&POSTFIELDS={"interval":"year","item_types":["PSOrthoTile"],"filter":{"type":"AndFilter","config":[{"type":"RangeFilter","field_name":"cloud_cover","config":{"gte":0}}]}}""", """{ "buckets": [ { "count": 1 }, { "count": 1} ] }""")
    gdal.FileFromMemBuffer("""/vsimem/data_v1/stats&POSTFIELDS={"interval":"year","item_types":["PSOrthoTile"],"filter":{"type":"AndFilter","config":[{"type":"RangeFilter","field_name":"cloud_cover","config":{"gte":0.0}}]}}""", """{ "buckets": [ { "count": 1 }, { "count": 1} ] }""")
    assert lyr.GetFeatureCount() == 2
    assert lyr.GetGeomType() == ogr.wkbMultiPolygon
    ext = lyr.GetExtent()
    assert ext == (-180.0, 180.0, -90.0, 90.0)

    field_count = lyr.GetLayerDefn().GetFieldCount()
    assert field_count == 78

    # Regular /items/ fetching
    gdal.FileFromMemBuffer("""/vsimem/data_v1/quick-search?_page_size=250&POSTFIELDS={"item_types":["PSOrthoTile"],"filter":{"type":"AndFilter","config":[]}}""",
                           """{
    "_links":
    {
        "_next": "/vsimem/data_v1/quick-search?page=2"
    },
    "features" : [
        {
            "id": "id",
            "_links" : {
                "_self" : "self",
                "assets" : "/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets"
            },
            "_permissions" : [ "download" ],
            "properties": {
                "acquired": "2016/02/11 12:34:56.789+00",
                "anomalous_pixels": 1.23,
                "columns": 1,
                "item_type": "foo",
                "ground_control": true
            },
            "geometry":
            {
                "type": "Polygon",
                "coordinates" : [ [ [2,49],[2,49.1],[2.1,49.1],[2.1,49],[2,49] ] ]
            }
        }
    ]
}""")

    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets',
                           """{
  "analytic" : {
      "_permissions": ["download"],
      "_links": {
        "_self": "analytic_links_self",
        "activate": "/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets/analytic/activate",
      },
      "location": "/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets/analytic/my.tiff",
      "status": "active",
      "expires_at": "2016-02-11T12:34:56.789"
  }
}""")

    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    ds = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1', 'API_KEY=foo', 'FOLLOW_LINKS=YES'])
    gdal.SetConfigOption('PL_URL', None)
    lyr = ds.GetLayer(0)

    f = lyr.GetNextFeature()
    if f.GetFID() != 1 or f['id'] != 'id' or f['self_link'] != 'self' or \
       f['assets_link'] != '/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets' or \
       f['acquired'] != '2016/02/11 12:34:56.789+00' or \
       f['anomalous_pixels'] != 1.23 or f['item_type'] != 'foo' or f['columns'] != 1 or \
       not f['ground_control'] or \
       f['asset_analytic_self_link'] != 'analytic_links_self' or \
       f['asset_analytic_activate_link'] != '/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets/analytic/activate' or \
       f['asset_analytic_permissions'] != ['download'] or \
       f['asset_analytic_expires_at'] != '2016/02/11 12:34:56.789' or \
       f['asset_analytic_location'] != '/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets/analytic/my.tiff' or \
       f['asset_analytic_status'] != 'active' or \
       f.GetGeometryRef().ExportToWkt() != 'MULTIPOLYGON (((2 49,2.0 49.1,2.1 49.1,2.1 49.0,2 49)))':
        f.DumpReadable()
        pytest.fail()

    lyr.ResetReading()
    f = lyr.GetNextFeature()
    if f.GetFID() != 1:
        f.DumpReadable()
        pytest.fail()

    gdal.FileFromMemBuffer('/vsimem/data_v1/quick-search?page=2',
                           """{
    "features" : [
        {
            "id": "id2"
        }
    ]
}""")

    f = lyr.GetNextFeature()
    if f.GetFID() != 2 or f['id'] != 'id2':
        f.DumpReadable()
        pytest.fail()

    lyr.ResetReading()
    f = lyr.GetNextFeature()
    if f.GetFID() != 1:
        f.DumpReadable()
        pytest.fail()

    f = lyr.GetNextFeature()
    if f.GetFID() != 2:
        f.DumpReadable()
        pytest.fail()

    f = lyr.GetNextFeature()
    assert f is None

    f = lyr.GetNextFeature()
    assert f is None

    gdal.FileFromMemBuffer("""/vsimem/data_v1/quick-search?_page_size=250&POSTFIELDS={"item_types":["PSOrthoTile"],"filter":{"type":"AndFilter","config":[{"type":"GeometryFilter","field_name":"geometry","config":{"type":"Point","coordinates":[2.0,49.0]}}]}}""",
                           """{"features" : [ { "id": "id3", "geometry": { "type": "Point", "coordinates": [2,49]} } ] }""")

    # POINT spatial filter
    lyr.SetSpatialFilterRect(2, 49, 2, 49)
    f = lyr.GetNextFeature()
    if f['id'] != 'id3':
        f.DumpReadable()
        pytest.fail()

    # Cannot find /vsimem/data_v1/stats&POSTFIELDS={"interval":"year","item_types":["PSOrthoTile"],"filter":{"type":"AndFilter","config":[{"type":"GeometryFilter","field_name":"geometry","config":{"type":"Point","coordinates":[2.0,49.0]}}]}}
    with gdaltest.error_handler():
        assert lyr.GetFeatureCount() == 1

    # Reset spatial filter
    lyr.SetSpatialFilter(0, None)
    f = lyr.GetNextFeature()
    if f['id'] != 'id':
        f.DumpReadable()
        pytest.fail()

    # Test attribute filter on id
    lyr.SetAttributeFilter("id = 'filtered_id'")
    gdal.FileFromMemBuffer("""/vsimem/data_v1/quick-search?_page_size=250&POSTFIELDS={"item_types":["PSOrthoTile"],"filter":{"type":"AndFilter","config":[{"type":"StringInFilter","field_name":"id","config":["filtered_id"]}]}}""",
                           """{
    "id": "filtered_id",
    "properties": {}
}""")

    f = lyr.GetNextFeature()
    if f['id'] != 'filtered_id':
        f.DumpReadable()
        pytest.fail()

    # Test attribute filter fully evaluated on server side.
    lyr.SetAttributeFilter("id != 'a' AND acquired >= '2016/02/11' AND acquired <= '2016/02/12' AND acquired > '1970/01/01 01:23:45' AND acquired < '2100/01/01 01:23:45' AND anomalous_pixels = 1.234567 AND (NOT id = 'b') AND columns > 0 AND columns < 2 AND columns = 1 AND columns IN (1, 2) AND (id IN ('filtered_2') OR id = 'foo') AND permissions = 'download' AND permissions IN ('download')")
    content = """{
    "features" : [
        {
            "id": "filtered_2",
            "_links" : {
                "_self" : "self",
                "assets" : "/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets"
            },
            "_permissions" : [ "download" ],
            "properties": {
                "acquired": "2016/02/11 12:34:56.789+00",
                "anomalous_pixels": 1.23,
                "columns": 1,
                "item_type": "foo"
            },
            "geometry":
            {
                "type": "Polygon",
                "coordinates" : [ [ [2,49],[2,49.1],[2.1,49.1],[2.1,49],[2,49] ] ]
            }
        }
    ]
}"""
    gdal.FileFromMemBuffer("""/vsimem/data_v1/quick-search?_page_size=250&POSTFIELDS={"item_types":["PSOrthoTile"],"filter":{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"NotFilter","config":{"type":"StringInFilter","field_name":"id","config":["a"]}},{"type":"DateRangeFilter","field_name":"acquired","config":{"gte":"2016-02-11T00:00:00Z"}}]},{"type":"DateRangeFilter","field_name":"acquired","config":{"lte":"2016-02-12T00:00:00Z"}}]},{"type":"DateRangeFilter","field_name":"acquired","config":{"gt":"1970-01-01T01:23:45Z"}}]},{"type":"DateRangeFilter","field_name":"acquired","config":{"lt":"2100-01-01T01:23:45Z"}}]},{"type":"RangeFilter","field_name":"anomalous_pixels","config":{"gte":1.234567,"lte":1.234567}}]},{"type":"NotFilter","config":{"type":"StringInFilter","field_name":"id","config":["b"]}}]},{"type":"RangeFilter","field_name":"columns","config":{"gt":0}}]},{"type":"RangeFilter","field_name":"columns","config":{"lt":2}}]},{"type":"NumberInFilter","field_name":"columns","config":[1]}]},{"type":"NumberInFilter","field_name":"columns","config":[1,2]}]},{"type":"OrFilter","config":[{"type":"StringInFilter","field_name":"id","config":["filtered_2"]},{"type":"StringInFilter","field_name":"id","config":["foo"]}]}]},{"type":"PermissionFilter","config":["download"]}]},{"type":"PermissionFilter","config":["download"]}]}]}}""", content)
    gdal.FileFromMemBuffer("""/vsimem/data_v1/quick-search?_page_size=250&POSTFIELDS={"item_types":["PSOrthoTile"],"filter":{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"AndFilter","config":[{"type":"NotFilter","config":{"type":"StringInFilter","field_name":"id","config":["a"]}},{"type":"DateRangeFilter","field_name":"acquired","config":{"gte":"2016-02-11T00:00:00Z"}}]},{"type":"DateRangeFilter","field_name":"acquired","config":{"lte":"2016-02-12T00:00:00Z"}}]},{"type":"DateRangeFilter","field_name":"acquired","config":{"gt":"1970-01-01T01:23:45Z"}}]},{"type":"DateRangeFilter","field_name":"acquired","config":{"lt":"2100-01-01T01:23:45Z"}}]},{"type":"RangeFilter","field_name":"anomalous_pixels","config":{"gte":1.23456699,"lte":1.2345670099999999}}]},{"type":"NotFilter","config":{"type":"StringInFilter","field_name":"id","config":["b"]}}]},{"type":"RangeFilter","field_name":"columns","config":{"gt":0}}]},{"type":"RangeFilter","field_name":"columns","config":{"lt":2}}]},{"type":"NumberInFilter","field_name":"columns","config":[1]}]},{"type":"NumberInFilter","field_name":"columns","config":[1,2]}]},{"type":"OrFilter","config":[{"type":"StringInFilter","field_name":"id","config":["filtered_2"]},{"type":"StringInFilter","field_name":"id","config":["foo"]}]}]},{"type":"PermissionFilter","config":["download"]}]},{"type":"PermissionFilter","config":["download"]}]}]}}""", content)
    f = lyr.GetNextFeature()
    if f['id'] != 'filtered_2':
        f.DumpReadable()
        pytest.fail()

    # Partly server / partly client
    lyr.SetAttributeFilter("id = 'filtered_3' AND id > 'a'")
    gdal.FileFromMemBuffer("""/vsimem/data_v1/quick-search?_page_size=250&POSTFIELDS={"item_types":["PSOrthoTile"],"filter":{"type":"AndFilter","config":[{"type":"StringInFilter","field_name":"id","config":["filtered_3"]}]}}""",
                           """{
    "features" : [
        {
            "id": "filtered_3",
            "properties": {
            }
        }
    ]
}""")
    f = lyr.GetNextFeature()
    if f['id'] != 'filtered_3':
        f.DumpReadable()
        pytest.fail()

    lyr.SetAttributeFilter("id > 'a' AND id = 'filtered_3'")
    f = lyr.GetNextFeature()
    if f['id'] != 'filtered_3':
        f.DumpReadable()
        pytest.fail()

    # Completely client side
    lyr.SetAttributeFilter("id > 'a' OR id = 'id'")
    f = lyr.GetNextFeature()
    if f['id'] != 'id':
        f.DumpReadable()
        pytest.fail()

    # Completely client side
    lyr.SetAttributeFilter("NOT id > 'z'")
    f = lyr.GetNextFeature()
    if f['id'] != 'id':
        f.DumpReadable()
        pytest.fail()

    # Reset attribute filter
    lyr.SetAttributeFilter(None)
    f = lyr.GetNextFeature()
    if f['id'] != 'id':
        f.DumpReadable()
        pytest.fail()

    # Try raster access

    # Missing catalog
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    with gdaltest.error_handler():
        ds_raster = gdal.OpenEx('PLScenes:', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'SCENE=id'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds_raster is None and gdal.GetLastErrorMsg().find('Missing catalog') >= 0

    # Invalid catalog
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    with gdaltest.error_handler():
        ds_raster = gdal.OpenEx('PLScenes:', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=invalid', 'SCENE=id'])
    gdal.SetConfigOption('PL_URL', None)

    # visual not an object
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets',
                           """{ "visual": false }""")
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    with gdaltest.error_handler():
        ds_raster = gdal.OpenEx('PLScenes:', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=PSOrthoTile', 'SCENE=id'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds_raster is None

    # Inactive file, and activation link not working
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets',
                           """{
  "analytic" : {
      "_links": {
        "_self": "analytic_links_self",
        "activate": "/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets/analytic/activate",
      },
      "_permissions": ["download"],
      "status": "inactive",
  }
}""")
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    with gdaltest.error_handler():
        ds_raster = gdal.OpenEx('PLScenes:', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=PSOrthoTile', 'SCENE=id', 'ACTIVATION_TIMEOUT=1', 'ASSET=analytic'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds_raster is None

    # File in activation
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets',
                           """{
  "analytic" : {
      "_links": {
        "_self": "analytic_links_self",
        "activate": "/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets/analytic/activate",
      },
      "_permissions": ["download"],
      "status": "activating",
  }
}""")
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    with gdaltest.error_handler():
        ds_raster = gdal.OpenEx('PLScenes:', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=PSOrthoTile', 'SCENE=id', 'ACTIVATION_TIMEOUT=1', 'ASSET=analytic'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds_raster is None

    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets',
                           """{
  "analytic" : {
      "_permissions": ["download"],
      "_links": {
        "_self": "analytic_links_self",
        "activate": "/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets/analytic/activate",
      },
      "location": "/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets/analytic/my.tiff",
      "status": "active",
      "expires_at": "2016-02-11T12:34:56.789"
  }
}""")

    # Missing /vsimem/data_v1/item-types/PSOrthoTile/items/id/assets/analytic/my.tiff
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    with gdaltest.error_handler():
        ds_raster = gdal.OpenEx('PLScenes:', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=PSOrthoTile', 'SCENE=id', 'ACTIVATION_TIMEOUT=1', 'ASSET=analytic'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds_raster is None

    # JSon content for /vsimem/data_v1/item-types/PSOrthoTile/items/id/assets/analytic/my.tiff
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets/analytic/my.tiff',
                           """{}""")
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    with gdaltest.error_handler():
        ds_raster = gdal.OpenEx('PLScenes:', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=PSOrthoTile', 'SCENE=id', 'ACTIVATION_TIMEOUT=1', 'ASSET=analytic'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds_raster is None

    # Missing metadata
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types/PSOrthoTile/items/id/assets/analytic/my.tiff',
                           open('../gcore/data/byte.tif', 'rb').read())
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    with gdaltest.error_handler():
        ds_raster = gdal.OpenEx('PLScenes:', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=PSOrthoTile', 'SCENE=id', 'ACTIVATION_TIMEOUT=1', 'ASSET=analytic'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds_raster is not None
    ds_raster = None

    # Failed filter by scene id
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types/PSOrthoTile',
                           """{"id": "PSOrthoTile"}""")
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    ds_raster = gdal.OpenEx('PLScenes:', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=PSOrthoTile', 'SCENE=id', 'ACTIVATION_TIMEOUT=1', 'ASSET=analytic'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds_raster is not None
    ds_raster = None

    # Test metadata items attached to dataset
    gdal.FileFromMemBuffer("""/vsimem/data_v1/quick-search?_page_size=250&POSTFIELDS={"item_types":["PSOrthoTile"],"filter":{"type":"AndFilter","config":[{"type":"StringInFilter","field_name":"id","config":["id"]}]}}""",
                           """{
    "id": "id",
    "properties": {
        "anomalous_pixels": 1.23
    },
}""")
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    ds_raster = gdal.OpenEx('PLScenes:', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=PSOrthoTile', 'SCENE=id', 'ACTIVATION_TIMEOUT=1', 'ASSET=analytic'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds_raster is not None
    assert ds_raster.GetMetadataItem('anomalous_pixels') == '1.23'
    ds_raster = None

    # Test invalid ASSET
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    with gdaltest.error_handler():
        ds_raster = gdal.OpenEx('PLScenes:', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=PSOrthoTile', 'SCENE=id', 'ACTIVATION_TIMEOUT=1', 'ASSET=invalid'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds_raster is None

    # Test subdatasets
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    ds_raster = gdal.OpenEx('PLScenes:', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=PSOrthoTile', 'ASSET=list', 'SCENE=id'])
    gdal.SetConfigOption('PL_URL', None)
    assert len(ds_raster.GetSubDatasets()) == 1
    ds_raster = None

    # Unsupported option
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    with gdaltest.error_handler():
        ds_raster = gdal.OpenEx('PLScenes:unsupported=yes', gdal.OF_RASTER, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=PSOrthoTile', 'SCENE=id'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds_raster is None

    # Test catalog with vector access
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    ds2 = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=PSOrthoTile'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds2 is not None and ds2.GetLayerCount() == 1

    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    with gdaltest.error_handler():
        ds2 = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1', 'API_KEY=foo', 'ITEMTYPES=invalid'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds2 is None

    fl = gdal.ReadDir('/vsimem/data_v1')
    for filename in fl:
        gdal.Unlink(filename)

    
###############################################################################
# Test robustness to errors in Data V1 API


def test_ogr_plscenes_data_v1_errors():
    # No PL_API_KEY
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    old_key = gdal.GetConfigOption('PL_API_KEY')
    if old_key:
        gdal.SetConfigOption('PL_API_KEY', '')
    with gdaltest.error_handler():
        ds = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1'])
    if old_key:
        gdal.SetConfigOption('PL_API_KEY', old_key)
    gdal.SetConfigOption('PL_URL', None)
    assert ds is None

    # Invalid option
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types', '{ "item-types": [] }')
    with gdaltest.error_handler():
        gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
        ds = gdal.OpenEx('PLScenes:version=data_v1,api_key=foo,invalid=invalid', gdal.OF_VECTOR)
        gdal.SetConfigOption('PL_URL', None)
    assert ds is None

    # Invalid JSON
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types', '{invalid_json')
    with gdaltest.error_handler():
        gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
        ds = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1', 'API_KEY=foo'])
        gdal.SetConfigOption('PL_URL', None)
    assert ds is None

    # Not an object
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types', 'false')
    with gdaltest.error_handler():
        gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
        ds = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1', 'API_KEY=foo'])
        gdal.SetConfigOption('PL_URL', None)
    assert ds is None

    # Lack of "item_types"
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types', '{}')
    with gdaltest.error_handler():
        gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
        ds = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1', 'API_KEY=foo'])
        gdal.SetConfigOption('PL_URL', None)
    assert ds is None

    # Invalid catalog objects
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types', """{"item_types": [{}, [], null, {"id":null},
    {"id":"foo"}]}""")
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    ds = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1', 'API_KEY=foo'])
    gdal.SetConfigOption('PL_URL', None)
    assert ds.GetLayerCount() == 1

    # Invalid next URL
    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types', '{"_links": { "_next": "/vsimem/inexisting" }, "item_types": [{"id": "my_catalog"}]}')
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    ds = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1', 'API_KEY=foo'])
    gdal.SetConfigOption('PL_URL', None)
    with gdaltest.error_handler():
        lyr_count = ds.GetLayerCount()
    assert lyr_count == 1

    gdal.FileFromMemBuffer('/vsimem/data_v1/item-types', '{"item_types": [{"id": "PSScene3Band"}]}')
    gdal.SetConfigOption('PL_URL', '/vsimem/data_v1/')
    ds = gdal.OpenEx('PLScenes:', gdal.OF_VECTOR, open_options=['VERSION=data_v1', 'API_KEY=foo'])
    gdal.SetConfigOption('PL_URL', None)
    lyr = ds.GetLayer(0)

    # Invalid index
    ds.GetLayer(-1)
    ds.GetLayer(1)

    with gdaltest.error_handler():
        ds.GetLayerByName('invalid_name')

    # Cannot find /vsimem/data_v1/quick-search?_page_size=250&POSTFIELDS={"item_types":["PSScene3Band"],"filter":{"type":"AndFilter","config":[]}}
    with gdaltest.error_handler():
        lyr.GetNextFeature()

    # Empty object
    gdal.FileFromMemBuffer('/vsimem/data_v1/quick-search?_page_size=250&POSTFIELDS={"item_types":["PSScene3Band"],"filter":{"type":"AndFilter","config":[]}}', '{}')
    lyr.ResetReading()
    lyr.GetNextFeature()

    # null feature
    gdal.FileFromMemBuffer('/vsimem/data_v1/quick-search?_page_size=250&POSTFIELDS={"item_types":["PSScene3Band"],"filter":{"type":"AndFilter","config":[]}}', '{ "features": [ null ] }')
    lyr.ResetReading()
    lyr.GetNextFeature()

    gdal.Unlink('/vsimem/data_v1/item-types')
    gdal.Unlink('/vsimem/data_v1/quick-search?_page_size=250&POSTFIELDS={"item_types":["PSScene3Band"],"filter":{"type":"AndFilter","config":[]}}')

###############################################################################
# Test Data V1 API against real server


def test_ogr_plscenes_data_v1_live():
    api_key = gdal.GetConfigOption('PL_API_KEY')
    if api_key is None:
        pytest.skip('Skipping test as PL_API_KEY not defined')

    gdal.SetConfigOption('PLSCENES_PAGE_SIZE', '10')
    ds = ogr.Open('PLScenes:version=data_v1,FOLLOW_LINKS=YES')
    gdal.SetConfigOption('PLSCENES_PAGE_SIZE', None)
    assert ds is not None

    lyr = ds.GetLayer(0)
    assert lyr is not None

    lyr.SetAttributeFilter("permissions = 'assets:download'")
    f = lyr.GetNextFeature()
    assert f is not None
    catalog = lyr.GetName()
    scene = f['id']
    f.DumpReadable()

    lyr_defn = lyr.GetLayerDefn()
    asset_name = None
    for i in range(lyr_defn.GetFieldCount()):
        fld_defn = lyr_defn.GetFieldDefn(i)
        name = fld_defn.GetName()
        if name.startswith('asset_') and name.endswith('_activate_link') and f.GetFieldAsString(i) != '':
            asset_name = name[len('asset_'):-len('_activate_link')]
            break
        elif name.startswith('asset_') and name.endswith('_location') and f.GetFieldAsString(i) != '':
            asset_name = name[len('asset_'):-len('_location')]
            break
    assert asset_name is not None

    acquired_field = lyr_defn.GetFieldIndex('acquired')
    assert acquired_field >= 0 and lyr_defn.GetFieldDefn(acquired_field).GetType() == ogr.OFTDateTime

    if not f.IsFieldSet(acquired_field):
        f.DumpReadable()
        pytest.fail()

    int_field = -1
    float_field = -1
    string_field = -1
    for i in range(lyr_defn.GetFieldCount()):
        typ = lyr_defn.GetFieldDefn(i).GetType()
        if int_field < 0 and typ == ogr.OFTInteger and f.IsFieldSet(i):
            int_field = i
        elif float_field < 0 and typ == ogr.OFTReal and f.IsFieldSet(i):
            float_field = i
        elif string_field < 0 and typ == ogr.OFTString and f.IsFieldSet(i):
            string_field = i

    filtr = "acquired='%s'" % f.GetFieldAsString(acquired_field)
    if int_field >= 0:
        name = lyr_defn.GetFieldDefn(int_field).GetName()
        mini = f.GetField(int_field) - 1
        maxi = f.GetField(int_field) + 1
        filtr += ' AND %s >= %d AND %s <= %d' % (name, mini, name, maxi)
    if float_field >= 0:
        name = lyr_defn.GetFieldDefn(float_field).GetName()
        mini = f.GetField(float_field) - 0.01
        maxi = f.GetField(float_field) + 0.01
        filtr += ' AND %s BETWEEN %f AND %f' % (name, mini, maxi)
    if string_field >= 0:
        name = lyr_defn.GetFieldDefn(string_field).GetName()
        value = f.GetField(string_field)
        filtr += " AND %s = '%s'" % (name, value)

    lyr.SetAttributeFilter(filtr)
    f = lyr.GetNextFeature()
    assert f is not None

    ds = None

    ds = gdal.Open('PLScenes:version=data_v1,itemtypes=%s,scene=%s,asset=%s' % (catalog, scene, asset_name))
    assert ds is not None
    assert ds.RasterCount != 0
