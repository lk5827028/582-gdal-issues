/******************************************************************************
 *
 * Project:  GDAL Utilities
 * Purpose:  Command line application to convert a multidimensional raster
 * Author:   Even Rouault,<even.rouault at spatialys.com>
 *
 * ****************************************************************************
 * Copyright (c) 2019, Even Rouault <even.rouault at spatialys.com>
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
 ****************************************************************************/

#include "cpl_string.h"
#include "gdal_version.h"
#include "commonutils.h"
#include "gdal_utils_priv.h"
#include "gdal_priv.h"

/**
 * @brief Makes sure the GDAL library is properly cleaned up before exiting.
 * @param nCode exit code
 * @todo Move to API
 */
static void GDALExit(int nCode)
{
    GDALDestroy();
    exit(nCode);
}

/************************************************************************/
/*                               Usage()                                */
/************************************************************************/

static void Usage()
{
    fprintf(stderr, "%s\n", GDALMultiDimTranslateAppGetParserUsage().c_str());
    GDALExit(1);
}

/************************************************************************/
/*                                main()                                */
/************************************************************************/

MAIN_START(argc, argv)
{
    /* Check strict compilation and runtime library version as we use C++ API */
    if (!GDAL_CHECK_VERSION(argv[0]))
        GDALExit(1);

    EarlySetConfigOptions(argc, argv);

    /* -------------------------------------------------------------------- */
    /*      Generic arg processing.                                         */
    /* -------------------------------------------------------------------- */
    GDALAllRegister();

    argc = GDALGeneralCmdLineProcessor(argc, &argv, 0);
    if (argc < 1)
        GDALExit(-argc);

    /* -------------------------------------------------------------------- */
    /*      Parse command line                                              */
    /* -------------------------------------------------------------------- */

    GDALMultiDimTranslateOptionsForBinary sOptionsForBinary;

    std::unique_ptr<GDALMultiDimTranslateOptions,
                    decltype(&GDALMultiDimTranslateOptionsFree)>
        psOptions{GDALMultiDimTranslateOptionsNew(argv + 1, &sOptionsForBinary),
                  GDALMultiDimTranslateOptionsFree};
    CSLDestroy(argv);
    if (!psOptions)
    {
        Usage();
    }

    if (!(sOptionsForBinary.bQuiet))
    {
        GDALMultiDimTranslateOptionsSetProgress(psOptions.get(),
                                                GDALTermProgress, nullptr);
    }

    /* -------------------------------------------------------------------- */
    /*      Open input file.                                                */
    /* -------------------------------------------------------------------- */
    GDALDatasetH hInDS = GDALOpenEx(
        sOptionsForBinary.osSource.c_str(),
        GDAL_OF_RASTER | GDAL_OF_MULTIDIM_RASTER | GDAL_OF_VERBOSE_ERROR,
        sOptionsForBinary.aosAllowInputDrivers.List(),
        sOptionsForBinary.aosOpenOptions.List(), nullptr);

    if (hInDS == nullptr)
        GDALExit(1);

    /* -------------------------------------------------------------------- */
    /*      Open output file if in update mode.                             */
    /* -------------------------------------------------------------------- */
    GDALDatasetH hDstDS = nullptr;
    // Note: since bUpdate is never changed and defaults to false this block
    //       will never be executed
    if (sOptionsForBinary.bUpdate)
    {
        CPLPushErrorHandler(CPLQuietErrorHandler);
        hDstDS = GDALOpenEx(sOptionsForBinary.osDest.c_str(),
                            GDAL_OF_RASTER | GDAL_OF_MULTIDIM_RASTER |
                                GDAL_OF_VERBOSE_ERROR | GDAL_OF_UPDATE,
                            nullptr, nullptr, nullptr);
        CPLPopErrorHandler();
    }

    int bUsageError = FALSE;
    GDALDatasetH hRetDS =
        GDALMultiDimTranslate(sOptionsForBinary.osDest.c_str(), hDstDS, 1,
                              &hInDS, psOptions.get(), &bUsageError);

    if (bUsageError == TRUE)
        Usage();

    int nRetCode = hRetDS ? 0 : 1;

    if (GDALClose(hRetDS) != CE_None)
        nRetCode = 1;

    if (GDALClose(hInDS) != CE_None)
        nRetCode = 1;

    GDALDestroy();

    return nRetCode;
}

MAIN_END
