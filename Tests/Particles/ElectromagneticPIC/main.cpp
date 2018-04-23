#include <iostream>

#include <AMReX.H>
#include <AMReX_ParmParse.H>
#include <AMReX_MultiFab.H>

#ifdef AMREX_USE_CUDA
#include <thrust/device_vector.h>
#endif

#include "Particles.H"
#include "Evolve.H"
#include "NodalFlags.H"
#include "Constants.H"
#include "IO.H"

using namespace amrex;

struct TestParams {
    IntVect ncell;      // num cells in domain
    IntVect nppc;       // number of particles per cell in each dim
    int max_grid_size;
    int nsteps;
    bool verbose;
};

void test_em_pic(const TestParams& parms)
{

    BL_PROFILE("test_em_pic");
    BL_PROFILE_VAR_NS("evolve_time", blp_evolve);
    BL_PROFILE_VAR_NS("init_time", blp_init);

    BL_PROFILE_VAR_START(blp_init);

    RealBox real_box;
    for (int n = 0; n < BL_SPACEDIM; n++) {
        real_box.setLo(n, -20e-6);
        real_box.setHi(n,  20e-6);
    }
    
    IntVect domain_lo(AMREX_D_DECL(0, 0, 0)); 
    IntVect domain_hi(AMREX_D_DECL(parms.ncell[0]-1,parms.ncell[1]-1,parms.ncell[2]-1)); 
    const Box domain(domain_lo, domain_hi);
    
    int coord = 0;
    int is_per[BL_SPACEDIM];
    for (int i = 0; i < BL_SPACEDIM; i++) 
        is_per[i] = 1; 
    Geometry geom(domain, &real_box, coord, is_per);
    
    BoxArray ba(domain);
    //  note - no domain decomposition right now
    //  ba.maxSize(parms.max_grid_size);
    DistributionMapping dm(ba);
    
    const int ng = 1;
    
    MultiFab Bx(amrex::convert(ba, YeeGrid::Bx_nodal_flag), dm, 1, ng);
    MultiFab By(amrex::convert(ba, YeeGrid::By_nodal_flag), dm, 1, ng);
    MultiFab Bz(amrex::convert(ba, YeeGrid::Bz_nodal_flag), dm, 1, ng);
    
    MultiFab Ex(amrex::convert(ba, YeeGrid::Ex_nodal_flag), dm, 1, ng);
    MultiFab Ey(amrex::convert(ba, YeeGrid::Ey_nodal_flag), dm, 1, ng);
    MultiFab Ez(amrex::convert(ba, YeeGrid::Ez_nodal_flag), dm, 1, ng);
    
    MultiFab jx(amrex::convert(ba, YeeGrid::jx_nodal_flag), dm, 1, ng);
    MultiFab jy(amrex::convert(ba, YeeGrid::jy_nodal_flag), dm, 1, ng);
    MultiFab jz(amrex::convert(ba, YeeGrid::jz_nodal_flag), dm, 1, ng);
    
    Bx.setVal(0.0); By.setVal(0.0); Bz.setVal(0.0);
    Ex.setVal(0.0); Ey.setVal(0.0); Ez.setVal(0.0);
    
    HostParticles electrons;
    init_particles(electrons, geom, ba, dm, parms.nppc,
                   0.01, 10.0, 1e25, -PhysConst::q_e, PhysConst::m_e);

    HostParticles H_ions;
    init_particles(H_ions, geom, ba, dm, parms.nppc,
                   0.01, 10.0, 1e25, PhysConst::q_e, PhysConst::m_p);

    Vector<Particles*> particles(2);    
#ifdef AMREX_USE_CUDA 
    DeviceParticles device_electrons;
    copy_particles_host_to_device(device_electrons, electrons);

    DeviceParticles device_H_ions;
    copy_particles_host_to_device(device_H_ions, H_ions);
    
    particles[0] = &device_electrons;
    particles[1] = &device_H_ions;
#else
    particles[0] = &electrons;
    particles[1] = &H_ions;
#endif
    
    int nsteps = parms.nsteps;
    const Real dt = compute_dt(geom);
    bool synchronized = true;
    
    BL_PROFILE_VAR_STOP(blp_init);
    
    BL_PROFILE_VAR_START(blp_evolve);

    for (int step = 0; step < nsteps; ++step) { 
        
        if (synchronized) {
            evolve_electric_field(Ex, Ey, Ez, Bx, By, Bz, jx, jy, jz, geom, 0.5*dt);
            push_particles_only(particles, 0.5*dt);
            enforce_periodic_bcs(particles, geom);
            synchronized = false;
        }
        
        evolve_magnetic_field(Ex, Ey, Ez, Bx, By, Bz, geom, 0.5*dt);
        
        push_and_depose_particles(Ex, Ey, Ez, Bx, By, Bz, jx, jy, jz,
                                  particles, geom, dt);
        
        enforce_periodic_bcs(particles, geom);
        
        evolve_magnetic_field(Ex, Ey, Ez, Bx, By, Bz, geom, 0.5*dt);
        
        if (step == nsteps - 1) {
            evolve_electric_field(Ex, Ey, Ez, Bx, By, Bz, jx, jy, jz, geom, 0.5*dt);
            push_particles_only(particles, -0.5*dt);
            synchronized = true;
        } else {
            evolve_electric_field(Ex, Ey, Ez, Bx, By, Bz, jx, jy, jz, geom, dt);
        }
    }

    BL_PROFILE_VAR_STOP(blp_evolve);

    WritePlotFile(Ex, Ey, Ez, Bx, By, Bz, jx, jy, jz, geom, 0.0, 10);   
}

int main(int argc, char* argv[])
{
    amrex::Initialize(argc,argv);
    
    amrex::InitRandom(451);

    ParmParse pp;
    
    TestParams parms;
    
    pp.get("ncell", parms.ncell);
    pp.get("nppc",  parms.nppc);
    pp.get("max_grid_size", parms.max_grid_size);
    pp.get("nsteps", parms.nsteps);

    parms.verbose = false;
    pp.query("verbose", parms.verbose);
    
    test_em_pic(parms);
    
    amrex::Finalize();
}