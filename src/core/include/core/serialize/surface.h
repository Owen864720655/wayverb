#pragma once

#include "core/scene_data_loader.h"

#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"

namespace cereal {

template <typename Archive>
void serialize(Archive& archive, core::bands_type& m) {
    cereal::size_type s = 8;
    archive(cereal::make_size_tag(s));
    if (s != 8) {
        throw std::runtime_error("volume array must be of length 8");
    }
    std::for_each(std::begin(m.s), std::end(m.s), [&archive](auto& i) {
        archive(i);
    });
}

template <typename Archive, size_t Bands>
void serialize(Archive& archive, core::surface<Bands>& m) {
    archive(cereal::make_nvp("absorption", m.absorption),
            cereal::make_nvp("scattering", m.scattering));
}

template <typename Archive>
void serialize(Archive& archive, core::scene_data_loader::material& m) {
    archive(cereal::make_nvp("name", m.name),
            cereal::make_nvp("surface", m.surface));
}

}  // namespace cereal