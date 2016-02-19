#include "waveguide.h"
#include "test_flag.h"
#include "conversions.h"

#include <iostream>
#include <algorithm>
#include <cstdlib>

//----------------------------------------------------------------------------//

void TetrahedralWaveguide::setup(cl::CommandQueue& queue,
                                 size_type o,
                                 float sr) {
    auto PORTS = decltype(mesh)::PORTS;
    transform_matrix = get_transform_matrix(PORTS, o, mesh.get_nodes());
    cl::copy(queue,
             transform_matrix.data(),
             transform_matrix.data() + PORTS * 3,
             transform_buffer);

    std::vector<cl_float3> starting_velocity(1, {{0, 0, 0, 0}});
    cl::copy(queue,
             starting_velocity.begin(),
             starting_velocity.end(),
             velocity_buffer);

    period = 1 / sr;
}

TetrahedralWaveguide::TetrahedralWaveguide(const TetrahedralProgram& program,
                                           cl::CommandQueue& queue,
                                           const TetrahedralMesh& mesh)
        : Waveguide<TetrahedralProgram>(program, queue, mesh.get_nodes().size())
        , mesh(mesh)
        //    TODO this seems like it's asking for problems
        , node_buffer(
              program.getInfo<CL_PROGRAM_CONTEXT>(),
              const_cast<TetrahedralMesh::Node*>(this->mesh.get_nodes().data()),
              const_cast<TetrahedralMesh::Node*>(
                  this->mesh.get_nodes().data()) +
                  this->mesh.get_nodes().size(),
              true)
        , transform_buffer(program.getInfo<CL_PROGRAM_CONTEXT>(),
                           CL_MEM_READ_WRITE,
                           sizeof(cl_float) * 12)
        , velocity_buffer(program.getInfo<CL_PROGRAM_CONTEXT>(),
                          CL_MEM_READ_WRITE,
                          sizeof(cl_float3) * 1) {
}

TetrahedralWaveguide::TetrahedralWaveguide(const TetrahedralProgram& program,
                                           cl::CommandQueue& queue,
                                           const Boundary& boundary,
                                           float spacing,
                                           const Vec3f& anchor)
        : TetrahedralWaveguide(
              program, queue, TetrahedralMesh(boundary, spacing, anchor)) {
}

RunStepResult TetrahedralWaveguide::run_step(size_type o,
                                             cl::CommandQueue& queue,
                                             kernel_type& kernel,
                                             size_type nodes,
                                             cl::Buffer& previous,
                                             cl::Buffer& current,
                                             cl::Buffer& output) {
    std::vector<cl_float> out(1);
    std::vector<cl_float3> current_velocity(1);

    kernel(cl::EnqueueArgs(queue, cl::NDRange(nodes)),
           current,
           previous,
           node_buffer,
           transform_buffer,
           velocity_buffer,
           mesh.get_spacing(),
           period,
           o,
           output);

    cl::copy(queue, output, out.begin(), out.end());
    cl::copy(queue,
             velocity_buffer,
             current_velocity.begin(),
             current_velocity.end());

    auto velocity = to_vec3f(current_velocity.front());
    auto intensity = velocity * out.front();

    return RunStepResult(out.front(), intensity);
}

TetrahedralWaveguide::size_type TetrahedralWaveguide::get_index_for_coordinate(
    const Vec3f& v) const {
    return mesh.compute_index(mesh.compute_locator(v));
}

Vec3f TetrahedralWaveguide::get_coordinate_for_index(size_type index) const {
    return to_vec3f(mesh.get_nodes()[index].position);
}

const TetrahedralMesh& TetrahedralWaveguide::get_mesh() const {
    return mesh;
}

bool TetrahedralWaveguide::inside(size_type index) const {
    return mesh.get_nodes()[index].inside;
}

//----------------------------------------------------------------------------//

RectangularWaveguide::RectangularWaveguide(const RectangularProgram& program,
                                           cl::CommandQueue& queue,
                                           const RectangularMesh& mesh)
        : RectangularWaveguide(
              program, queue, mesh, mesh.get_condensed_nodes()) {
}

RectangularWaveguide::RectangularWaveguide(
    const RectangularProgram& program,
    cl::CommandQueue& queue,
    const RectangularMesh& mesh,
    std::vector<RectangularMesh::CondensedNode> nodes)
        : Waveguide<RectangularProgram>(program, queue, mesh.get_nodes().size())
        , mesh(mesh)
        //    TODO this seems like it's asking for problems
        , node_buffer(program.getInfo<CL_PROGRAM_CONTEXT>(),
                      nodes.begin(),
                      nodes.end(),
                      false)
        , transform_buffer(program.getInfo<CL_PROGRAM_CONTEXT>(),
                           CL_MEM_READ_WRITE,
                           sizeof(cl_float) * 18)
        , velocity_buffer(program.getInfo<CL_PROGRAM_CONTEXT>(),
                          CL_MEM_READ_WRITE,
                          sizeof(cl_float3) * 1)
        , boundary_data_1_buffer(
              program.getInfo<CL_PROGRAM_CONTEXT>(),
              CL_MEM_READ_WRITE,
              sizeof(RectangularProgram::BoundaryDataArray1) *
                  mesh.compute_num_boundary<1>())
        , boundary_data_2_buffer(
              program.getInfo<CL_PROGRAM_CONTEXT>(),
              CL_MEM_READ_WRITE,
              sizeof(RectangularProgram::BoundaryDataArray2) *
                  mesh.compute_num_boundary<2>())
        , boundary_data_3_buffer(
              program.getInfo<CL_PROGRAM_CONTEXT>(),
              CL_MEM_READ_WRITE,
              sizeof(RectangularProgram::BoundaryDataArray3) *
                  mesh.compute_num_boundary<3>())
        , boundary_coefficients_buffer(
              program.getInfo<CL_PROGRAM_CONTEXT>(),
              CL_MEM_READ_WRITE,
              sizeof(RectangularProgram::CanonicalCoefficients) *
                  mesh.compute_num_surface()) {
}

RectangularWaveguide::RectangularWaveguide(const RectangularProgram& program,
                                           cl::CommandQueue& queue,
                                           const Boundary& boundary,
                                           float spacing,
                                           const Vec3f& anchor)
        : RectangularWaveguide(
              program, queue, RectangularMesh(boundary, spacing, anchor)) {
}

void RectangularWaveguide::setup(cl::CommandQueue& queue,
                                 size_type o,
                                 float sr) {
    auto PORTS = decltype(mesh)::PORTS;
    transform_matrix = get_transform_matrix(PORTS, o, mesh.get_nodes());
    cl::copy(queue,
             transform_matrix.data(),
             transform_matrix.data() + PORTS * 3,
             transform_buffer);

    std::vector<cl_float3> starting_velocity(1, {{0, 0, 0, 0}});
    cl::copy(queue,
             starting_velocity.begin(),
             starting_velocity.end(),
             velocity_buffer);

    //  TODO set boundary data structures properly
    setup_boundary_data_buffer<1>(queue, boundary_data_1_buffer);
    setup_boundary_data_buffer<2>(queue, boundary_data_2_buffer);
    setup_boundary_data_buffer<3>(queue, boundary_data_3_buffer);

    //  TODO set coefficients properly
    std::vector<RectangularProgram::CanonicalCoefficients> vec(
        1, RectangularProgram::CanonicalCoefficients{});
    cl::copy(queue, vec.begin(), vec.end(), boundary_coefficients_buffer);

    period = 1 / sr;
}

RunStepResult RectangularWaveguide::run_step(size_type o,
                                             cl::CommandQueue& queue,
                                             kernel_type& kernel,
                                             size_type nodes,
                                             cl::Buffer& previous,
                                             cl::Buffer& current,
                                             cl::Buffer& output) {
    std::vector<cl_float> out(1);
    std::vector<cl_float3> current_velocity(1);

    kernel(cl::EnqueueArgs(queue, cl::NDRange(nodes)),
           current,
           previous,
           node_buffer,
           to_cl_int3(get_mesh().get_dim()),
           boundary_data_1_buffer,
           boundary_data_2_buffer,
           boundary_data_3_buffer,
           boundary_coefficients_buffer,
           transform_buffer,
           velocity_buffer,
           mesh.get_spacing(),
           period,
           o,
           output);

    cl::copy(queue, output, out.begin(), out.end());
    cl::copy(queue,
             velocity_buffer,
             current_velocity.begin(),
             current_velocity.end());

    auto velocity = to_vec3f(current_velocity.front());
    auto intensity = velocity * out.front();

    return RunStepResult(out.front(), intensity);
}

RectangularWaveguide::size_type RectangularWaveguide::get_index_for_coordinate(
    const Vec3f& v) const {
    return mesh.compute_index(mesh.compute_locator(v));
}

Vec3f RectangularWaveguide::get_coordinate_for_index(size_type index) const {
    return to_vec3f(mesh.get_nodes()[index].position);
}

const RectangularMesh& RectangularWaveguide::get_mesh() const {
    return mesh;
}

bool RectangularWaveguide::inside(size_type index) const {
    return mesh.get_nodes()[index].inside;
}
