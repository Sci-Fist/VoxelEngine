import sys

filepath_h = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Biomes\VoxelBiomeGenerators.h"
filepath_cpp = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Biomes\VoxelBiomeGenerators.cpp"

def apply_patch(filepath, target, replacement, label):
    with open(filepath, 'rb') as f:
        content = f.read()

    # Detect line endings
    if b'\r\n' in content:
        target_f = target.replace(b'\n', b'\r\n')
        replacement_f = replacement.replace(b'\n', b'\r\n')
    else:
        target_f = target
        replacement_f = replacement

    if target_f not in content:
        print(f"Target for {label} not found!")
        # Print a snippet of what we seek for debugging
        print("Looking for:", repr(target_f[:50]))
        return False

    content = content.replace(target_f, replacement_f)
    with open(filepath, 'wb') as f:
        f.write(content)
    print(f"Applied {label} successfully.")
    return True

# --- 1. Update VoxelBiomeGenerators.h ---
target_h = b'''struct FSkylandColumnCache
{
    bool bHasSkyland = false;
    float SkyAlt = 0.f;
    float HalfThick = 0.f;
    float ShapeXY = 0.f;
    float Threshold = 0.f;
    float Prob = 0.f;
    float HeightNorm = 0.f;
    float ShardFalloff = 0.f;

    /**
     * ShardT: 0 = pure sky-shard (rock), 1 = full floating island.
     * Used in GetSkylandDensityFromCache to blend between:
     *   Rock falloff  (ShardT=0): spherical, no flat top, strong 3D noise
     *   Island falloff(ShardT=1): flat-top plateau, low Z noise, organic taper
     */
    float ShardT = 0.f;

    // Cache dimensions
    float WX = 0.f;
    float WY = 0.f;
    float WX_base = 0.f;
    float WY_base = 0.f;
    float Freq = 0.f;
};'''

replacement_h = b'''struct FSkylandIslandData
{
    float SkyAlt = 0.f;
    float HalfThick = 0.f;
    float Threshold = 0.f;
    float ShardT = 0.f;
    float Freq = 0.f;
    float HeightNorm = 0.f;
    float ShardFalloff = 0.f;
    float IslandSize = 0.f;
};

struct FSkylandColumnCache
{
    bool bHasSkyland = false;
    TArray<FSkylandIslandData, TInlineAllocator<4>> Islands;

    // Cache dimensions (global to column)
    float WX_base = 0.f;
    float WY_base = 0.f;
};'''

if not apply_patch(filepath_h, target_h, replacement_h, "Header Struct"):
    sys.exit(1)

# --- 2. Update VoxelBiomeGenerators.cpp ---
with open(filepath_cpp, 'rb') as f:
    content_cpp = f.read()

# Normalize spacing inside targets to match what content read has (it had lots of empty lines)
# Let's find the exact strings inside content_cpp to be ABSOLUTELY sure they exist
# Or do search in Python using regex or full block replacement.
# Setup chunk:
target_cpp_1 = b'''    float MaxW             = -1.f;
    float BestSkyAlt       = 0.f;
    float BestHalfThick    = 0.f;

    float BestThreshold    = 0.f;

    float BestHeightNorm   = 0.f;

    float BestShardFalloff = 0.f;

    float BestFreq         = 0.f;

    float BestIslandSize   = 0.f;


    float BestCellShardT   = 0.f;

    float BestDistRatio    = FLT_MAX;  // Lower is better - normalized distance to cell center'''

replacement_cpp_1 = b'''    // Multi-island caching: collect all overlapping candidate cells
    Cache.bHasSkyland = false;'''

# Target 2:
target_cpp_2 = b'''        // Track the cell with smallest normalized distance (nearest cell)
        if (DistRatio < BestDistRatio && W > 0.001f)  // Only consider cells with meaningful influence

        {

            MaxW             = W;

            BestSkyAlt       = SkyAlt;
            BestHalfThick    = HalfThick;
            BestThreshold    = Threshold;
            BestHeightNorm   = HeightNorm;
            BestShardFalloff = ShardFalloff;
            BestIslandSize   = IslandSize;
            BestCellShardT   = CellShardT;

            const float SizeRatio = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
            const float IslandFreq = SC.ShapeFrequency / SizeRatio;
            const float ShardFreq  = SC.ShapeFrequency * 6.0f;
            BestFreq = FMath::Lerp(ShardFreq, IslandFreq, CellShardT);

            // FIX: Update the tracking ratio so it correctly selects the NEAREST cell 
            // instead of falling back to the last cell in the grid loop iterator.
            BestDistRatio = DistRatio; 
        }'''

replacement_cpp_2 = b'''        if (W > 0.001f)
        {
            FSkylandIslandData Island;
            Island.SkyAlt       = SkyAlt;
            Island.HalfThick    = HalfThick;
            Island.Threshold    = Threshold;
            Island.ShardT       = CellShardT;
            Island.HeightNorm   = HeightNorm;
            Island.ShardFalloff = ShardFalloff;
            Island.IslandSize   = IslandSize;

            const float SizeRatio = FMath::Max(1.f, IslandSize / SC.BaseIslandSize);
            const float IslandFreq = SC.ShapeFrequency / SizeRatio;
            const float ShardFreq  = SC.ShapeFrequency * 6.0f;
            const float BaseFreq = FMath::Lerp(ShardFreq, IslandFreq, CellShardT);
            Island.Freq = FMath::Max(BaseFreq, 0.00025f);

            Cache.Islands.Add(Island);
            Cache.bHasSkyland = true;
        }'''

# Target 3 (End Tail):
target_cpp_3 = b'''    if (MaxW <= 0.f) return Cache;




    Cache.SkyAlt       = BestSkyAlt;



    Cache.HalfThick    = BestHalfThick;



    Cache.Threshold    = BestThreshold;



    Cache.HeightNorm   = BestHeightNorm;



    Cache.ShardFalloff = BestShardFalloff;

    // ShardT drives falloff shape and noise in GetSkylandDensityFromCache.
    Cache.ShardT      = BestCellShardT;


    // Post-selection terrain clearance adjustment: commented out per user request.
    // The 200cm buffer was clamping HalfThick down too aggressively,
    // causing shards to lose thickness and appear as thin pillar slabs.
    // Island altitude is already set well above terrain via AltitudeBase;
    // the post-selection column-height adjustment below handles real clipping.
    //
    // const float Clearance = 200.f;
    // const float MaxAllow  = AltitudeBase - Clearance;
    // if (MaxAllow <= 0.f)
    //     HalfThick = 0.01f;
    // else
    //     HalfThick = FMath::Min(HalfThick, MaxAllow);




    // Freq: shards need much higher frequency noise to look jagged.

    // Low shards: ShapeFrequency * 6  (high-freq = rough, spiky silhouette)

    // High islands: ShapeFrequency / sqrt(SizeRatio)  (smooth, organic)

    // Use the selected cell's island size (not blended) for consistent shape


    const float SizeRatio = FMath::Max(1.f, BestIslandSize / SC.BaseIslandSize);



    // FIX: Linear scaling (was sqrt) to maintain consistent aspect ratio across island sizes.
    // With sqrt scaling, large islands had disproportionately small horizontal extent,

    // causing spikes. Linear scaling makes wavelength \xe2\x88\x9d IslandSize, so solid region scales
    // proportionally with thickness \xe2\x86\x92 flat discs at all sizes.
    const float IslandFreq = SC.ShapeFrequency / SizeRatio;  // Linear scaling for consistent aspect ratio



    const float ShardFreq = SC.ShapeFrequency * 6.0f;


    Cache.Freq = FMath::Lerp(ShardFreq, IslandFreq, BestCellShardT);

    Cache.Freq = FMath::Max(Cache.Freq, 0.00025f);




    Cache.Prob    = 0.5f;





    Cache.WX_base = X + Off.X;



    Cache.WY_base = Y + Off.Y;



    Cache.WX      = Cache.WX_base;



    Cache.WY      = Cache.WY_base;



    Cache.bHasSkyland = true;'''

replacement_cpp_3 = b'''    Cache.WX_base = X + Off.X;
    Cache.WY_base = Y + Off.Y;'''

# We must apply dynamic line ending replacement on THESE as well!
# Setup Chunk 1
apply_patch(filepath_cpp, target_cpp_1, replacement_cpp_1, "CPP Chunk 1")
apply_patch(filepath_cpp, target_cpp_2, replacement_cpp_2, "CPP Chunk 2")
apply_patch(filepath_cpp, target_cpp_3, replacement_cpp_3, "CPP Chunk 3")

# --- 3. Replace GetSkylandDensityFromCache Body ---
with open(filepath_cpp, 'rb') as f:
    content_cpp_full = f.read()

func_start = content_cpp_full.find(b"float FVoxelBiomeGenerators::GetSkylandDensityFromCache")
if func_start != -1:
    brace_start = content_cpp_full.find(b"{", func_start)
    index_end = content_cpp_full.find(b"return FMath::Clamp(D, -2.f, 2.f);\r\n}", brace_start)
    if index_end == -1:
        index_end = content_cpp_full.find(b"return FMath::Clamp(D, -2.f, 2.f);\n}", brace_start)
        
    if index_end != -1:
        index_end += len(b"return FMath::Clamp(D, -2.f, 2.f);\n}") # exact limit
        
        new_body = b'''
    if (!Cache.bHasSkyland) return -2.f;

    const FSkylandsLayerConfig& SC = Config.SkylandsLayer;
    const FVector Off = Config.GetSeedOffset();
    const float WX_base = Cache.WX_base;
    const float WY_base = Cache.WY_base;
    const float WZ = Z + Off.Z;

    float MaxD = -2.f;

    for (const FSkylandIslandData& Island : Cache.Islands)
    {
        const float HalfThick = Island.HalfThick;
        const float Margin = HalfThick * 0.4f;

        if (Z < Island.SkyAlt - HalfThick - Margin || Z > Island.SkyAlt + HalfThick + Margin)
            continue;

        const float FullRange = HalfThick + Margin;
        const float tCenter = FMath::Clamp((Z - Island.SkyAlt) / (FullRange + 1.f), -1.f, 1.f);

        float IslandFalloff;
        {
            if (tCenter >= 0.f) {
                const float FlatZone = 0.35f;
                if (tCenter < FlatZone) IslandFalloff = 1.0f;
                else {
                    const float nt = (tCenter - FlatZone) / (1.f - FlatZone);
                    IslandFalloff = FMath::SmoothStep(0.f, 1.f, 1.f - nt);
                }
            } else {
                const float t = FMath::Clamp(-tCenter, 0.f, 1.f);
                IslandFalloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(t, 0.85f));
            }
        }

        const float tAbs = FMath::Abs(tCenter);
        const float RockFalloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(tAbs, 0.6f));
        float Falloff = FMath::Lerp(RockFalloff, IslandFalloff, FMath::Max(0.40f, Island.ShardT));

        if (Island.ShardT < 0.3f) {
            const float RoundnessFactor = FMath::Lerp(1.0f, 0.6f, Island.ShardT);
            const float RoundedFalloff = FMath::SmoothStep(0.f, 1.f, 1.f - FMath::Pow(tAbs, RoundnessFactor));
            Falloff = FMath::Lerp(RoundedFalloff, Falloff, FMath::Lerp(0.8f, 0.2f, Island.ShardT));
        }

        if (Falloff < 0.001f) {
            const float MaxBreakUpEO = FMath::Lerp(0.50f, 2.80f, Island.HeightNorm);
            const float BreakUpStrengthEO = FMath::Lerp(0.10f, MaxBreakUpEO, Island.ShardT);
            const float BreakUp = FMath::Max(0.f, FastNoise3D(WX_base * 0.002f, WY_base * 0.002f, WZ * 0.001f)) * BreakUpStrengthEO;
            MaxD = FMath::Max(MaxD, -1.8f - BreakUp);
            continue;
        }

        float WX = WX_base;
        float WY = WY_base;
        if (SC.bEnableDomainWarping) {
            const float WF = SC.DomainWarpFrequency;
            WX += FastNoise3D(WX * WF + 10.f, WY * WF + 20.f, 0.f) * SC.DomainWarpStrength;
            WY += FastNoise3D(WX * WF + 50.f, WY * WF + 10.f, 0.f) * SC.DomainWarpStrength;
        }

        float ShapeDetail = 0.f;
        const float ZFreqScale = FMath::Lerp(0.50f, 0.05f, Island.ShardT);
        const float DetailStrength = FMath::Lerp(0.55f, 0.25f, Island.ShardT);
        if (Config.Performance.bEnable3DSkylandNoise || Island.ShardT < 0.5f) {
            ShapeDetail = FastNoise3D(WX * Island.Freq * 0.6f, WY * Island.Freq * 0.6f, WZ * Island.Freq * ZFreqScale) * DetailStrength;
        }

        const int32 Oct2D = FMath::Clamp(FMath::Min((int32)SC.ShapeOctaves, 2), 1, Config.Performance.MaxNoiseOctaves);
        const float ShapeZ = (Island.ShardT < 0.5f) ? WZ * Island.Freq : 0.f;
        const float ShapeXY = FBM(WX * Island.Freq, WY * Island.Freq, ShapeZ, Oct2D, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves);
        const float Shape = ShapeXY + ShapeDetail;

        float RootDensity = 0.f;
        if (SC.bEnableHangingRoots && tCenter < -0.25f) {
            const float RootZNorm = FMath::Clamp((-tCenter - 0.25f) / 0.75f, 0.f, 1.f);
            const float RootNoise = FMath::Max(0.f, FBM(WX * SC.RootFrequency, WY * SC.RootFrequency, WZ * SC.RootFrequency, 2, 2.0f, 0.5f, Config.Performance.MaxNoiseOctaves));
            RootDensity = RootNoise * (1.f - RootZNorm) * 0.4f * Falloff;
        }

        const float HorizStrength = FMath::SmoothStep(Island.Threshold, Island.Threshold + 0.4f, Shape);
        float D = HorizStrength * Falloff * 2.5f - (1.f - Falloff) * 1.8f + RootDensity;

        const float MaxBreakUp = FMath::Lerp(0.50f, 2.80f, Island.HeightNorm);
        const float BreakUpStrength = FMath::Lerp(0.10f, MaxBreakUp, Island.ShardT);
        const float BreakUp = FMath::Max(0.f, FastNoise3D(WX_base * 0.002f, WY_base * 0.002f, WZ * 0.001f)) * BreakUpStrength;
        
        float PlateauMask = 1.0f;
        if (Island.ShardT > 0.5f && tCenter > 0.0f) {
            PlateauMask = FMath::SmoothStep(0.15f, 0.45f, 1.0f - tCenter);
        }
        D -= BreakUp * PlateauMask;
        MaxD = FMath::Max(MaxD, D);
    }

    return FMath::Clamp(MaxD, -2.f, 2.f);
'''
        if b'\r\n' in content_cpp_full:
            new_body = new_body.replace(b'\n', b'\r\n')
        content_cpp_full = content_cpp_full[:brace_start+1] + new_body + content_cpp_full[index_end-1:]
        with open(filepath_cpp, 'wb') as f:
            f.write(content_cpp_full)
        print("Body replacement applied.")
    else:
        print("End brace index not found!")
else:
    print("Function not found!")

print("All tasks complete.")
