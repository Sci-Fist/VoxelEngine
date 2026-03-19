import os

search_dir = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source"
target = "SpawnChunk"
output_file = os.path.join(search_dir, "found_spawn_chunk.txt")

with open(output_file, 'w', encoding='utf-8') as out:
    for root, dirs, files in os.walk(search_dir):
        for file in files:
            if file.endswith((".cpp", ".h")):
                path = os.path.join(root, file)
                try:
                    with open(path, 'r', encoding='utf-8') as f:
                        for i, line in enumerate(f, 1):
                            if "void" in line and target in line and "{" in line:
                                out.write(f"{path}:{i}: {line.strip()}\n")
                            elif "class" in line or "struct" in line:
                                pass
                except:
                    pass

print("Done")
