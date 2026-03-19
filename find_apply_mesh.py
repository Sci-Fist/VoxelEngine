import os

path = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Core\VoxelChunk.cpp"
output_file = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\apply_mesh_body.txt"

with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

import re
# Match double colon and braces
pattern = r"void\s+AVoxelChunk::ApplyMesh\([\s\S]*?\{([\s\S]*?)\n\}"

match = re.search(pattern, content)
with open(output_file, 'w', encoding='utf-8') as out:
    if match:
        out.write(match.group(0))
        print("Found ApplyMesh!")
    else:
        out.write("ApplyMesh not found with regex")
        # Just write lines containing ApplyMesh to be safe
        out.write("\n\nFallback line scan:\n")
        f.seek(0)
        for i, line in enumerate(f, 1):
            if "ApplyMesh" in line:
                out.write(f"{i}: {line.strip()}\n")

print("Done")
