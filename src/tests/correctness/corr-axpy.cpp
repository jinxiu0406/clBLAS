/* ************************************************************************
 * Copyright 2013 Advanced Micro Devices, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ************************************************************************/


#include <stdlib.h>             // srand()
#include <string.h>             // memcpy()
#include <gtest/gtest.h>
#include <clBLAS.h>

#include <common.h>
#include <blas-internal.h>
#include <blas-wrapper.h>
#include <clBLAS-wrapper.h>
#include <BlasBase.h>
#include <blas-random.h>
#include <axpy.h>

static void
releaseMemObjects(cl_mem objX,  cl_mem objY)
{
  if(objX != NULL)
  {
  	clReleaseMemObject(objX);
  }
  if(objY != NULL)
  {
    clReleaseMemObject(objY);
  }
}

template <typename T> static void
deleteBuffers(T *X, T *Y,  T *blasX, T *blasY)
{
    if(X != NULL)
    {
    delete[] X;
    }
	if(blasX != NULL)
	{
    delete[] blasX;
	}
    if(Y != NULL)
    {
    delete[] Y;
    }
	if(blasY != NULL)
	{
    delete[] blasY;
	}
}

template <typename T>
void
axpyCorrectnessTest(TestParams *params)
{
    cl_int err;
    T *X, *Y; //For OpenCL implementation
    T *blasX, *blasY;// For reference implementation
    cl_mem bufX, bufY;
    clMath::BlasBase *base;
    cl_event *events;
    T alpha;

    base = clMath::BlasBase::getInstance();

    if ((typeid(T) == typeid(cl_double) ||
         typeid(T) == typeid(DoubleComplex)) &&
        !base->isDevSupportDoublePrecision()) {

        std::cerr << ">> WARNING: The target device doesn't support native "
                     "double precision floating point arithmetic" <<
                     std::endl << ">> Test skipped" << std::endl;
        SUCCEED();
        return;
    }

	printf("number of command queues : %d\n\n", params->numCommandQueues);

    events = new cl_event[params->numCommandQueues];
    memset(events, 0, params->numCommandQueues * sizeof(cl_event));

    size_t lengthX = (1 + ((params->N -1) * abs(params->incx)));
    size_t lengthY = (1 + ((params->N -1) * abs(params->incy)));

    X 		= new T[lengthX + params->offBX ];
    Y 		= new T[lengthY + params->offCY ];
    blasX 	= new T[lengthX + params->offBX ];
    blasY	= new T[lengthY + params->offCY ];

	if((X == NULL) || (blasX == NULL) || (Y == NULL) || (blasY == NULL))
	{
		::std::cerr << "Cannot allocate memory on host side\n" << "!!!!!!!!!!!!Test skipped.!!!!!!!!!!!!" << ::std::endl;
        deleteBuffers<T>(X, Y, blasX, blasY);
		delete[] events;
		SUCCEED();
        return;
	}

    srand(params->seed);

    // Populate X and Y
    randomVectors(params->N, (X+params->offBX), params->incx, (Y+params->offCY), params->incy);

	memcpy(blasX, X, (lengthX + params->offBX) * sizeof(T));
	memcpy(blasY, Y, (lengthY + params->offCY) * sizeof(T));
    alpha = convertMultiplier<T>(params->alpha);

	// Allocate buffers
    bufX = base->createEnqueueBuffer(X, (lengthX + params->offBX)* sizeof(T), 0, CL_MEM_READ_ONLY);
    bufY = base->createEnqueueBuffer(Y, (lengthY + params->offCY)* sizeof(T), 0, CL_MEM_READ_WRITE);

	if ((bufX == NULL) || (bufY == NULL)) {
        /* Skip the test, the most probable reason is
         *     matrix too big for a device.
         */
        releaseMemObjects(bufX, bufY);
        deleteBuffers<T>(X, Y, blasX, blasY);
        delete[] events;
        ::std::cerr << ">> Failed to create/enqueue buffer for a matrix."
            << ::std::endl
            << ">> Can't execute the test, because data is not transfered to GPU."
            << ::std::endl
            << ">> Test skipped." << ::std::endl;
        SUCCEED();
        return;
    }

	::clMath::blas::axpy((size_t)params->N, alpha, blasX, (size_t)params->offBX, params->incx,
						 blasY, (size_t)params->offCY, params->incy);

    err = (cl_int)::clMath::clblas::axpy(params->N, alpha, bufX, params->offBX, params->incx, bufY, params->offCY, params->incy,
										  params->numCommandQueues, base->commandQueues(), 0, NULL, events);

    if (err != CL_SUCCESS) {
        releaseMemObjects(bufX, bufY);
        deleteBuffers<T>(X, Y, blasX, blasY);
        delete[] events;
        ASSERT_EQ(CL_SUCCESS, err) << "::clMath::clblas::AXPY() failed";
    }

    err = waitForSuccessfulFinish(params->numCommandQueues,
        base->commandQueues(), events);
    if (err != CL_SUCCESS) {
        releaseMemObjects(bufX, bufY);
        deleteBuffers<T>(X, Y, blasX, blasY);
        delete[] events;
        ASSERT_EQ(CL_SUCCESS, err) << "waitForSuccessfulFinish()";
    }

    err = clEnqueueReadBuffer(base->commandQueues()[0], bufY, CL_TRUE, 0,
        (lengthY + params->offCY) * sizeof(T), Y, 0, NULL, NULL);
	if (err != CL_SUCCESS)
	{
		::std::cerr << "AXPY: Reading results failed...." << std::endl;
	}

    releaseMemObjects(bufX, bufY);

    compareMatrices<T>(clblasRowMajor, lengthY , 1, (blasY + params->offCY), (Y + params->offCY), 1);

    if (::testing::Test::HasFailure())
    {
        printTestParams(params->N, params->alpha, params->offBX, params->incx, params->offCY, params->incy);
        ::std::cerr << "queues = " << params->numCommandQueues << ::std::endl;
    }

    deleteBuffers<T>(X, Y, blasX, blasY);
    delete[] events;
}

// Instantiate the test

TEST_P(AXPY, saxpy) {
    TestParams params;

    getParams(&params);
    axpyCorrectnessTest<cl_float>(&params);
}

TEST_P(AXPY, daxpy) {
    TestParams params;

    getParams(&params);
    axpyCorrectnessTest<cl_double>(&params);
}

TEST_P(AXPY, caxpy) {
    TestParams params;

    getParams(&params);
    axpyCorrectnessTest<FloatComplex>(&params);
}

TEST_P(AXPY, zaxpy) {
    TestParams params;

    getParams(&params);
    axpyCorrectnessTest<DoubleComplex>(&params);
}
