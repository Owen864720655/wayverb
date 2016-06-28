#pragma once

#include "OtherComponents/BasicDrawableObject.hpp"

#include "common/scene_data.h"

class ModelObject final : public BasicDrawableObject {
public:
    ModelObject(mglu::GenericShader& shader, const SceneData& scene_data);

private:
    std::vector<GLuint> get_indices(const SceneData& scene_data) const;
};
