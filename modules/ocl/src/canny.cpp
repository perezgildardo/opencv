/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2010-2012, Multicoreware, Inc., all rights reserved.
// Copyright (C) 2010-2012, Advanced Micro Devices, Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// @Authors
//    Peng Xiao, pengxiao@multicorewareinc.com
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other oclMaterials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors as is and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include <iomanip>
#include "precomp.hpp"

using namespace cv;
using namespace cv::ocl;
using namespace std;

#if !defined (HAVE_OPENCL)
void cv::ocl::Canny(const oclMat& image, oclMat& edges, double low_thresh, double high_thresh, int apperture_size = 3, bool L2gradient = false) { throw_nogpu(); }
void cv::ocl::Canny(const oclMat& image, CannyBuf& buf, oclMat& edges, double low_thresh, double high_thresh, int apperture_size = 3, bool L2gradient = false){ throw_nogpu(); }
void cv::ocl::Canny(const oclMat& dx, const oclMat& dy, oclMat& edges, double low_thresh, double high_thresh, bool L2gradient = false){ throw_nogpu(); }
void cv::ocl::Canny(const oclMat& dx, const oclMat& dy, CannyBuf& buf, oclMat& edges, double low_thresh, double high_thresh, bool L2gradient = false){ throw_nogpu(); }
#else

namespace cv
{
	namespace ocl
	{
		///////////////////////////OpenCL kernel strings///////////////////////////
		extern const char *imgproc_canny;
	}
}

cv::ocl::CannyBuf::CannyBuf(const oclMat& dx_, const oclMat& dy_) : dx(dx_), dy(dy_)
{
    CV_Assert(dx_.type() == CV_32SC1 && dy_.type() == CV_32SC1 && dx_.size() == dy_.size());

    create(dx_.size(), -1);
}

void cv::ocl::CannyBuf::create(const Size& image_size, int apperture_size)
{
	dx.create(image_size, CV_32SC1);
	dy.create(image_size, CV_32SC1);

	if(apperture_size == 3)
	{
		dx_buf.create(image_size, CV_32SC1);
		dy_buf.create(image_size, CV_32SC1);
	}
	else if(apperture_size > 0)
    {
		Mat kx, ky;
        if (!filterDX)
		{
			filterDX = createDerivFilter_GPU(CV_32F, CV_32F, 1, 0, apperture_size, BORDER_REPLICATE);
		}
        if (!filterDY)
		{
            filterDY = createDerivFilter_GPU(CV_32F, CV_32F, 0, 1, apperture_size, BORDER_REPLICATE);
		}
    }
	edgeBuf.create(image_size.height + 2, image_size.width + 2, CV_32FC1);
	
	trackBuf1.create(1, image_size.width * image_size.height, CV_16UC2);
	trackBuf2.create(1, image_size.width * image_size.height, CV_16UC2);

	counter.create(1,1, CV_32SC1);
}

void cv::ocl::CannyBuf::release()
{
    dx.release();
    dy.release();
    dx_buf.release();
    dy_buf.release();
    edgeBuf.release();
    trackBuf1.release();
    trackBuf2.release();
	counter.release();
}

namespace cv { namespace ocl {
	namespace canny
	{
        void calcSobelRowPass_gpu(const oclMat& src, oclMat& dx_buf, oclMat& dy_buf, int rows, int cols);

        void calcMagnitude_gpu(const oclMat& dx_buf, const oclMat& dy_buf, oclMat& dx, oclMat& dy, oclMat& mag, int rows, int cols, bool L2Grad);
        void calcMagnitude_gpu(const oclMat& dx, const oclMat& dy, oclMat& mag, int rows, int cols, bool L2Grad);

        void calcMap_gpu(oclMat& dx, oclMat& dy, oclMat& mag, oclMat& map, int rows, int cols, float low_thresh, float high_thresh);

        void edgesHysteresisLocal_gpu(oclMat& map, oclMat& st1, oclMat& counter, int rows, int cols);

        void edgesHysteresisGlobal_gpu(oclMat& map, oclMat& st1, oclMat& st2, oclMat& counter, int rows, int cols);

        void getEdges_gpu(oclMat& map, oclMat& dst, int rows, int cols);
	}
}}// cv::ocl

namespace
{
    void CannyCaller(CannyBuf& buf, oclMat& dst, float low_thresh, float high_thresh)
    {
        using namespace ::cv::ocl::canny;
        calcMap_gpu(buf.dx, buf.dy, buf.edgeBuf, buf.edgeBuf, dst.rows, dst.cols, low_thresh, high_thresh);

        edgesHysteresisLocal_gpu(buf.edgeBuf, buf.trackBuf1, buf.counter, dst.rows, dst.cols);

        edgesHysteresisGlobal_gpu(buf.edgeBuf, buf.trackBuf1, buf.trackBuf2, buf.counter, dst.rows, dst.cols);

        getEdges_gpu(buf.edgeBuf, dst, dst.rows, dst.cols);
    }
}

void cv::ocl::Canny(const oclMat& src, oclMat& dst, double low_thresh, double high_thresh, int apperture_size, bool L2gradient)
{
    CannyBuf buf(src.size(), apperture_size);
    Canny(src, buf, dst, low_thresh, high_thresh, apperture_size, L2gradient);
}

void cv::ocl::Canny(const oclMat& src, CannyBuf& buf, oclMat& dst, double low_thresh, double high_thresh, int apperture_size, bool L2gradient)
{
    using namespace ::cv::ocl::canny;

    CV_Assert(src.type() == CV_8UC1);

    if( low_thresh > high_thresh )
        std::swap( low_thresh, high_thresh );

    dst.create(src.size(), CV_8U);
    dst.setTo(Scalar::all(0));

    buf.create(src.size(), apperture_size);
    buf.edgeBuf.setTo(Scalar::all(0));
	buf.counter.setTo(Scalar::all(0));

    if (apperture_size == 3)
    {
        calcSobelRowPass_gpu(src, buf.dx_buf, buf.dy_buf, src.rows, src.cols);

        calcMagnitude_gpu(buf.dx_buf, buf.dy_buf, buf.dx, buf.dy, buf.edgeBuf, src.rows, src.cols, L2gradient);
    }
    else
    {
		// FIXME:
		// current ocl implementation requires the src and dst having same type
		// convertTo is time consuming so this may be optimized later.
		oclMat src_omat32f = src;
		src.convertTo(src_omat32f, CV_32F); // FIXME

        buf.filterDX->apply(src_omat32f, buf.dx);
        buf.filterDY->apply(src_omat32f, buf.dy);

		buf.dx.convertTo(buf.dx, CV_32S); // FIXME
		buf.dy.convertTo(buf.dy, CV_32S); // FIXME

        calcMagnitude_gpu(buf.dx, buf.dy, buf.edgeBuf, src.rows, src.cols, L2gradient);
    }
    CannyCaller(buf, dst, static_cast<float>(low_thresh), static_cast<float>(high_thresh));
}
void cv::ocl::Canny(const oclMat& dx, const oclMat& dy, oclMat& dst, double low_thresh, double high_thresh, bool L2gradient)
{
    CannyBuf buf(dx, dy);
    Canny(dx, dy, buf, dst, low_thresh, high_thresh, L2gradient);
}

void cv::ocl::Canny(const oclMat& dx, const oclMat& dy, CannyBuf& buf, oclMat& dst, double low_thresh, double high_thresh, bool L2gradient)
{
    using namespace ::cv::ocl::canny;

    CV_Assert(dx.type() == CV_32SC1 && dy.type() == CV_32SC1 && dx.size() == dy.size());

    if( low_thresh > high_thresh )
        std::swap( low_thresh, high_thresh);

    dst.create(dx.size(), CV_8U);
    dst.setTo(Scalar::all(0));

    buf.dx = dx; buf.dy = dy;
    buf.create(dx.size(), -1);
    buf.edgeBuf.setTo(Scalar::all(0));
	buf.counter.setTo(Scalar::all(0));
    calcMagnitude_gpu(buf.dx, buf.dy, buf.edgeBuf, dx.rows, dx.cols, L2gradient);

    CannyCaller(buf, dst, static_cast<float>(low_thresh), static_cast<float>(high_thresh));
}

void canny::calcSobelRowPass_gpu(const oclMat& src, oclMat& dx_buf, oclMat& dy_buf, int rows, int cols)
{
	Context *clCxt = src.clCxt;
	string kernelName = "calcSobelRowPass";
	vector< pair<size_t, const void *> > args;

	args.push_back( make_pair( sizeof(cl_mem), (void *)&src.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&dx_buf.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&dy_buf.data));
	args.push_back( make_pair( sizeof(cl_int), (void *)&rows));
	args.push_back( make_pair( sizeof(cl_int), (void *)&cols));
	args.push_back( make_pair( sizeof(cl_int), (void *)&src.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&src.offset));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dx_buf.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dx_buf.offset));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dy_buf.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dy_buf.offset));

	size_t globalThreads[3] = {cols, rows, 1};
	size_t localThreads[3]  = {16, 16, 1};
	openCLExecuteKernel(clCxt, &imgproc_canny, kernelName, globalThreads, localThreads, args, -1, -1);
}

void canny::calcMagnitude_gpu(const oclMat& dx_buf, const oclMat& dy_buf, oclMat& dx, oclMat& dy, oclMat& mag, int rows, int cols, bool L2Grad)
{
	Context *clCxt = dx_buf.clCxt;
	string kernelName = "calcMagnitude_buf";
	vector< pair<size_t, const void *> > args;

	args.push_back( make_pair( sizeof(cl_mem), (void *)&dx_buf.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&dy_buf.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&dx.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&dy.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&mag.data));
	args.push_back( make_pair( sizeof(cl_int), (void *)&rows));
	args.push_back( make_pair( sizeof(cl_int), (void *)&cols));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dx_buf.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dx_buf.offset));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dy_buf.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dy_buf.offset));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dx.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dx.offset));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dy.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dy.offset));
	args.push_back( make_pair( sizeof(cl_int), (void *)&mag.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&mag.offset));

	size_t globalThreads[3] = {cols, rows, 1};
	size_t localThreads[3]  = {16, 16, 1};

	char build_options [15] = "";
	if(L2Grad)
	{
		strcat(build_options, "-D L2GRAD");
	}
	openCLExecuteKernel(clCxt, &imgproc_canny, kernelName, globalThreads, localThreads, args, -1, -1, build_options);
}
void canny::calcMagnitude_gpu(const oclMat& dx, const oclMat& dy, oclMat& mag, int rows, int cols, bool L2Grad)
{
	Context *clCxt = dx.clCxt;
	string kernelName = "calcMagnitude";
	vector< pair<size_t, const void *> > args;

	args.push_back( make_pair( sizeof(cl_mem), (void *)&dx.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&dy.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&mag.data));
	args.push_back( make_pair( sizeof(cl_int), (void *)&rows));
	args.push_back( make_pair( sizeof(cl_int), (void *)&cols));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dx.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dx.offset));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dy.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dy.offset));
	args.push_back( make_pair( sizeof(cl_int), (void *)&mag.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&mag.offset));

	size_t globalThreads[3] = {cols, rows, 1};
	size_t localThreads[3]  = {16, 16, 1};

	char build_options [15] = "";
	if(L2Grad)
	{
		strcat(build_options, "-D L2GRAD");
	}
	openCLExecuteKernel(clCxt, &imgproc_canny, kernelName, globalThreads, localThreads, args, -1, -1, build_options);
}

void canny::calcMap_gpu(oclMat& dx, oclMat& dy, oclMat& mag, oclMat& map, int rows, int cols, float low_thresh, float high_thresh)
{
	Context *clCxt = dx.clCxt;
	
	vector< pair<size_t, const void *> > args;

	args.push_back( make_pair( sizeof(cl_mem), (void *)&dx.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&dy.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&mag.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&map.data));
	args.push_back( make_pair( sizeof(cl_int), (void *)&rows));
	args.push_back( make_pair( sizeof(cl_int), (void *)&cols));
	args.push_back( make_pair( sizeof(cl_float), (void *)&low_thresh));
	args.push_back( make_pair( sizeof(cl_float), (void *)&high_thresh));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dx.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dx.offset));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dy.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dy.offset));
	args.push_back( make_pair( sizeof(cl_int), (void *)&mag.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&mag.offset));
	args.push_back( make_pair( sizeof(cl_int), (void *)&map.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&map.offset));

#if CALCMAP_FIXED
	size_t globalThreads[3] = {cols, rows, 1};
	string kernelName = "calcMap";
	size_t localThreads[3]  = {16, 16, 1};
#else
	size_t globalThreads[3] = {cols, rows, 1};
	string kernelName = "calcMap_2";
	size_t localThreads[3]  = {256, 1, 1};
#endif
	openCLExecuteKernel(clCxt, &imgproc_canny, kernelName, globalThreads, localThreads, args, -1, -1);
}

void canny::edgesHysteresisLocal_gpu(oclMat& map, oclMat& st1, oclMat& counter, int rows, int cols)
{
	Context *clCxt = map.clCxt;
	string kernelName = "edgesHysteresisLocal";
	vector< pair<size_t, const void *> > args;

	args.push_back( make_pair( sizeof(cl_mem), (void *)&map.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&st1.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&counter.data));
	args.push_back( make_pair( sizeof(cl_int), (void *)&rows));
	args.push_back( make_pair( sizeof(cl_int), (void *)&cols));
	args.push_back( make_pair( sizeof(cl_int), (void *)&map.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&map.offset));

	size_t globalThreads[3] = {cols, rows, 1};
	size_t localThreads[3]  = {16, 16, 1};

	openCLExecuteKernel(clCxt, &imgproc_canny, kernelName, globalThreads, localThreads, args, -1, -1);
}

void canny::edgesHysteresisGlobal_gpu(oclMat& map, oclMat& st1, oclMat& st2, oclMat& counter, int rows, int cols)
{
	unsigned int count = Mat(counter).at<unsigned int>(0);

	Context *clCxt = map.clCxt;
	string kernelName = "edgesHysteresisGlobal";
	vector< pair<size_t, const void *> > args;
	size_t localThreads[3]  = {128, 1, 1};

#define DIVUP(a, b) ((a)+(b)-1)/(b)

	while(count > 0)
	{
		counter.setTo(0);
		args.clear();
		size_t globalThreads[3] = {std::min(count, 65535u) * 128, DIVUP(count, 65535), 1};
		args.push_back( make_pair( sizeof(cl_mem), (void *)&map.data));
		args.push_back( make_pair( sizeof(cl_mem), (void *)&st1.data));
		args.push_back( make_pair( sizeof(cl_mem), (void *)&st2.data));
		args.push_back( make_pair( sizeof(cl_mem), (void *)&counter.data));
		args.push_back( make_pair( sizeof(cl_int), (void *)&rows));
		args.push_back( make_pair( sizeof(cl_int), (void *)&cols));
		args.push_back( make_pair( sizeof(cl_int), (void *)&count));
		args.push_back( make_pair( sizeof(cl_int), (void *)&map.step));
		args.push_back( make_pair( sizeof(cl_int), (void *)&map.offset));

		openCLExecuteKernel(clCxt, &imgproc_canny, kernelName, globalThreads, localThreads, args, -1, -1);
		count = Mat(counter).at<unsigned int>(0);
		std::swap(st1, st2);
	}
#undef DIVUP
}

void canny::getEdges_gpu(oclMat& map, oclMat& dst, int rows, int cols)
{
	Context *clCxt = map.clCxt;
	string kernelName = "getEdges";
	vector< pair<size_t, const void *> > args;

	args.push_back( make_pair( sizeof(cl_mem), (void *)&map.data));
	args.push_back( make_pair( sizeof(cl_mem), (void *)&dst.data));
	args.push_back( make_pair( sizeof(cl_int), (void *)&rows));
	args.push_back( make_pair( sizeof(cl_int), (void *)&cols));
	args.push_back( make_pair( sizeof(cl_int), (void *)&map.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&map.offset));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dst.step));
	args.push_back( make_pair( sizeof(cl_int), (void *)&dst.offset));

	size_t globalThreads[3] = {cols, rows, 1};
	size_t localThreads[3]  = {16, 16, 1};

	openCLExecuteKernel(clCxt, &imgproc_canny, kernelName, globalThreads, localThreads, args, -1, -1);
}

#endif // HAVE_OPENCL
