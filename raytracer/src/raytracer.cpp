#include "raytracer/raytracer.h"

#include "common/azimuth_elevation.h"
#include "common/boundaries.h"
#include "common/conversions.h"
#include "common/geometric.h"
#include "common/hrtf.h"
#include "common/progress.h"
#include "common/triangle.h"
#include "common/voxel_collection.h"

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <random>
#include <sstream>
#include <streambuf>

namespace {
constexpr auto volume_factor = 0.001f;
constexpr const VolumeType AIR_COEFFICIENT{{
        volume_factor * -0.1f,
        volume_factor * -0.2f,
        volume_factor * -0.5f,
        volume_factor * -1.1f,
        volume_factor * -2.7f,
        volume_factor * -9.4f,
        volume_factor * -29.0f,
        volume_factor * -60.0f,
}};
}  // namespace

namespace raytracer {

int compute_optimum_reflection_number(float min_amp, float max_reflectivity) {
    return std::log(min_amp) / std::log(max_reflectivity);
}

std::vector<std::vector<std::vector<float>>> flatten_impulses(
        const std::vector<std::vector<AttenuatedImpulse>>& attenuated,
        float samplerate) {
    std::vector<std::vector<std::vector<float>>> flattened(attenuated.size());
    proc::transform(attenuated, begin(flattened), [samplerate](const auto& i) {
        return flatten_impulses(i, samplerate);
    });
    return flattened;
}

double pressure_to_intensity(double pressure, double Z = 400) {
    return pressure * pressure / Z;
}

double intensity_to_pressure(double intensity, double Z = 400) {
    return std::sqrt(intensity * Z);
}

/// Turn a collection of AttenuatedImpulses into a vector of 8 vectors, where
/// each of the 8 vectors represent sample values in a different frequency band.
std::vector<std::vector<float>> flatten_impulses(
        const std::vector<AttenuatedImpulse>& impulse, float samplerate) {
    const auto MAX_TIME_LIMIT = 20.0f;
    // Find the index of the final sample based on time and samplerate
    float maxtime = 0;
    for (const auto& i : impulse)
        maxtime = std::max(maxtime, i.time);
    maxtime = std::min(maxtime, MAX_TIME_LIMIT);
    const auto MAX_SAMPLE = round(maxtime * samplerate) + 1;

    //  Create somewhere to store the results.
    std::vector<std::vector<float>> flattened(
            sizeof(VolumeType) / sizeof(float),
            std::vector<float>(MAX_SAMPLE, 0));

    //  For each impulse, calculate its index, then add the impulse's volumes
    //  to the volumes already in the output array.
    for (const auto& i : impulse) {
        const auto SAMPLE = round(i.time * samplerate);
        if (SAMPLE < MAX_SAMPLE) {
            for (auto j = 0u; j != flattened.size(); ++j) {
                flattened[j][SAMPLE] += i.volume.s[j];
            }
        }
    }

    //  impulses are intensity levels, now we need to convert to pressure
    proc::for_each(flattened, [](auto& i) {
        proc::for_each(i, [](auto& j) {
            j = std::copysign(intensity_to_pressure(std::abs(j)), j);
        });
    });

    return flattened;
}

/// Find the index of the last sample with an amplitude of minVol or higher,
/// then resize the vectors down to this length.
void trimTail(std::vector<std::vector<float>>& audioChannels, float minVol) {
    using index_type = std::common_type_t<
            std::iterator_traits<
                    std::vector<float>::reverse_iterator>::difference_type,
            int>;

    // Find last index of required amplitude or greater.
    auto len = proc::accumulate(
            audioChannels, 0, [minVol](auto current, const auto& i) {
                return std::max(
                        index_type{current},
                        index_type{
                                distance(i.begin(),
                                         std::find_if(i.rbegin(),
                                                      i.rend(),
                                                      [minVol](auto j) {
                                                          return std::abs(j) >=
                                                                 minVol;
                                                      })
                                                 .base()) -
                                1});
            });

    // Resize.
    for (auto&& i : audioChannels)
        i.resize(len);
}

/// Collects together all the post-processing steps.
std::vector<std::vector<float>> process(
        std::vector<std::vector<std::vector<float>>>& data,
        float sr,
        bool do_normalize,
        float lo_cutoff,
        bool do_trim_tail,
        float volume_scale) {
    std::vector<std::vector<float>> ret;
    ret.reserve(data.size());
    for (const auto& i : data) {
        ret.push_back(multiband_filter_and_mixdown(i, sr));
    }

    if (do_normalize)
        normalize(ret);

    if (volume_scale != 1)
        mul(ret, volume_scale);

    if (do_trim_tail)
        trimTail(ret, 0.00001);

    return ret;
}

/// Call binary operation u on pairs of elements from a and b, where a and b are
/// cl_floatx types.
template <typename T, typename U>
inline T elementwise(const T& a, const T& b, const U& u) {
    T ret;
    proc::transform(a.s, std::begin(b.s), std::begin(ret.s), u);
    return ret;
}

std::vector<cl_float3> get_random_directions(int num) {
    std::vector<cl_float3> ret(num);
    std::uniform_real_distribution<float> z(-1, 1);
    std::uniform_real_distribution<float> theta(-M_PI, M_PI);
    auto seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::default_random_engine engine(seed);

    for (auto& i : ret)
        i = to_cl_float3(sphere_point(z(engine), theta(engine)));

    return ret;
}

RaytracerResults Results::get_diffuse() const {
    return RaytracerResults(diffuse, mic, source, rays, reflections);
}

RaytracerResults Results::get_image_source(bool remove_direct) const {
    auto temp = image_source;
    if (remove_direct)
        temp.erase(std::vector<int>{0});

    std::vector<Impulse> ret(temp.size());
    proc::transform(temp, begin(ret), [](const auto& i) { return i.second; });
    return RaytracerResults(ret, mic, source, rays, reflections);
}

RaytracerResults Results::get_all(bool remove_direct) const {
    auto diffuse = get_diffuse().impulses;
    const auto image = get_image_source(remove_direct).impulses;
    diffuse.insert(diffuse.end(), image.begin(), image.end());
    return RaytracerResults(diffuse, mic, source, rays, reflections);
}

//----------------------------------------------------------------------------//

void remove_duplicates(const std::vector<cl_ulong>& path,
                       const std::vector<Impulse>& image,
                       const int RAY_GROUP_SIZE,
                       Results& results) {
    for (auto j = 0; j != RAY_GROUP_SIZE * NUM_IMAGE_SOURCE;
         j += NUM_IMAGE_SOURCE) {
        for (auto k = 1; k != NUM_IMAGE_SOURCE + 1; ++k) {
            std::vector<int> surfaces(path.begin() + j, path.begin() + j + k);

            if (k == 1 || surfaces.back() != 0) {
                auto it = results.image_source.find(surfaces);
                if (it == results.image_source.end()) {
                    results.image_source[surfaces] = image[j + k - 1];
                }
            }
        }
    }
}

Raytracer::Raytracer(const RaytracerProgram& program)
        : queue(program.get_info<CL_PROGRAM_CONTEXT>(), program.get_device())
        , context(program.get_info<CL_PROGRAM_CONTEXT>())
        , kernel(program.get_improved_raytrace_kernel()) {
}

template <typename T>
auto transpose(const T& t, int x, int y) {
    assert(t.size() == x * y);

    auto c = t;
    for (auto i = 0u; i != x; ++i) {
        for (auto j = 0u; j != y; ++j) {
            c[j + i * y] = t[i + j * x];
        }
    }
    return c;
}

VolumeType air_attenuation_for_distance(float distance) {
    VolumeType ret;
    proc::transform(AIR_COEFFICIENT.s, std::begin(ret.s), [distance](auto i) {
        return pow(M_E, distance * i);
    });
    return ret;
}

float power_attenuation_for_distance(float distance) {
    return 1 / (4 * M_PI * distance * distance);
}

VolumeType attenuation_for_distance(float distance) {
    auto ret = air_attenuation_for_distance(distance);
    auto power = power_attenuation_for_distance(distance);
    proc::for_each(ret.s, [power](auto& i) { i *= power; });
    return ret;
}

void add_direct_impulse(const glm::vec3& micpos,
                        const glm::vec3& source,
                        const CopyableSceneData& scene_data,
                        Results& results) {
    if (geo::point_intersection(micpos,
                                source,
                                scene_data.get_triangles(),
                                scene_data.get_converted_vertices())) {
        auto init_diff = source - micpos;
        auto init_dist = glm::length(init_diff);
        results.image_source[{0}] = Impulse{
                attenuation_for_distance(init_dist),
                to_cl_float3(micpos + init_diff),
                static_cast<cl_float>(init_dist / SPEED_OF_SOUND),
        };
    }
}

Results Raytracer::run(const CopyableSceneData& scene_data,
                       const glm::vec3& micpos,
                       const glm::vec3& source,
                       int rays,
                       int reflections,
                       std::atomic_bool& keep_going,
                       const std::function<void()>& callback) {
    return run(scene_data,
               micpos,
               source,
               get_random_directions(rays),
               reflections,
               keep_going,
               callback);
}

Results Raytracer::run(const CopyableSceneData& scene_data,
                       const glm::vec3& micpos,
                       const glm::vec3& source,
                       const std::vector<cl_float3>& directions,
                       int reflections,
                       std::atomic_bool& keep_going,
                       const std::function<void()>& callback) {
    VoxelCollection vox(scene_data, 4, 0.1);
    auto flattened_vox = vox.get_flattened();

    auto rays = directions.size();
    cl::Buffer cl_ray_info(context, CL_MEM_READ_WRITE, rays * sizeof(RayInfo));

    std::vector<RayInfo> ray_info(directions.size());
    proc::transform(
            directions, ray_info.begin(), [micpos, source](const auto& i) {
                RayInfo ret;
                ret.ray.direction = i;
                ret.ray.position = to_cl_float3(source);
                ret.distance = 0;
                ret.volume = VolumeType{{1, 1, 1, 1, 1, 1, 1, 1}};
                ret.cont = 1;

                ret.mic_reflection = to_cl_float3(micpos);
                return ret;
            });

    cl::copy(queue, begin(ray_info), end(ray_info), cl_ray_info);

    cl::Buffer cl_triangles(
            context,
            begin(const_cast<std::vector<Triangle>&>(
                    scene_data.get_triangles())),
            end(const_cast<std::vector<Triangle>&>(scene_data.get_triangles())),
            false);
    cl::Buffer cl_vertices(
            context,
            begin(const_cast<std::vector<cl_float3>&>(
                    scene_data.get_vertices())),
            end(const_cast<std::vector<cl_float3>&>(scene_data.get_vertices())),
            false);
    auto surfaces = scene_data.get_surfaces();
    cl::Buffer cl_surfaces(context, surfaces.begin(), surfaces.end(), false);
    cl::Buffer cl_impulses(context, CL_MEM_READ_WRITE, rays * sizeof(Impulse));
    cl::Buffer cl_image_source(
            context, CL_MEM_READ_WRITE, rays * sizeof(Impulse));
    cl::Buffer cl_image_source_index(
            context, CL_MEM_READ_WRITE, rays * sizeof(cl_ulong));

    cl::Buffer cl_voxel_index(
            context, begin(flattened_vox), end(flattened_vox), false);

    Results results;
    results.mic = micpos;
    results.source = source;
    results.rays = directions.size();
    results.reflections = reflections;
    results.diffuse.resize(rays * reflections);

    add_direct_impulse(micpos, source, scene_data, results);

    std::vector<cl_ulong> image_source_index(rays * NUM_IMAGE_SOURCE, 0);
    std::vector<Impulse> image_source(
            rays * NUM_IMAGE_SOURCE,
            Impulse{{{0, 0, 0, 0, 0, 0, 0, 0}}, {{0, 0, 0}}, 0});

    AABB global_aabb{to_cl_float3(vox.get_aabb().get_c0()),
                     to_cl_float3(vox.get_aabb().get_c1())};

    for (auto i = 0u; i != reflections; ++i) {
        if (!keep_going) {
            throw std::runtime_error("flag state false, stopping");
        }

        auto b = (i + 0) * rays;
        auto e = (i + 1) * rays;

        if (i < NUM_IMAGE_SOURCE) {
            cl::copy(queue,
                     image_source_index.begin() + b,
                     image_source_index.begin() + e,
                     cl_image_source_index);
            cl::copy(queue,
                     image_source.begin() + b,
                     image_source.begin() + e,
                     cl_image_source);
        }

        kernel(cl::EnqueueArgs(queue, cl::NDRange(rays)),
               cl_ray_info,
               cl_voxel_index,
               global_aabb,
               vox.get_side(),
               cl_triangles,
               scene_data.get_triangles().size(),
               cl_vertices,
               cl_surfaces,
               to_cl_float3(source),
               to_cl_float3(micpos),
               cl_impulses,
               cl_image_source,
               cl_image_source_index,
               AIR_COEFFICIENT,
               i);

        cl::copy(queue,
                 cl_impulses,
                 results.diffuse.begin() + b,
                 results.diffuse.begin() + e);

        if (i < NUM_IMAGE_SOURCE) {
            cl::copy(queue,
                     cl_image_source_index,
                     image_source_index.begin() + b,
                     image_source_index.begin() + e);
            cl::copy(queue,
                     cl_image_source,
                     image_source.begin() + b,
                     image_source.begin() + e);
        }

        callback();
    }

    results.diffuse = transpose(results.diffuse, rays, reflections);

    image_source = transpose(image_source, rays, NUM_IMAGE_SOURCE);
    image_source_index = transpose(image_source_index, rays, NUM_IMAGE_SOURCE);

    remove_duplicates(image_source_index, image_source, rays, results);

    return results;
}

//----------------------------------------------------------------------------//

HrtfAttenuator::HrtfAttenuator(const RaytracerProgram& program)
        : queue(program.get_info<CL_PROGRAM_CONTEXT>(), program.get_device())
        , kernel(program.get_hrtf_kernel())
        , context(program.get_info<CL_PROGRAM_CONTEXT>())
        , cl_hrtf(context, CL_MEM_READ_WRITE, sizeof(VolumeType) * 360 * 180) {
}

std::vector<AttenuatedImpulse> HrtfAttenuator::process(
        const RaytracerResults& results,
        const glm::vec3& direction,
        const glm::vec3& up,
        const glm::vec3& position,
        HrtfChannel channel) {
    auto channel_index = channel == HrtfChannel::left ? 0 : 1;
    //  muck around with the table format
    std::vector<VolumeType> hrtf_channel_data(360 * 180);
    auto offset = 0;
    for (const auto& i : get_hrtf_data()[channel_index]) {
        proc::copy(i, hrtf_channel_data.begin() + offset);
        offset += i.size();
    }

    //  copy hrtf table to buffer
    cl::copy(queue, begin(hrtf_channel_data), end(hrtf_channel_data), cl_hrtf);

    //  set up buffers
    cl_in = cl::Buffer(context,
                       CL_MEM_READ_WRITE,
                       results.impulses.size() * sizeof(Impulse));
    cl_out = cl::Buffer(context,
                        CL_MEM_READ_WRITE,
                        results.impulses.size() * sizeof(AttenuatedImpulse));

    //  copy input to buffer
    cl::copy(queue, results.impulses.begin(), results.impulses.end(), cl_in);

    //  run kernel
    kernel(cl::EnqueueArgs(queue, cl::NDRange(results.impulses.size())),
           to_cl_float3(position),
           cl_in,
           cl_out,
           cl_hrtf,
           to_cl_float3(direction),
           to_cl_float3(up),
           channel_index);

    //  create output storage
    std::vector<AttenuatedImpulse> ret(results.impulses.size());

    //  copy to output
    cl::copy(queue, cl_out, ret.begin(), ret.end());

    return ret;
}

const std::array<std::array<std::array<cl_float8, 180>, 360>, 2>&
HrtfAttenuator::get_hrtf_data() const {
    return HrtfData::HRTF_DATA;
}

MicrophoneAttenuator::MicrophoneAttenuator(const RaytracerProgram& program)
        : queue(program.get_info<CL_PROGRAM_CONTEXT>(), program.get_device())
        , kernel(program.get_attenuate_kernel())
        , context(program.get_info<CL_PROGRAM_CONTEXT>()) {
}

std::vector<AttenuatedImpulse> MicrophoneAttenuator::process(
        const RaytracerResults& results,
        const glm::vec3& pointing,
        float shape,
        const glm::vec3& position) {
    //  init buffers
    cl_in = cl::Buffer(context,
                       CL_MEM_READ_WRITE,
                       results.impulses.size() * sizeof(Impulse));

    cl_out = cl::Buffer(context,
                        CL_MEM_READ_WRITE,
                        results.impulses.size() * sizeof(AttenuatedImpulse));
    std::vector<AttenuatedImpulse> zero(
            results.impulses.size(),
            AttenuatedImpulse{{{0, 0, 0, 0, 0, 0, 0, 0}}, 0});
    cl::copy(queue, zero.begin(), zero.end(), cl_out);

    //  copy input data to buffer
    cl::copy(queue, results.impulses.begin(), results.impulses.end(), cl_in);

    //  run kernel
    kernel(cl::EnqueueArgs(queue, cl::NDRange(results.impulses.size())),
           to_cl_float3(position),
           cl_in,
           cl_out,
           Speaker{to_cl_float3(pointing), shape});

    //  create output location
    std::vector<AttenuatedImpulse> ret(results.impulses.size());

    //  copy from buffer to output
    cl::copy(queue, cl_out, ret.begin(), ret.end());

    return ret;
}

}  // namespace raytracer
