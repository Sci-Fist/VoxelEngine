import os

path = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Core\VoxelChunk.cpp"
output_file = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\found_surround_upload.txt"

with open(path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

with open(output_file, 'w', encoding='utf-8') as out:
    for i, line in enumerate(lines):
        if "void AVoxelChunk::UploadSection" in line:
            out.write(f"--- MATCH AT LINE {i+1} ---\n")
            start = max(0, i - 2)
            end = min(len(lines), i + 20)
            for j in range(start, end):
                out.write(f"{j+1}: {lines[j]}")
            out.write("\n\n")

print("Done")
