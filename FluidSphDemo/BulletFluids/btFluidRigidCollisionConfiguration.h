/*
Bullet-FLUIDS 
Copyright (c) 2012 Jackson Lee

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, 
including commercial applications, and to alter it and redistribute it freely, 
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. 
   If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/
#ifndef BT_FLUID_RIGID_COLLISION_CONFIGURATION_H
#define BT_FLUID_RIGID_COLLISION_CONFIGURATION_H

#include "LinearMath/btPoolAllocator.h"
#include "BulletCollision/CollisionDispatch/btDefaultCollisionConfiguration.h"

#include "Sph/btFluidSphRigidCollisionAlgorithm.h"


///Includes btFluidSph collision support on top of btDefaultCollisionConfiguration.
class btFluidRigidCollisionConfiguration : public btDefaultCollisionConfiguration
{
	btCollisionAlgorithmCreateFunc*	m_fluidRigidCreateFunc;
	btCollisionAlgorithmCreateFunc*	m_fluidRigidCreateFuncSwapped;

public:

	btFluidRigidCollisionConfiguration( const btDefaultCollisionConstructionInfo& constructionInfo = btDefaultCollisionConstructionInfo() )
	: btDefaultCollisionConfiguration(constructionInfo)
	{
		void* ptr;

		ptr = btAlignedAlloc( sizeof(btFluidSphRigidCollisionAlgorithm::CreateFunc), 16 );
		m_fluidRigidCreateFunc = new(ptr) btFluidSphRigidCollisionAlgorithm::CreateFunc;
		
		ptr = btAlignedAlloc( sizeof(btFluidSphRigidCollisionAlgorithm::CreateFunc), 16 );
		m_fluidRigidCreateFuncSwapped = new(ptr) btFluidSphRigidCollisionAlgorithm::CreateFunc;
		m_fluidRigidCreateFuncSwapped->m_swapped = true;
		
		//Collision algorithms introducted by btFluidRigidCollisionConfiguration may be
		//larger than m_collisionAlgorithmPool's element size. Resize if it is not large enough.
		int maxAlgorithmSize = sizeof(btFluidSphRigidCollisionAlgorithm);
		if( m_ownsCollisionAlgorithmPool && m_collisionAlgorithmPool && maxAlgorithmSize > m_collisionAlgorithmPool->getElementSize() )
		{
			m_collisionAlgorithmPool->~btPoolAllocator();
			btAlignedFree(m_collisionAlgorithmPool);
			
			ptr = btAlignedAlloc( sizeof(btPoolAllocator), 16 );
			m_collisionAlgorithmPool = new(ptr) btPoolAllocator(maxAlgorithmSize, constructionInfo.m_defaultMaxCollisionAlgorithmPoolSize);
		}
	}

	virtual ~btFluidRigidCollisionConfiguration()
	{
		m_fluidRigidCreateFunc->~btCollisionAlgorithmCreateFunc();
		btAlignedFree(m_fluidRigidCreateFunc);

		m_fluidRigidCreateFuncSwapped->~btCollisionAlgorithmCreateFunc();
		btAlignedFree(m_fluidRigidCreateFuncSwapped);
	}

	virtual btCollisionAlgorithmCreateFunc* getCollisionAlgorithmCreateFunc(int proxyType0, int proxyType1)
	{
		//	btFluidSph-btSoftBody interaction is not implemented
		//	temporarily use SOFTBODY_SHAPE_PROXYTYPE (replace later with FLUID_SPH_SHAPE_PROXYTYPE)
	
		bool collideProxyType1 = ( btBroadphaseProxy::isConvex(proxyType1)
									|| btBroadphaseProxy::isConcave(proxyType1)
									|| btBroadphaseProxy::isCompound(proxyType1) );
	
		if(proxyType0 == SOFTBODY_SHAPE_PROXYTYPE  && collideProxyType1) return m_fluidRigidCreateFunc;

		bool collideProxyType0 = ( btBroadphaseProxy::isConvex(proxyType0)
									|| btBroadphaseProxy::isConcave(proxyType0)
									|| btBroadphaseProxy::isCompound(proxyType0) );
		
		if(collideProxyType0 && proxyType1 == SOFTBODY_SHAPE_PROXYTYPE ) return m_fluidRigidCreateFuncSwapped;

		return btDefaultCollisionConfiguration::getCollisionAlgorithmCreateFunc(proxyType0, proxyType1);
	}

};
#endif