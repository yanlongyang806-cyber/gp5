/*----------------------------------------------------------------------
    This Software and Related Documentation are Proprietary to Ageia
    Technologies, Inc.

    Copyright 2007 Ageia Technologies, Inc. St. Louis, MO
    Unpublished -
    All Rights Reserved Under the Copyright Laws of the United States.

    Restricted Rights Legend:  Use, Duplication, or Disclosure by
    the Government is Subject to Restrictions as Set Forth in
    Paragraph (c)(1)(ii) of the Rights in Technical Data and
    Computer Software Clause at DFARS 252.227-7013.  Ageia
    Technologies Inc.
-----------------------------------------------------------------------*/

// This file is used to define a list of AgPerfMon events.
//
// This file is included exclusively by AgPerfMonEventSrcAPI.h
// and by AgPerfMonEventSrcAPI.cpp, for the purpose of building
// an enumeration (enum xx) and an array of strings ()
// that contain the list of events.
//
// This file should only contain event definitions, using the
// DEFINE_EVENT macro.  E.g.:
//
//     DEFINE_EVENT(sample_name_1)
//     DEFINE_EVENT(sample_name_2)
//     DEFINE_EVENT(sample_name_3)


// Statistics from the fluid mesh packet cooker
DEFINE_EVENT(meshCuisineNumUniqCell)
DEFINE_EVENT(meshCuisineNumUniqTri)
DEFINE_EVENT(meshCuisineNumBigCells54)
DEFINE_EVENT(meshCuisinePacketSizeBytes)
DEFINE_EVENT(statsSubsetOnly)

DEFINE_EVENT(NgPrSyncGPUClock)
DEFINE_EVENT(NgPrGatherGPUEvents)

DEFINE_EVENT(NgPrDeformableUberBatcherHostWork)
DEFINE_EVENT(NgPrDeformableUberBatcherCudaWork)

DEFINE_EVENT(NgPrDeformablePackParticleData)
DEFINE_EVENT(NgPrDeformableUnpackParticleData)

DEFINE_EVENT(NgPrDeformablePressure)

DEFINE_EVENT(NgPrDeformableUpdate)

DEFINE_EVENT(NgPrDeformableSelfCollision)

DEFINE_EVENT(NgPrDeformableCollision)
DEFINE_EVENT(NgPrDeformableCollisionAllocBuffers)
DEFINE_EVENT(NgPrDeformableCollisionAdjustPrimitiveOffsets)
DEFINE_EVENT(NgPrDeformableCollisionComputeTriangleAABBs)	
DEFINE_EVENT(NgPrDeformableCollisionAggregateData)
DEFINE_EVENT(NgPrDeformableCollisionSendParticleData)
DEFINE_EVENT(NgPrDeformableCollisionSendShapeData)
DEFINE_EVENT(NgPrDeformableCollisionSendTriangleData)
DEFINE_EVENT(NgPrDeformableCollisionSendConvexPlaneData)
DEFINE_EVENT(NgPrDeformableCollisionSendBlockControls)
DEFINE_EVENT(NgPrDeformableCollisionInitialization)
DEFINE_EVENT(NgPrDeformableCollisionDetectionPlane)
DEFINE_EVENT(NgPrDeformableCollisionDetectionBox)
DEFINE_EVENT(NgPrDeformableCollisionDetectionCapsule)
DEFINE_EVENT(NgPrDeformableCollisionDetectionConvex)
DEFINE_EVENT(NgPrDeformableCollisionDetectionMesh)
DEFINE_EVENT(NgPrDeformableCollisionDetectionAll)
DEFINE_EVENT(NgPrDeformableCollisionResponse)

DEFINE_EVENT(NgPrDeformableSolver)
DEFINE_EVENT(NgPrDeformableCooker)
DEFINE_EVENT(NgPrDeformableSolverInit)
DEFINE_EVENT(NgPrDeformableSolverIteration)
DEFINE_EVENT(NgPrDeformableReceiveParticleData)

DEFINE_EVENT(NgPrFluidUberBatcherHostWork)
DEFINE_EVENT(NgPrFluidUberBatcherCudaWork)

DEFINE_EVENT(NgPrFluidSphSortParticles)
DEFINE_EVENT(NgPrFluidSphCalcHashEvent)
DEFINE_EVENT(NgPrFluidSphRadixSort)
DEFINE_EVENT(NgPrFluidSphRadixSortStep)
DEFINE_EVENT(NgPrFluidSphReorder)
DEFINE_EVENT(NgPrFluidSphMemset)
DEFINE_EVENT(NgPrFluidSphFindCellstart)
DEFINE_EVENT(NgPrFluidSphCalcDensity)
DEFINE_EVENT(NgPrFluidSphCalcForce)

DEFINE_EVENT(NgPrFluidCollision)
DEFINE_EVENT(NgPrFluidCollisionAllocArrays)
DEFINE_EVENT(NgPrFluidCollisionFinalMemcpy)
DEFINE_EVENT(NgPrFluidCollisionShapeConverter)
DEFINE_EVENT(NgPrFluidCollisionDynamicShapes)
DEFINE_EVENT(NgPrFluidCollisionDynamicResponse)
DEFINE_EVENT(NgPrFluidCollisionStaticShapes)
DEFINE_EVENT(NgPrFluidCollisionTriangleWorkUnits)
DEFINE_EVENT(NgPrFluidCollisionTriangles)
DEFINE_EVENT(NgPrFluidCollisionStaticResponse)
DEFINE_EVENT(NgPrFluidCollisionFinalize)
DEFINE_EVENT(NgPrFluidCollisionUpdateShapePtrsConvex)
DEFINE_EVENT(NgPrFluidCollisionCreateShapeWorkUnits)
DEFINE_EVENT(NgPrFluidCollisionConstructArrays)
DEFINE_EVENT(NgPrFluidCollisionMemcpyParticles)
DEFINE_EVENT(NgPrFluidCollisionReorderToPackets)
DEFINE_EVENT(NgPrFluidCollisionMemcpyConstraints)
DEFINE_EVENT(NgPrFluidCollisionConstraints)
DEFINE_EVENT(NgPrFluidCollisionAllocWorkUnitArrays)

DEFINE_EVENT(NgPrFluidUpdateParticles)
DEFINE_EVENT(NgPrFluidCreatePacketHash)
DEFINE_EVENT(NgPrFluidRemoveParticles)
DEFINE_EVENT(NgPrFluidReorderToPackets)
DEFINE_EVENT(NgPrFluidCreateReorderIndexTable)
DEFINE_EVENT(NgPrFluidPackParticleData)
DEFINE_EVENT(NgPrFluidUnpackParticleData)

DEFINE_EVENT(NgPrFluidPipelineDeleteBatch)
DEFINE_EVENT(NgPrFluidPipelineCreateBatch)
DEFINE_EVENT(NgPrFluidPipelineIdle)

DEFINE_EVENT(NgPrUnitTestFrame)

DEFINE_EVENT(FluidParticleDemoFrameStart)
DEFINE_EVENT(FluidParticleDemoFrameStop)
DEFINE_EVENT(FluidParticleDemoFrame)
DEFINE_EVENT(FluidParticleDemoRunPhysics)
DEFINE_EVENT(FluidParticleDemoDisplayText)
DEFINE_EVENT(FluidParticleDemoRenderScene)
DEFINE_EVENT(FluidParticleDemoRenderTerrain)
DEFINE_EVENT(FluidParticleDemoRenderFluid)
DEFINE_EVENT(FluidParticleDemoRenderShadows)

DEFINE_EVENT(NgPrCpuHeapTotalSize)
DEFINE_EVENT(NgPrGpuHeapTotalSize)
DEFINE_EVENT(NgPrCpuHeapTotalAllocated)
DEFINE_EVENT(NgPrGpuHeapTotalAllocated)
DEFINE_EVENT(NgPrCpuHeapBiggestFreeBlock)
DEFINE_EVENT(NgPrGpuHeapBiggestFreeBlock)
DEFINE_EVENT(NgPrCpuHeapInternalFragmentation)
DEFINE_EVENT(NgPrGpuHeapInternalFragmentation)
DEFINE_EVENT(NgPrCpuHeapNumEntries)
DEFINE_EVENT(NgPrGpuHeapNumEntries)
