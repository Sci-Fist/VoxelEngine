import os

search_dir = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source"
tags = ["TODO", "FIXME"]
output_file = os.path.join(search_dir, "todos.txt")

with open(output_file, 'w', encoding='utf-8') as out:
    for root, dirs, files in os.walk(search_dir):
        for file in files:
            if file.endswith((".cpp", ".h", ".py")):
                path = os.path.join(root, file)
                try:
                    with open(path, 'r', encoding='utf-8') as f:
                        for i, line in enumerate(f, 1):
                            for tag in tags:
                                if tag in line:
                                    out.write(f"{path}:{i}: {line.strip()}\n")
                except:
                    pass

print(f"Done, wrote to {output_file}")
