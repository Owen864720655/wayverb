#include "common/cl_common.h"
#include "common/string_builder.h"

#include "glog/logging.h"

namespace {

void print_device_info(const cl::Device& i) {
    LOG(INFO) << i.getInfo<CL_DEVICE_NAME>();
    LOG(INFO) << "available: " << i.getInfo<CL_DEVICE_AVAILABLE>();
}

cl::Context get_context() {
    std::vector<cl::Platform> platform;
    cl::Platform::get(&platform);

    cl_context_properties cps[] = {
            CL_CONTEXT_PLATFORM,
            reinterpret_cast<cl_context_properties>((platform.front())()),
            0,
    };

    return cl::Context(CL_DEVICE_TYPE_GPU, cps);
    // return cl::Context(CL_DEVICE_TYPE_CPU, cps);
}

cl::Device get_device(const cl::Context& context) {
    auto devices = context.getInfo<CL_CONTEXT_DEVICES>();

    LOG(INFO) << "## all devices:";

    for (auto& i : devices) {
        print_device_info(i);
    }

    auto device = devices.back();
    // auto device = devices.front();

    auto available = device.getInfo<CL_DEVICE_AVAILABLE>();
    CHECK(available) << "device must be available";

    auto preferred_double_width =
            device.getInfo<CL_DEVICE_PREFERRED_VECTOR_WIDTH_DOUBLE>();
    CHECK(preferred_double_width) << "device must support double precision";

    LOG(INFO) << "## used device:";
    print_device_info(device);

    return device;
}

}  // namespace

compute_context::compute_context()
        : context(::get_context())
        , device(::get_device(context)) {
}

cl::Context compute_context::get_context() const {
    return context;
}

cl::Device compute_context::get_device() const {
    return device;
}
