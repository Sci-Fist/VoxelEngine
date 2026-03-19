import os

path = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Core\VoxelChunk.cpp"
output_file = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\found_clear_lines.txt"

with open(output_file, 'w', encoding='utf-8') as out:
    with open(path, 'r', encoding='utf-8') as f:
        for i, line in enumerate(f, 1):
            if "ProceduralMesh->ClearAllMeshSections()" in line:
                out.write(f"Line {i}: {line.strip()}\n")

print("Done")
