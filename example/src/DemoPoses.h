// The demo's kinematics, externalized from main.cpp: the world poses of the
// animated models and the joint poses of their internal frames, computed from
// the simulation time and the wave surface. main.cpp only applies what these
// functions return - the trajectories themselves live here, the seam where a
// dynamics simulator will plug in later.
//
// getPose(model, surface, t): the WORLD pose of an animated model riding the
//     wave surface at time t; nullopt for a model the demo does not animate
//     (keep the loaded SDF pose).
// getJoint(model, joint, t): the pose of an internal frame wrt its parent
//     (a proper pose: the joint's loaded pose composed with its motion);
//     nullopt for an unknown joint (keep the loaded pose).

#pragma once

#include <vsgOcean/core/Frame.h>
#include <vsgOcean/core/Wavefield.h>
#include <vsgOcean/scene/Scene.h>

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vsgOceanExample
{

    class DemoPoses
    {
    public:
        /// The demo's animated internal frames, as (model, joint) pairs - the joints
        /// whose poses getJoint() computes (and whose loaded poses it needs captured).
        static const std::vector<std::pair<std::string, std::string>>& animatedJoints()
        {
            static const std::vector<std::pair<std::string, std::string>> joints = {
                {"aquabot", "left_engine_link"}, {"aquabot", "right_engine_link"},
                {"aquabot", "left_propeller_link"}, {"aquabot", "right_propeller_link"},
                {"bluerov2", "thruster1"}, {"bluerov2", "thruster2"}, {"bluerov2", "thruster3"},
                {"bluerov2", "thruster4"}, {"bluerov2", "thruster5"}, {"bluerov2", "thruster6"},
                {"floatgen", "blades"}};
            return joints;
        }

        /// Capture the joints' loaded SDF poses from the scene (getJoint composes
        /// its motion on top of them). Call once, after Scene::load.
        void capture(const vsgOcean::Scene& scene)
        {
            for (const auto& [model, joint] : animatedJoints())
            {
                if (const auto& frame = scene.frame(model + "/" + joint)) initialJointPoses_[model + "/" + joint] = frame->matrix;
            }
        }

        /// The WORLD pose of an animated model at time t, riding the wave surface;
        /// nullopt for a model the demo does not animate.
        std::optional<vsg::dmat4> getPose(const std::string& model, const vsgOcean::Wavefield& surface, double t)
        {
            if (model == "aquabot")
            {
                // the surface vehicle performs a large circle, riding the waves: the
                // wave field is sampled at its position, slightly below the local
                // surface so the hulls keep a visible draft (the mesh waterline is
                // at its origin, the hull bottom 0.026 m above)
                const auto angle = 2.0 * vsg::PI * t / 60.0;
                const double x = 10.0 * std::cos(angle);
                const double y = 10.0 * std::sin(angle);
                const double surfaceZ = surface.height(x, y, t);
                return vsg::translate(vsg::dvec3(x, y, surfaceZ - 0.03)) * vsg::rotate(angle + vsg::PI / 2.0, 0.0, 0.0, 1.0);
            }

            if (model == "bluerov2")
            {
                // the ROV circles the wreck at (12, 0, -48), facing it. The vehicle
                // frame is x-front, y-port, z-up: the heading rotation points the
                // model's +X at the wreck, and the pitch tilts the nose down towards
                // it, so the forward camera takes in more of the wreck
                const auto angle = -2.0 * vsg::PI * t / 60.0;
                const vsg::dvec3 position(12.0 + 8.0 * std::cos(angle), 8.0 * std::sin(angle), -37.0);
                const auto attitude = vsg::rotate(angle + vsg::PI, 0.0, 0.0, 1.0) * vsg::rotate(vsg::radians(25.0), 0.0, 1.0, 0.0);
                return vsg::translate(position) * attitude;
            }

            if (model == "floatgen")
            {
                // the floating wind turbine rides the waves at its moored position
                // (40, -40): the platform's inertia damps the heave, so the wave
                // height is low-passed with a ~3 s response time - it follows the
                // swell but skips the chop (a real spar platform is slower still)
                const double targetZ = surface.height(40.0, -40.0, t);
                if (floatgenPrevT_ < 0.0)
                {
                    floatgenZ_ = targetZ;
                }
                else
                {
                    const double dt = std::max(0.0, t - floatgenPrevT_);
                    const double tau = 3.0;
                    floatgenZ_ += (targetZ - floatgenZ_) * std::min(1.0, dt / tau);
                }
                floatgenPrevT_ = t;
                return vsg::translate(vsg::dvec3(40.0, -40.0, floatgenZ_));
            }

            return std::nullopt;
        }

        /// The pose of an internal frame wrt its parent at time t - the joint's
        /// loaded pose composed with its motion; nullopt for an unknown joint.
        std::optional<vsg::dmat4> getJoint(const std::string& model, const std::string& joint, double t) const
        {
            const auto initial = initialJointPoses_.find(model + "/" + joint);
            if (initial == initialJointPoses_.end()) return std::nullopt;

            if (model == "aquabot" && (joint == "left_engine_link" || joint == "right_engine_link"))
            {
                // steering pods of the surface vehicle: rotation about the local Z axis
                const auto steer = 0.5 * std::sin(2.0 * vsg::PI * t / 10.0);
                return initial->second * vsg::rotate(steer, 0.0, 0.0, 1.0);
            }

            if (joint == "blades")
            {
                // the floating wind turbine's rotor: a realistic ~30 rpm (the
                // propellers' 5 rev/s spin would blur), about its local X axis
                const auto rotor = 2.0 * vsg::PI * 0.5 * t;
                return initial->second * vsg::rotate(rotor, 1.0, 0.0, 0.0);
            }

            // the remaining animated joints spin about their local X axis: the
            // aquabot's propellers and the bluerov2's thrusters (5 rev/s)
            const auto spin = 2.0 * vsg::PI * 5.0 * t;
            return initial->second * vsg::rotate(spin, 1.0, 0.0, 0.0);
        }

    private:
        std::map<std::string, vsg::dmat4> initialJointPoses_; // keyed "model/joint"
        double floatgenZ_ = 0.0;                              // the damped heave state
        double floatgenPrevT_ = -1.0;
    };

} // namespace vsgOceanExample
