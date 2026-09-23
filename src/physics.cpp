#include "backends/jolt_support.hpp"
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <map>
#include <mutex>
#include <thread>

namespace sengine {
using namespace physics_detail;
namespace {
using body_names = std::map<JPH::uint32, entity>;
class single_body final : public JPH::BodyFilter {
  public:
    explicit single_body(JPH::BodyID id) : id_(id) {}
    bool ShouldCollide(const JPH::BodyID& id) const override { return id == id_; }

  private:
    JPH::BodyID id_;
};
class contacts final : public JPH::ContactListener, public JPH::CharacterContactListener {
  public:
    explicit contacts(const body_names& names) : names_(names) {}
    void OnContactAdded(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override {
        record(first, second, manifold, settings);
    }
    void OnContactPersisted(const JPH::Body& first, const JPH::Body& second,
                            const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) override {
        record(first, second, manifold, settings);
    }
    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
        const std::lock_guard lock(mutex_);
        if (const auto found = active_.find(key(pair.GetBody1ID(), pair.GetBody2ID()));
            found != active_.end())
            found->second.detached = true;
    }
    void OnContactAdded(const JPH::CharacterVirtual* character, const JPH::CharacterContact& contact,
                        JPH::CharacterContactSettings&) override {
        record(character->GetInnerBodyID(), contact.mBodyB, value(contact.mPosition),
               -value(contact.mContactNormal), contact.mIsSensorB);
    }
    void OnContactPersisted(const JPH::CharacterVirtual* character, const JPH::CharacterContact& contact,
                            JPH::CharacterContactSettings& settings) override {
        OnContactAdded(character, contact, settings);
    }
    void OnContactRemoved(const JPH::CharacterVirtual* character, const JPH::BodyID& other,
                          const JPH::SubShapeID&) override {
        const std::lock_guard lock(mutex_);
        if (const auto found = active_.find(key(character->GetInnerBodyID(), other)); found != active_.end())
            found->second.detached = true;
    }
    void forget(JPH::BodyID body) {
        const std::lock_guard lock(mutex_);
        for (auto& [pair, state] : active_)
            if (pair.first == body.GetIndexAndSequenceNumber() ||
                pair.second == body.GetIndexAndSequenceNumber())
                state.event.phase = collision_phase::end;
    }
    std::vector<collision_event> take(const JPH::PhysicsSystem& system) {
        const std::lock_guard lock(mutex_);
        std::vector<collision_event> result;
        result.reserve(active_.size());
        for (auto it = active_.begin(); it != active_.end();) {
            auto& state = it->second;
            if (state.event.phase != collision_phase::end && state.detached && !touching(system, it->first))
                state.event.phase = collision_phase::end;
            result.push_back(state.event);
            if (state.event.phase == collision_phase::end)
                it = active_.erase(it);
            else {
                state.event.phase = collision_phase::persist;
                ++it;
            }
        }
        return result;
    }

  private:
    using pair = std::pair<JPH::uint32, JPH::uint32>;
    struct contact {
        collision_event event;
        bool detached{};
    };
    static pair key(JPH::BodyID first, JPH::BodyID second) {
        const auto a = first.GetIndexAndSequenceNumber(), b = second.GetIndexAndSequenceNumber();
        return {std::min(a, b), std::max(a, b)};
    }
    static bool touching(const JPH::PhysicsSystem& system, pair bodies) {
        JPH::RefConst<JPH::Shape> shape;
        JPH::RMat44 transform;
        {
            JPH::BodyLockRead first(system.GetBodyLockInterface(), JPH::BodyID(bodies.first));
            if (!first.Succeeded() || !first.GetBody().IsInBroadPhase())
                return false;
            shape = first.GetBody().GetShape();
            transform = first.GetBody().GetCenterOfMassTransform();
        }
        JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
        JPH::CollideShapeSettings settings;
        settings.mMaxSeparationDistance = system.GetPhysicsSettings().mSpeculativeContactDistance;
        system.GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sReplicate(1), transform, settings,
                                                  JPH::RVec3::sZero(), collector, {}, {},
                                                  single_body(JPH::BodyID(bodies.second)));
        return collector.HadHit();
    }
    void record(const JPH::Body& first, const JPH::Body& second, const JPH::ContactManifold& manifold,
                const JPH::ContactSettings& settings) {
        record(first.GetID(), second.GetID(), value(manifold.GetWorldSpaceContactPointOn1(0)),
               value(manifold.mWorldSpaceNormal), settings.mIsSensor);
    }
    void record(JPH::BodyID first, JPH::BodyID second, float3 point, float3 normal, bool sensor) {
        const std::lock_guard lock(mutex_);
        const auto first_name = names_.find(first.GetIndexAndSequenceNumber());
        const auto second_name = names_.find(second.GetIndexAndSequenceNumber());
        if (first_name == names_.end() || second_name == names_.end())
            return;
        const auto pair = key(first, second);
        const auto found = active_.find(pair);
        const auto phase = found == active_.end() ? collision_phase::begin : found->second.event.phase;
        collision_event event{first_name->second, second_name->second, phase, point, normal, sensor};
        if (event.second < event.first) {
            std::swap(event.first, event.second);
            event.normal = -event.normal;
        }
        active_.insert_or_assign(pair, contact{event});
    }

  private:
    const body_names& names_;
    std::mutex mutex_;
    std::map<pair, contact> active_;
};
class body_binding {
  public:
    body_binding(JPH::PhysicsSystem& system, const rigid_body& settings, const world_pose& pose)
        : system_(system), rigid(settings), scale(pose.scale) {
        const auto shape = make_shape(settings.geometry(), scale);
        const auto motion = settings.motion == body_motion::fixed     ? JPH::EMotionType::Static
                            : settings.motion == body_motion::dynamic ? JPH::EMotionType::Dynamic
                                                                      : JPH::EMotionType::Kinematic;
        JPH::BodyCreationSettings description(
            shape, native(pose.pose.position), native(pose.pose.orientation), motion,
            JPH::ObjectLayer(settings.layer + (motion == JPH::EMotionType::Static ? 0 : 32)));
        description.mFriction = settings.friction;
        description.mRestitution = settings.restitution;
        description.mGravityFactor = settings.gravity_scale;
        description.mLinearDamping = settings.linear_damping;
        description.mAngularDamping = settings.angular_damping;
        description.mIsSensor = settings.sensor;
        description.mCollideKinematicVsNonDynamic = settings.sensor;
        description.mMotionQuality =
            settings.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        description.mLinearVelocity = native(settings.linear_velocity);
        description.mAngularVelocity = native(settings.angular_velocity);
        description.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        description.mMassPropertiesOverride.mMass = settings.mass;
        auto* body = system_.GetBodyInterface().CreateBody(description);
        if (!body)
            throw std::runtime_error("Physics body capacity exhausted");
        id_ = body->GetID();
    }
    body_binding(JPH::PhysicsSystem& system, const character_body& settings, const world_pose& pose)
        : system_(system), character(settings), scale(pose.scale) {
        const auto shape =
            make_shape({collider_kind::capsule, float3{.5f}, settings.radius, settings.half_height}, scale);
        JPH::CharacterVirtualSettings description;
        description.mShape = shape;
        description.mInnerBodyShape = shape;
        description.mInnerBodyLayer = JPH::ObjectLayer(settings.layer + 32);
        description.mMass = settings.mass;
        description.mMaxSlopeAngle = settings.max_slope;
        description.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -settings.radius * scale.y);
        controller = std::make_unique<JPH::CharacterVirtual>(&description, native(pose.pose.position),
                                                             native(pose.pose.orientation), &system_);
        id_ = controller->GetInnerBodyID();
        if (id_.IsInvalid())
            throw std::runtime_error("Physics character capacity exhausted");
        controller->SetLinearVelocity(native(settings.linear_velocity));
    }
    ~body_binding() {
        if (!controller && !id_.IsInvalid()) {
            auto& bodies = system_.GetBodyInterface();
            if (bodies.IsAdded(id_))
                bodies.RemoveBody(id_);
            bodies.DestroyBody(id_);
        }
    }
    body_binding(const body_binding&) = delete;
    body_binding& operator=(const body_binding&) = delete;
    JPH::BodyID id() const { return id_; }
    void activate() {
        if (!controller)
            system_.GetBodyInterface().AddBody(id_, JPH::EActivation::Activate);
    }
    physics_pose pose() const {
        if (controller)
            return {value(controller->GetPosition()), value(controller->GetRotation())};
        JPH::RVec3 position;
        JPH::Quat rotation;
        system_.GetBodyInterface().GetPositionAndRotation(id_, position, rotation);
        return {value(position), value(rotation)};
    }
    void teleport(physics_pose pose) {
        validate(pose.position);
        const auto rotation = native(pose.orientation);
        if (controller) {
            controller->SetPosition(native(pose.position));
            controller->SetRotation(rotation);
        } else {
            system_.GetBodyInterface().SetPositionAndRotation(id_, native(pose.position), rotation,
                                                              JPH::EActivation::Activate);
        }
    }

  private:
    JPH::PhysicsSystem& system_;
    JPH::BodyID id_;

  public:
    std::optional<rigid_body> rigid;
    std::optional<character_body> character;
    float3 scale;
    mat4 last_transform;
    std::unique_ptr<JPH::CharacterVirtual> controller;
    float2 desired_velocity;
    bool jump{};
};
class query_layers final : public JPH::ObjectLayerFilter {
  public:
    explicit query_layers(std::uint32_t mask) : mask_(mask) {}
    bool ShouldCollide(JPH::ObjectLayer layer) const override { return (mask_ & (1u << (layer % 32))) != 0; }

  private:
    std::uint32_t mask_;
};
class query_bodies final : public JPH::BodyFilter {
  public:
    query_bodies(const body_names& names, query_filter filter) : names_(names), filter_(filter) {}
    bool ShouldCollide(const JPH::BodyID& id) const override {
        const auto found = names_.find(id.GetIndexAndSequenceNumber());
        return found != names_.end() && found->second != filter_.ignore;
    }
    bool ShouldCollideLocked(const JPH::Body& body) const override {
        return filter_.sensors || !body.IsSensor();
    }

  private:
    const body_names& names_;
    query_filter filter_;
};
}
struct physics_world::impl {
  public:
    impl(world& entities, physics_options settings)
        : environment_(environment()), entities(entities), options(std::move(settings)),
          pairs(options.layers), listener(names),
          jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, int(options.worker_threads)) {
        system.Init(options.max_bodies + 1, 0, options.max_bodies * 8, options.max_bodies * 4, layers, broad,
                    pairs);
        system.SetGravity(native(options.gravity));
        system.SetContactListener(&listener);
    }
    ~impl() {
        system.SetContactListener(nullptr);
        bindings.clear();
    }
    void require_owner() const {
        if (std::this_thread::get_id() != owner_)
            throw std::logic_error("Physics belongs to its creating thread");
    }
    body_binding& require(entity id) {
        require_owner();
        if (!entities.alive(id))
            throw std::invalid_argument("Stale or foreign physics entity");
        const auto found = bindings.find(id);
        if (found == bindings.end() || (found->second->rigid && !entities.get<rigid_body>(id)) ||
            (found->second->character && !entities.get<character_body>(id)))
            throw std::invalid_argument("Entity has no physics body");
        return *found->second;
    }
    void erase(std::map<entity, std::unique_ptr<body_binding>>::iterator it) {
        listener.forget(it->second->id());
        names.erase(it->second->id().GetIndexAndSequenceNumber());
        bindings.erase(it);
    }
    void synchronize() {
        require_owner();
        for (auto it = bindings.begin(); it != bindings.end();) {
            if (!entities.alive(it->first) ||
                (!entities.get<rigid_body>(it->first) && !entities.get<character_body>(it->first)))
                erase(it++);
            else
                ++it;
        }
        for (auto id : entities.entities()) {
            const auto* rigid = entities.get<rigid_body>(id);
            const auto* character = entities.get<character_body>(id);
            if (!rigid && !character)
                continue;
            if (rigid && character)
                throw std::invalid_argument("An entity cannot have both a rigid body and a character");
            if (rigid)
                validate(*rigid);
            if (character) {
                validate(*character);
                if (options.gravity.x != 0 || options.gravity.z != 0 || options.gravity.y >= 0)
                    throw std::invalid_argument("Characters need downward Y gravity");
            }
            const auto transform = entities.world_transform(id);
            const auto pose = decompose(transform);
            const auto found = bindings.find(id);
            if (found == bindings.end() || (rigid && found->second->rigid != *rigid) ||
                (character && found->second->character != *character) ||
                !same_scale(found->second->scale, pose.scale)) {
                if (found == bindings.end() && bindings.size() >= options.max_bodies)
                    throw std::runtime_error("Physics body capacity exhausted");
                auto next = rigid ? std::make_unique<body_binding>(system, *rigid, pose)
                                  : std::make_unique<body_binding>(system, *character, pose);
                next->last_transform = transform;
                names.emplace(next->id().GetIndexAndSequenceNumber(), id);
                if (found != bindings.end())
                    erase(found);
                auto& added = bindings.emplace(id, std::move(next)).first->second;
                added->activate();
                if (added->controller)
                    added->controller->SetListener(&listener);
            } else if (transform != found->second->last_transform) {
                found->second->teleport(pose.pose);
                found->second->last_transform = transform;
            }
        }
    }
    void update_character(body_binding& body) {
        auto& controller = *body.controller;
        const auto& settings = *body.character;
        controller.UpdateGroundVelocity();
        const bool grounded = controller.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
        auto velocity = controller.GetLinearVelocity();
        if (grounded && velocity.GetY() <= controller.GetGroundVelocity().GetY() + .1f)
            velocity = controller.GetGroundVelocity() + JPH::Vec3(body.desired_velocity.x,
                                                                  body.jump ? settings.jump_speed : 0,
                                                                  body.desired_velocity.y);
        else
            velocity = JPH::Vec3(body.desired_velocity.x, velocity.GetY(), body.desired_velocity.y);
        body.jump = false;
        velocity += native(options.gravity) * float(options.fixed_step);
        controller.SetLinearVelocity(velocity);
        JPH::CharacterVirtual::ExtendedUpdateSettings movement;
        movement.mWalkStairsStepUp = JPH::Vec3(0, settings.step_height, 0);
        movement.mStickToFloorStepDown = JPH::Vec3(0, -settings.step_height, 0);
        std::uint32_t mask{};
        for (unsigned layer = 0; layer < 32; ++layer)
            if (options.layers.collides(settings.layer, layer))
                mask |= 1u << layer;
        controller.ExtendedUpdate(float(options.fixed_step), native(options.gravity), movement, {},
                                  query_layers(mask), query_bodies(names, {.ignore = {}, .sensors = false}),
                                  {}, allocator);
    }
    void publish() {
        std::vector<std::pair<unsigned, entity>> ordered;
        for (const auto& [id, body] : bindings) {
            unsigned depth{};
            for (auto parent = entities.parent(id); parent; parent = entities.parent(parent))
                ++depth;
            ordered.emplace_back(depth, id);
        }
        std::ranges::sort(ordered);
        for (auto [depth, id] : ordered) {
            const auto found = bindings.find(id);
            if (found == bindings.end())
                continue;
            auto& body = *found->second;
            const auto pose = body.pose();
            const auto transform =
                translation(pose.position) * rotation(pose.orientation) * scaling(body.scale);
            entities.set_world_transform(id, transform);
            if (body.controller) {
                auto& settings = *entities.get<character_body>(id);
                settings.linear_velocity = value(body.controller->GetLinearVelocity());
                body.character = settings;
            } else {
                auto& settings = *entities.get<rigid_body>(id);
                settings.linear_velocity = value(system.GetBodyInterface().GetLinearVelocity(body.id()));
                settings.angular_velocity = value(system.GetBodyInterface().GetAngularVelocity(body.id()));
                body.rigid = settings;
            }
        }
        for (auto& [id, body] : bindings)
            body->last_transform = entities.world_transform(id);
    }
    entity name(JPH::BodyID id) const {
        const auto found = names.find(id.GetIndexAndSequenceNumber());
        return found == names.end() ? entity{} : found->second;
    }

  private:
    jolt_environment& environment_;
    std::thread::id owner_{std::this_thread::get_id()};

  public:
    world& entities;
    physics_options options;
    broad_layers layers;
    broad_filter broad;
    pair_filter pairs;
    body_names names;
    contacts listener;
    JPH::TempAllocatorImplWithMallocFallback allocator{32 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs;
    JPH::PhysicsSystem system;
    std::map<entity, std::unique_ptr<body_binding>> bindings;
    std::vector<collision_event> events;
};
physics_world::physics_world(world& entities, physics_options options) {
    validate(options.gravity);
    if (!std::isfinite(options.fixed_step) || options.fixed_step <= 0 || options.fixed_step > .1 ||
        options.max_bodies == 0 || options.max_bodies > 1000000 || options.worker_threads > 64)
        throw std::invalid_argument("Invalid physics capacity or fixed step");
    impl_ = std::make_unique<impl>(entities, std::move(options));
    impl_->synchronize();
}
physics_world::~physics_world() = default;
void physics_world::synchronize() {
    impl_->synchronize();
}
void physics_world::step() {
    auto& state = *impl_;
    state.synchronize();
    for (auto& [id, body] : state.bindings)
        if (body->controller)
            state.update_character(*body);
    const auto error = state.system.Update(float(state.options.fixed_step), 1, &state.allocator, &state.jobs);
    if (error != JPH::EPhysicsUpdateError::None)
        throw std::runtime_error("Physics contact capacity exhausted");
    state.publish();
    state.events = state.listener.take(state.system);
}
double physics_world::fixed_step() const noexcept {
    return impl_->options.fixed_step;
}
std::size_t physics_world::size() const noexcept {
    return impl_->bindings.size();
}
void physics_world::teleport(entity id, physics_pose pose) {
    auto& body = impl_->require(id);
    body.teleport(pose);
    const auto transform =
        translation(pose.position) * rotation(value(native(pose.orientation))) * scaling(body.scale);
    impl_->entities.set_world_transform(id, transform);
    body.last_transform = impl_->entities.world_transform(id);
}
void physics_world::velocity(entity id, float3 linear, float3 angular) {
    validate(linear);
    validate(angular);
    auto& body = impl_->require(id);
    if (body.controller) {
        body.controller->SetLinearVelocity(native(linear));
        impl_->entities.get<character_body>(id)->linear_velocity = linear;
        body.character->linear_velocity = linear;
    } else {
        if (body.rigid->motion == body_motion::fixed)
            throw std::invalid_argument("A static body cannot have velocity");
        impl_->system.GetBodyInterface().SetLinearAndAngularVelocity(body.id(), native(linear),
                                                                     native(angular));
        impl_->system.GetBodyInterface().ActivateBody(body.id());
        auto& settings = *impl_->entities.get<rigid_body>(id);
        settings.linear_velocity = linear;
        settings.angular_velocity = angular;
        body.rigid = settings;
    }
}
float3 physics_world::velocity(entity id) const {
    const auto& body = impl_->require(id);
    return body.controller ? value(body.controller->GetLinearVelocity())
                           : value(impl_->system.GetBodyInterface().GetLinearVelocity(body.id()));
}
void physics_world::impulse(entity id, float3 impulse) {
    validate(impulse);
    auto& body = impl_->require(id);
    if (!body.rigid || body.rigid->motion != body_motion::dynamic)
        throw std::invalid_argument("Only dynamic bodies accept impulses");
    impl_->system.GetBodyInterface().AddImpulse(body.id(), native(impulse));
}
void physics_world::force(entity id, float3 force) {
    validate(force);
    auto& body = impl_->require(id);
    if (!body.rigid || body.rigid->motion != body_motion::dynamic)
        throw std::invalid_argument("Only dynamic bodies accept forces");
    impl_->system.GetBodyInterface().AddForce(body.id(), native(force));
}
void physics_world::move_kinematic(entity id, physics_pose pose) {
    validate(pose.position);
    auto& body = impl_->require(id);
    if (!body.rigid || body.rigid->motion != body_motion::kinematic)
        throw std::invalid_argument("Entity is not kinematic");
    impl_->system.GetBodyInterface().MoveKinematic(body.id(), native(pose.position), native(pose.orientation),
                                                   float(impl_->options.fixed_step));
}
void physics_world::walk(entity id, float2 velocity, bool jump) {
    validate(float3{velocity.x, 0, velocity.y});
    auto& body = impl_->require(id);
    if (!body.controller)
        throw std::invalid_argument("Entity is not a character");
    body.desired_velocity = velocity;
    body.jump = body.jump || jump;
}
character_state physics_world::character(entity id) const {
    const auto& body = impl_->require(id);
    if (!body.controller)
        throw std::invalid_argument("Entity is not a character");
    return {body.controller->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround,
            value(body.controller->GetLinearVelocity()), impl_->name(body.controller->GetGroundBodyID())};
}
std::span<const collision_event> physics_world::events() const noexcept {
    return impl_->events;
}
std::optional<physics_hit> physics_world::raycast(float3 origin, float3 displacement,
                                                  query_filter filter) const {
    impl_->require_owner();
    validate(origin);
    validate(displacement);
    if (length(displacement) == 0)
        throw std::invalid_argument("A ray needs nonzero displacement");
    JPH::RayCastResult hit;
    if (!impl_->system.GetNarrowPhaseQuery().CastRay({native(origin), native(displacement)}, hit, {},
                                                     query_layers(filter.layers),
                                                     query_bodies(impl_->names, filter)))
        return {};
    const auto point = origin + displacement * hit.mFraction;
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), hit.mBodyID);
    const auto normal =
        hit.mFraction == 0
            ? -normalize(displacement)
            : value(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, native(point)));
    return physics_hit{impl_->name(hit.mBodyID), hit.mFraction, point, normal};
}
std::optional<physics_hit> physics_world::sweep(const collider& geometry, physics_pose pose,
                                                float3 displacement, query_filter filter) const {
    impl_->require_owner();
    validate(pose.position);
    validate(displacement);
    if (length(displacement) == 0)
        throw std::invalid_argument("A sweep needs nonzero displacement");
    const auto shape = make_shape(geometry);
    const auto transform = JPH::RMat44::sRotationTranslation(native(pose.orientation), native(pose.position));
    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
    JPH::ShapeCastSettings settings;
    settings.mReturnDeepestPoint = true;
    impl_->system.GetNarrowPhaseQuery().CastShape(
        JPH::RShapeCast::sFromWorldTransform(shape, JPH::Vec3::sReplicate(1), transform,
                                             native(displacement)),
        settings, JPH::RVec3::sZero(), collector, {}, query_layers(filter.layers),
        query_bodies(impl_->names, filter));
    if (!collector.HadHit())
        return {};
    const auto& hit = collector.mHit;
    return physics_hit{impl_->name(hit.mBodyID2), hit.mFraction, value(hit.mContactPointOn2),
                       -value(hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY()))};
}
std::vector<entity> physics_world::overlap(const collider& geometry, physics_pose pose,
                                           query_filter filter) const {
    impl_->require_owner();
    validate(pose.position);
    const auto shape = make_shape(geometry);
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    impl_->system.GetNarrowPhaseQuery().CollideShape(
        shape, JPH::Vec3::sReplicate(1),
        JPH::RMat44::sRotationTranslation(native(pose.orientation), native(pose.position)), {},
        JPH::RVec3::sZero(), collector, {}, query_layers(filter.layers), query_bodies(impl_->names, filter));
    std::vector<entity> result;
    for (const auto& hit : collector.mHits)
        result.push_back(impl_->name(hit.mBodyID2));
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
}
