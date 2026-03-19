# FirstVoxel API Reference Guide

This document provides comprehensive API documentation for all public interfaces in the FirstVoxel voxel engine, including usage examples and best practices.

## Core System APIs

### VoxelChunkManager

**Thread Safety:** Thread-safe for all public methods
**Performance:** O(log n) operations for chunk lookup and management

#### Public Methods

```cpp
class VoxelChunkManager
{
public:
    /**
     * Get or create a chunk at the specified coordinates
     * @param ChunkCoord World chunk coordinates
     * @param WorldOrigin World position of chunk [0,0,0]
     * @param ChunkSize Voxel size per chunk side
     * @param VoxelSize World units per voxel
     * @param StepSize LOD step (1=full, 2=half, 4=quarter)
     * @return Shared pointer to chunk, null if generation failed
     */
    TSharedPtr<AVoxelChunk> GetChunk(
        const FIntVector& ChunkCoord,
        const FVector& WorldOrigin,
        int32 ChunkSize,
        float VoxelSize,
        int32 StepSize = 1
    );

    /**
     * Return a chunk to the pool for reuse
     * @param Chunk Weak pointer to chunk to return
     */
    void ReturnChunk(TWeakPtr<AVoxelChunk> Chunk);

    /**
     * Get chunk coordinates from world position
     * @param WorldPos World position in cm
     * @param ChunkSize Voxel size per chunk side
     * @return Chunk coordinates
     */
    static FIntVector GetChunkCoord(const FVector& WorldPos, int32 ChunkSize);

    /**
     * Get world origin for chunk coordinates
     * @param ChunkCoord Chunk coordinates
     * @param ChunkSize Voxel size per chunk side
     * @param VoxelSize World units per voxel
     * @return World position of chunk [0,0,0]
     */
    static FVector GetChunkWorldOrigin(
        const FIntVector& ChunkCoord,
        int32 ChunkSize,
        float VoxelSize
    );
};
```

#### Usage Examples

```cpp
// Get a chunk with LOD
auto Chunk = ChunkManager->GetChunk(
    FIntVector(1, 2, 0),           // Chunk coordinates
    FVector(1600, 3200, 0),        // World origin
    32,                            // 32 voxels per side
    100.0f,                        // 100cm per voxel
    2                              // Half resolution LOD
);

// Return chunk when done
ChunkManager->ReturnChunk(Chunk);
```

### VoxelDensityChunk

**Thread Safety:** Thread-safe for read operations
**Performance:** O(1) density access, O(N³) for full field operations

#### Public Methods

```cpp
class VoxelDensityChunk
{
public:
    /**
     * Get density at local voxel coordinates
     * @param X,Y,Z Local voxel coordinates (0 to ChunkSize-1)
     * @return Density value (-1.0 to 1.0)
     */
    float GetDensity(int32 X, int32 Y, int32 Z) const;

    /**
     * Set density at local voxel coordinates
     * @param X,Y,Z Local voxel coordinates
     * @param Density Density value
     */
    void SetDensity(int32 X, int32 Y, int32 Z, float Density);

    /**
     * Get chunk size (voxels per side)
     * @return Chunk size
     */
    int32 GetChunkSize() const;

    /**
     * Get voxel size (world units)
     * @return Voxel size in cm
     */
    float GetVoxelSize() const;

    /**
     * Get world origin of chunk
     * @return World position of local [0,0,0]
     */
    FVector GetWorldOrigin() const;
};
```

#### Usage Examples

```cpp
// Access density values
float density = Chunk->GetDensity(10, 15, 5);

// Modify density
Chunk->SetDensity(10, 15, 5, 0.5f);

// Get chunk properties
int32 size = Chunk->GetChunkSize();
float voxelSize = Chunk->GetVoxelSize();
```

### IVoxelGenerationStage

**Thread Safety:** Thread-safe when implemented correctly
**Performance:** Depends on specific implementation

#### Interface Definition

```cpp
class IVoxelGenerationStage
{
public:
    virtual ~IVoxelGenerationStage() = default;

    /**
     * Execute generation stage on density field
     * @param Densities Density field array
     * @param ChunkSize Voxel size per chunk side
     * @param VoxelSize World units per voxel
     * @param WorldOrigin World position of chunk [0,0,0]
     * @param Config Generation configuration
     * @param ColumnContext Per-column context data
     */
    virtual void Execute(
        TArray<float>& Densities,
        int32 ChunkSize,
        float VoxelSize,
        const FVector& WorldOrigin,
        const FVoxelGenerationConfig& Config,
        const TArray<FColumnContext>& ColumnContext
    ) = 0;

    /**
     * Get stage name for debugging
     * @return Human-readable stage name
     */
    virtual FString GetStageName() const = 0;

    /**
     * Check if stage should run for given configuration
     * @param Config Generation configuration
     * @return True if stage should execute
     */
    virtual bool ShouldExecute(const FVoxelGenerationConfig& Config) const = 0;
};
```

#### Implementation Example

```cpp
class VoxelSurfaceGenerator : public IVoxelGenerationStage
{
public:
    virtual void Execute(
        TArray<float>& Densities,
        int32 ChunkSize,
        float VoxelSize,
        const FVector& WorldOrigin,
        const FVoxelGenerationConfig& Config,
        const TArray<FColumnContext>& ColumnContext
    ) override
    {
        // Surface generation implementation
    }

    virtual FString GetStageName() const override
    {
        return TEXT("Surface Generation");
    }

    virtual bool ShouldExecute(const FVoxelGenerationConfig& Config) const override
    {
        return Config.SurfaceLayer.Enabled;
    }
};
```

## Mesh Generation APIs

### VoxelMeshGenerator

**Thread Safety:** Fully thread-safe, stateless
**Performance:** O(N³) density sampling, O(N²) mesh output

#### Public Methods

```cpp
struct FVoxelMeshGenerator
{
    /**
     * Generate mesh from density field using Surface Nets
     * @param Densities Density field array
     * @param InChunkSize Voxel size per chunk side
     * @param InVoxelSize World units per voxel
     * @param ChunkOrigin World position of local [0,0,0]
     * @param OutMesh Output mesh data
     * @param Config Generation configuration
     * @param InStepSize LOD step (1=full, 2=half, 4=quarter)
     */
    static void GenerateMesh(
        const TArray<float>& Densities,
        int32 InChunkSize,
        float InVoxelSize,
        const FVector& ChunkOrigin,
        FVoxelMeshOutput& OutMesh,
        const FVoxelGenerationConfig& Config,
        int32 InStepSize = 1
    );

    /**
     * Compute normal at voxel coordinates
     * @param Densities Density field array
     * @param X,Y,Z Voxel coordinates
     * @param InChunkSize Voxel size per chunk side
     * @return Normal vector pointing out of solid
     */
    static FVector ComputeNormal(
        const TArray<float>& Densities,
        int32 X, int32 Y, int32 Z,
        int32 InChunkSize
    );

private:
    /**
     * Interpolate edge intersection point
     * @param P1,P2 Edge endpoints
     * @param D1,D2 Density values at endpoints
     * @return Intersection point
     */
    static FVector InterpolateEdge(
        const FVector& P1, float D1,
        const FVector& P2, float D2
    );
};
```

#### Usage Examples

```cpp
// Generate mesh from density field
FVoxelMeshOutput meshOutput;
FVoxelMeshGenerator::GenerateMesh(
    densities,              // Density field
    32,                     // Chunk size
    100.0f,                 // Voxel size
    FVector(0, 0, 0),       // Chunk origin
    meshOutput,             // Output
    config,                 // Configuration
    1                       // Full resolution
);

// Access mesh sections
auto& flatMesh = meshOutput.FlatMesh;
auto& slopeMesh = meshOutput.SlopeMesh;
auto& backMesh = meshOutput.BackMesh;
auto& slopeBackMesh = meshOutput.SlopeBackMesh;
```

### FVoxelMeshData

**Thread Safety:** Thread-safe for read operations
**Performance:** O(1) access to all arrays

#### Structure Definition

```cpp
struct FVoxelMeshData
{
    TArray<FVector> Vertices;           // Vertex positions
    TArray<int32> Triangles;            // Triangle indices
    TArray<FVector> Normals;            // Vertex normals
    TArray<FVector2D> UVs;              // Texture coordinates
    TArray<FColor> VertexColors;        // Vertex colors
    TArray<FProcMeshTangent> Tangents;  // Vertex tangents

    /**
     * Clear all mesh data
     */
    void Reset();

    /**
     * Check if mesh is empty
     * @return True if no vertices
     */
    bool IsEmpty() const;

    /**
     * Reserve memory for initial vertices
     * @param N Number of vertices to reserve
     */
    void ReserveInitial(int32 N);
};
```

#### Usage Examples

```cpp
// Create mesh data
FVoxelMeshData meshData;
meshData.ReserveInitial(1000);

// Add vertex
int32 vertexIndex = meshData.Vertices.Add(FVector(100, 200, 300));
meshData.Normals.Add(FVector(0, 0, 1));
meshData.UVs.Add(FVector2D(0.5f, 0.5f));
meshData.VertexColors.Add(FColor::Green);

// Add triangle
meshData.Triangles.Add(vertexIndex);
meshData.Triangles.Add(vertexIndex + 1);
meshData.Triangles.Add(vertexIndex + 2);
```

## Water Simulation APIs

### FVoxelWaterSimulator

**Thread Safety:** Game-thread only
**Performance:** O(N) per chunk where N = number of water cells

#### Public Methods

```cpp
class FVoxelWaterSimulator
{
public:
    /**
     * Constructor
     * @param InChunkSize Voxel size per chunk side
     * @param InVoxelSize World units per voxel
     */
    explicit FVoxelWaterSimulator(int32 InChunkSize, float InVoxelSize);

    /**
     * Register chunk for water simulation
     * @param ChunkCoord Chunk coordinates
     * @param WaterData Water data pointer
     */
    void RegisterChunk(const FIntVector& ChunkCoord, FVoxelWaterData* WaterData);

    /**
     * Unregister chunk from water simulation
     * @param ChunkCoord Chunk coordinates
     */
    void UnregisterChunk(const FIntVector& ChunkCoord);

    /**
     * Check if chunk is registered
     * @param ChunkCoord Chunk coordinates
     * @return True if registered
     */
    bool IsRegistered(const FIntVector& ChunkCoord) const;

    /**
     * Place permanent water source
     * @param WorldVoxel World voxel coordinates
     */
    void SetSource(const FIntVector& WorldVoxel);

    /**
     * Place flowing water
     * @param WorldVoxel World voxel coordinates
     * @param Level Water level (0-255)
     */
    void SetFlowing(const FIntVector& WorldVoxel, uint8 Level = WATER_FULL);

    /**
     * Clear water from voxel
     * @param WorldVoxel World voxel coordinates
     */
    void ClearCell(const FIntVector& WorldVoxel);

    /**
     * Get water level at voxel
     * @param WorldVoxel World voxel coordinates
     * @return Water level (0 if no water)
     */
    uint8 GetLevel(const FIntVector& WorldVoxel) const;

    /**
     * Check if voxel contains water
     * @param WorldVoxel World voxel coordinates
     * @return True if contains water
     */
    bool IsWater(const FIntVector& WorldVoxel) const;

    /**
     * Check if voxel is solid terrain
     * @param WorldVoxel World voxel coordinates
     * @return True if solid
     */
    bool IsSolid(const FIntVector& WorldVoxel) const;

    /**
     * Advance simulation by one step
     * @return Array of chunks that changed
     */
    TArray<FIntVector> Step();

    /**
     * Clear all water from all chunks
     */
    void ClearAll();
};
```

#### Usage Examples

```cpp
// Create water simulator
FVoxelWaterSimulator waterSim(32, 100.0f);

// Register chunk
waterSim.RegisterChunk(FIntVector(1, 2, 0), waterData);

// Place water source
waterSim.SetSource(FIntVector(100, 200, 50));

// Place flowing water
waterSim.SetFlowing(FIntVector(101, 200, 50), 128);

// Advance simulation
auto changedChunks = waterSim.Step();

// Unregister chunk
waterSim.UnregisterChunk(FIntVector(1, 2, 0));
```

### FVoxelWaterData

**Thread Safety:** Thread-safe for read operations
**Performance:** O(1) access to water cells

#### Structure Definition

```cpp
struct FVoxelWaterData
{
    TArray<uint8> Cells;           // Water levels (0-255)
    TArray<bool> SolidCells;       // Solid terrain flags
    bool bMeshDirty;               // Mesh needs regeneration

    /**
     * Reset all water data
     */
    void Reset();

    /**
     * Get water level at local coordinates
     * @param X,Y,Z Local voxel coordinates
     * @return Water level
     */
    uint8 GetLevel(int32 X, int32 Y, int32 Z) const;

    /**
     * Set water level at local coordinates
     * @param X,Y,Z Local voxel coordinates
     * @param Level Water level
     */
    void SetLevel(int32 X, int32 Y, int32 Z, uint8 Level);

    /**
     * Check if voxel is solid
     * @param X,Y,Z Local voxel coordinates
     * @return True if solid
     */
    bool IsSolid(int32 X, int32 Y, int32 Z) const;
};
```

## Map Generation APIs

### FVoxelMapGenerator

**Thread Safety:** Thread-safe for all methods
**Performance:** O(N²) for N×N pixel resolution

#### Public Methods

```cpp
struct FVoxelMapGenerator
{
    /**
     * Generate pixel buffer for top-down map
     * @param CenterX,Y Map center world coordinates
     * @param WorldRadius Half-extent of map in world cm
     * @param Resolution Pixel dimensions (square)
     * @param Config Generation configuration
     * @param LoadedChunkCoords Currently loaded chunks
     * @param ChunkWorldSize One chunk side in world cm
     * @param OutPixels Output pixel buffer
     */
    static void GeneratePixelBuffer(
        float CenterX,
        float CenterY,
        float WorldRadius,
        int32 Resolution,
        const FVoxelGenerationConfig& Config,
        const TSet<FIntVector>& LoadedChunkCoords,
        float ChunkWorldSize,
        TArray<FColor>& OutPixels
    );

    // Biome color palette (static)
    static const FLinearColor BiomeColors[FVoxelBiomeWeightMap::MaxBiomes];

    // Biome display names (static)
    static const TCHAR* BiomeNames[FVoxelBiomeWeightMap::MaxBiomes];
};
```

#### Usage Examples

```cpp
// Generate map
TArray<FColor> pixelBuffer;
FVoxelMapGenerator::GeneratePixelBuffer(
    0.0f,                    // Center X
    0.0f,                    // Center Y
    10000.0f,                // 10km radius
    512,                     // 512x512 pixels
    config,                  // Generation config
    loadedChunks,            // Loaded chunks
    3200.0f,                 // 32 voxels * 100cm
    pixelBuffer              // Output buffer
);

// Create texture from buffer
UTexture2D* mapTexture = UTexture2D::CreateTransient(512, 512, PF_B8G8R8A8);
```

## Background Task APIs

### FVoxelGeneratorTask

**Thread Safety:** Thread-safe execution, results safe for game thread
**Performance:** O(N³) density generation, O(N²) mesh output

#### Public Methods

```cpp
class FVoxelGeneratorTask
{
public:
    /**
     * Constructor
     * @param ChunkCoord Chunk coordinates
     * @param WorldOrigin World position of chunk [0,0,0]
     * @param ChunkSize Voxel size per chunk side
     * @param VoxelSize World units per voxel
     * @param StepSize LOD step
     * @param Config Generation configuration
     * @param DensityProvider Density provider interface
     * @param FoliageDensity Legacy foliage density
     * @param MaxFoliageSlope Legacy max foliage slope
     * @param DataMap Voxel data map
     */
    FVoxelGeneratorTask(
        const FIntVector& ChunkCoord,
        const FVector& WorldOrigin,
        int32 ChunkSize,
        float VoxelSize,
        int32 StepSize,
        const FVoxelGenerationConfig& Config,
        IVoxelDensityProvider* DensityProvider,
        float FoliageDensity,
        float MaxFoliageSlope,
        FVoxelDataMap* DataMap
    );

    /**
     * Execute background generation
     */
    void Execute();

    /**
     * Cancel generation
     */
    void Cancel();

    /**
     * Check if cancelled
     * @return True if cancelled
     */
    bool IsCancelled() const;

    // Result accessors
    const FVoxelMeshOutput& GetMeshOutput() const;
    const TArray<TArray<FTransform>>& GetPerFoliageTransforms() const;
    const TArray<UStaticMesh*>& GetPerFoliageMeshes() const;
    const TArray<FTransform>& GetTreeTransforms() const;
    const TArray<FTransform>& GetGrassTransforms() const;
    const TArray<FIntVector>& GetWaterSources() const;
    const TArray<float>& GetDensities() const;
};
```

#### Usage Examples

```cpp
// Create and execute task
auto task = MakeUnique<FVoxelGeneratorTask>(
    chunkCoord, worldOrigin, chunkSize, voxelSize, stepSize,
    config, densityProvider, foliageDensity, maxFoliageSlope, dataMap
);

// Execute on background thread
FGraphTask::CreateAndDispatchThen(
    nullptr,
    ENamedThreads::AnyBackgroundThreadNormalTask,
    [task = MoveTemp(task)]() mutable
    {
        task->Execute();
        // Handle results on game thread
        AsyncTask(ENamedThreads::GameThread, [task]()
        {
            if (!task->IsCancelled())
            {
                ApplyMesh(task->GetMeshOutput());
            }
        });
    }
);
```

## Configuration APIs

### FVoxelGenerationConfig

**Thread Safety:** Thread-safe for read operations
**Performance:** O(1) access to all configuration values

#### Structure Definition

```cpp
struct FVoxelGenerationConfig
{
    // Surface layer configuration
    struct FSurfaceLayerConfig SurfaceLayer;
    
    // Skylands layer configuration  
    struct FSkylandsLayerConfig SkylandsLayer;
    
    // Cave layer configuration
    struct FCaveLayerConfig CaveLayer;

    // Biome configuration
    struct FSurfaceBiomesConfig Biomes;

    // Performance settings
    int32 LODLevels;
    float SlopeThreshold;
    float SeaLevel;

    // Foliage settings
    TArray<FFoliageConfig> Foliage;

    /**
     * Validate configuration
     * @return True if valid
     */
    bool IsValid() const;

    /**
     * Get default configuration
     * @return Default config instance
     */
    static FVoxelGenerationConfig GetDefault();
};
```

#### Usage Examples

```cpp
// Create custom configuration
FVoxelGenerationConfig config;
config.SurfaceLayer.Enabled = true;
config.SurfaceLayer.NoiseScale = 0.01f;
config.SurfaceLayer.Amplitude = 1000.0f;

config.SkylandsLayer.Enabled = true;
config.SkylandsLayer.MaxTerrainReference = 5000.0f;

config.LODLevels = 3;
config.SlopeThreshold = 0.7f;
config.SeaLevel = 0.0f;

// Validate configuration
if (config.IsValid())
{
    // Use configuration
}
```

## Best Practices

### Memory Management

1. **Use Smart Pointers**: Always use TSharedPtr/TWeakPtr for chunk management
2. **Implement Pooling**: Use object pooling for frequently allocated objects
3. **Monitor Memory**: Regularly check memory usage and implement cleanup

### Threading

1. **Background Generation**: Always run heavy computation on background threads
2. **Thread Safety**: Ensure all public APIs are thread-safe
3. **Cancellation**: Implement proper cancellation mechanisms

### Performance

1. **LOD System**: Use LOD for distant chunks to improve performance
2. **Caching**: Cache expensive calculations when possible
3. **Profiling**: Regularly profile to identify bottlenecks

### Error Handling

1. **Graceful Degradation**: Implement fallback mechanisms for failed operations
2. **Logging**: Use appropriate logging levels for different error types
3. **Validation**: Validate inputs and configurations before use

This API reference provides comprehensive documentation for all public interfaces in the FirstVoxel engine, enabling developers to effectively use and extend the system.