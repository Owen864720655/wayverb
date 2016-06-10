#include "ImpulseRenderer.hpp"
#include "Orientable.hpp"
#include "Waterfall.hpp"
#include "Waveform.hpp"

#include "common/stl_wrappers.h"

#include "modern_gl_utils/generic_shader.h"

#include "glm/gtx/transform.hpp"

#include <array>
#include <thread>

namespace {
class Playhead : public BasicDrawableObject {
public:
    static const float width;
    Playhead(GenericShader& shader)
            : BasicDrawableObject(
                      shader,
                      std::vector<glm::vec3>{
                              {-width, -1, 0},
                              {width, -1, 0},
                              {-width, -1, -Waterfall::width - 0.2},
                              {width, -1, -Waterfall::width - 0.2},
                              {-width, 1, -Waterfall::width - 0.2},
                              {width, 1, -Waterfall::width - 0.2},
                              {-width, 1, 0},
                              {width, 1, 0}},
                      std::vector<glm::vec4>{{1, 0, 0, 1},
                                             {1, 0, 0, 1},
                                             {1, 0, 0, 1},
                                             {1, 0, 0, 1},
                                             {1, 0, 0, 1},
                                             {1, 0, 0, 1},
                                             {1, 0, 0, 1},
                                             {1, 0, 0, 1}},
                      std::vector<GLuint>{0, 1, 2, 3, 4, 5, 6, 7, 0, 1},
                      GL_TRIANGLE_STRIP) {
    }
};
const float Playhead::width{0.01};
}  // namespace

class ImpulseRenderer::ContextLifetime : public BaseContextLifetime,
                                         public GLAudioThumbnailBase {
public:
    ContextLifetime(const AudioTransportSource& audio_transport_source)
            : audio_transport_source(audio_transport_source)
            , waveform(generic_shader)
            , waterfall(waterfall_shader, fade_shader, quad_shader)
            , playhead(generic_shader) {
    }

    float get_length_in_seconds() const {
        return waveform.get_length_in_seconds();
    }

    void update(float dt) override {
        waveform_max_view =
                ScaleFactor{get_viewport().x, get_viewport().y * 0.5f};
        current_params.update(target_params);

        auto max_view =
                mode == Mode::waveform ? waveform_max_view : waterfall_max_view;

        auto scale = current_scale_factor * max_view;
        scale.time /= waveform.get_length_in_seconds();

        for (auto i : get_thumbnails()) {
            i->set_time_scale(scale.time);
            i->set_amplitude_scale(scale.amplitude);
        }

        playhead.set_scale(glm::vec3{
                mode == Mode::waveform ? 100 : 1, scale.amplitude, 1});

        auto base_position =
                mode == Mode::waveform
                        ? glm::vec3{0, get_viewport().y * 0.5, 0.0}
                        : glm::vec3{0, 0, current_params.waveform_z};

        waveform.set_position(base_position);
        waterfall.set_position(base_position +
                               glm::vec3{0, 0, -Waterfall::width - 0.1});
        playhead.set_position(
                base_position +
                glm::vec3{audio_transport_source.getCurrentPosition() *
                                  scale.time,
                          0,
                          0.1});

        waveform.update(dt);
        waterfall.update(dt);
    }

    void draw() const override {
        assert(glGetError() == GL_NO_ERROR);

        auto c = 0.0;
        glClearColor(c, c, c, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_MULTISAMPLE);
        glEnable(GL_LINE_SMOOTH);
        glEnable(GL_POLYGON_SMOOTH);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        assert(glGetError() == GL_NO_ERROR);

        auto config_shader = [this](auto& shader) {
            auto s_shader = shader.get_scoped();
            shader.set_model_matrix(glm::mat4());
            shader.set_view_matrix(get_view_matrix());
            shader.set_projection_matrix(get_projection_matrix());
        };

        config_shader(generic_shader);
        config_shader(fade_shader);
        config_shader(waterfall_shader);
        config_shader(quad_shader);

        {
            auto s_shader = fade_shader.get_scoped();
            fade_shader.set_fade(current_params.fade);
        }

        {
            auto s_shader = quad_shader.get_scoped();
            quad_shader.set_screen_size(get_viewport());
            quad_shader.set_fade(current_params.fade);
        }
        {
            auto s_shader = waterfall_shader.get_scoped();
            waterfall_shader.set_fade(current_params.fade);
        }

        waveform.draw();
        if (mode == Mode::waterfall) {
            waterfall.draw();
        }
        playhead.draw();
    }

    void set_mode(Mode u) {
        mode = u;

        switch (mode) {
            case Mode::waveform:
                target_params = waveform_params;
                break;
            case Mode::waterfall:
                target_params = waterfall_params;
                break;
        }
    }

    void mouse_down(const glm::vec2& pos) override {
        if (mode == Mode::waterfall) {
            mousing = std::make_unique<Rotate>(target_params.azel, pos);
        }
    }

    void mouse_drag(const glm::vec2& pos) override {
        if (mousing) {
            const auto& m = dynamic_cast<Rotate&>(*mousing);
            auto diff = pos - m.position;
            target_params.azel = Orientable::AzEl{
                    m.orientation.azimuth + diff.x * Rotate::angle_scale,
                    glm::clamp(m.orientation.elevation +
                                       diff.y * Rotate::angle_scale,
                               static_cast<float>(-M_PI / 2),
                               static_cast<float>(M_PI / 2))};
        }
    }

    void mouse_up(const glm::vec2& pos) override {
        mousing = nullptr;
    }

    void mouse_wheel_move(float delta_y) override {
    }

    //  inherited audio thumbnail stuff
    void clear() override {
        for (auto i : get_thumbnails()) {
            i->clear();
        }
    }
    void load_from(AudioFormatManager& manager, const File& file) override {
        for (auto i : get_thumbnails()) {
            i->load_from(manager, file);
        }
    }
    void reset(int num_channels,
               double sample_rate,
               int64 total_samples) override {
        for (auto i : get_thumbnails()) {
            i->reset(num_channels, sample_rate, total_samples);
        }
    }
    void addBlock(int64 sample_number_in_source,
                  const AudioSampleBuffer& new_data,
                  int start_offset,
                  int num_samples) override {
        for (auto i : get_thumbnails()) {
            i->addBlock(sample_number_in_source,
                        new_data,
                        start_offset,
                        num_samples);
        }
    }

    void set_amplitude_scale(float f) override {
        for (auto i : get_thumbnails()) {
            i->set_amplitude_scale(f);
        }
    }

    void set_time_scale(float f) override {
        for (auto i : get_thumbnails()) {
            i->set_time_scale(f);
        }
    }

private:
    glm::mat4 get_projection_matrix() const {
        switch (mode) {
            case Mode::waterfall:
                return glm::perspective(45.0f, get_aspect(), 0.05f, 1000.0f);
            case Mode::waveform:
                return glm::ortho(0.0f,
                                  get_viewport().x,
                                  0.0f,
                                  get_viewport().y,
                                  -10.0f,
                                  100.0f);
        }
    }

    glm::mat4 get_rotation_matrix() const {
        switch (mode) {
            case Mode::waterfall: {
                auto i = glm::rotate(current_params.azel.azimuth,
                                     glm::vec3(0, 1, 0));
                auto j = glm::rotate(current_params.azel.elevation,
                                     glm::vec3(1, 0, 0));
                return j * i;
            }

            case Mode::waveform:
                return glm::mat4{};
        }
    }

    glm::mat4 get_view_matrix() const {
        switch (mode) {
            case Mode::waterfall:
                return glm::lookAt(glm::vec3{0, 0, current_params.eye},
                                   glm::vec3{0},
                                   glm::vec3{0, 1, 0}) *
                       get_rotation_matrix();
            case Mode::waveform:
                return glm::mat4{};
        }
    }

    std::array<GLAudioThumbnailBase*, 2> get_thumbnails() {
        return {&waveform, &waterfall};
    }

    struct Mousing {
        virtual ~Mousing() noexcept = default;
        enum class Mode { rotate, move };
        virtual Mode get_mode() const = 0;
    };

    struct Rotate : public Mousing {
        Rotate(const Orientable::AzEl& azel, const glm::vec2& position)
                : orientation(azel)
                , position(position) {
        }
        Mode get_mode() const {
            return Mode::rotate;
        }

        static const float angle_scale;
        Orientable::AzEl orientation;
        glm::vec2 position;
    };

    const AudioTransportSource& audio_transport_source;

    GenericShader generic_shader;
    FadeShader fade_shader;
    TexturedQuadShader quad_shader;
    WaterfallShader waterfall_shader;

    struct WaterfallParams {
        Orientable::AzEl azel{0, 0};
        float fade{0};
        float waveform_z{0};
        float eye{4};

        void update(const WaterfallParams& target) {
            azel += (target.azel - azel) * 0.1;
            fade += (target.fade - fade) * 0.1;
            waveform_z += (target.waveform_z - waveform_z) * 0.1;
            eye += (target.eye - eye) * 0.1;
        }
    };

    struct ScaleFactor {
        float time{1};
        float amplitude{1};

        ScaleFactor operator*(const ScaleFactor& s) const {
            return ScaleFactor{time * s.time, amplitude * s.amplitude};
        }
    };

    ScaleFactor waterfall_max_view{3, 1};
    ScaleFactor waveform_max_view;

    ScaleFactor current_scale_factor;

    static const float waterfall_max_x;

    static const WaterfallParams waveform_params;
    static const WaterfallParams waterfall_params;
    WaterfallParams current_params;
    WaterfallParams target_params;

    static const float angle_scale;

    Mode mode;
    Waveform waveform;
    Waterfall waterfall;
    Playhead playhead;

    std::unique_ptr<Mousing> mousing;
};

const ImpulseRenderer::ContextLifetime::WaterfallParams
        ImpulseRenderer::ContextLifetime::waveform_params{
                Orientable::AzEl{0, 0}, 0, 0, 3};
const ImpulseRenderer::ContextLifetime::WaterfallParams
        ImpulseRenderer::ContextLifetime::waterfall_params{
                Orientable::AzEl{-0.8, 0.2}, 1, 2, 4};

const float ImpulseRenderer::ContextLifetime::Rotate::angle_scale{0.01};
const float ImpulseRenderer::ContextLifetime::waterfall_max_x{3};

//----------------------------------------------------------------------------//

ImpulseRenderer::ImpulseRenderer(
        const AudioTransportSource& audio_transport_source)
        : audio_transport_source(audio_transport_source) {
}

ImpulseRenderer::~ImpulseRenderer() noexcept = default;

void ImpulseRenderer::newOpenGLContextCreated() {
    std::lock_guard<std::mutex> lck(mut);
    context_lifetime =
            std::make_unique<ContextLifetime>(audio_transport_source);
    BaseRenderer::newOpenGLContextCreated();
}

void ImpulseRenderer::openGLContextClosing() {
    std::lock_guard<std::mutex> lck(mut);
    context_lifetime = nullptr;
    BaseRenderer::openGLContextClosing();
}

BaseContextLifetime* ImpulseRenderer::get_context_lifetime() {
    return context_lifetime.get();
}

void ImpulseRenderer::set_mode(Mode mode) {
    std::lock_guard<std::mutex> lck(mut);
    push_incoming([this, mode] {
        assert(context_lifetime);
        context_lifetime->set_mode(mode);
    });
}

void ImpulseRenderer::clear() {
    std::lock_guard<std::mutex> lck(mut);
    push_incoming([this] {
        assert(context_lifetime);
        context_lifetime->clear();
    });
}
void ImpulseRenderer::load_from(AudioFormatManager& manager, const File& file) {
    std::lock_guard<std::mutex> lck(mut);
    push_incoming([this, &manager, file] {
        assert(context_lifetime);
        context_lifetime->load_from(manager, file);
    });
}
void ImpulseRenderer::reset(int num_channels,
                            double sample_rate,
                            int64 total_samples) {
    std::lock_guard<std::mutex> lck(mut);
    push_incoming([this, num_channels, sample_rate, total_samples] {
        assert(context_lifetime);
        context_lifetime->reset(num_channels, sample_rate, total_samples);
    });
}
void ImpulseRenderer::addBlock(int64 sample_number_in_source,
                               const AudioSampleBuffer& new_data,
                               int start_offset,
                               int num_samples) {
    std::lock_guard<std::mutex> lck(mut);
    push_incoming([this,
                   sample_number_in_source,
                   new_data,
                   start_offset,
                   num_samples] {
        assert(context_lifetime);
        context_lifetime->addBlock(
                sample_number_in_source, new_data, start_offset, num_samples);
    });
}

void ImpulseRenderer::set_amplitude_scale(float f) {
    std::lock_guard<std::mutex> lck(mut);
    push_incoming([this, f] {
        assert(context_lifetime);
        context_lifetime->set_amplitude_scale(f);
    });
}
void ImpulseRenderer::set_time_scale(float f) {
    std::lock_guard<std::mutex> lck(mut);
    push_incoming([this, f] {
        assert(context_lifetime);
        context_lifetime->set_time_scale(f);
    });
}