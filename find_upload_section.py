import os

path = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Core\VoxelChunk.cpp"
output_file = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\upload_section_body.txt"

with open(path, 'r', encoding='utf-8') as f:
    content = f.read()

import re
pattern = r"void\s+AVoxelChunk::UploadSection\([\s\S]*?\{([\s\S]*?)\n\}"

match = re.search(pattern, content)
with open(output_file, 'w', encoding='utf-8') as out:
    if match:
        out.write(match.group(0))
        print("Found UploadSection!")
    else:
        out.write("UploadSection not found with regex\n")
        # Line scan fallback
        f.seek(0)
        for i, line in enumerate(f, 1):
            if "UploadSection" in line and "::" in line:
                out.write(f"{i}: {line.strip()}\n")

print("Done")
