# FirstVoxel Architecture Guide

This guide provides a comprehensive overview of the FirstVoxel engine's architecture, design patterns, and system interactions.

## System Architecture Overview

The FirstVoxel engine follows a modular, layered architecture designed for performance, scalability, and maintainability. The system is organized into several key layers that work together to create dynamic voxel terrain.

### Architecture Layers

```
┌─────────────────────────────────────────────────────────────────┐
│                    Application Layer                             │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐   │
│  │   Game Logic    │  │     UI/UX       │  │   Debug Tools   │   │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────────┐
│                    Engine Integration Layer                      │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐   │
│  │ VoxelWorld      │  │ VoxelChunk      │  │ VoxelPlayer     │   │
│  │ Manager         │  │ Manager         │  │ Controller      │   │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────────┐
│                    Generation Layer                              │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐   │
│  │ VoxelGenerator  │  │ VoxelDensity    │  │ VoxelBiome      │   │
│  │ Task            │  │ Generator       │  │ Manager         │   │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────────┐
│                    Core Systems Layer                            │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐   │
│  │ VoxelMesh       │  │ VoxelWater      │  │ VoxelMap        │   │
│  │ Generator       │  │ Simulator       │  │ Generator       │   │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────────┐
│                    Data Layer                                    │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐   │
│  │ VoxelChunk      │  │ VoxelDataMap    │  │ VoxelConfig     │   │
│  │ Pool            │  │                 │  │                 │   │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## Design Patterns

### 1. Factory Pattern

The engine uses factory patterns extensively for object creation and management.

**VoxelChunkFactory Pattern:**
```cpp
class VoxelChunkFactory
{
public:
    static TSharedPtr<AVoxelChunk> CreateChunk(
        const FIntVector& ChunkCoord,
        const FVector& WorldOrigin,
        int32 ChunkSize,
        float VoxelSize,
        int32 StepSize
    )
    {
        auto chunk = MakeShared<AVoxelChunk>();
        chunk->Initialize(ChunkCoord, WorldOrigin, ChunkSize, VoxelSize, StepSize);
        return chunk;
    }
};
```

**Generation Stage Factory:**
```cpp
class VoxelGenerationStageFactory
{
public:
    static TUniquePtr<IVoxelGenerationStage> CreateStage(EVoxelGenerationStage StageType)
    {
        switch (StageType)
        {
            case EVoxelGenerationStage::Surface: return MakeUnique<VoxelSurfaceGenerator>();
            case EVoxelGenerationStage::Skylands: return MakeUnique<VoxelSkylandsGenerator>();
            case EVoxelGenerationStage::Caves: return MakeUnique<VoxelCaveGenerator>();
            default: return nullptr;
        }
    }
};
```

### 2. Observer Pattern

Used for chunk lifecycle management and event handling.

**Chunk Observer Pattern:**
```cpp
class IChunkObserver
{
public:
    virtual ~IChunkObserver() = default;
    virtual void OnChunkGenerated(TSharedPtr<AVoxelChunk> Chunk) = 0;
    virtual void OnChunkUnloaded(const FIntVector& ChunkCoord) = 0;
    virtual void OnChunkModified(TSharedPtr<AVoxelChunk> Chunk) = 0;
};

class VoxelChunkManager : public IChunkObserver
{
private:
    TArray<IChunkObserver*> Observers;
    
public:
    void RegisterObserver(IChunkObserver* Observer)
    {
        Observers.Add(Observer);
    }
    
    void NotifyChunkGenerated(TSharedPtr<AVoxelChunk> Chunk)
    {
        for (auto Observer : Observers)
        {
            Observer->OnChunkGenerated(Chunk);
        }
    }
};
```

### 3. Strategy Pattern

Used for different generation algorithms and optimization strategies.

**Generation Strategy Pattern:**
```cpp
class IVoxelGenerationStrategy
{
public:
    virtual ~IVoxelGenerationStrategy() = default;
    virtual void GenerateChunk(TSharedPtr<AVoxelChunk> Chunk, const FVoxelGenerationConfig& Config) = 0;
    virtual bool SupportsLOD() const = 0;
    virtual float GetGenerationTime() const = 0;
};

class FastGenerationStrategy : public IVoxelGenerationStrategy
{
public:
    virtual void GenerateChunk(TSharedPtr<AVoxelChunk> Chunk, const FVoxelGenerationConfig& Config) override
    {
        // Fast generation implementation
    }
    
    virtual bool SupportsLOD() const override { return true; }
    virtual float GetGenerationTime() const override { return 0.1f; }
};
```

### 4. Command Pattern

Used for chunk operations and undo/redo functionality.

**Chunk Command Pattern:**
```cpp
class IChunkCommand
{
public:
    virtual ~IChunkCommand() = default;
    virtual void Execute() = 0;
    virtual void Undo() = 0;
    virtual bool CanUndo() const = 0;
};

class EditVoxelCommand : public IChunkCommand
{
private:
    TWeakPtr<AVoxelChunk> Chunk;
    FIntVector VoxelPos;
    float OldValue, NewValue;
    
public:
    virtual void Execute() override
    {
        if (auto chunk = Chunk.Pin())
        {
            chunk->SetDensity(VoxelPos.X, VoxelPos.Y, VoxelPos.Z, NewValue);
        }
    }
    
    virtual void Undo() override
    {
        if (auto chunk = Chunk.Pin())
        {
            chunk->SetDensity(VoxelPos.X, VoxelPos.Y, VoxelPos.Z, OldValue);
        }
    }
    
    virtual bool CanUndo() const override { return true; }
};
```

## System Interactions

### Chunk Lifecycle Management

The chunk lifecycle follows a well-defined pattern:

```
1. Request → 2. Generation → 3. Mesh Building → 4. Foliage Placement → 5. Water Source Detection → 6. Upload → 7. Active → 8. Unload
```

**Lifecycle States:**
```cpp
enum class EChunkState
{
    Requested,      // Chunk requested but not yet generated
    Generating,     // Background generation in progress
    Generated,      // Generation complete, ready for mesh building
    MeshBuilding,   // Surface Nets mesh generation
    FoliagePlacing, // Foliage placement and optimization
    WaterPlacing,   // Water source detection and placement
    Ready,          // Fully processed, ready for rendering
    Active,         // Currently being rendered
    Unloading,      // Being unloaded from memory
    Unloaded        // Completely unloaded
};
```

### Data Flow Architecture

The engine follows a clear data flow pattern from generation to rendering:

```
World Request → Chunk Manager → Generation Task → Density Field → Mesh Generator → Foliage System → Water System → Renderable Chunk
```

**Data Flow Example:**
```cpp
// 1. World requests chunk
auto chunk = ChunkManager->GetChunk(chunkCoord, worldOrigin, chunkSize, voxelSize);

// 2. Generation task processes chunk
auto task = MakeUnique<FVoxelGeneratorTask>(...);
task->Execute();

// 3. Density field generated
auto& densities = task->GetDensities();

// 4. Mesh generated from densities
FVoxelMeshOutput meshOutput;
FVoxelMeshGenerator::GenerateMesh(densities, ..., meshOutput);

// 5. Foliage placed on mesh
auto& foliageTransforms = task->GetPerFoliageTransforms();

// 6. Water sources detected
auto& waterSources = task->GetWaterSources();

// 7. Chunk ready for rendering
chunk->ApplyMesh(meshOutput, foliageTransforms, waterSources);
```

## Performance Architecture

### Memory Management

The engine implements several memory management strategies:

**Chunk Pooling:**
```cpp
class VoxelChunkPool
{
private:
    TQueue<TSharedPtr<AVoxelChunk>> AvailableChunks;
    TSet<TSharedPtr<AVoxelChunk>> ActiveChunks;
    
public:
    TSharedPtr<AVoxelChunk> AcquireChunk()
    {
        TSharedPtr<AVoxelChunk> chunk;
        if (AvailableChunks.Dequeue(chunk))
        {
            chunk->Reset();
            ActiveChunks.Add(chunk);
            return chunk;
        }
        
        chunk = MakeShared<AVoxelChunk>();
        ActiveChunks.Add(chunk);
        return chunk;
    }
    
    void ReturnChunk(TSharedPtr<AVoxelChunk> chunk)
    {
        if (ActiveChunks.Contains(chunk))
        {
            ActiveChunks.Remove(chunk);
            AvailableChunks.Enqueue(chunk);
        }
    }
};
```

**LOD Memory Optimization:**
```cpp
class VoxelLODManager
{
private:
    struct LODLevel
    {
        int32 StepSize;
        float DistanceThreshold;
        float MemoryBudget;
    };
    
    TArray<LODLevel> LODLevels;
    
public:
    int32 GetOptimalLOD(const FVector& CameraPosition, const FIntVector& ChunkCoord)
    {
        float distance = FVector::Dist(
            CameraPosition,
            VoxelChunkManager::GetChunkWorldOrigin(ChunkCoord, ChunkSize, VoxelSize)
        );
        
        for (int32 i = 0; i < LODLevels.Num(); ++i)
        {
            if (distance <= LODLevels[i].DistanceThreshold)
            {
                return LODLevels[i].StepSize;
            }
        }
        
        return LODLevels.Last().StepSize; // Maximum LOD
    }
};
```

### Threading Architecture

The engine uses a sophisticated threading model for optimal performance:

**Task Scheduling:**
```cpp
class VoxelTaskScheduler
{
private:
    TQueue<TUniquePtr<FVoxelGeneratorTask>> TaskQueue;
    TArray<FThread> WorkerThreads;
    
public:
    void ScheduleTask(TUniquePtr<FVoxelGeneratorTask> Task)
    {
        TaskQueue.Enqueue(MoveTemp(Task));
    }
    
    void ProcessTasks()
    {
        while (!TaskQueue.IsEmpty())
        {
            TUniquePtr<FVoxelGeneratorTask> task;
            if (TaskQueue.Dequeue(task))
            {
                if (!task->IsCancelled())
                {
                    task->Execute();
                    // Handle results on game thread
                }
            }
        }
    }
};
```

**Thread Safety Patterns:**
```cpp
class VoxelThreadSafeData
{
private:
    FCriticalSection DataLock;
    TMap<FIntVector, TSharedPtr<AVoxelChunk>> ChunkData;
    
public:
    TSharedPtr<AVoxelChunk> GetChunk(const FIntVector& Coord)
    {
        FScopeLock Lock(&DataLock);
        return ChunkData.FindRef(Coord);
    }
    
    void AddChunk(const FIntVector& Coord, TSharedPtr<AVoxelChunk> Chunk)
    {
        FScopeLock Lock(&DataLock);
        ChunkData.Add(Coord, Chunk);
    }
};
```

## Error Handling Architecture

### Exception Safety

The engine implements comprehensive error handling:

**Generation Error Handling:**
```cpp
class VoxelGenerationError
{
public:
    enum class ErrorType
    {
        OutOfMemory,
        InvalidConfiguration,
        GenerationTimeout,
        MeshGenerationFailed,
        FoliagePlacementFailed
    };
    
    ErrorType Type;
    FString Message;
    FIntVector ChunkCoord;
    
    VoxelGenerationError(ErrorType Type, const FString& Message, const FIntVector& ChunkCoord)
        : Type(Type), Message(Message), ChunkCoord(ChunkCoord) {}
};

class VoxelGenerationResult
{
public:
    bool Success;
    TUniquePtr<FVoxelGeneratorTask> Task;
    VoxelGenerationError Error;
    
    static VoxelGenerationResult SuccessResult(TUniquePtr<FVoxelGeneratorTask> Task)
    {
        return { true, MoveTemp(Task), VoxelGenerationError() };
    }
    
    static VoxelGenerationResult ErrorResult(ErrorType Type, const FString& Message, const FIntVector& ChunkCoord)
    {
        return { false, nullptr, VoxelGenerationError(Type, Message, ChunkCoord) };
    }
};
```

### Graceful Degradation

The engine implements fallback mechanisms for robust operation:

**Fallback Generation:**
```cpp
class VoxelGenerationFallback
{
public:
    static TUniquePtr<FVoxelGeneratorTask> CreateFallbackTask(
        const FIntVector& ChunkCoord,
        const FVector& WorldOrigin,
        int32 ChunkSize,
        float VoxelSize
    )
    {
        // Create simplified generation task
        auto config = FVoxelGenerationConfig::GetDefault();
        config.SurfaceLayer.Enabled = true;
        config.SkylandsLayer.Enabled = false;
        config.CaveLayer.Enabled = false;
        
        return MakeUnique<FVoxelGeneratorTask>(
            ChunkCoord, WorldOrigin, ChunkSize, VoxelSize, 1,
            config, nullptr, 0.0f, 0.0f, nullptr
        );
    }
};
```

## Configuration Architecture

### Hierarchical Configuration

The engine uses a hierarchical configuration system:

```cpp
struct FVoxelEngineConfig
{
    FVoxelGenerationConfig Generation;
    FVoxelRenderingConfig Rendering;
    FVoxelPerformanceConfig Performance;
    FVoxelDebugConfig Debug;
    
    bool Validate() const
    {
        return Generation.IsValid() && 
               Rendering.IsValid() && 
               Performance.IsValid();
    }
    
    static FVoxelEngineConfig LoadFromFile(const FString& FilePath)
    {
        // Load configuration from file
        // Apply defaults for missing values
        // Validate final configuration
    }
};
```

### Runtime Configuration

Configuration can be modified at runtime:

```cpp
class VoxelConfigManager
{
private:
    FVoxelEngineConfig CurrentConfig;
    TArray<FConfigObserver*> Observers;
    
public:
    void UpdateConfig(const FVoxelEngineConfig& NewConfig)
    {
        if (NewConfig.Validate())
        {
            CurrentConfig = NewConfig;
            NotifyConfigChanged();
        }
    }
    
    void RegisterObserver(FConfigObserver* Observer)
    {
        Observers.Add(Observer);
    }
    
private:
    void NotifyConfigChanged()
    {
        for (auto Observer : Observers)
        {
            Observer->OnConfigChanged(CurrentConfig);
        }
    }
};
```

## Testing Architecture

### Unit Testing

The engine includes comprehensive unit testing:

```cpp
class VoxelGenerationTests
{
public:
    static void TestSurfaceGeneration()
    {
        // Test surface generation algorithm
        // Verify output quality and performance
    }
    
    static void TestLODSystem()
    {
        // Test LOD transitions
        // Verify memory usage
    }
    
    static void TestThreading()
    {
        // Test thread safety
        // Verify cancellation behavior
    }
    
    static void TestMemoryManagement()
    {
        // Test chunk pooling
        // Verify memory leaks
    }
};
```

### Integration Testing

Integration tests verify system interactions:

```cpp
class VoxelIntegrationTests
{
public:
    static void TestChunkLifecycle()
    {
        // Test complete chunk lifecycle
        // Verify state transitions
    }
    
    static void TestSystemIntegration()
    {
        // Test mesh + water + foliage integration
        // Verify data consistency
    }
    
    static void TestPerformance()
    {
        // Test performance under load
        // Verify scalability
    }
};
```

## Best Practices

### Code Organization

1. **Separation of Concerns**: Each class has a single, well-defined responsibility
2. **Interface Segregation**: Use specific interfaces rather than large, general ones
3. **Dependency Injection**: Inject dependencies rather than creating them internally
4. **RAII**: Use RAII for resource management

### Performance Guidelines

1. **Memory Efficiency**: Minimize allocations, use pooling where possible
2. **Cache Efficiency**: Organize data for optimal cache usage
3. **Parallel Processing**: Use background threads for heavy computation
4. **LOD Systems**: Implement LOD for performance optimization

### Maintainability

1. **Documentation**: Document all public APIs and complex algorithms
2. **Testing**: Write comprehensive tests for all functionality
3. **Code Reviews**: Implement code review process
4. **Refactoring**: Regularly refactor to maintain code quality

This architecture guide provides the foundation for understanding and extending the FirstVoxel engine's sophisticated design patterns and system interactions.