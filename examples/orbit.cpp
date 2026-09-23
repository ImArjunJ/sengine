#include <functional>
#include <iostream>
#include <numbers>
#include <sengine/animation.hpp>
#include <sengine/assets.hpp>
#include <sengine/input.hpp>
#include <sengine/signal.hpp>
#include <sengine/world.hpp>
#include <sstream>

namespace {
struct settings {
    unsigned bodies;
    double step;
    unsigned ticks;
};
struct orbit {
    float radius, phase, speed;
    std::size_t index;
};
std::shared_ptr<const settings> load_settings(const std::filesystem::path& path) {
    settings result{};
    std::istringstream source(sengine::read_text(path));
    if (!(source >> result.bodies >> result.step >> result.ticks) || result.bodies > 100000 ||
        !result.bodies || !std::isfinite(result.step) || result.step <= 0 || result.ticks > 100000)
        throw std::runtime_error("Invalid orbit settings");
    return std::make_shared<settings>(result);
}
sengine::input_event pause_event(sengine::event_type type) {
    sengine::input_event event{.type = type};
    event.key.code = sengine::key_code::space;
    return event;
}
class orbital_scene {
  public:
    orbital_scene(settings options, sengine::job_system& jobs)
        : options_(options), jobs_(jobs), poses_(options.bodies) {
        system_ = world_.create("orbital system");
        create_satellites();
        configure_input();
        center_motion_.position =
            sengine::animation_curve<sengine::float3>({{0, {0, 0, 0}}, {3, {0, 2, 0}}, {6, {0, 0, 0}}});
    }
    void run() {
        for (unsigned tick = 0; tick < options_.ticks; ++tick) {
            process_input(tick);
            if (!paused_) {
                elapsed_ += options_.step;
                center_time_.advance(options_.step);
            }
            update_poses();
        }
        std::cout << world_.size() - 1 << " satellites / " << elapsed_ << " seconds / " << checksum() << '\n';
    }

  private:
    void create_satellites() {
        trajectories_.reserve(options_.bodies);
        for (unsigned i = 0; i < options_.bodies; ++i) {
            const auto body = world_.create("satellite " + std::to_string(i), system_);
            const orbit motion{2.f + i * .15f, 2 * std::numbers::pi_v<float> * i / options_.bodies,
                               .2f + 1.f / (i + 1), i};
            world_.emplace<orbit>(body, motion);
            trajectories_.push_back(motion);
        }
    }
    void configure_input() {
        input_.define("pause", {{.key = sengine::key_code::space}});
        recording_.append(120, pause_event(sengine::event_type::key_down));
        recording_.append(121, pause_event(sengine::event_type::key_up));
        recording_.append(180, pause_event(sengine::event_type::key_down));
        recording_.append(181, pause_event(sengine::event_type::key_up));
        pause_listener_ = pause_changed_.subscribe([this](bool value) { paused_ = value; });
    }
    void process_input(unsigned tick) {
        input_.begin_frame();
        for (const auto& event : recording_.at(tick))
            input_.process(event.event);
        if (input_.action("pause").pressed)
            pause_changed_.emit(!paused_);
    }
    void calculate_pose(std::size_t i) {
        const auto& motion = trajectories_[i];
        const float phase = motion.phase + float(elapsed_) * motion.speed;
        poses_[i] =
            sengine::translation({std::cos(phase) * motion.radius, 0, std::sin(phase) * motion.radius});
    }
    void apply_pose(sengine::entity body, const orbit& motion) {
        world_.set_local_transform(body, poses_[motion.index]);
    }
    void update_poses() {
        world_.set_local_transform(system_, center_motion_.sample(center_time_.time()).matrix());
        jobs_.parallel_for(trajectories_.size(), 32, std::bind_front(&orbital_scene::calculate_pose, this));
        world_.each<orbit>(std::bind_front(&orbital_scene::apply_pose, this));
    }
    void accumulate_position(double& result, sengine::entity body, const orbit&) {
        const auto position = world_.world_transform(body)[3];
        result += position.x + position.z;
    }
    double checksum() {
        double result = 0;
        world_.each<orbit>(std::bind_front(&orbital_scene::accumulate_position, this, std::ref(result)));
        return result;
    }

  private:
    settings options_;
    sengine::job_system& jobs_;
    sengine::world world_;
    sengine::entity system_;
    std::vector<orbit> trajectories_;
    std::vector<sengine::mat4> poses_;
    sengine::input_map input_;
    sengine::input_recording recording_;
    sengine::signal<bool> pause_changed_;
    sengine::connection pause_listener_;
    sengine::transform_track center_motion_;
    sengine::playback center_time_{6};
    double elapsed_{};
    bool paused_{};
};
}
int main(int argc, char** argv) {
    try {
        const auto directory = argc > 1 ? std::filesystem::path(argv[1])
                                        : std::filesystem::absolute(argv[0]).parent_path() / "data";
        sengine::asset_store assets(directory);
        sengine::job_system jobs(4);
        const auto configuration = assets.prepare<settings>("orbit.txt", jobs, load_settings).commit();
        orbital_scene(*configuration.snapshot(), jobs).run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
