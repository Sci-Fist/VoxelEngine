import os

path = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Core\World\VoxelWorld.cpp"
output_file = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\spawn_chunk_impl.txt"

with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

import re
pattern = r"void\s+AVoxelWorld::SpawnChunk\([\s\S]*?\{([\s\S]*?)\n\}"

match = re.search(pattern, content)
with open(output_file, 'w', encoding='utf-8') as out:
    if match:
        out.write(match.group(0))
        print("Found SpawnChunk impl!")
    else:
        out.write("SpawnChunk not found with regex\n")
        # Direct line scan fallback
        f.seek(0)
        for i, line in enumerate(f, 1):
            if "SpawnChunk" in line and "::" in line:
                out.write(f"{i}: {line.strip()}\n")

print("Done")
