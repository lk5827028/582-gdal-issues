///////////////////////////////////////////////////////////////////////////////
//
// Project:  C++ Test Suite for GDAL/OGR
// Purpose:  Test general OGR features.
// Author:   Andrew Bell
//
///////////////////////////////////////////////////////////////////////////////
/*
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

#include <algorithm>
#include <array>
#include <utility>

#include "gdal_unit_test.h"

#include "gtest_include.h"

#include "viewshed.h"

namespace gdal
{

namespace
{
using Coord = std::pair<int, int>;
using DatasetPtr = std::unique_ptr<GDALDataset>;
using Transform = std::array<double, 6>;
Transform identity{0, 1, 0, 0, 0, 1};

Viewshed::Options stdOptions(int x, int y)
{
    Viewshed::Options opts;
    opts.observer.x = x;
    opts.observer.y = y;
    opts.outputFilename = "none";
    opts.outputFormat = "mem";
    opts.curveCoeff = 0;

    return opts;
}

Viewshed::Options stdOptions(const Coord &observer)
{
    return stdOptions(observer.first, observer.second);
}

DatasetPtr runViewshed(int8_t *in, int edgeLength, Viewshed::Options opts)
{
    Viewshed v(opts);

    GDALDriver *driver = (GDALDriver *)GDALGetDriverByName("MEM");
    GDALDataset *dataset =
        driver->Create("", edgeLength, edgeLength, 1, GDT_Int8, nullptr);
    EXPECT_TRUE(dataset);
    dataset->SetGeoTransform(identity.data());
    GDALRasterBand *band = dataset->GetRasterBand(1);
    EXPECT_TRUE(band);
    CPLErr err =
        band->RasterIO(GF_Write, 0, 0, edgeLength, edgeLength, in, edgeLength,
                       edgeLength, GDT_Int8, 0, 0, nullptr);
    EXPECT_EQ(err, CE_None);

    EXPECT_TRUE(v.run(band));
    return v.output();
}

}  // namespace

TEST(Viewshed, all_visible)
{
    // clang-format off
    const int edge = 3;
    std::array<int8_t, edge * edge> in
    {
        1, 2, 3,
        4, 5, 6,
        3, 2, 1
    };
    // clang-format on

    SCOPED_TRACE("all_visible");
    DatasetPtr output = runViewshed(in.data(), edge, stdOptions(1, 1));

    std::array<uint8_t, edge * edge> out;
    GDALRasterBand *band = output->GetRasterBand(1);
    CPLErr err = band->RasterIO(GF_Read, 0, 0, edge, edge, out.data(), edge,
                                edge, GDT_Int8, 0, 0, nullptr);
    EXPECT_EQ(err, CE_None);

    for (uint8_t o : out)
        EXPECT_EQ(o, 127);
}

TEST(Viewshed, simple_height)
{
    // clang-format off
    const int edge = 5;
    std::array<int8_t, edge * edge> in
    {
        -1, 0, 1, 0, -1,
        -1, 2, 0, 4, -1,
        -1, 1, 0, -1, -1,
         0, 3, 0, 2, 0,
        -1, 0, 0, 3, -1
    };

    std::array<double, edge * edge> observable
    {
        4, 2, 0, 4, 8,
        3, 2, 0, 4, 3,
        2, 1, 0, -1, -2,
        4, 3, 0, 2, 1,
        6, 3, 0, 2, 4
    };
    // clang-format on

    {
        SCOPED_TRACE("simple_height:normal");

        DatasetPtr output = runViewshed(in.data(), 5, stdOptions(2, 2));

        std::array<uint8_t, edge * edge> out;
        GDALRasterBand *band = output->GetRasterBand(1);
        CPLErr err = band->RasterIO(GF_Read, 0, 0, edge, edge, out.data(), edge,
                                    edge, GDT_Int8, 0, 0, nullptr);
        EXPECT_EQ(err, CE_None);

        // We expect the cell to be observable when the input is higher than the observable
        // height.
        std::array<uint8_t, edge * edge> expected;
        for (size_t i = 0; i < in.size(); ++i)
            expected[i] = (in[i] >= observable[i] ? 127 : 0);

        EXPECT_EQ(expected, out);
    }

    {
        std::array<double, edge * edge> dem;
        SCOPED_TRACE("simple_height:dem");
        Viewshed::Options opts = stdOptions(2, 2);
        opts.outputMode = Viewshed::OutputMode::DEM;
        DatasetPtr output = runViewshed(in.data(), 5, opts);

        GDALRasterBand *band = output->GetRasterBand(1);
        CPLErr err = band->RasterIO(GF_Read, 0, 0, edge, edge, dem.data(), edge,
                                    edge, GDT_Float64, 0, 0, nullptr);
        EXPECT_EQ(err, CE_None);

        // DEM values are observable values clamped at 0. Not sure why.
        std::array<double, edge *edge> expected = observable;
        for (double &d : expected)
            d = std::max(0.0, d);

        // Double equality is fine here as all the values are small integers.
        EXPECT_EQ(dem, expected);
    }

    {
        std::array<double, edge * edge> ground;
        SCOPED_TRACE("simple_height:ground");
        Viewshed::Options opts = stdOptions(2, 2);
        opts.outputMode = Viewshed::OutputMode::Ground;
        DatasetPtr output = runViewshed(in.data(), 5, opts);

        GDALRasterBand *band = output->GetRasterBand(1);
        CPLErr err = band->RasterIO(GF_Read, 0, 0, edge, edge, ground.data(),
                                    edge, edge, GDT_Float64, 0, 0, nullptr);
        EXPECT_EQ(err, CE_None);

        std::array<double, edge * edge> expected;
        for (size_t i = 0; i < expected.size(); ++i)
            expected[i] = std::max(0.0, observable[i] - in[i]);

        // Double equality is fine here as all the values are small integers.
        EXPECT_EQ(expected, ground);
    }
}

TEST(Viewshed, dem_vs_ground)
{
    auto process = [](const std::array<int8_t, 8> &in, Viewshed::Options &opts)
    {
        Viewshed v(opts);

        GDALDriver *driver = (GDALDriver *)GDALGetDriverByName("MEM");
        // 8 cols x 1 row
        GDALDataset *dataset = driver->Create("", 8, 1, 1, GDT_Int8, nullptr);
        EXPECT_TRUE(dataset);
        dataset->SetGeoTransform(identity.data());
        GDALRasterBand *band = dataset->GetRasterBand(1);
        EXPECT_TRUE(band);
        CPLErr err = band->RasterIO(GF_Write, 0, 0, 8, 1, (void *)in.data(), 8,
                                    1, GDT_Int8, 0, 0, nullptr);
        EXPECT_EQ(err, CE_None);

        EXPECT_TRUE(v.run(band));
        return v.output();
    };

    auto run = [&process](const std::array<int8_t, 8> &in, Coord observer,
                          const std::array<double, 8> &ground,
                          const std::array<double, 8> &dem)
    {
        Viewshed::Options opts = stdOptions(observer);

        std::array<double, 8> out;
        opts.outputMode = Viewshed::OutputMode::Ground;
        DatasetPtr ds = process(in, opts);
        GDALRasterBand *band = ds->GetRasterBand(1);
        CPLErr err = band->RasterIO(GF_Read, 0, 0, 8, 1, out.data(), 8, 1,
                                    GDT_Float64, 0, 0, nullptr);
        EXPECT_EQ(err, CE_None);
        for (size_t i = 0; i < ground.size(); ++i)
            EXPECT_DOUBLE_EQ(out[i], ground[i]);

        opts.outputMode = Viewshed::OutputMode::DEM;
        ds = process(in, opts);
        band = ds->GetRasterBand(1);
        err = band->RasterIO(GF_Read, 0, 0, 8, 1, out.data(), 8, 1, GDT_Float64,
                             0, 0, nullptr);
        EXPECT_EQ(err, CE_None);
        for (size_t i = 0; i < dem.size(); ++i)
            EXPECT_DOUBLE_EQ(out[i], dem[i]);
    };

    // Input / Observer / Minimum expected above ground / Minimum expected above zero
    run({0, 0, 0, 1, 0, 0, 0, 0}, {2, 0}, {0, 0, 0, 0, 2, 3, 4, 5},
        {0, 0, 0, 1, 2, 3, 4, 5});
    run({1, 1, 0, 1, 0, 1, 2, 2}, {3, 0}, {0, 0, 0, 0, 0, 0, 0, 1 / 3.0},
        {1, 0, 0, 1, 0, 0, 1, 7 / 3.0});
    run({0, 0, 0, 1, 1, 0, 0, 0}, {0, 0},
        {0, 0, 0, 0, 1 / 3.0, 5 / 3.0, 6 / 3.0, 7 / 3.0},
        {0, 0, 0, 0, 4 / 3.0, 5 / 3.0, 6 / 3.0, 7 / 3.0});
    run({0, 0, 1, 2, 3, 4, 5, 6}, {0, 0}, {0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 3 / 2.0, 8 / 3.0, 15 / 4.0, 24 / 5.0, 35 / 6.0});
    run({0, 0, 1, 1, 3, 4, 5, 4}, {0, 0}, {0, 0, 0, .5, 0, 0, 0, 11 / 6.0},
        {0, 0, 0, 3 / 2.0, 2, 15 / 4.0, 24 / 5.0, 35 / 6.0});
}

}  // namespace gdal
