#include "common/filters_common.h"

#include "common/serialize/json_read_write.h"

#include "cereal/types/vector.hpp"

#include "gtest/gtest.h"

template <typename T>
JSON_OSTREAM_OVERLOAD(aligned::vector<T>);

TEST(convolution, convolution) {
    aligned::vector<float> a = {1, 0, 0, 0, 0};
    aligned::vector<float> b = {1, 2, 3, 4, 3, 2, 1, 0, 0};

    FastConvolver fc(a.size() + b.size() - 1);
    auto convolved = fc.convolve(a, b);

    LOG(INFO) << convolved;
}
