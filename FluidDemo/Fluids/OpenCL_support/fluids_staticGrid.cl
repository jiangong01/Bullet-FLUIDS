/* fluids.cl
	Fluids v.2 OpenCL Port

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

#ifdef cl_amd_printf
	#pragma OPENCL EXTENSION cl_amd_printf : enable
#endif

typedef float btScalar;
typedef float4 btVector3;
	
inline btScalar btVector3_length2(btVector3 v) { return v.x*v.x + v.y*v.y + v.z*v.z; }
inline btScalar btVector3_dot(btVector3 a, btVector3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline btVector3 btVector3_normalize(btVector3 v)
{
	btScalar length2 = btVector3_length2(v);
	if( length2 != (btScalar)0.0 ) v /= sqrt(length2);
	
	return v;
}

//Defined in "FluidParticles.h"
#define INVALID_PARTICLE_INDEX -1

//Syncronize with 'class FluidNeighbors' in "FluidParticles.h"
#define MAX_NEIGHBORS 80
typedef struct
{
	int m_count;
	int m_particleIndicies[MAX_NEIGHBORS];
	btScalar m_distances[MAX_NEIGHBORS];
	
} FluidNeighbors;

//Syncronize with 'struct FluidParametersGlobal' in "FluidParameters.h"
typedef struct
{
	btVector3 m_planeGravity;
	btVector3 m_pointGravityPosition;
	btScalar m_pointGravity;
	btScalar m_timeStep;
	btScalar m_simulationScale;
	btScalar m_particleRadius;
	btScalar m_speedLimit;
	btScalar m_sphSmoothRadius;
	btScalar m_sphRadiusSquared;
	btScalar m_poly6KernCoeff;
	btScalar m_spikyKernGradCoeff;
	btScalar m_viscosityKernLapCoeff;
} FluidParametersGlobal;

//Syncronize with 'struct FluidParametersLocal' in "FluidParameters.h"
typedef struct
{
	btVector3 m_volumeMin;
	btVector3 m_volumeMax;
	btScalar m_viscosity;
	btScalar m_restDensity;
	btScalar m_particleMass;
	btScalar m_stiffness;
	btScalar m_boundaryStiff;
	btScalar m_boundaryDamp;
	btScalar m_particleDist;
} FluidParametersLocal;

//Syncronize with 'struct FluidStaticGridParameters' in "FluidStaticGrid.h"
typedef struct
{
	btVector3 m_min;
	btVector3 m_max;
	btScalar m_gridCellSize;
	int m_resolutionX;
	int m_resolutionY;
	int m_resolutionZ;
	int m_numCells;

} FluidStaticGridParameters;

// /////////////////////////////////////////////////////////////////////////////
//  class FluidSystem
// /////////////////////////////////////////////////////////////////////////////
__kernel void grid_insertParticles(__global btVector3 *fluidPositions, __global int *fluidNextIndicies, 
								   __global FluidStaticGridParameters *gridParams, __global volatile int *gridCells, 
								   __global volatile int *gridCellsNumFluids)	
{
	//Current implementation assumes that all values in
	//gridCells[] are set to INVALID_PARTICLE_INDEX,
	//and all values in gridCellsNumFluids[] are set to 0
	//before this function is called.

	__global FluidStaticGridParameters *GP = gridParams;
	
	//
	int particleIndex = get_global_id(0);
	__global int *nextFluidIndex = &fluidNextIndicies[particleIndex];
	
	//Reset particles
	*nextFluidIndex = INVALID_PARTICLE_INDEX;
	
	//Load into grid
	int index_x = convert_int( (fluidPositions[particleIndex].x - GP->m_min.x) / GP->m_gridCellSize );
	int index_y = convert_int( (fluidPositions[particleIndex].y - GP->m_min.y) / GP->m_gridCellSize );
	int index_z = convert_int( (fluidPositions[particleIndex].z - GP->m_min.z) / GP->m_gridCellSize );
	
	int cellIndex = (index_z*GP->m_resolutionY + index_y)*GP->m_resolutionX + index_x;
	
	if(0 <= cellIndex && cellIndex < GP->m_numCells) 
	{
		//Add particle to linked list
		//Equivalent to:
		//		*nextFluidIndex = grid->cells[cellIndex];
		//		grid->cells[cellIndex] = particleIndex;
		*nextFluidIndex = atomic_xchg(&gridCells[cellIndex], particleIndex);
		
		//Equivalent to:
		//		grid->cells_num_fluids[cellIndex]++;
		atomic_inc(&gridCellsNumFluids[cellIndex]);
	}
}


//Grid::findCells()
#define FluidGrid_NUM_FOUND_CELLS 8
inline void findCells(__global FluidStaticGridParameters *gridParams, btVector3 position, btScalar radius, int8 *out_cells)
{
	__global FluidStaticGridParameters *GP = gridParams;

	//Store a 2x2x2 grid cell query result in m_findCellsResult,
	//where m_findCellsResult.m_indicies[0], the cell with the lowest index,
	//corresponds to the minimum point of the sphere's AABB
	
	//Determine the grid cell index at the minimum point of the particle's AABB
	int index_x = convert_int( (-radius + position.x - GP->m_min.x) / GP->m_gridCellSize );
	int index_y = convert_int( (-radius + position.y - GP->m_min.y) / GP->m_gridCellSize );
	int index_z = convert_int( (-radius + position.z - GP->m_min.z) / GP->m_gridCellSize );
	
	//Clamp index to grid bounds
	if(index_x < 0) index_x = 0;
	if(index_y < 0) index_y = 0;
	if(index_z < 0) index_z = 0;
	
		//Since a 2x2x2 volume is accessed, subtract 2 from the upper index bounds
			//Subtract 1 as a 2x2x2 volume is accessed, and the index we want is the 'min' index
			//Subtract 1 again as indicies start from 0 (GP->m_resolutionX/Y/Z is out of bounds)
	if(index_x >= GP->m_resolutionX - 2) index_x = GP->m_resolutionX - 2;
	if(index_y >= GP->m_resolutionY - 2) index_y = GP->m_resolutionY - 2;
	if(index_z >= GP->m_resolutionZ - 2) index_z = GP->m_resolutionZ - 2;
	
	//Load indicies
	const int stride_x = 1;
	const int stride_y = GP->m_resolutionX;
	const int stride_z = GP->m_resolutionX*GP->m_resolutionY;
	
	(*out_cells).s0 = (index_z * GP->m_resolutionY + index_y) * GP->m_resolutionX + index_x ;
	(*out_cells).s1 = (*out_cells).s0 + stride_x;
	(*out_cells).s2 = (*out_cells).s0 + stride_y;
	(*out_cells).s3 = (*out_cells).s0 + stride_y + stride_x;

	(*out_cells).s4 = (*out_cells).s0 + stride_z;
	(*out_cells).s5 = (*out_cells).s1 + stride_z;
	(*out_cells).s6 = (*out_cells).s2 + stride_z;
	(*out_cells).s7 = (*out_cells).s3 + stride_z;
}

//
__kernel void sph_computePressure(__global FluidParametersGlobal *FG,
								  __global FluidParametersLocal *FL, __global btVector3 *fluidPosition,
								  __global btScalar *fluidPressure, __global btScalar *fluidInvDensity,  
								  __global int *fluidNextIndicies, __global FluidNeighbors *fluidNeighbors,
								  __global FluidStaticGridParameters *gridParams,  __global int *gridCells)
{	
	btScalar searchRadius = FG->m_sphSmoothRadius / FG->m_simulationScale;


	int i = get_global_id(0);
	
	btScalar sum = 0.0f;	
	int neighborCount = 0;	//m_neighborTable[i].clear();

	int8 grid_query_result;
	findCells(gridParams, fluidPosition[i], searchRadius, &grid_query_result);
	
	int* query_result = (int*) &grid_query_result;
	for(int cell = 0; cell < FluidGrid_NUM_FOUND_CELLS; ++cell) 
	{
		for(int n = gridCells[ query_result[cell] ]; n != INVALID_PARTICLE_INDEX; n = fluidNextIndicies[n])	
		{					
			if(i == n) continue; 
			
			btVector3 distance = (fluidPosition[i] - fluidPosition[n]) * FG->m_simulationScale;	//Simulation scale distance
			btScalar distanceSquared = btVector3_length2(distance);
			
			if(FG->m_sphRadiusSquared > distanceSquared) 
			{
				btScalar c = FG->m_sphRadiusSquared - distanceSquared;
				sum += c * c * c;
				
				if(neighborCount < MAX_NEIGHBORS)	//if( !m_neighborTable[i].isFilled() ) 
				{	
					//m_neighborTable[i].addNeighbor( n, sqrt(distanceSquared) );
					fluidNeighbors[i].m_particleIndicies[neighborCount] = n;
					fluidNeighbors[i].m_distances[neighborCount] = sqrt(distanceSquared);
					++neighborCount;
				}
			}
		}
	}
	
	btScalar density = sum * FL->m_particleMass * FG->m_poly6KernCoeff;	
	fluidPressure[i] = (density - FL->m_restDensity) * FL->m_stiffness;		
	fluidInvDensity[i] = 1.0f / density;
	
	fluidNeighbors[i].m_count = neighborCount;
}


__kernel void sph_computeForce(__global FluidParametersGlobal *FG, __global FluidParametersLocal *FL,
							   __global btVector3 *fluidPosition, __global btVector3 *fluidVelEval, 
							   __global btVector3 *fluidSphForce, __global btScalar *fluidPressure, 
							   __global btScalar *fluidInvDensity, __global FluidNeighbors *fluidNeighbors)
{
	btScalar vterm = FG->m_viscosityKernLapCoeff * FL->m_viscosity;
	
	int i = get_global_id(0);
	
	btVector3 force = {0.0f, 0.0f, 0.0f, 0.0f};
	for(int j = 0; j < fluidNeighbors[i].m_count; ++j) 
	{
		int n = fluidNeighbors[i].m_particleIndicies[j];
	
		btVector3 distance = (fluidPosition[i] - fluidPosition[n]) * FG->m_simulationScale;	//Simulation scale distance
		
		btScalar c = FG->m_sphSmoothRadius - fluidNeighbors[i].m_distances[j];
		btScalar pterm = -0.5f * c * FG->m_spikyKernGradCoeff * ( fluidPressure[i] + fluidPressure[n] ) / fluidNeighbors[i].m_distances[j];
		btScalar dterm = c * fluidInvDensity[i] * fluidInvDensity[n];
		
		//force += (distance * pterm + (fluidVelEval[n] - fluidVelEval[i]) * vterm) * dterm;
		force.x += ( pterm * distance.x + vterm * (fluidVelEval[n].x - fluidVelEval[i].x) ) * dterm;
		force.y += ( pterm * distance.y + vterm * (fluidVelEval[n].y - fluidVelEval[i].y) ) * dterm;
		force.z += ( pterm * distance.z + vterm * (fluidVelEval[n].z - fluidVelEval[i].z) ) * dterm;
	}
	
	fluidSphForce[i] = force;
}

inline void resolveAabbCollision(btScalar stiff, btScalar damp, btVector3 vel_eval,
							 	 btVector3 *acceleration, btVector3 normal, btScalar penetrationDepth)
{
	const btScalar COLLISION_EPSILON = 0.00001f;
	
	if(penetrationDepth > COLLISION_EPSILON)
	{
		btScalar adj = stiff * penetrationDepth - damp * btVector3_dot(normal, vel_eval);
		
		*acceleration += adj * normal;			
	}
}



__kernel void integrate(__global FluidParametersGlobal *FG, __global FluidParametersLocal *FL,
						__global btVector3 *fluidPosition, __global btVector3 *fluidVel, 
						__global btVector3 *fluidVelEval, __global btVector3 *fluidSphForce, 
						__global btVector3 *fluidExternalAcceleration)
{
	btScalar speedLimit = FG->m_speedLimit;
	btScalar speedLimitSquared = speedLimit*speedLimit;
	
	btScalar stiff = FL->m_boundaryStiff;
	btScalar damp = FL->m_boundaryDamp;
	btScalar R2 = 2.0f * FG->m_particleRadius;
	btScalar ss = FG->m_simulationScale;
	
	btVector3 min = FL->m_volumeMin;
	btVector3 max = FL->m_volumeMax;
	
	bool planeGravityEnabled = ( FG->m_planeGravity.x != 0.0f 
							  || FG->m_planeGravity.y != 0.0f 
							  || FG->m_planeGravity.z != 0.0f );
	
	int i = get_global_id(0);
	
	//Compute Acceleration		
	btVector3 accel = fluidSphForce[i];
	accel *= FL->m_particleMass;

	//Limit speed
	btScalar speedSquared = btVector3_length2(accel);
	if(speedSquared > speedLimitSquared) accel *= speedLimit / sqrt(speedSquared);

	//Apply acceleration to keep particles in the FluidSystem's AABB
	resolveAabbCollision( stiff, damp, fluidVelEval[i], &accel, (btVector3){1.0f, 0.0f, 0.0f, 0.0f}, R2 - (fluidPosition[i].x - min.x)*ss );
	resolveAabbCollision( stiff, damp, fluidVelEval[i], &accel, (btVector3){-1.0f, 0.0f, 0.0f, 0.0f}, R2 - (max.x - fluidPosition[i].x)*ss );
	resolveAabbCollision( stiff, damp, fluidVelEval[i], &accel, (btVector3){0.0f, 1.0f, 0.0f, 0.0f}, R2 - (fluidPosition[i].y - min.y)*ss );
	resolveAabbCollision( stiff, damp, fluidVelEval[i], &accel, (btVector3){0.0f, -1.0f, 0.0f, 0.0f}, R2 - (max.y - fluidPosition[i].y)*ss );
	resolveAabbCollision( stiff, damp, fluidVelEval[i], &accel, (btVector3){0.0f, 0.0f, 1.0f, 0.0f}, R2 - (fluidPosition[i].z - min.z)*ss );
	resolveAabbCollision( stiff, damp, fluidVelEval[i], &accel, (btVector3){0.0f, 0.0f, -1.0f, 0.0f}, R2 - (max.z - fluidPosition[i].z)*ss );
	
	//Plane gravity
	if(planeGravityEnabled) accel += FG->m_planeGravity;

	//Point gravity
	if(FG->m_pointGravity > 0.0f) 
	{
		btVector3 norm = fluidPosition[i] - FG->m_pointGravityPosition;
		norm = btVector3_normalize(norm);
		norm *= FG->m_pointGravity;
		accel -= norm;
	}
	
	//Apply external forces
	accel += fluidExternalAcceleration[i];
	fluidExternalAcceleration[i] = (btVector3){0.0f, 0.0f, 0.0f, 0.0f};

	//Leapfrog Integration
	btVector3 vnext = fluidVel[i] + accel * FG->m_timeStep;		//v(t+1/2) = v(t-1/2) + a(t) dt	
	fluidVelEval[i] = (fluidVel[i] + vnext) * 0.5f;				//v(t+1) = [v(t-1/2) + v(t+1/2)] * 0.5		used to compute forces later
	fluidVel[i] = vnext;
			
	fluidPosition[i] += vnext * (FG->m_timeStep / ss);			//p(t+1) = p(t) + v(t+1/2) dt
}

