#pragma once

#include "common/aligned/vector.h"
#include "common/orientable.h"

#include "modern_gl_utils/buffer_object.h"
#include "modern_gl_utils/drawable.h"
#include "modern_gl_utils/fbo.h"
#include "modern_gl_utils/generic_shader.h"
#include "modern_gl_utils/render_buffer.h"
#include "modern_gl_utils/screen_quad.h"
#include "modern_gl_utils/texture_object.h"
#include "modern_gl_utils/updatable.h"
#include "modern_gl_utils/vao.h"

class Node : public Orientable {
public:
    Node() = default;

    Node(Node&&) noexcept;
    Node& operator=(Node&&) noexcept;

    glm::vec3 get_position() const;
    void set_position(const glm::vec3& v);

    glm::vec3 get_scale() const;
    void set_scale(float s);
    void set_scale(const glm::vec3& s);

    glm::mat4 get_matrix() const;

private:
    glm::vec3 position{0};
    glm::vec3 scale{1};
};

class BasicDrawableObject : public mglu::Drawable, public Node {
public:
    BasicDrawableObject(mglu::ShaderProgram& shader,
                        const aligned::vector<glm::vec3>& g,
                        const aligned::vector<glm::vec4>& c,
                        const aligned::vector<GLuint>& i,
                        GLuint mode);

    BasicDrawableObject(BasicDrawableObject&&) noexcept;
    BasicDrawableObject& operator=(BasicDrawableObject&&) noexcept;

    GLuint get_mode() const;
    void set_mode(GLuint mode);

    void set_highlight(float amount);

private:
    void do_draw(const glm::mat4& modelview_matrix) const override;
    glm::mat4 get_local_modelview_matrix() const override;

    mglu::ShaderProgram* shader;

    aligned::vector<glm::vec4> color_vector;

    mglu::VAO vao;
    mglu::StaticVBO geometry;
    mglu::StaticVBO colors;
    mglu::StaticIBO ibo;

    GLuint mode{GL_LINES};
};
