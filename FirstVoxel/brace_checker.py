import sys

def check_braces(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    level = 0
    lines = content.split('\n')
    for i, line in enumerate(lines):
        clean_line = line.split('//')[0] # ignore comments
        for char in clean_line:
            if char == '{':
                level += 1
            elif char == '}':
                level -= 1
        
        # Print drops to 0 specifically
        if level == 0 and '}' in clean_line:
            print(f"Line {i+1}: Drops to level 0. Line: {line.strip()}")
            
    print(f"Final Level at EOF: {level}")

check_braces(r'c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Generation\VoxelNoiseSIMD.cpp')
