with open(r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Generation\VoxelNoiseSIMD.cpp", "r") as f:
    lines = f.readlines()
    for i in range(318, 325):
        print(f"Line {i+1}: {repr(lines[i])}")
