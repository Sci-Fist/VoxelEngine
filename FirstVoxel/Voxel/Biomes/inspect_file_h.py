import sys

filepath_h = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Biomes\VoxelBiomeGenerators.h"

with open(filepath_h, 'rb') as f:
    content = f.read()

index = content.find(b"struct FSkylandColumnCache")
if index != -1:
    print("Found H!")
    start = max(0, index - 50)
    end = min(len(content), index + 1000)
    print(repr(content[start:end]))
else:
    print("Not found H!")
