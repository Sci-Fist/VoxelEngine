# FirstVoxel Debugging and Troubleshooting Guide

This guide provides comprehensive debugging strategies, troubleshooting techniques, and diagnostic tools for the FirstVoxel voxel engine.

## Debugging Overview

The FirstVoxel engine includes extensive debugging capabilities to help developers identify and resolve issues quickly. This guide covers debugging techniques for all major systems including mesh generation, water simulation, biome generation, and performance optimization.

## Debug Tools and Utilities

### 1. Voxel Debug Manager

The central debugging interface for the engine:

```cpp
class VoxelDebugManager
{
public:
    // Enable/disable debug visualization
    void SetDebugMode(EVoxelDebugMode Mode, bool Enabled);
    
    // Log generation statistics
    void LogGenerationStats();
    
    // Visualize chunk boundaries
    void DrawChunkBoundaries(UWorld* World);
    
    // Profile generation performance
    void ProfileGeneration(const FIntVector& ChunkCoord);
    
    // Validate chunk data integrity
    void ValidateChunkData(TSharedPtr<AVoxelChunk> Chunk);
};
```

### 2. Debug Visualization Modes

**Chunk Debug Visualization:**
```cpp
enum class EVoxelDebugMode
{
    None,           // No debug visualization
    ChunkBounds,    // Show chunk boundaries
    GenerationState,// Show generation progress
    MeshQuality,    // Show mesh quality metrics
    WaterFlow,      // Show water flow paths
    BiomeWeights,   // Show biome weight distribution
    Performance     // Show performance metrics
};
```

**Mesh Debug Visualization:**
```cpp
class VoxelMeshDebugger
{
public:
    // Visualize mesh normals
    void DrawMeshNormals(UWorld* World, const FVoxelMeshOutput& Mesh);
    
    // Highlight mesh errors
    void HighlightMeshErrors(UWorld* World, const FVoxelMeshOutput& Mesh);
    
    // Show mesh section boundaries
    void DrawMeshSections(UWorld* World, const FVoxelMeshOutput& Mesh);
    
    // Visualize mesh density
    void DrawMeshDensity(UWorld* World, const FVoxelMeshOutput& Mesh);
};
```

### 3. Performance Profiling Tools

**Generation Profiler:**
```cpp
class VoxelGenerationProfiler
{
public:
    struct GenerationStats
    {
        float TotalTime;
        float DensityGenerationTime;
        float MeshGenerationTime;
        float FoliagePlacementTime;
        float WaterSourceTime;
        int32 ChunkCount;
        int32 VertexCount;
        int32 TriangleCount;
    };
    
    void StartProfiling();
    void StopProfiling();
    GenerationStats GetStats() const;
    void LogStats() const;
};
```

**Memory Profiler:**
```cpp
class VoxelMemoryProfiler
{
public:
    struct MemoryStats
    {
        SIZE_T TotalMemory;
        SIZE_T ChunkMemory;
        SIZE_T MeshMemory;
        SIZE_T WaterMemory;
        SIZE_T BiomeMemory;
        int32 ActiveChunks;
        int32 PooledChunks;
    };
    
    void UpdateMemoryStats();
    MemoryStats GetMemoryStats() const;
    void LogMemoryUsage() const;
};
```

## Common Issues and Solutions

### 1. Mesh Generation Issues

**Issue: Holes in generated mesh**
```cpp
// Problem: Surface Nets algorithm missing edge cases
// Solution: Check density field completeness and edge table

void DebugMeshHoles(TArray<float>& Densities, int32 ChunkSize)
{
    // Verify density field has proper padding
    check(Densities.Num() == (ChunkSize + 3) * (ChunkSize + 3) * (ChunkSize + 3));
    
    // Check for incomplete edge cases
    for (int32 z = 1; z <= ChunkSize + 1; ++z)
    for (int32 y = 1; y <= ChunkSize + 1; ++y)
    for (int32 x = 1; x <= ChunkSize + 1; ++x)
    {
        int32 cubeIndex = CalculateCubeIndex(Densities, x, y, z, ChunkSize + 3);
        if (cubeIndex != 0 && cubeIndex != 255)
        {
            // Verify edge interpolation
            FVector vertex = InterpolateVertex(Densities, x, y, z, ChunkSize + 3);
            check(vertex != FVector::ZeroVector);
        }
    }
}
```

**Issue: Incorrect mesh normals**
```cpp
// Problem: Gradient calculation errors
// Solution: Verify gradient computation and normalization

void DebugMeshNormals(const TArray<float>& Densities, int32 ChunkSize)
{
    for (int32 z = 1; z <= ChunkSize + 1; ++z)
    for (int32 y = 1; y <= ChunkSize + 1; ++y)
    for (int32 x = 1; x <= ChunkSize + 1; ++x)
    {
        FVector normal = FVoxelMeshGenerator::ComputeNormal(Densities, x, y, z, ChunkSize);
        
        // Verify normal is normalized
        check(FMath::IsNearlyEqual(normal.Size(), 1.0f, 0.01f));
        
        // Verify normal points out of solid
        float density = Densities[Idx(x, y, z, ChunkSize + 3)];
        if (density > 0.0f)
        {
            // Solid region - normal should point out
            FVector gradient = ComputeGradient(Densities, x, y, z, ChunkSize + 3);
            check((gradient | normal) > 0.0f);
        }
    }
}
```

### 2. Water Simulation Issues

**Issue: Water not flowing correctly**
```cpp
// Problem: Incorrect gravity or boundary handling
// Solution: Debug water flow logic

void DebugWaterFlow(const FVoxelWaterSimulator& Simulator, const FIntVector& ChunkCoord)
{
    const auto& chunkData = Simulator.GetChunkData(ChunkCoord);
    
    // Check water source placement
    for (const auto& source : chunkData->WaterSources)
    {
        check(Simulator.IsWater(source));
        check(Simulator.GetLevel(source) == WATER_FULL);
    }
    
    // Verify gravity flow
    for (int32 z = 1; z < chunkData->ChunkSize; ++z)
    for (int32 y = 0; y < chunkData->ChunkSize; ++y)
    for (int32 x = 0; x < chunkData->ChunkSize; ++x)
    {
        FIntVector current(x, y, z);
        FIntVector below(x, y, z - 1);
        
        if (Simulator.IsWater(current) && !Simulator.IsSolid(below))
        {
            uint8 currentLevel = Simulator.GetLevel(current);
            uint8 belowLevel = Simulator.GetLevel(below);
            
            // Water should flow down if space available
            if (belowLevel < WATER_FULL)
            {
                check(currentLevel > 0);
            }
        }
    }
}
```

**Issue: Water leaking through terrain**
```cpp
// Problem: Incorrect solid cell detection
// Solution: Verify solid cell mapping

void DebugWaterLeakage(const FVoxelWaterSimulator& Simulator, const FIntVector& ChunkCoord)
{
    const auto& chunkData = Simulator.GetChunkData(ChunkCoord);
    
    // Check solid cell consistency
    for (int32 z = 0; z < chunkData->ChunkSize; ++z)
    for (int32 y = 0; y < chunkData->ChunkSize; ++y)
    for (int32 x = 0; x < chunkData->ChunkSize; ++x)
    {
        FIntVector voxel(x, y, z);
        
        if (chunkData->SolidCells[x + y * chunkData->ChunkSize + z * chunkData->ChunkSize * chunkData->ChunkSize])
        {
            // Solid cell should block water
            check(!Simulator.IsWater(voxel));
        }
    }
}
```

### 3. Biome Generation Issues

**Issue: Incorrect biome transitions**
```cpp
// Problem: Noise function or weight calculation errors
// Solution: Debug biome weight computation

void DebugBiomeTransitions(const FVoxelBiomeManager& BiomeManager, const FVector& WorldPos)
{
    FVoxelBiomeWeightMap weights = BiomeManager.GetBiomeWeights(WorldPos);
    
    // Verify weight normalization
    float totalWeight = 0.0f;
    for (int32 i = 0; i < FVoxelBiomeWeightMap::MaxBiomes; ++i)
    {
        totalWeight += weights[i];
        check(weights[i] >= 0.0f && weights[i] <= 1.0f);
    }
    check(FMath::IsNearlyEqual(totalWeight, 1.0f, 0.01f));
    
    // Check dominant biome
    int32 dominantBiome = weights.GetDominantBiome();
    check(dominantBiome >= 0 && dominantBiome < FVoxelBiomeWeightMap::MaxBiomes);
}
```

**Issue: Foliage not spawning in correct biomes**
```cpp
// Problem: Foliage configuration or placement errors
// Solution: Debug foliage placement logic

void DebugFoliagePlacement(const FVoxelGeneratorTask& Task, const FIntVector& ChunkCoord)
{
    const auto& foliageTransforms = Task.GetPerFoliageTransforms();
    const auto& biomeWeights = Task.GetColumnWeights();
    
    // Verify foliage transforms match biome configuration
    for (int32 slot = 0; slot < foliageTransforms.Num(); ++slot)
    {
        const auto& transforms = foliageTransforms[slot];
        const auto& mesh = Task.GetPerFoliageMeshes()[slot];
        
        if (mesh != nullptr && transforms.Num() > 0)
        {
            // Check if transforms are reasonable
            for (const auto& transform : transforms)
            {
                check(transform.GetLocation().Z > 0.0f); // Above ground
                check(transform.GetScale3D().Size() > 0.0f); // Valid scale
            }
        }
    }
}
```

### 4. Performance Issues

**Issue: Slow generation times**
```cpp
// Problem: Inefficient algorithms or excessive computation
// Solution: Profile and optimize generation pipeline

void DebugGenerationPerformance(FVoxelGenerationProfiler& Profiler)
{
    Profiler.StartProfiling();
    
    // Profile each generation stage
    auto start = FPlatformTime::Seconds();
    // Density generation
    auto densityTime = FPlatformTime::Seconds() - start;
    
    start = FPlatformTime::Seconds();
    // Mesh generation
    auto meshTime = FPlatformTime::Seconds() - start;
    
    start = FPlatformTime::Seconds();
    // Foliage placement
    auto foliageTime = FPlatformTime::Seconds() - start;
    
    start = FPlatformTime::Seconds();
    // Water source detection
    auto waterTime = FPlatformTime::Seconds() - start;
    
    Profiler.StopProfiling();
    
    // Log performance metrics
    UE_LOG(LogTemp, Warning, TEXT("Generation Performance:"));
    UE_LOG(LogTemp, Warning, TEXT("  Density: %.2fms"), densityTime * 1000.0f);
    UE_LOG(LogTemp, Warning, TEXT("  Mesh: %.2fms"), meshTime * 1000.0f);
    UE_LOG(LogTemp, Warning, TEXT("  Foliage: %.2fms"), foliageTime * 1000.0f);
    UE_LOG(LogTemp, Warning, TEXT("  Water: %.2fms"), waterTime * 1000.0f);
}
```

**Issue: High memory usage**
```cpp
// Problem: Memory leaks or inefficient data structures
// Solution: Monitor and optimize memory usage

void DebugMemoryUsage(FVoxelMemoryProfiler& Profiler)
{
    Profiler.UpdateMemoryStats();
    auto stats = Profiler.GetMemoryStats();
    
    // Log memory usage
    UE_LOG(LogTemp, Warning, TEXT("Memory Usage:"));
    UE_LOG(LogTemp, Warning, TEXT("  Total: %.2fMB"), stats.TotalMemory / (1024.0f * 1024.0f));
    UE_LOG(LogTemp, Warning, TEXT("  Chunks: %.2fMB"), stats.ChunkMemory / (1024.0f * 1024.0f));
    UE_LOG(LogTemp, Warning, TEXT("  Mesh: %.2fMB"), stats.MeshMemory / (1024.0f * 1024.0f));
    UE_LOG(LogTemp, Warning, TEXT("  Active Chunks: %d"), stats.ActiveChunks);
    UE_LOG(LogTemp, Warning, TEXT("  Pooled Chunks: %d"), stats.PooledChunks);
    
    // Check for memory leaks
    if (stats.ActiveChunks > 1000)
    {
        UE_LOG(LogTemp, Error, TEXT("High number of active chunks - possible memory leak"));
    }
}
```

## Diagnostic Commands

### 1. Console Commands

**Generation Debug Commands:**
```cpp
// Enable generation debugging
ConsoleCommand("voxel.debug.generation 1");

// Show generation statistics
ConsoleCommand("voxel.debug.stats");

// Profile generation performance
ConsoleCommand("voxel.debug.profile");

// Validate chunk data
ConsoleCommand("voxel.debug.validate");
```

**Visual Debug Commands:**
```cpp
// Show chunk boundaries
ConsoleCommand("voxel.debug.chunks 1");

// Show mesh quality
ConsoleCommand("voxel.debug.mesh 1");

// Show water flow
ConsoleCommand("voxel.debug.water 1");

// Show biome weights
ConsoleCommand("voxel.debug.biomes 1");
```

### 2. Debug Functions

**Runtime Debug Functions:**
```cpp
// Enable debug mode
void EnableDebugMode(EVoxelDebugMode Mode)
{
    VoxelDebugManager::Get().SetDebugMode(Mode, true);
}

// Log generation statistics
void LogGenerationStats()
{
    VoxelDebugManager::Get().LogGenerationStats();
}

// Draw debug visualization
void DrawDebugVisualization(UWorld* World)
{
    VoxelDebugManager::Get().DrawChunkBoundaries(World);
}
```

## Error Handling and Recovery

### 1. Graceful Error Handling

**Generation Error Recovery:**
```cpp
class VoxelErrorRecovery
{
public:
    static bool RecoverFromGenerationError(const VoxelGenerationError& Error)
    {
        switch (Error.Type)
        {
            case VoxelGenerationError::OutOfMemory:
                return HandleOutOfMemoryError(Error);
                
            case VoxelGenerationError::InvalidConfiguration:
                return HandleInvalidConfigError(Error);
                
            case VoxelGenerationError::GenerationTimeout:
                return HandleTimeoutError(Error);
                
            default:
                return false;
        }
    }
    
private:
    static bool HandleOutOfMemoryError(const VoxelGenerationError& Error)
    {
        // Reduce chunk size or LOD
        // Clear unused chunks
        // Retry generation
        return true;
    }
    
    static bool HandleInvalidConfigError(const VoxelGenerationError& Error)
    {
        // Reset to default configuration
        // Log configuration issues
        return true;
    }
    
    static bool HandleTimeoutError(const VoxelGenerationError& Error)
    {
        // Reduce generation complexity
        // Use simplified generation
        return true;
    }
};
```

### 2. Data Validation

**Chunk Data Validation:**
```cpp
class VoxelDataValidator
{
public:
    static bool ValidateChunkData(TSharedPtr<AVoxelChunk> Chunk)
    {
        if (!Chunk.IsValid())
        {
            UE_LOG(LogTemp, Error, TEXT("Invalid chunk pointer"));
            return false;
        }
        
        // Validate density data
        if (!ValidateDensityData(Chunk->GetDensities()))
        {
            UE_LOG(LogTemp, Error, TEXT("Invalid density data"));
            return false;
        }
        
        // Validate mesh data
        if (!ValidateMeshData(Chunk->GetMeshOutput()))
        {
            UE_LOG(LogTemp, Error, TEXT("Invalid mesh data"));
            return false;
        }
        
        // Validate water data
        if (!ValidateWaterData(Chunk->GetWaterData()))
        {
            UE_LOG(LogTemp, Error, TEXT("Invalid water data"));
            return false;
        }
        
        return true;
    }
    
private:
    static bool ValidateDensityData(const TArray<float>& Densities)
    {
        for (float density : Densities)
        {
            if (!FMath::IsFinite(density))
            {
                return false;
            }
        }
        return true;
    }
    
    static bool ValidateMeshData(const FVoxelMeshOutput& Mesh)
    {
        return Mesh.FlatMesh.Vertices.Num() > 0 ||
               Mesh.SlopeMesh.Vertices.Num() > 0;
    }
    
    static bool ValidateWaterData(const FVoxelWaterData& WaterData)
    {
        return WaterData.Cells.Num() > 0;
    }
};
```

## Best Practices for Debugging

### 1. Systematic Debugging Approach

1. **Reproduce the Issue**: Create a consistent way to reproduce the problem
2. **Isolate the Cause**: Use debugging tools to identify the root cause
3. **Verify the Fix**: Ensure the solution resolves the issue without side effects
4. **Prevent Regression**: Add tests to prevent the issue from recurring

### 2. Performance Debugging

1. **Profile Before Optimizing**: Always measure performance before making changes
2. **Focus on Hot Paths**: Optimize the most frequently executed code paths
3. **Use Appropriate Tools**: Leverage Unreal Engine's profiling tools
4. **Test on Target Hardware**: Validate performance on actual deployment hardware

### 3. Memory Debugging

1. **Monitor Memory Usage**: Regularly check memory consumption patterns
2. **Check for Leaks**: Use memory profiling tools to detect leaks
3. **Validate Allocations**: Ensure all allocations have corresponding deallocations
4. **Use Smart Pointers**: Leverage RAII and smart pointers for automatic memory management

### 4. Threading Debugging

1. **Check Thread Safety**: Verify all shared data is properly synchronized
2. **Monitor Deadlocks**: Use tools to detect and prevent deadlocks
3. **Validate Cancellation**: Ensure cancellation mechanisms work correctly
4. **Test Race Conditions**: Create tests that stress concurrent access patterns

## Troubleshooting Checklist

### Generation Issues
- [ ] Verify configuration is valid
- [ ] Check for memory constraints
- [ ] Profile generation performance
- [ ] Validate input data
- [ ] Test with simplified generation

### Mesh Issues
- [ ] Check density field completeness
- [ ] Verify Surface Nets implementation
- [ ] Validate mesh data integrity
- [ ] Test mesh rendering pipeline
- [ ] Check for degenerate geometry

### Water Issues
- [ ] Verify solid cell mapping
- [ ] Check water source placement
- [ ] Validate flow logic
- [ ] Test boundary conditions
- [ ] Monitor simulation stability

### Performance Issues
- [ ] Profile all generation stages
- [ ] Monitor memory usage
- [ ] Check for memory leaks
- [ ] Validate threading implementation
- [ ] Test under load conditions

This debugging guide provides comprehensive tools and techniques for identifying and resolving issues in the FirstVoxel engine. Regular use of these debugging practices will help maintain code quality and performance.