#!/usr/bin/env python
###############################################################################
# $Id$
#
# Project:  GDAL/OGR Test Suite
# Purpose:  GFT driver testing.
# Author:   Even Rouault <even dot rouault at mines dash paris dot org>
#
###############################################################################
# Copyright (c) 2011, Even Rouault <even dot rouault at mines dash paris dot org>
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
import string

sys.path.append( '../pymod' )

import gdaltest
import ogrtest
import gdal
import ogr

###############################################################################
# Test if driver is available

def ogr_gft_init():

    ogrtest.gft_auth_key = """DQAAALAAAABVidHwAv07_rffxcF-uHLY7btNP-rWkoRaaxqB4aht62gtwNDVsE2cOoi2x57H6I2jLLHOQjTQAD3Tv4MYZ9Qqxf7ElmuDu7SKmANcbWN9BogKNeYWRhuXo6QRLCbfKc3tI8Y0wvfnP5T88MPun2IKyDlJIS9jGxat9usjyLkhZlVO4vuXcXHZZmDMKsq8C9dMHTRErJRaNHJywxIFYKKzqYn0VGnMYSERJ8HCExfFTg"""
    ogrtest.gft_drv = None

    try:
        ogrtest.gft_drv = ogr.GetDriverByName('GFT')
    except:
        pass

    if ogrtest.gft_drv is None:
        return 'skip'

    if gdaltest.gdalurlopen('http://www.google.com') is None:
        print('cannot open URL')
        ogrtest.gft_drv = None
        return 'skip'

    return 'success'

###############################################################################
# Read test

def ogr_gft_read():
    if ogrtest.gft_drv is None:
        return 'skip'

    old_auth = gdal.GetConfigOption('GFT_AUTH', None)
    old_email = gdal.GetConfigOption('GFT_EMAIL', None)
    old_password = gdal.GetConfigOption('GFT_PASSWORD', None)
    gdal.SetConfigOption('GFT_AUTH', None)
    gdal.SetConfigOption('GFT_EMAIL', None)
    gdal.SetConfigOption('GFT_PASSWORD', None)
    ds = ogr.Open('GFT:tables=224453')
    gdal.SetConfigOption('GFT_AUTH', old_auth)
    gdal.SetConfigOption('GFT_EMAIL', old_email)
    gdal.SetConfigOption('GFT_PASSWORD', old_password)
    if ds is None:
        return 'fail'

    lyr = ds.GetLayer(0)
    if lyr is None:
        return 'fail'

    lyr.SetSpatialFilterRect(67,31.5,67.5,32)
    lyr.SetAttributeFilter("'Attack on' = 'ENEMY'")

    count = lyr.GetFeatureCount()
    if count == 0:
        gdaltest.post_reason('did not get expected feature count')
        print(count)
        return 'fail'

    return 'success'

###############################################################################
# Write test

def ogr_gft_write():
    if ogrtest.gft_drv is None:
        return 'skip'

    ds = ogr.Open('GFT:auth=%s' % ogrtest.gft_auth_key, update = 1)
    if ds is None:
        return 'fail'

    import random
    rand_val = random.randint(0,2147000000)
    table_name = "test_%d" % rand_val

    lyr = ds.CreateLayer(table_name)
    lyr.CreateField(ogr.FieldDefn('strcol', ogr.OFTString))
    lyr.CreateField(ogr.FieldDefn('numcol', ogr.OFTReal))

    feat = ogr.Feature(lyr.GetLayerDefn())

    feat.SetField('strcol', 'foo')
    feat.SetField('numcol', '3.45')
    expected_wkt = "POLYGON ((0 0,0 1,1 1,1 0),(0.25 0.25,0.25 0.75,0.75 0.75,0.75 0.25))"
    geom = ogr.CreateGeometryFromWkt(expected_wkt)
    feat.SetGeometry(geom)
    if lyr.CreateFeature(feat) != 0:
        gdaltest.post_reason('CreateFeature() failed')
        return 'fail'

    fid = feat.GetFID()
    feat.SetField('strcol', 'bar')
    if lyr.SetFeature(feat) != 0:
        gdaltest.post_reason('SetFeature() failed')
        return 'fail'

    lyr.ResetReading()
    feat = lyr.GetNextFeature()
    if feat.GetFieldAsString('strcol') != 'bar':
        gdaltest.post_reason('GetNextFeature() did not get expected feature')
        feat.DumpReadable()
        return 'fail'

    feat = lyr.GetFeature(fid)
    if feat.GetFieldAsString('strcol') != 'bar':
        gdaltest.post_reason('GetFeature() did not get expected feature')
        feat.DumpReadable()
        return 'fail'
    got_wkt = feat.GetGeometryRef().ExportToWkt()
    if got_wkt != expected_wkt:
        gdaltest.post_reason('did not get expected geometry')
        print(got_wkt)
        return 'fail'

    if lyr.GetFeatureCount() != 1:
        gdaltest.post_reason('GetFeatureCount() did not returned expected value')
        return 'fail'

    if lyr.DeleteFeature(feat.GetFID()) != 0:
        gdaltest.post_reason('DeleteFeature() failed')
        return 'fail'

    ds.ExecuteSQL('DELLAYER:%s' % table_name)

    ds = None

    return 'success'

gdaltest_list = [ 
    ogr_gft_init,
    ogr_gft_read,
    ogr_gft_write,
    ]

if __name__ == '__main__':

    gdaltest.setup_run( 'ogr_gft' )

    gdaltest.run_tests( gdaltest_list )

    gdaltest.summarize()


