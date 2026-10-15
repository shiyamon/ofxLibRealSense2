//
//  ofxLibRealSense2Processing.h
//
//  Multi-threaded versions of the two librealsense processing steps that dominate the capture
//  thread's time, with the same results as librealsense's own (single-threaded) ones:
//
//  - spatialFilter(): librealsense's spatial (domain transform, edge-preserving) filter on a
//    disparity image. Every row of the horizontal pass and every column of the vertical pass
//    is independent, so they're spread over threads. ~10 ms -> ~1 ms at 1280 x 720.
//  - alignDepthToOther(): depth re-projected into another camera (librealsense's align to
//    colour), z-buffered so nearer surfaces win. Rows of the depth image in parallel.
//
//  Internal to the addon (and its tests); not part of the API.
//

#pragma once

#include <librealsense2/rs.h>
#include <librealsense2/rsutil.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace ofxLibRealSense2Processing
{
    // Runs f(band, begin, end) over [0, count) split into `threads` contiguous bands, the
    // last band on the calling thread.
    template<typename F>
    void parallelRows(int count, int threads, F&& f)
    {
        threads = std::max(1, std::min(threads, count / 16));
        if (threads <= 1)
        {
            f(0, 0, count);
            return;
        }
        std::vector<std::thread> workers;
        workers.reserve(threads - 1);
        for (int t = 0; t < threads - 1; ++t)
        {
            workers.emplace_back([&f, t, threads, count] { f(t, count * t / threads, count * (t + 1) / threads); });
        }
        f(threads - 1, count * (threads - 1) / threads, count);
        for (auto& worker : workers) worker.join();
    }

    // librealsense's validity test for disparity: the float's bit pattern as an int is > 0.
    inline bool valid(float v)
    {
        int32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        return bits > 0;
    }

    // One pass of librealsense's recursive edge-preserving filter along a line of n samples,
    // `step` apart, from line[0] towards line[(n - 1) * step]. A transcription of
    // spatial_filter::recursive_filter_horizontal_fp / _vertical_fp (librealsense 2.59), state
    // machine included, so the output matches it bit for bit.
    inline void filterLine(float* line, int n, std::ptrdiff_t step, float alpha, float deltaZ)
    {
        float* im = line;
        float state = *im;
        float previousInnovation = state;
        im += step;
        float innovation = *im;
        int u = n - 1;
        if (!valid(previousInnovation)) goto CurrentlyInvalid;

    CurrentlyValid:
        for (;;)
        {
            if (valid(innovation))
            {
                const float delta = previousInnovation - innovation;
                if (delta < deltaZ && delta > -deltaZ)
                {
                    const float filtered = innovation * alpha + state * (1.0f - alpha);
                    *im = state = filtered;
                }
                else
                {
                    state = innovation;
                }
                if (--u <= 0) return;
                previousInnovation = innovation;
                im += step;
                innovation = *im;
            }
            else
            {
                if (--u <= 0) return;
                previousInnovation = innovation;
                im += step;
                innovation = *im;
                goto CurrentlyInvalid;
            }
        }

    CurrentlyInvalid:
        for (;;)
        {
            if (--u <= 0) return;
            if (valid(innovation))
            {
                previousInnovation = state = innovation;
                im += step;
                innovation = *im;
                goto CurrentlyValid;
            }
            im += step;
            innovation = *im;
        }
    }

    // Both directions along a line, as librealsense does: forward from the first sample, then
    // backward from the last.
    inline void filterLineBothWays(float* line, int n, std::ptrdiff_t step, float alpha, float deltaZ)
    {
        if (n < 2) return;
        filterLine(line, n, step, alpha, deltaZ);
        filterLine(line + static_cast<std::ptrdiff_t>(n - 1) * step, n, -step, alpha, deltaZ);
    }

    /// librealsense's spatial filter (disparity domain, no hole filling) in place on a
    /// width x height float disparity image. alpha, delta and iterations as the filter's
    /// RS2_OPTION_FILTER_SMOOTH_ALPHA / _SMOOTH_DELTA / _MAGNITUDE options.
    inline void spatialFilter(float* disparity, int width, int height, float alpha, float delta, int iterations, int threads)
    {
        for (int i = 0; i < iterations; ++i)
        {
            parallelRows(height, threads, [&](int, int y0, int y1) {
                for (int y = y0; y < y1; ++y) filterLineBothWays(disparity + static_cast<std::ptrdiff_t>(y) * width, width, 1, alpha, delta);
            });
            parallelRows(width, threads, [&](int, int x0, int x1) {
                for (int x = x0; x < x1; ++x) filterLineBothWays(disparity + x, height, width, alpha, delta);
            });
        }
    }

    /// Depth (16-bit, `scale` metres per unit) re-projected into another camera: each depth
    /// pixel's footprint is mapped through the calibration and filled with its depth, keeping
    /// the nearest where footprints overlap - librealsense's align (align_z_to_other).
    /// `out` is other.width * other.height; 0 where nothing lands.
    ///
    /// Two passes, both parallel and without atomics: the footprints (the expensive part:
    /// two deprojections and projections per pixel) over bands of depth rows, then the
    /// z-buffer fill over bands of output rows, each thread writing only its own band.
    struct AlignScratch
    {
        struct Rect { int16_t x0, y0, x1, y1; };
        std::vector<Rect> rects;
        std::vector<int> rowMinY, rowMaxY; // per depth row: output rows its footprints touch
    };

    inline void alignDepthToOther(const uint16_t* depth, const rs2_intrinsics& depthIn, const rs2_extrinsics& depthToOther,
                                  const rs2_intrinsics& otherIn, float scale, uint16_t* out, int threads,
                                  AlignScratch& scratch)
    {
        const int dw = depthIn.width, dh = depthIn.height;
        scratch.rects.resize(static_cast<size_t>(dw) * dh);
        scratch.rowMinY.assign(dh, otherIn.height);
        scratch.rowMaxY.assign(dh, -1);
        auto* rects = scratch.rects.data();

        parallelRows(dh, threads, [&](int, int y0, int y1) {
            for (int depthY = y0; depthY < y1; ++depthY)
            {
                int minY = otherIn.height, maxY = -1;
                int index = depthY * dw;
                for (int depthX = 0; depthX < dw; ++depthX, ++index)
                {
                    auto& r = rects[index];
                    r.x0 = -1; // nothing
                    const uint16_t raw = depth[index];
                    if (!raw) continue;
                    const float z = scale * raw;

                    float depthPixel[2] = { depthX - 0.5f, depthY - 0.5f }, depthPoint[3], otherPoint[3], otherPixel[2];
                    rs2_deproject_pixel_to_point(depthPoint, &depthIn, depthPixel, z);
                    rs2_transform_point_to_point(otherPoint, &depthToOther, depthPoint);
                    rs2_project_point_to_pixel(otherPixel, &otherIn, otherPoint);
                    const int x0 = static_cast<int>(otherPixel[0] + 0.5f);
                    const int oy0 = static_cast<int>(otherPixel[1] + 0.5f);

                    depthPixel[0] = depthX + 0.5f;
                    depthPixel[1] = depthY + 0.5f;
                    rs2_deproject_pixel_to_point(depthPoint, &depthIn, depthPixel, z);
                    rs2_transform_point_to_point(otherPoint, &depthToOther, depthPoint);
                    rs2_project_point_to_pixel(otherPixel, &otherIn, otherPoint);
                    const int x1 = static_cast<int>(otherPixel[0] + 0.5f);
                    const int oy1 = static_cast<int>(otherPixel[1] + 0.5f);

                    if (x0 < 0 || oy0 < 0 || x1 >= otherIn.width || oy1 >= otherIn.height) continue;
                    r = { static_cast<int16_t>(x0), static_cast<int16_t>(oy0), static_cast<int16_t>(x1), static_cast<int16_t>(oy1) };
                    minY = std::min(minY, oy0);
                    maxY = std::max(maxY, oy1);
                }
                scratch.rowMinY[depthY] = minY;
                scratch.rowMaxY[depthY] = maxY;
            }
        });

        parallelRows(otherIn.height, threads, [&](int, int band0, int band1) {
            std::fill(out + static_cast<size_t>(band0) * otherIn.width, out + static_cast<size_t>(band1) * otherIn.width, uint16_t(0));
            for (int depthY = 0; depthY < dh; ++depthY)
            {
                if (scratch.rowMaxY[depthY] < band0 || scratch.rowMinY[depthY] >= band1) continue;
                const int index0 = depthY * dw;
                for (int depthX = 0; depthX < dw; ++depthX)
                {
                    const auto& r = rects[index0 + depthX];
                    if (r.x0 < 0 || r.y1 < band0 || r.y0 >= band1) continue;
                    const uint16_t raw = depth[index0 + depthX];
                    const int ya = std::max<int>(r.y0, band0), yb = std::min<int>(r.y1, band1 - 1);
                    for (int y = ya; y <= yb; ++y)
                    {
                        uint16_t* row = out + static_cast<size_t>(y) * otherIn.width;
                        for (int x = r.x0; x <= r.x1; ++x)
                        {
                            row[x] = row[x] ? std::min(row[x], raw) : raw;
                        }
                    }
                }
            }
        });
    }
}
