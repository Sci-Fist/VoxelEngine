import os

path = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Core\VoxelChunk.cpp"
output_file = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\found_surround_clear.txt"

with open(path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

with open(output_file, 'w', encoding='utf-8') as out:
    for i, line in enumerate(lines):
        if "ProceduralMesh->ClearAllMeshSections()" in line:
            out.write(f"--- MATCH AT LINE {i+1} ---\n")
            # Print 5 lines before and after
            start = max(0, i - 10)
            end = min(len(lines), i + 10)
            for j in range(start, end):
                out.write(f"{j+1}: {lines[j]}")
            out.write("\n\n")

print("Done")
