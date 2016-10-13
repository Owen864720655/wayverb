#pragma once

#include "program.h"

#include "raytracer/cl/structs.h"

#include "common/cl/common.h"
#include "common/conversions.h"
#include "common/spatial_division/scene_buffers.h"

#include "utilities/aligned/vector.h"

namespace raytracer {
namespace diffuse {

class finder final {
public:
    finder(const compute_context& cc,
           const glm::vec3& source,
           const glm::vec3& receiver,
           size_t rays)
            : cc_{cc}
            , queue_{cc.context, cc.device}
            , kernel_{program{cc}.get_kernel()}
            , receiver_{to_cl_float3(receiver)}
            , rays_{rays}
            , reflections_buffer_{cc.context,
                                  CL_MEM_READ_WRITE,
                                  sizeof(reflection) * rays}
            , diffuse_path_buffer_{load_to_buffer(
                      cc.context,
                      aligned::vector<diffuse_path_info>(
                              rays,
                              diffuse_path_info{make_volume_type(1.0 / rays),
                                                to_cl_float3(source),
                                                0}),
                      false)}
            , impulse_buffer_{cc.context,
                              CL_MEM_READ_WRITE,
                              sizeof(impulse<8>) * rays} {}

    template <typename It>
    void push(It b, It e, const scene_buffers& scene_buffers) {
        //  copy the current batch of reflections to the device
        cl::copy(queue_, b, e, reflections_buffer_);

        //  get the kernel and run it
        kernel_(cl::EnqueueArgs(queue_, cl::NDRange(rays_)),
                reflections_buffer_,
                receiver_,
                scene_buffers.get_triangles_buffer(),
                scene_buffers.get_vertices_buffer(),
                scene_buffers.get_surfaces_buffer(),
                diffuse_path_buffer_,
                impulse_buffer_);

        //  copy impulses out
        const auto ret{read_from_buffer<impulse<8>>(queue_, impulse_buffer_)};
        for (const auto& i : ret) {
            if (i.distance) {
                results_.emplace_back(i);
            }
        }
    }

    const auto& get_data() const { return results_; }
    auto& get_data() { return results_; }

private:
    using kernel_t = decltype(std::declval<program>().get_kernel());

    compute_context cc_;
    cl::CommandQueue queue_;
    kernel_t kernel_;
    cl_float3 receiver_;
    size_t rays_;

    cl::Buffer reflections_buffer_;
    cl::Buffer diffuse_path_buffer_;
    cl::Buffer impulse_buffer_;

    aligned::vector<impulse<8>> results_;
};

}  // namespace diffuse
}  // namespace raytracer
