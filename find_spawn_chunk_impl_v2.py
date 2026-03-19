import os

path = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Core\World\VoxelWorld.cpp"
output_file = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\spawn_chunk_impl_v2.txt"

with open(output_file, 'w', encoding='utf-8') as out:
    with open(path, 'r', encoding='utf-8') as f:
        for i, line in enumerate(f, 1):
            if "void AVoxelWorld::SpawnChunk" in line:
                out.write(f"Found line {i}: {line.strip()}\n")

print("Done")
