/**
 * @file 	velocity_gradient.cpp
 * @brief 	test the approximation of velocity gradient.
 * @author 	Xiangyu Hu
 */
#include "sphinxsys.h" //SPHinXsys Library.
using namespace SPH;   // Namespace cite here.
//----------------------------------------------------------------------
//	Material parameters.
//----------------------------------------------------------------------
Real rho0_f = 1.0;
Real mu_f = 1.0e-1;      /**< Viscosity. */
Real U_max = 1.0;        // make sure the maximum anticipated speed
Real c_f = 10.0 * U_max; /**< Reference sound speed. */
//----------------------------------------------------------------------
//	Basic geometry parameters and numerical setup.
//----------------------------------------------------------------------
Real width = 1.0;
Real height = 0.5;
Real particle_spacing = 0.01;
Real boundary_width = particle_spacing * 4; // boundary width
//----------------------------------------------------------------------
//	Complex shapes for wall boundary
//----------------------------------------------------------------------
class UpperBoundary : public ComplexShape
{
  public:
    explicit UpperBoundary(const std::string &shape_name) : ComplexShape(shape_name)
    {
        Vecd scaled_container(0.5 * width + boundary_width, 0.5 * boundary_width);
        Transform translate_to_origin(scaled_container);
        Vecd transform(-boundary_width, height);
        Transform translate_to_position(transform + scaled_container);
        add<TransformShape<GeometricShapeBox>>(Transform(translate_to_position), scaled_container);
    }
};
class WallBoundary : public ComplexShape
{
  public:
    explicit WallBoundary(const std::string &shape_name) : ComplexShape(shape_name)
    {
        Vecd scaled_container_outer(0.5 * width + boundary_width, 0.5 * height + boundary_width);
        Vecd scaled_container(0.5 * width + boundary_width, 0.5 * height);
        Transform translate_to_origin_outer(Vec2d(-boundary_width, -boundary_width) + scaled_container_outer);
        Transform translate_to_origin_inner(scaled_container);

        add<TransformShape<GeometricShapeBox>>(Transform(translate_to_origin_outer), scaled_container_outer);
        subtract<TransformShape<GeometricShapeBox>>(Transform(translate_to_origin_inner), scaled_container);
    }
};
class WaterBlock : public ComplexShape
{
  public:
    explicit WaterBlock(const std::string &shape_name) : ComplexShape(shape_name)
    {
        Vecd scaled_container(0.5 * width, 0.5 * height);
        Transform translate_to_origin(scaled_container);
        add<TransformShape<GeometricShapeBox>>(Transform(translate_to_origin), scaled_container);
    }
};
//----------------------------------------------------------------------
//	application dependent initial condition
//----------------------------------------------------------------------
class CouetteFlowInitialCondition
    : public fluid_dynamics::FluidInitialCondition
{
  public:
    explicit CouetteFlowInitialCondition(SPHBody &sph_body)
        : fluid_dynamics::FluidInitialCondition(sph_body){};

    void update(size_t index_i, Real dt)
    {
        Vecd velocity = ZeroData<Vecd>::value;
        velocity[0] = pos_[index_i][0] / height;
        vel_[index_i] = velocity;
    }
};

class BoundaryVelocity : public solid_dynamics::MotionConstraint
{
  public:
    explicit BoundaryVelocity(BodyPartByParticle &body_part)
        : solid_dynamics::MotionConstraint(body_part) {}

    void update(size_t index_i, Real dt = 0.0)
    {
        Vecd velocity = ZeroData<Vecd>::value;
        velocity[0] = 1.0;
        vel_[index_i] = velocity;
    };
};

//----------------------------------------------------------------------
//	Main program starts here.
//----------------------------------------------------------------------
int main(int ac, char *av[])
{
    //----------------------------------------------------------------------
    //	Build up an SPHSystem and IO environment.
    //----------------------------------------------------------------------
    BoundingBox system_domain_bounds(Vecd(-boundary_width * 2, -boundary_width * 2), Vecd(width + boundary_width * 2, height + boundary_width * 2));
    SPHSystem sph_system(system_domain_bounds, particle_spacing);
    sph_system.handleCommandlineOptions(ac, av)->setIOEnvironment();
    //----------------------------------------------------------------------
    //	Creating bodies with corresponding materials and particles.
    //----------------------------------------------------------------------
    FluidBody water_block(sph_system, makeShared<WaterBlock>("WaterBody"));
    water_block.defineParticlesAndMaterial<BaseParticles, WeaklyCompressibleFluid>(rho0_f, c_f, mu_f);
    water_block.generateParticles<ParticleGeneratorLattice>();

    SolidBody wall_boundary(sph_system, makeShared<WallBoundary>("Wall"));
    wall_boundary.defineParticlesAndMaterial<SolidParticles, Solid>();
    wall_boundary.generateParticles<ParticleGeneratorLattice>();
    //----------------------------------------------------------------------
    //	Define body relation map.
    //	The contact map gives the topological connections between the bodies.
    //	Basically the the range of bodies to build neighbor particle lists.
    //  Generally, we first define all the inner relations, then the contact relations.
    //----------------------------------------------------------------------
    InnerRelation water_block_inner(water_block);
    ContactRelation water_wall_contact(water_block, {&wall_boundary});
    //----------------------------------------------------------------------
    // Combined relations built from basic relations
    // which is only used for update configuration.
    //----------------------------------------------------------------------
    ComplexRelation water_block_complex(water_block_inner, water_wall_contact);
    //----------------------------------------------------------------------
    //	Define the numerical methods used in the simulation.
    //	Note that there may be data dependence on the sequence of constructions.
    //----------------------------------------------------------------------
    SimpleDynamics<CouetteFlowInitialCondition> initial_condition(water_block);
    SimpleDynamics<NormalDirectionFromBodyShape> wall_boundary_normal_direction(wall_boundary);
    PeriodicConditionUsingCellLinkedList periodic_condition(water_block, water_block.getBodyShapeBounds(), xAxis);
    InteractionWithUpdate<fluid_dynamics::VelocityGradientWithWall<NoKernelCorrection>> vel_grad_calculation(water_block_inner, water_wall_contact);
    BodyRegionByParticle upper_wall(wall_boundary, makeShared<UpperBoundary>("UpperWall"));
    SimpleDynamics<BoundaryVelocity> upper_wall_velocity(upper_wall);
    //----------------------------------------------------------------------
    //	Define the methods for I/O operations, observations
    //	and regression tests of the simulation.
    //----------------------------------------------------------------------
    water_block.addBodyStateForRecording<Matd>("VelocityGradient");
    BodyStatesRecordingToVtp body_states_recording(sph_system.real_bodies_);
    //----------------------------------------------------------------------
    //	Prepare the simulation with cell linked list, configuration
    //	and case specified initial condition if necessary.
    //----------------------------------------------------------------------
    sph_system.initializeSystemCellLinkedLists();
    periodic_condition.update_cell_linked_list_.exec();
    sph_system.initializeSystemConfigurations();
    initial_condition.exec();
    wall_boundary_normal_direction.exec();
    vel_grad_calculation.exec();

    body_states_recording.writeToFile(0);
    return 0;
}
