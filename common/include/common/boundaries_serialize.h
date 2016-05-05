#pragma once

#include "boundaries.h"
#include "vec_serialize.h"

template <typename Archive>
void serialize(Archive& archive, Box& m) {
    archive(cereal::make_nvp("c0", m.c0), cereal::make_nvp("c1", m.c1));
}
JSON_OSTREAM_OVERLOAD(Box);

template <typename Archive>
void serialize(Archive& archive, CuboidBoundary& m) {
    archive(cereal::make_nvp("box", cereal::base_class<Box>(&m)));
}
JSON_OSTREAM_OVERLOAD(CuboidBoundary);
