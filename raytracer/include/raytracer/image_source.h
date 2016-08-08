#pragma once

#include "raytracer/cl_structs.h"
#include "raytracer/iterative_builder.h"

#include "common/aligned/set.h"
#include "common/aligned/vector.h"
#include "common/cl_include.h"
#include "common/voxel_collection.h"

#include <experimental/optional>

namespace raytracer {

class image_source_finder final {
public:
    image_source_finder(size_t rays, size_t depth);

    void push(const aligned::vector<reflection>&);
    aligned::vector<impulse> get_results(const glm::vec3& source,
                                         const glm::vec3& receiver,
                                         const copyable_scene_data& scene_data,
                                         const voxel_collection<3>& vox);

private:
    struct item final {
        cl_ulong index;
        bool visible;
    };

    iterative_builder<item> reflection_path_builder;
};

}  // namespace raytracer
