/** SPHInterface.cpp
	Copyright (C) 2012 Jackson Lee

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

#include "SPHInterface.h"

void getAabb(const FluidSystem &FS, btVector3 *out_min, btVector3 *out_max)
{
	btVector3 min(BT_LARGE_FLOAT, BT_LARGE_FLOAT, BT_LARGE_FLOAT);
	btVector3 max(-BT_LARGE_FLOAT, -BT_LARGE_FLOAT, -BT_LARGE_FLOAT);
	
	for(int i = 0; i < FS.numParticles(); ++i)
	{
		const btVector3 &pos = FS.getPosition(i);
		if( pos.x() < min.x() ) min.setX( pos.x() );
		if( pos.y() < min.y() ) min.setY( pos.y() );
		if( pos.z() < min.z() ) min.setZ( pos.z() );
		
		if( pos.x() > max.x() ) max.setX( pos.x() );
		if( pos.y() > max.y() ) max.setY( pos.y() );
		if( pos.z() > max.z() ) max.setZ( pos.z() );
	}
	
	*out_min = min;
	*out_max = max;
}

void BulletFluidsInterface::collideFluidsWithBullet(FluidSystem *fluidSystem, btCollisionWorld *world)
{
	BT_PROFILE("BulletFluidsInterface::collideFluidsWithBullet()");
	
	//sph_pradius is at simscale; divide by simscale to transform into world scale
	btSphereShape particleShape( fluidSystem->getParameters().sph_pradius / fluidSystem->getParameters().sph_simscale );
	
	btCollisionObject particleObject;
	particleObject.setCollisionShape(&particleShape);
		
	btTransform &particleTransform = particleObject.getWorldTransform();
	particleTransform.setRotation( btQuaternion::getIdentity() );
		
	for(int i = 0; i < fluidSystem->numParticles(); ++i)
	{
		particleTransform.setOrigin( fluidSystem->getPosition(i) );
			
		ParticleResultMulti result(&particleObject);
		world->contactTest(&particleObject, result);
			
		for(int n = 0; n < result.m_numCollisions; ++n)
		{
			resolveCollision(fluidSystem, i, result.m_collidedWith[n], 
							 result.m_normals[n], result.m_collidedWithHitPointsWorld[n], result.m_distances[n]);
		}
	}
}

void BulletFluidsInterface::collideFluidsWithBullet2(FluidSystem *fluidSystem, btCollisionWorld *world)
{
	//Sample AABB from all fluid particles, detect intersecting 
	//broadphase proxies, and test each btCollisionObject
	//against all fluid grid cells intersecting with the btCollisionObject's AABB.
	
	BT_PROFILE("BulletFluidsInterface::collideFluidsWithBullet2()");

	btVector3 fluidSystemMin, fluidSystemMax;
	getAabb(*fluidSystem, &fluidSystemMin, &fluidSystemMax);
	
	//sph_pradius is at simscale; divide by simscale to transform into world scale
	float particleRadius = fluidSystem->getParameters().sph_pradius / fluidSystem->getParameters().sph_simscale;
	btSphereShape particleShape(particleRadius);
	
	btCollisionObject particleObject;
	particleObject.setCollisionShape(&particleShape);
					
	btTransform &particleTransform = particleObject.getWorldTransform();
	particleTransform.setRotation( btQuaternion::getIdentity() );
	
	//
	const Grid &G = fluidSystem->getGrid();
	const GridParameters &GP = G.getParameters();
	
	AabbTestCallback aabbTest;
	world->getBroadphase()->aabbTest(fluidSystemMin, fluidSystemMax, aabbTest);
	
	for(int i = 0; i < aabbTest.m_results.size(); ++i)
	{
		btCollisionObject *const object = aabbTest.m_results[i];
	
		const btVector3 &objectMin = object->getBroadphaseHandle()->m_aabbMin;
		const btVector3 &objectMax = object->getBroadphaseHandle()->m_aabbMax;
		
		int gridMinX, gridMinY, gridMinZ;
		int gridMaxX, gridMaxY, gridMaxZ;
		G.getIndicies(objectMin, &gridMinX, &gridMinY, &gridMinZ);
		G.getIndicies(objectMax, &gridMaxX, &gridMaxY, &gridMaxZ);
		
		for(int z = gridMinZ; z <= gridMaxZ; ++z)
			for(int y = gridMinY; y <= gridMaxY; ++y)
				for(int x = gridMinX; x <= gridMaxX; ++x)
				{
					int lastIndex = G.getLastParticleIndex( (z*GP.m_resolutionY + y)*GP.m_resolutionX + x );
					for( int n = lastIndex; n != INVALID_PARTICLE_INDEX; n = fluidSystem->getNextIndex(n) )
					{
						const btVector3 &fluidPos = fluidSystem->getPosition(n);
						
						btVector3 fluidMin( fluidPos.x() - particleRadius, fluidPos.y() - particleRadius, fluidPos.z() - particleRadius );
						btVector3 fluidMax( fluidPos.x() + particleRadius, fluidPos.y() + particleRadius, fluidPos.z() + particleRadius );
						
						if( fluidMin.x() <= objectMax.x() && objectMin.x() <= fluidMax.x()  
						 && fluidMin.y() <= objectMax.y() && objectMin.y() <= fluidMax.y()
						 && fluidMin.z() <= objectMax.z() && objectMin.z() <= fluidMax.z() )
						{
							particleTransform.setOrigin(fluidPos);
							
							ParticleResult result(&particleObject);
							world->contactPairTest(&particleObject, const_cast<btCollisionObject*>(object), result);
							
							if( result.hasHit() ) 
							{
								resolveCollision(fluidSystem, n, result.m_collidedWith, 
												 result.m_normal, result.m_collidedWithHitPointWorld, result.m_distance);
							}
						}
					}
				}
	}
}

void BulletFluidsInterface::collideFluidsWithBulletCcd(FluidSystem *fluidSystem, btCollisionWorld *world)
{
	BT_PROFILE("BulletFluidsInterface::collideFluidsWithBulletCcd()");
	
	//from collideFluidsWithBullet()
	//sph_pradius is at simscale; divide by simscale to transform into world scale
	btSphereShape particleShape( fluidSystem->getParameters().sph_pradius / fluidSystem->getParameters().sph_simscale );
	
	btCollisionObject particleObject;
	particleObject.setCollisionShape(&particleShape);
		
	btTransform &particleTransform = particleObject.getWorldTransform();
	particleTransform.setRotation( btQuaternion::getIdentity() );
	//from collideFluidsWithBullet()
	
	//int numCollided = 0;
	for(int i = 0; i < fluidSystem->numParticles(); ++i)
	{
		const btVector3& pos = fluidSystem->getPosition(i);
		const btVector3& prev_pos = fluidSystem->getPrevPosition(i);
		
		//	investigate:
		//	Calling btCollisionWorld::rayTest() with the ray's origin inside a btCollisionObject
		//	reports no collisions; is this expected behavior when using btCollisionWorld::ClosestRayResultCallback?
		//btCollisionWorld::ClosestRayResultCallback result( btVector3(0.f, -1.0f, 0.f), btVector3(0.f, -2.0f, 0.f) );
		btCollisionWorld::ClosestRayResultCallback result(prev_pos, pos);
		result.m_collisionFilterGroup = btBroadphaseProxy::DefaultFilter;
		result.m_collisionFilterMask = btBroadphaseProxy::AllFilter;
		
		world->rayTest(result.m_rayFromWorld, result.m_rayToWorld, result);
		
		//printf( "result.m_closestHitFraction, result.hasHit(): %f, %d \n", result.m_closestHitFraction, result.hasHit() );
		
		if( result.hasHit() )
		{
			float distanceMoved = result.m_rayFromWorld.distance(result.m_rayToWorld);
			float distanceCollided = result.m_rayFromWorld.distance(result.m_hitPointWorld);
	
			float distance = distanceMoved - distanceCollided;
		
			//++numCollided;
			resolveCollision(fluidSystem, i, result.m_collisionObject, result.m_hitNormalWorld, result.m_hitPointWorld, distance);
		}
		else
		{
			//Since btCollisionWorld::rayTest() does not report a collision if the ray's origin
			//is inside a btCollsionObject, use btCollisionWorld::contactTest() in case
			//the particle is inside.
			
			//from collideFluidsWithBullet()
			particleTransform.setOrigin(pos);
				
			ParticleResultMulti result(&particleObject);
			world->contactTest(&particleObject, result);
				
			for(int n = 0; n < result.m_numCollisions; ++n)
				resolveCollision(fluidSystem, i, result.m_collidedWith[n], 
								 result.m_normals[n], result.m_collidedWithHitPointsWorld[n], result.m_distances[n]);
			//from collideFluidsWithBullet()
		}
	}
	
	//printf("numCollided: %d \n", numCollided);
}	


void BulletFluidsInterface::resolveCollision(FluidSystem *FS, int fluidIndex, btCollisionObject *object, 
											 const btVector3 &fluidNormal, const btVector3 &hitPointWorld, float distance)
{
	const float COLLISION_EPSILON = 0.00001f;

	const FluidParameters &FP = FS->getParameters();
	
	float depthOfPenetration = abs(distance)*FP.sph_simscale;
	if(depthOfPenetration > COLLISION_EPSILON)
	{
		float adj = FP.sph_extstiff * depthOfPenetration - FP.sph_extdamp * fluidNormal.dot( FS->getEvalVelocity(fluidIndex) );
		
		btVector3 acceleration = fluidNormal;
		acceleration *= adj;
			
		//if acceleration is very high, the fluid simulation will explode
		FS->applyAcceleration(fluidIndex, acceleration);
		
		//btRigidBody-Fluid interaction
		//	test
		btRigidBody *rigidBody = btRigidBody::upcast(object);
		if( rigidBody && rigidBody->getInvMass() != 0.0f )
		{
			const float fluidMass = FP.sph_pmass;
			const float invertedMass = rigidBody->getInvMass();
			
			//Rigid body to particle
			//const btVector3& linearVelocity = rigidBody->getLinearVelocity();
			
			//Particle to rigid body
			btVector3 force = -acceleration * fluidMass;
			rigidBody->applyForce( force, hitPointWorld - rigidBody->getWorldTransform().getOrigin() );
			rigidBody->activate(true);
		}
	}
}

void BulletFluidsInterface_P::getDynamicRigidBodies(FluidSystem *fluidSystem, btDynamicsWorld *world, 
													btAlignedObjectArray<btRigidBody*> *out_rigidBodies)
{
	btVector3 fluidSystemMin, fluidSystemMax;
	getAabb(*fluidSystem, &fluidSystemMin, &fluidSystemMax);
	
	AabbTestCallback aabbTest;
	world->getBroadphase()->aabbTest(fluidSystemMin, fluidSystemMax, aabbTest);
	for(int i = 0; i < aabbTest.m_results.size(); ++i)
	{
		//btRigidBody::setMassProps() sets inverse mass to 0.0 if(mass == 0.0)
		btRigidBody *rigidBody = btRigidBody::upcast(aabbTest.m_results[i]);
		if(rigidBody && rigidBody->getInvMass() != 0.0) out_rigidBodies->push_back(rigidBody);
	}
}

void BulletFluidsInterface_P::convertIntoParticles( btCollisionWorld *world, btCollisionObject *object, 
													btScalar particleRadius, ParticlesShape *out_shape )
{
	btVector3 min, max;
	object->getCollisionShape()->getAabb( btTransform::getIdentity(), min, max );
	
	//
	btVector3 extents = max - min;
	//int numCellsX = static_cast<int>( ceil( extents.x() / particleRadius ) );
	//int numCellsY = static_cast<int>( ceil( extents.y() / particleRadius ) );
	//int numCellsZ = static_cast<int>( ceil( extents.z() / particleRadius ) );
	int numCellsX = static_cast<int>( extents.x() / particleRadius );
	int numCellsY = static_cast<int>( extents.y() / particleRadius );
	int numCellsZ = static_cast<int>( extents.z() / particleRadius );
	numCellsX = (numCellsX > 0) ? numCellsX : 1;
	numCellsY = (numCellsY > 0) ? numCellsY : 1;
	numCellsZ = (numCellsZ > 0) ? numCellsZ : 1;
	
	//btSphereShape cellShape(particleRadius);
	btBoxShape cellShape( btVector3(particleRadius, particleRadius, particleRadius) );
	
	btCollisionObject cellObject;
	cellObject.setCollisionShape(&cellShape);
	cellObject.setWorldTransform( btTransform::getIdentity() );
	btTransform &cellTransform = cellObject.getWorldTransform();
	
	btCollisionObject objectWithDefaultTransform = *object;
	objectWithDefaultTransform.setWorldTransform( btTransform::getIdentity() );
	
	//
	btScalar cellSpacing = particleRadius * 2.0;
	
	//lowest(x,y,z) point representing the btCollisionShape as particles
	btVector3 firstParticle( static_cast<btScalar>(numCellsX - 1) * cellSpacing * -0.5, 
						 	 static_cast<btScalar>(numCellsY - 1) * cellSpacing * -0.5, 
							 static_cast<btScalar>(numCellsZ - 1) * cellSpacing * -0.5 );
	
	for(int z = 0; z < numCellsZ; ++z)
	for(int y = 0; y < numCellsY; ++y)
	for(int x = 0; x < numCellsX; ++x)
	{
		btVector3 cellCenter( firstParticle.x() + static_cast<btScalar>(x) * cellSpacing,
							  firstParticle.y() + static_cast<btScalar>(y) * cellSpacing,
							  firstParticle.z() + static_cast<btScalar>(z) * cellSpacing );
		cellTransform.setOrigin(cellCenter);
	
		ParticleResult result(&cellObject);
		world->contactPairTest(&objectWithDefaultTransform, &cellObject, result);
	
		if( result.hasHit() ) out_shape->m_points.push_back(cellCenter);
	}
	
	out_shape->m_particleRadius = particleRadius;
	
	btRigidBody *rigidBody = btRigidBody::upcast(object);
	
	if( rigidBody && out_shape->m_points.size() )
		out_shape->m_particleMass = ( 1.0 / rigidBody->getInvMass() ) / static_cast<btScalar>( out_shape->m_points.size() );
	else 
		out_shape->m_particleMass = 0.0;
}


void BulletFluidsInterface_P::runFluidSimulation( FluidSystem *fluidSystem, btDynamicsWorld *world, float secondsElapsed,  
												  btAlignedObjectArray<ParticlesShape> *particles,
												  btAlignedObjectArray<btRigidBody*> *rigidBodies )
{
	const FluidParameters &FP = fluidSystem->getParameters();
	const Grid &G = fluidSystem->getGrid();
	const GridParameters &GP = G.getParameters();

	const float stiff = FP.sph_extstiff;
	const float damp = FP.sph_extdamp;
	const float radius = FP.sph_pradius;
	const float simScale = FP.sph_simscale;
	
	const float COLLISION_EPSILON = 0.00001f;
	
	//	Currently, particles may get stuck inside btRigidBody(s)
	//	as there are gaps between rigid body particles;
	//	extend the radius of rigid bodies in order to mitigate this
	const float RIGIDBODY_MARGIN = 1.0;
	

	fluidSystem->stepSimulation();

	btAlignedObjectArray<ParticlesShape> &collidedParticles = *particles;
	btAlignedObjectArray<btRigidBody*> &dynamicRigidBodies = *rigidBodies;
	for(int i = 0; i < collidedParticles.size(); ++i)
	{
		const float rigidParticleRadius = (collidedParticles[i].m_particleRadius + RIGIDBODY_MARGIN) * simScale;
	
		const btVector3 &objectMin = dynamicRigidBodies[i]->getBroadphaseHandle()->m_aabbMin;
		const btVector3 &objectMax = dynamicRigidBodies[i]->getBroadphaseHandle()->m_aabbMax;
		
		int gridMinX, gridMinY, gridMinZ;
		int gridMaxX, gridMaxY, gridMaxZ;
		G.getIndicies(objectMin, &gridMinX, &gridMinY, &gridMinZ);
		G.getIndicies(objectMax, &gridMaxX, &gridMaxY, &gridMaxZ);

		for(int z = gridMinZ; z <= gridMaxZ; ++z)
			for(int y = gridMinY; y <= gridMaxY; ++y)
				for(int x = gridMinX; x <= gridMaxX; ++x)
				{
					int lastIndex = G.getLastParticleIndex( (z*GP.m_resolutionY + y)*GP.m_resolutionX + x );
					for( int n = lastIndex; n != INVALID_PARTICLE_INDEX; n = fluidSystem->getNextIndex(n) )
					{
						const btVector3 &pos = fluidSystem->getPosition(n);
						const btVector3 &vel_eval = fluidSystem->getEvalVelocity(n);
						
						for(int n = 0; n < collidedParticles[i].m_points.size(); ++n)
						{
							float distance = collidedParticles[i].m_points[n].distance(pos) * simScale;
							float depthOfPenetration = (rigidParticleRadius + radius) - distance;
							
							if(depthOfPenetration > COLLISION_EPSILON)
							{
								//Move the fluid particle
								btVector3 fluidNormal = (pos - collidedParticles[i].m_points[n]).normalized();
								float adj = stiff * depthOfPenetration - damp * fluidNormal.dot(vel_eval);
								
								const float SCALE = 1.0f;
								btVector3 acceleration( adj * fluidNormal.x() * SCALE,
														adj * fluidNormal.y() * SCALE,
														adj * fluidNormal.z() * SCALE );
									
								//if acceleration is very high, the fluid simulation will explode
								fluidSystem->applyAcceleration(n, acceleration);
								
								//Move the rigid body particle
								//	test
								btVector3 rigidNormal = fluidNormal * -1.0;
								collidedParticles[i].m_points[n] += rigidNormal * depthOfPenetration;
							}
							
						}
					}
				}
	}
}

