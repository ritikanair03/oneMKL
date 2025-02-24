/*******************************************************************************
* Copyright 2020-2021 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing,
* software distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions
* and limitations under the License.
*
*
* SPDX-License-Identifier: Apache-2.0
*******************************************************************************/

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

#if __has_include(<sycl/sycl.hpp>)
#include <sycl/sycl.hpp>
#else
#include <CL/sycl.hpp>
#endif
#include "allocator_helper.hpp"
#include "cblas.h"
#include "oneapi/mkl.hpp"
#include "oneapi/mkl/detail/config.hpp"
#include "onemkl_blas_helper.hpp"
#include "reference_blas_templates.hpp"
#include "test_common.hpp"
#include "test_helper.hpp"

#include <gtest/gtest.h>

using namespace sycl;
using std::vector;

extern std::vector<sycl::device*> devices;

namespace {

template <typename fp>
int test(device* dev, oneapi::mkl::layout layout, oneapi::mkl::side left_right,
         oneapi::mkl::uplo upper_lower, int m, int n, int lda, int ldb, int ldc, fp alpha,
         fp beta) {
    // Prepare data.
    vector<fp, allocator_helper<fp, 64>> A, B, C, C_ref;
    if (left_right == oneapi::mkl::side::left)
        rand_matrix(A, layout, oneapi::mkl::transpose::nontrans, m, m, lda);
    else
        rand_matrix(A, layout, oneapi::mkl::transpose::nontrans, n, n, lda);
    rand_matrix(B, layout, oneapi::mkl::transpose::nontrans, m, n, ldb);
    rand_matrix(C, layout, oneapi::mkl::transpose::nontrans, m, n, ldc);
    C_ref = C;

    // Call Reference SYMM.
    const int m_ref = m, n_ref = n;
    const int lda_ref = lda, ldb_ref = ldb, ldc_ref = ldc;

    using fp_ref = typename ref_type_info<fp>::type;

    ::symm(convert_to_cblas_layout(layout), convert_to_cblas_side(left_right),
           convert_to_cblas_uplo(upper_lower), &m_ref, &n_ref, (fp_ref*)&alpha, (fp_ref*)A.data(),
           &lda_ref, (fp_ref*)B.data(), &ldb_ref, (fp_ref*)&beta, (fp_ref*)C_ref.data(), &ldc_ref);

    // Call DPC++ SYMM.

    // Catch asynchronous exceptions.
    auto exception_handler = [](exception_list exceptions) {
        for (std::exception_ptr const& e : exceptions) {
            try {
                std::rethrow_exception(e);
            }
            catch (exception const& e) {
                std::cout << "Caught asynchronous SYCL exception during SYMM:\n"
                          << e.what() << std::endl;
                print_error_code(e);
            }
        }
    };

    queue main_queue(*dev, exception_handler);

    buffer<fp, 1> A_buffer(A.data(), range<1>(A.size()));
    buffer<fp, 1> B_buffer(B.data(), range<1>(B.size()));
    buffer<fp, 1> C_buffer(C.data(), range<1>(C.size()));

    try {
#ifdef CALL_RT_API
        switch (layout) {
            case oneapi::mkl::layout::column_major:
                oneapi::mkl::blas::column_major::symm(main_queue, left_right, upper_lower, m, n,
                                                      alpha, A_buffer, lda, B_buffer, ldb, beta,
                                                      C_buffer, ldc);
                break;
            case oneapi::mkl::layout::row_major:
                oneapi::mkl::blas::row_major::symm(main_queue, left_right, upper_lower, m, n, alpha,
                                                   A_buffer, lda, B_buffer, ldb, beta, C_buffer,
                                                   ldc);
                break;
            default: break;
        }
#else
        switch (layout) {
            case oneapi::mkl::layout::column_major:
                TEST_RUN_CT_SELECT(main_queue, oneapi::mkl::blas::column_major::symm, left_right,
                                   upper_lower, m, n, alpha, A_buffer, lda, B_buffer, ldb, beta,
                                   C_buffer, ldc);
                break;
            case oneapi::mkl::layout::row_major:
                TEST_RUN_CT_SELECT(main_queue, oneapi::mkl::blas::row_major::symm, left_right,
                                   upper_lower, m, n, alpha, A_buffer, lda, B_buffer, ldb, beta,
                                   C_buffer, ldc);
                break;
            default: break;
        }
#endif
    }
    catch (exception const& e) {
        std::cout << "Caught synchronous SYCL exception during SYMM:\n" << e.what() << std::endl;
        print_error_code(e);
    }

    catch (const oneapi::mkl::unimplemented& e) {
        return test_skipped;
    }

    catch (const std::runtime_error& error) {
        std::cout << "Error raised during execution of SYMM:\n" << error.what() << std::endl;
    }

    // Compare the results of reference implementation and DPC++ implementation.
    auto C_accessor = C_buffer.template get_access<access::mode::read>();
    bool good =
        check_equal_matrix(C_accessor, C_ref, layout, m, n, ldc, 10 * std::max(m, n), std::cout);

    return (int)good;
}

class SymmTests : public ::testing::TestWithParam<std::tuple<sycl::device*, oneapi::mkl::layout>> {
};

TEST_P(SymmTests, RealSinglePrecision) {
    float alpha(2.0);
    float beta(3.0);
    EXPECT_TRUEORSKIP(test<float>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                  oneapi::mkl::side::left, oneapi::mkl::uplo::lower, 72, 27, 101,
                                  102, 103, alpha, beta));
    EXPECT_TRUEORSKIP(test<float>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                  oneapi::mkl::side::right, oneapi::mkl::uplo::lower, 72, 27, 101,
                                  102, 103, alpha, beta));
    EXPECT_TRUEORSKIP(test<float>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                  oneapi::mkl::side::left, oneapi::mkl::uplo::upper, 72, 27, 101,
                                  102, 103, alpha, beta));
    EXPECT_TRUEORSKIP(test<float>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                  oneapi::mkl::side::right, oneapi::mkl::uplo::upper, 72, 27, 101,
                                  102, 103, alpha, beta));
}
TEST_P(SymmTests, RealDoublePrecision) {
    double alpha(2.0);
    double beta(3.0);
    EXPECT_TRUEORSKIP(test<double>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                   oneapi::mkl::side::left, oneapi::mkl::uplo::lower, 72, 27, 101,
                                   102, 103, alpha, beta));
    EXPECT_TRUEORSKIP(test<double>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                   oneapi::mkl::side::right, oneapi::mkl::uplo::lower, 72, 27, 101,
                                   102, 103, alpha, beta));
    EXPECT_TRUEORSKIP(test<double>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                   oneapi::mkl::side::left, oneapi::mkl::uplo::upper, 72, 27, 101,
                                   102, 103, alpha, beta));
    EXPECT_TRUEORSKIP(test<double>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                   oneapi::mkl::side::right, oneapi::mkl::uplo::upper, 72, 27, 101,
                                   102, 103, alpha, beta));
}
TEST_P(SymmTests, ComplexSinglePrecision) {
    std::complex<float> alpha(2.0, -0.5);
    std::complex<float> beta(3.0, -1.5);
    EXPECT_TRUEORSKIP(test<std::complex<float>>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                                oneapi::mkl::side::left, oneapi::mkl::uplo::lower,
                                                72, 27, 101, 102, 103, alpha, beta));
    EXPECT_TRUEORSKIP(test<std::complex<float>>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                                oneapi::mkl::side::right, oneapi::mkl::uplo::lower,
                                                72, 27, 101, 102, 103, alpha, beta));
    EXPECT_TRUEORSKIP(test<std::complex<float>>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                                oneapi::mkl::side::left, oneapi::mkl::uplo::upper,
                                                72, 27, 101, 102, 103, alpha, beta));
    EXPECT_TRUEORSKIP(test<std::complex<float>>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                                oneapi::mkl::side::right, oneapi::mkl::uplo::upper,
                                                72, 27, 101, 102, 103, alpha, beta));
}
TEST_P(SymmTests, ComplexDoublePrecision) {
    std::complex<double> alpha(2.0, -0.5);
    std::complex<double> beta(3.0, -1.5);
    EXPECT_TRUEORSKIP(test<std::complex<double>>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                                 oneapi::mkl::side::left, oneapi::mkl::uplo::lower,
                                                 72, 27, 101, 102, 103, alpha, beta));
    EXPECT_TRUEORSKIP(test<std::complex<double>>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                                 oneapi::mkl::side::right, oneapi::mkl::uplo::lower,
                                                 72, 27, 101, 102, 103, alpha, beta));
    EXPECT_TRUEORSKIP(test<std::complex<double>>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                                 oneapi::mkl::side::left, oneapi::mkl::uplo::upper,
                                                 72, 27, 101, 102, 103, alpha, beta));
    EXPECT_TRUEORSKIP(test<std::complex<double>>(std::get<0>(GetParam()), std::get<1>(GetParam()),
                                                 oneapi::mkl::side::right, oneapi::mkl::uplo::upper,
                                                 72, 27, 101, 102, 103, alpha, beta));
}

INSTANTIATE_TEST_SUITE_P(SymmTestSuite, SymmTests,
                         ::testing::Combine(testing::ValuesIn(devices),
                                            testing::Values(oneapi::mkl::layout::column_major,
                                                            oneapi::mkl::layout::row_major)),
                         ::LayoutDeviceNamePrint());

} // anonymous namespace
