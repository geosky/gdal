/******************************************************************************
 * $Id$
 *
 * Project:  High Performance Image Reprojector
 * Purpose:  Implementation of the GDALWarpKernel class.  Implements the actual
 *           image warping for a "chunk" of input and output imagery already
 *           loaded into memory.
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 2003, Frank Warmerdam <warmerdam@pobox.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ******************************************************************************
 *
 * $Log$
 * Revision 1.2  2003/02/20 21:53:06  warmerda
 * partial implementation
 *
 * Revision 1.1  2003/02/18 17:25:50  warmerda
 * New
 *
 */

#include "gdalwarper.h"
#include "cpl_string.h"

CPL_CVSID("$Id$");

static CPLErr GWKGeneralCase( GDALWarpKernel * );

/************************************************************************/
/* ==================================================================== */
/*                            GDALWarpKernel                            */
/* ==================================================================== */
/************************************************************************/

/**
 * \class GDALWarpKernel "gdalwarper.h"
 *
 * Low level image warping class.
 *
 * This class is responsible for low level image warping for one
 * "chunk" of imagery.  The class is essentially a structure with all
 * data members public - primarily so that new special-case functions 
 * can be added without changing the class declaration.  
 *
 * Applications are normally intended to interactive with warping facilities
 * through the GDALWarpOperation class, though the GDALWarpKernel can in
 * theory be used directly if great care is taken in setting up the 
 * control data. 
 *
 * <h3>Design Issues</h3>
 *
 * My intention is that PerformWarp() would analyse the setup in terms
 * of the datatype, resampling type, and validity/density mask usage and
 * pick one of many specific implementations of the warping algorithm over
 * a continuim of optimization vs. generality.  At one end there will be a
 * reference general purpose implementation of the algorithm that supports
 * any data type (working internally in double precision complex), all three
 * resampling types, and any or all of the validity/density masks.  At the
 * other end would be highly optimized algorithms for common cases like
 * nearest neighbour resampling on GDT_Byte data with no masks.  
 *
 * The full set of optimized versions have not been decided but we should 
 * expect to have at least:
 *  - One for each resampling algorithm for 8bit data with no masks. 
 *  - One for each resampling algorithm for float data with no masks.
 *  - One for each resampling algorithm for float data with any/all masks
 *    (essentially the generic case for just float data). 
 *  - One for each resampling algorithm for 8bit data with support for
 *    input validity masks (per band or per pixel).  This handles the common 
 *    case of nodata masking.
 *  - One for each resampling algorithm for float data with support for
 *    input validity masks (per band or per pixel).  This handles the common 
 *    case of nodata masking.
 *
 * Some of the specializations would operate on all bands in one pass
 * (especially the ones without masking would do this), while others might
 * process each band individually to reduce code complexity.
 *
 * <h3>Masking Semantics</h3>
 * 
 * A detailed explanation of the semantics of the validity and density masks,
 * and their effects on resampling kernels is needed here. 
 */

/************************************************************************/
/*                     GDALWarpKernel Data Members                      */
/************************************************************************/

/**
 * \var GDALResampleAlg GDALWarpKernel::eResample;
 * 
 * Resampling algorithm.
 *
 * The resampling algorithm to use.  One of GRA_NearestNeighbour, 
 * GRA_Bilinear, or GRA_Cubic. 
 *
 * This field is required. GDT_NearestNeighbour may be used as a default
 * value. 
 */
                                  
/**
 * \var GDALDataType GDALWarpKernel::eWorkingDataType;
 * 
 * Working pixel data type.
 *
 * The datatype of pixels in the source image (papabySrcimage) and
 * destination image (papabyDstImage) buffers.  Note that operations on 
 * some data types (such as GDT_Byte) may be much better optimized than other
 * less common cases. 
 *
 * This field is required.  It may not be GDT_Unknown.
 */
                                  
/**
 * \var int GDALWarpKernel::nBands;
 * 
 * Number of bands.
 *
 * The number of bands (layers) of imagery being warped.  Determines the
 * number of entries in the papabySrcImage, papanBandSrcValid, 
 * and papabyDstImage arrays. 
 *
 * This field is required.
 */

/**
 * \var int GDALWarpKernel::nSrcXSize;
 * 
 * Source image width in pixels.
 *
 * This field is required.
 */

/**
 * \var int GDALWarpKernel::nSrcYSize;
 * 
 * Source image height in pixels.
 *
 * This field is required.
 */

/**
 * \var int GDALWarpKernel::papabySrcImage;
 * 
 * Array of source image band data.
 *
 * This is an array of pointers (of size GDALWarpKernel::nBands) pointers
 * to image data.  Each individual band of image data is organized as a single 
 * block of image data in left to right, then bottom to top order.  The actual
 * type of the image data is determined by GDALWarpKernel::eWorkingDataType.
 *
 * To access the the pixel value for the (x=3,y=4) pixel (zero based) of
 * the second band with eWorkingDataType set to GDT_Float32 use code like
 * this:
 *
 * \code 
 *   float dfPixelValue;
 *   int   nBand = 1;  // band indexes are zero based.
 *   int   nPixel = 3; // zero based
 *   int   nLine = 4;  // zero based
 *
 *   assert( nPixel >= 0 && nPixel < poKern->nSrcXSize );
 *   assert( nLine >= 0 && nLine < poKern->nSrcYSize );
 *   assert( nBand >= 0 && nBand < poKern->nBands );
 *   dfPixelValue = ((float *) poKern->papabySrcImage[nBand-1])
 *                                  [nPixel + nLine * poKern->nSrcXSize];
 * \endcode
 *
 * This field is required.
 */

/**
 * \var GUInt32 **GDALWarpKernel::papanBandSrcValid;
 *
 * Per band validity mask for source pixels. 
 *
 * Array of pixel validity mask layers for each source band.   Each of
 * the mask layers is the same size (in pixels) as the source image with
 * one bit per pixel.  Note that it is legal (and common) for this to be
 * NULL indicating that none of the pixels are invalidated, or for some
 * band validity masks to be NULL in which case all pixels of the band are
 * valid.  The following code can be used to test the validity of a particular
 * pixel.
 *
 * \code 
 *   int   bIsValid = TRUE;
 *   int   nBand = 1;  // band indexes are zero based.
 *   int   nPixel = 3; // zero based
 *   int   nLine = 4;  // zero based
 *
 *   assert( nPixel >= 0 && nPixel < poKern->nSrcXSize );
 *   assert( nLine >= 0 && nLine < poKern->nSrcYSize );
 *   assert( nBand >= 0 && nBand < poKern->nBands );
 * 
 *   if( poKern->papanBandSrcValid != NULL
 *       && poKern->papanBandSrcValid[nBand] != NULL )
 *   {
 *       GUInt32 *panBandMask = poKern->papanBandSrcValid[nBand];
 *       int    iPixelOffset = nPixel + nLine * poKern->nSrcXSize;
 * 
 *       bIsValid = panBandMask[iPixelOffset>>5] 
 *                  & (0x01 << (iPixelOffset & 0x1f));
 *   }
 * \endcode
 */

/**
 * \var GUInt32 *GDALWarpKernel::panUnifiedSrcValid;
 *
 * Per pixel validity mask for source pixels. 
 *
 * A single validity mask layer that applies to the pixels of all source
 * bands.  It is accessed similarly to papanBandSrcValid, but without the
 * extra level of band indirection.
 *
 * This pointer may be NULL indicating that all pixels are valid. 
 * 
 * Note that if both panUnifiedSrcValid, and papanBandSrcValid are available,
 * the pixel isn't considered to be valid unless both arrays indicate it is
 * valid.  
 */

/**
 * \var float *GDALWarpKernel::pafUnifiedSrcDensity;
 *
 * Per pixel density mask for source pixels. 
 *
 * A single density mask layer that applies to the pixels of all source
 * bands.  It contains values between 0.0 and 1.0 indicating the degree to 
 * which this pixel should be allowed to contribute to the output result. 
 *
 * This pointer may be NULL indicating that all pixels have a density of 1.0.
 *
 * The density for a pixel may be accessed like this:
 *
 * \code 
 *   float fDensity = 1.0;
 *   int   nPixel = 3; // zero based
 *   int   nLine = 4;  // zero based
 *
 *   assert( nPixel >= 0 && nPixel < poKern->nSrcXSize );
 *   assert( nLine >= 0 && nLine < poKern->nSrcYSize );
 *   if( poKern->pafUnifiedSrcDensity != NULL )
 *     fDensity = poKern->pafUnifiedSrcDensity
 *                                  [nPixel + nLine * poKern->nSrcXSize];
 * \endcode
 */

/**
 * \var int GDALWarpKernel::nDstXSize;
 *
 * Width of destination image in pixels.
 *
 * This field is required.
 */

/**
 * \var int GDALWarpKernel::nDstYSize;
 *
 * Height of destination image in pixels.
 *
 * This field is required.
 */

/**
 * \var GByte **GDALWarpKernel::papabyDstImage;
 * 
 * Array of destination image band data.
 *
 * This is an array of pointers (of size GDALWarpKernel::nBands) pointers
 * to image data.  Each individual band of image data is organized as a single 
 * block of image data in left to right, then bottom to top order.  The actual
 * type of the image data is determined by GDALWarpKernel::eWorkingDataType.
 *
 * To access the the pixel value for the (x=3,y=4) pixel (zero based) of
 * the second band with eWorkingDataType set to GDT_Float32 use code like
 * this:
 *
 * \code 
 *   float dfPixelValue;
 *   int   nBand = 1;  // band indexes are zero based.
 *   int   nPixel = 3; // zero based
 *   int   nLine = 4;  // zero based
 *
 *   assert( nPixel >= 0 && nPixel < poKern->nDstXSize );
 *   assert( nLine >= 0 && nLine < poKern->nDstYSize );
 *   assert( nBand >= 0 && nBand < poKern->nBands );
 *   dfPixelValue = ((float *) poKern->papabyDstImage[nBand-1])
 *                                  [nPixel + nLine * poKern->nSrcYSize];
 * \endcode
 *
 * This field is required.
 */

/**
 * \var GUInt32 *GDALWarpKernel::panDstValid;
 *
 * Per pixel validity mask for destination pixels. 
 *
 * A single validity mask layer that applies to the pixels of all destination
 * bands.  It is accessed similarly to papanUnitifiedSrcValid, but based
 * on the size of the destination image.
 *
 * This pointer may be NULL indicating that all pixels are valid. 
 */

/**
 * \var float *GDALWarpKernel::pafDstDensity;
 *
 * Per pixel density mask for destination pixels. 
 *
 * A single density mask layer that applies to the pixels of all destination
 * bands.  It contains values between 0.0 and 1.0.
 *
 * This pointer may be NULL indicating that all pixels have a density of 1.0.
 *
 * The density for a pixel may be accessed like this:
 *
 * \code 
 *   float fDensity = 1.0;
 *   int   nPixel = 3; // zero based
 *   int   nLine = 4;  // zero based
 *
 *   assert( nPixel >= 0 && nPixel < poKern->nDstXSize );
 *   assert( nLine >= 0 && nLine < poKern->nDstYSize );
 *   if( poKern->pafDstDensity != NULL )
 *     fDensity = poKern->pafDstDensity[nPixel + nLine * poKern->nDstXSize];
 * \endcode
 */

/**
 * \var int GDALWarpKernel::nSrcXOff;
 *
 * X offset to source pixel coordinates for transformation.
 *
 * See pfnTransformer.
 *
 * This field is required.
 */

/**
 * \var int GDALWarpKernel::nSrcYOff;
 *
 * Y offset to source pixel coordinates for transformation.
 *
 * See pfnTransformer.
 *
 * This field is required.
 */

/**
 * \var int GDALWarpKernel::nDstXOff;
 *
 * X offset to destination pixel coordinates for transformation.
 *
 * See pfnTransformer.
 *
 * This field is required.
 */

/**
 * \var int GDALWarpKernel::nDstYOff;
 *
 * Y offset to destination pixel coordinates for transformation.
 *
 * See pfnTransformer.
 *
 * This field is required.
 */

/**
 * \var GDALTransformerFunc GDALWarpKernel::pfnTransformer;
 *
 * Source/destination location transformer.
 *
 * The function to call to transform coordinates between source image 
 * pixel/line coordinates and destination image pixel/line coordinates.  
 * See GDALTransformerFunc() for details of the semantics of this function. 
 *
 * The GDALWarpKern algorithm will only ever use this transformer in 
 * "destination to source" mode (bDstToSrc=TRUE), and will always pass 
 * partial or complete scanlines of points in the destination image as
 * input.  This means, amoung other things, that it is safe to the the
 * approximating transform GDALApproxTransform() as the transformation 
 * function. 
 *
 * Source and destination images may be subsets of a larger overall image.
 * The transformation algorithms will expect and return pixel/line coordinates
 * in terms of this larger image, so coordinates need to be offset by
 * the offsets specified in nSrcXOff, nSrcYOff, nDstXOff, and nDstYOff before
 * passing to pfnTransformer, and after return from it. 
 * 
 * The GDALWarpKernel::pfnTransformerArg value will be passed as the callback
 * data to this function when it is called.
 *
 * This field is required.
 */

/**
 * \var void *GDALWarpKernel::pTransformerArg;
 *
 * Callback data for pfnTransformer.
 *
 * This field may be NULL if not required for the pfnTransformer being used.
 */

/**
 * \var GDALProgressFunc GDALWarpKernel::pfnProgress;
 *
 * The function to call to report progress of the algorithm, and to check
 * for a requested termination of the operation.  It operates according to
 * GDALProgressFunc() semantics. 
 *
 * Generally speaking the progress function will be invoked for each 
 * scanline of the destination buffer that has been processed. 
 *
 * This field may be NULL (internally set to GDALDummyProgress()). 
 */

/**
 * \var void *GDALWarpKernel::pProgress;
 *
 * Callback data for pfnProgress.
 *
 * This field may be NULL if not required for the pfnProgress being used.
 */


/************************************************************************/
/*                           GDALWarpKernel()                           */
/************************************************************************/

GDALWarpKernel::GDALWarpKernel()

{
    eResample = GRA_NearestNeighbour;
    eWorkingDataType = GDT_Unknown;
    nBands = 0;
    nDstXOff = 0;
    nDstYOff = 0;
    nDstXSize = 0;
    nDstYSize = 0;
    nSrcXOff = 0;
    nSrcYOff = 0;
    nSrcXSize = 0;
    nSrcYSize = 0;
    pafDstDensity = NULL;
    pafUnifiedSrcDensity = NULL;
    panDstValid = NULL;
    panUnifiedSrcValid = NULL;
    papabyDstImage = NULL;
    papabySrcImage = NULL;
    papanBandSrcValid = NULL;
    pfnProgress = GDALDummyProgress;
    pProgress = NULL;
    dfProgressBase = 0.0;
    dfProgressScale = 1.0;
    pfnTransformer = NULL;
    pTransformerArg = NULL;
}

/************************************************************************/
/*                          ~GDALWarpKernel()                           */
/************************************************************************/

GDALWarpKernel::~GDALWarpKernel()

{
}

/************************************************************************/
/*                            PerformWarp()                             */
/************************************************************************/

/**
 * \fn CPLErr GDALWarpKernel::PerformWarp();
 * 
 * This method performs the warp described in the GDALWarpKernel.
 *
 * @return CE_None on success or CE_Failure if an error occurs.
 */

CPLErr GDALWarpKernel::PerformWarp()

{
    CPLErr eErr;

    if( (eErr = Validate()) != CE_None )
        return eErr;

    return GWKGeneralCase( this );
}
                                  
/************************************************************************/
/*                              Validate()                              */
/************************************************************************/

/**
 * \fn CPLErr GDALWarpKernel::Validate()
 * 
 * Check the settings in the GDALWarpKernel, and issue a CPLError()
 * (and return CE_Failure) if the configuration is considered to be
 * invalid for some reason.  
 *
 * This method will also do some standard defaulting such as setting
 * pfnProgress to GDALDummyProgress() if it is NULL. 
 *
 * @return CE_None on success or CE_Failure if an error is detected.
 */

CPLErr GDALWarpKernel::Validate()

{
    return CE_None;
}

/************************************************************************/
/*                          GWKSetPixelValue()                          */
/************************************************************************/

static int GWKSetPixelValue( GDALWarpKernel *poWK, int iBand, 
                             int iDstOffset, double dfDensity, 
                             double dfReal, double dfImag )

{
    GByte *pabyDst = poWK->papabyDstImage[iBand];

    // I need to add a bunch more stuff related to input density, setting
    // the destination buffer density and setting the destination buffer
    // valid flag. 


    switch( poWK->eWorkingDataType )
    {
      case GDT_Byte:
        if( dfReal < 0.0 )
            pabyDst[iDstOffset] = 0;
        else if( dfReal > 255.0 )
            pabyDst[iDstOffset] = 255;
        else
            pabyDst[iDstOffset] = (GByte) dfReal;
        break;

      case GDT_Int16:
        if( dfReal < -32768 )
            ((GInt16 *) pabyDst)[iDstOffset] = -32768;
        else if( dfReal > 32767 )
            ((GInt16 *) pabyDst)[iDstOffset] = 32767;
        else
            ((GInt16 *) pabyDst)[iDstOffset] = (GInt16) dfReal;
        break;

      case GDT_UInt16:
        if( dfReal < 0 )
            ((GUInt16 *) pabyDst)[iDstOffset] = 0;
        else if( dfReal > 65535 )
            ((GUInt16 *) pabyDst)[iDstOffset] = 65535;
        else
            ((GUInt16 *) pabyDst)[iDstOffset] = (GUInt16) dfReal;
        break;

      case GDT_UInt32:
        if( dfReal < 0 )
            ((GUInt32 *) pabyDst)[iDstOffset] = 0;
        else if( dfReal > 4294967295.0 )
            ((GUInt32 *) pabyDst)[iDstOffset] = (GUInt32) 4294967295.0;
        else
            ((GUInt32 *) pabyDst)[iDstOffset] = (GUInt32) dfReal;
        break;

      case GDT_Int32:
        if( dfReal < 2147483648.0 )
            ((GInt32 *) pabyDst)[iDstOffset] = 0;
        else if( dfReal > 2147483647.0 )
            ((GInt32 *) pabyDst)[iDstOffset] = 2147483647;
        else
            ((GInt32 *) pabyDst)[iDstOffset] = (GInt32) dfReal;
        break;

      case GDT_Float32:
        ((float *) pabyDst)[iDstOffset] = (float) dfReal;
        break;

      case GDT_Float64:
        ((double *) pabyDst)[iDstOffset] = dfReal;
        break;

      case GDT_CInt16:
        if( dfReal < -32768 )
            ((GInt16 *) pabyDst)[iDstOffset*2] = -32768;
        else if( dfReal > 32767 )
            ((GInt16 *) pabyDst)[iDstOffset*2] = 32767;
        else
            ((GInt16 *) pabyDst)[iDstOffset*2] = (GInt16) dfReal;
        if( dfImag < -32768 )
            ((GInt16 *) pabyDst)[iDstOffset*2+1] = -32768;
        else if( dfImag > 32767 )
            ((GInt16 *) pabyDst)[iDstOffset*2+1] = 32767;
        else
            ((GInt16 *) pabyDst)[iDstOffset*2+1] = (GInt16) dfImag;
        break;

      case GDT_CInt32:
        if( dfReal < -2147483648.0 )
            ((GInt32 *) pabyDst)[iDstOffset*2] = (GInt32) -2147483648.0;
        else if( dfReal > 2147483647.0 )
            ((GInt32 *) pabyDst)[iDstOffset*2] = (GInt32) 2147483647.0;
        else
            ((GInt32 *) pabyDst)[iDstOffset*2] = (GInt32) dfReal;
        if( dfImag < -2147483648.0 )
            ((GInt32 *) pabyDst)[iDstOffset*2+1] = (GInt32) -2147483648.0;
        else if( dfImag > 2147483647.0 )
            ((GInt32 *) pabyDst)[iDstOffset*2+1] = (GInt32) 2147483647.0;
        else
            ((GInt32 *) pabyDst)[iDstOffset*2+1] = (GInt32) dfImag;
        break;

      case GDT_CFloat32:
        ((float *) pabyDst)[iDstOffset*2] = (float) dfReal;
        ((float *) pabyDst)[iDstOffset*2+1] = (float) dfImag;
        break;

      case GDT_CFloat64:
        ((double *) pabyDst)[iDstOffset*2] = (double) dfReal;
        ((double *) pabyDst)[iDstOffset*2+1] = (double) dfImag;
        break;

      default:
        return FALSE;
    }

    return TRUE;
}
                             
/************************************************************************/
/*                          GWKGetPixelValue()                          */
/************************************************************************/

static int GWKGetPixelValue( GDALWarpKernel *poWK, int iBand, 
                             int iSrcOffset, double *pdfDensity, 
                             double *pdfReal, double *pdfImag )

{
    GByte *pabySrc = poWK->papabySrcImage[iBand];

    if( poWK->panUnifiedSrcValid != NULL
        && !(poWK->panUnifiedSrcValid[iSrcOffset>>5]
             && (0x01 << (iSrcOffset & 0x1f)) ) )
    {
        *pdfDensity = 0.0;
        return FALSE;
    }

    if( poWK->papanBandSrcValid != NULL
        && poWK->papanBandSrcValid[iBand] != NULL
        && !(poWK->papanBandSrcValid[iBand][iSrcOffset>>5]
             && (0x01 << (iSrcOffset & 0x1f)) ) )
    {
        *pdfDensity = 0.0;
        return FALSE;
    }

    switch( poWK->eWorkingDataType )
    {
      case GDT_Byte:
        *pdfReal = pabySrc[iSrcOffset];
        *pdfImag = 0.0;
        break;

      case GDT_Int16:
        *pdfReal = ((GInt16 *) pabySrc)[iSrcOffset];
        *pdfImag = 0.0;
        break;

      case GDT_UInt16:
        *pdfReal = ((GUInt16 *) pabySrc)[iSrcOffset];
        *pdfImag = 0.0;
        break;
 
      case GDT_Int32:
        *pdfReal = ((GInt32 *) pabySrc)[iSrcOffset];
        *pdfImag = 0.0;
        break;
 
      case GDT_UInt32:
        *pdfReal = ((GUInt32 *) pabySrc)[iSrcOffset];
        *pdfImag = 0.0;
        break;
 
      case GDT_Float32:
        *pdfReal = ((float *) pabySrc)[iSrcOffset];
        *pdfImag = 0.0;
        break;
 
      case GDT_Float64:
        *pdfReal = ((double *) pabySrc)[iSrcOffset];
        *pdfImag = 0.0;
        break;
 
      case GDT_CInt16:
        *pdfReal = ((GInt16 *) pabySrc)[iSrcOffset*2];
        *pdfImag = ((GInt16 *) pabySrc)[iSrcOffset*2+1];
        break;
 
      case GDT_CInt32:
        *pdfReal = ((GInt32 *) pabySrc)[iSrcOffset*2];
        *pdfImag = ((GInt32 *) pabySrc)[iSrcOffset*2+1];
        break;
 
      case GDT_CFloat32:
        *pdfReal = ((float *) pabySrc)[iSrcOffset*2];
        *pdfImag = ((float *) pabySrc)[iSrcOffset*2+1];
        break;
 
      case GDT_CFloat64:
        *pdfReal = ((double *) pabySrc)[iSrcOffset*2];
        *pdfImag = ((double *) pabySrc)[iSrcOffset*2+1];
        break;

      default:
        *pdfDensity = 0.0;
        return FALSE;
    }

    if( poWK->pafUnifiedSrcDensity != NULL )
        *pdfDensity = poWK->pafUnifiedSrcDensity[iSrcOffset];
    else
        *pdfDensity = 1.0;

    return *pdfDensity != 0.0;
}
                             
/************************************************************************/
/*                           GWKGeneralCase()                           */
/*                                                                      */
/*      This is the most general case.  It attempts to handle all       */
/*      possible features with relatively little concern for            */
/*      efficiency.                                                     */
/************************************************************************/

static CPLErr GWKGeneralCase( GDALWarpKernel *poWK )

{
    int iDstY;
    int nDstXSize = poWK->nDstXSize, nDstYSize = poWK->nDstYSize;
    int nSrcXSize = poWK->nSrcXSize, nSrcYSize = poWK->nSrcYSize;
    CPLErr eErr = CE_None;

    if( !poWK->pfnProgress( poWK->dfProgressBase + poWK->dfProgressScale *
                            ((iDstY+1) / (double) nDstYSize), 
                            "", poWK->pProgress ) )
    {
        CPLError( CE_Failure, CPLE_UserInterrupt, "User terminated" );
        return CE_Failure;
    }

/* -------------------------------------------------------------------- */
/*      How much of a window around our source pixel might we need      */
/*      to collect data from based on the resampling kernel?  Even      */
/*      if the requested central pixel falls off the source image,      */
/*      we may need to collect data if some portion of the              */
/*      resampling kernel could be on-image.                            */
/* -------------------------------------------------------------------- */
    int nResWinSize = 0;

    if( poWK->eResample == GRA_Bilinear )
        nResWinSize = 1;
    
    if( poWK->eResample == GRA_Cubic )
        nResWinSize = 2;

/* -------------------------------------------------------------------- */
/*      Allocate x,y,z coordinate arrays for transformation ... one     */
/*      scanlines worth of positions.                                   */
/* -------------------------------------------------------------------- */
    double *padfX, *padfY, *padfZ;
    int    *pabSuccess;

    padfX = (double *) CPLMalloc(sizeof(double) * nDstXSize);
    padfY = (double *) CPLMalloc(sizeof(double) * nDstXSize);
    padfZ = (double *) CPLMalloc(sizeof(double) * nDstXSize);
    pabSuccess = (int *) CPLMalloc(sizeof(int) * nDstXSize);

/* ==================================================================== */
/*      Loop over output lines.                                         */
/* ==================================================================== */
    for( iDstY = 0; iDstY < nDstYSize && eErr == CE_None; iDstY++ )
    {
        int iDstX;

/* -------------------------------------------------------------------- */
/*      Setup points to transform to source image space.                */
/* -------------------------------------------------------------------- */
        for( iDstX = 0; iDstX < nDstXSize; iDstX++ )
        {
            padfX[iDstX] = iDstX + 0.5 + poWK->nDstXOff;
            padfY[iDstX] = iDstY + 0.5 + poWK->nDstYOff;
            padfZ[iDstX] = 0.0;
        }

/* -------------------------------------------------------------------- */
/*      Transform the points from destination pixel/line coordinates    */
/*      to source pixel/line coordinates.                               */
/* -------------------------------------------------------------------- */
        poWK->pfnTransformer( poWK->pTransformerArg, TRUE, nDstXSize, 
                              padfX, padfY, padfZ, pabSuccess );

/* ==================================================================== */
/*      Loop over pixels in output scanline.                            */
/* ==================================================================== */
        for( iDstX = 0; iDstX < nDstXSize; iDstX++ )
        {
            int iDstOffset;

            if( !pabSuccess[iDstX] )
                continue;

/* -------------------------------------------------------------------- */
/*      Figure out what pixel we want in our source raster, and skip    */
/*      further processing if it is well off the source image.          */
/* -------------------------------------------------------------------- */
            // We test against the value before casting to avoid the
            // problem of asymmetric truncation effects around zero.  That is
            // -0.5 will be 0 when cast to an int. 
            if( padfX[iDstX] < poWK->nSrcXOff + nResWinSize
                || padfY[iDstX] < poWK->nSrcYOff + nResWinSize )
                continue;

            int iSrcX, iSrcY, iSrcOffset;

            iSrcX = ((int) padfX[iDstX]) - poWK->nSrcXOff;
            iSrcY = ((int) padfY[iDstX]) - poWK->nSrcYOff;

            if( iSrcX >= nSrcXSize - nResWinSize 
                || iSrcY >= nSrcYSize - nResWinSize )
                continue;

            iSrcOffset = iSrcX + iSrcY * nSrcXSize;

/* -------------------------------------------------------------------- */
/*      Don't generate output pixels for which the destination valid    */
/*      mask exists and is already set.                                 */
/* -------------------------------------------------------------------- */
            iDstOffset = iDstX + iDstY * nDstXSize;
            if( poWK->panDstValid != NULL
                && (poWK->panDstValid[iDstOffset>>5]
                    && (0x01 << (iDstOffset & 0x1f))) )
                continue;

/* ==================================================================== */
/*      Loop processing each band.                                      */
/* ==================================================================== */
            int iBand;
            
            for( iBand = 0; iBand < poWK->nBands; iBand++ )
            {
                double dfDensity = 0.0;
                double dfValueReal = 0.0;
                double dfValueImag = 0.0;

/* -------------------------------------------------------------------- */
/*      Collect the source value.                                       */
/* -------------------------------------------------------------------- */
                if( poWK->eResample == GRA_NearestNeighbour )
                {
                    GWKGetPixelValue( poWK, iBand, iSrcOffset, &dfDensity, 
                                      &dfValueReal, &dfValueImag );
                }
                else if( poWK->eResample == GRA_Bilinear )
                {
                    ;
                }
                else if( poWK->eResample == GRA_Cubic )
                {
                    ;
                }

                // If we didn't find any valid inputs skip to next band.
                if( dfDensity == 0.0 )
                    continue;

/* -------------------------------------------------------------------- */
/*      We have a computed value from the source.  Now apply it to      */
/*      the destination pixel.                                          */
/* -------------------------------------------------------------------- */
                GWKSetPixelValue( poWK, iBand, iDstOffset,
                                  dfDensity, dfValueReal, dfValueImag );

            }
        }

/* -------------------------------------------------------------------- */
/*      Report progress to the user, and optionally cancel out.         */
/* -------------------------------------------------------------------- */
        if( !poWK->pfnProgress( poWK->dfProgressBase + poWK->dfProgressScale *
                                ((iDstY+1) / (double) nDstYSize), 
                                "", poWK->pProgress ) )
        {
            CPLError( CE_Failure, CPLE_UserInterrupt, "User terminated" );
            eErr = CE_Failure;
        }
    }

/* -------------------------------------------------------------------- */
/*      Cleanup and return.                                             */
/* -------------------------------------------------------------------- */
    CPLFree( padfX );
    CPLFree( padfY );
    CPLFree( padfZ );
    CPLFree( pabSuccess );

    return eErr;
}
