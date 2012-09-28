/* FluidSolver.h

	ZLib license
	This software is provided 'as-is', without any express or implied
	warranty. In no event will the authors be held liable for any damages
	arising from the use of this software.
	
	Permission is granted to anyone to use this software for any purpose,
	including commercial applications, and to alter it and redistribute it
	freely, subject to the following restrictions:
	
	1. The origin of this software must not be misrepresented; you must not
	   claim that you wrote the original software. If you use this software
	   in a product, an acknowledgment in the product documentation would be
	   appreciated but is not required.
	2. Altered source versions must be plainly marked as such, and must not be
	   misrepresented as being the original software.
	3. This notice may not be removed or altered from any source distribution.
*/
#ifndef FLUID_SOLVER_H
#define FLUID_SOLVER_H

#include "LinearMath/btQuickProf.h"		//BT_PROFILE(name) macro

#include "FluidSph.h"

///@brief Interface for SPH force computation. 
///@remarks
///Determines how the positions and velocities of fluid particles change from 
///one simulation step to the next; including changes as a result of interactions 
///between FluidSph(s).
class FluidSolver
{
public:
	virtual void stepSimulation(const FluidParametersGlobal &FG, btAlignedObjectArray<FluidSph*> *fluids) = 0;
	
protected:
	virtual void integrate(const FluidParametersGlobal &FG, const FluidParametersLocal &FL, FluidParticles *fluids);
};

///@brief Reference fluid solver.
///@remarks
///Calculates pressure using a FluidGrid, and force using FluidNeighbors 
///table generated during the pressure calculation.
///@par
///Does not implement fluid-fluid interactions.
class FluidSolverGridNeighbor : public FluidSolver
{
public:
	virtual void stepSimulation(const FluidParametersGlobal &FG, btAlignedObjectArray<FluidSph*> *fluids)
	{
		BT_PROFILE("FluidSolverGridNeighbor::stepSimulation()");
	
		//
		for(int i = 0; i < fluids->size(); ++i) (*fluids)[i]->insertParticlesIntoGrid();
		
		//
		for(int i = 0; i < fluids->size(); ++i) 
			sphComputePressure( FG, (*fluids)[i] );
			
		for(int i = 0; i < fluids->size(); ++i) 
			sphComputeForce( FG, (*fluids)[i] );
			
		for(int i = 0; i < fluids->size(); ++i)
			integrate( FG, (*fluids)[i]->getLocalParameters(), &(*fluids)[i]->internalGetFluidParticles() );
	}
	
protected:
	virtual void sphComputePressure(const FluidParametersGlobal &FG, FluidSph *fluid);
	virtual void sphComputeForce(const FluidParametersGlobal &FG, FluidSph *fluid);
	
	void sphComputeForceGrid(const FluidParametersGlobal &FG, FluidSph *fluid);
};

///@brief Optimized fluid solver based on FluidSolverGridNeighbor.
///@remarks
///FluidSolverReducedGridNeighbor exploits symmetry by excluding particles from calculations
///(removing particles from grid cells) after their interactions are determined. With this method, 
///the number of calculations is reduced, in theory, from n^2 to n(n + 1) / 2 == (n^2 + n) / 2.
///@par
///Experimental multithreading support is implemented for this solver.
///In testing, performance decreases when over 3 threads are used.
///@par
///Does not implement fluid-fluid interactions.
class FluidSolverReducedGridNeighbor : public FluidSolverGridNeighbor
{
protected:
	virtual void sphComputePressure(const FluidParametersGlobal &FG, FluidSph *fluid) { sphComputePressureGridReduce(FG, fluid); }
	virtual void sphComputeForce(const FluidParametersGlobal &FG, FluidSph *fluid) { sphComputeForceReduce(FG, fluid); } 
	
	void sphComputePressureGridReduce(const FluidParametersGlobal &FG, FluidSph *fluid);
	void sphComputeForceReduce(const FluidParametersGlobal &FG, FluidSph *fluid);
};

#endif

