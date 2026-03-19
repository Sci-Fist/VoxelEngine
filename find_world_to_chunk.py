import os

search_dir = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel"
target = "WorldToChunkCoord"
output_file = os.path.join(search_dir, "found_coord_def.txt")

with open(output_file, 'w', encoding='utf-8') as out:
    for root, dirs, files in os.walk(search_dir):
        for file in files:
            if file.endswith((".h", ".cpp")):
                path = os.path.join(root, file)
                try:
                    with open(path, 'r', encoding='utf-8') as f:
                        for i, line in enumerate(f, 1):
                            if target in line:
                                out.write(f"{path}:{i}: {line.strip()}\n")
                except:
                    pass

print("Done")
