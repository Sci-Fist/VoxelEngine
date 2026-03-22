path = r'c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Generation\VoxelMeshGenerator.cpp'

with open(path, 'r', encoding='utf-8') as f:
    lines = f.readlines()

count = 0
for i in range(len(lines)):
    # Look for the Z-axis edges block section to be safe
    if 'Z-axis edges' in lines[max(0, i-3)]:
        if 'Idx(X, Y-1, Z, S)' in lines[i] or 'Idx(X,Y-1,Z,S)' in lines[i]:
            # Exact line 329 formulation
            orig = lines[i]
            # Strip spaces inside Idx calls to enable standard replace
            with_spaces = 'Idx(X-1, Y-1, Z, S),Idx(X, Y-1, Z, S)'
            without_spaces = 'Idx(X-1,Y-1,Z,S),Idx(X,Y-1,Z,S)'
            
            if with_spaces in orig:
                lines[i] = orig.replace(with_spaces, 'Idx(X-1, Y-1, Z, S),Idx(X-1, Y, Z, S)')
                count += 1
            elif without_spaces in orig:
                lines[i] = orig.replace(without_spaces, 'Idx(X-1,Y-1,Z,S),Idx(X-1,Y,Z,S)')
                count += 1
            else:
                # Direct string substitution fallback for ANY formatting of that line
                line_no_space = orig.replace(' ', '')
                if 'Idx(X-1,Y-1,Z,S),Idx(X,Y-1,Z,S)' in line_no_space:
                    # Let's override the line based on the view_file literal
                    lines[i] = '              EmitQuad(Idx(X,Y,Z,S),Idx(X,Y-1,Z,S),Idx(X-1,Y-1,Z,S),Idx(X-1,Y,Z,S),\n'
                    count += 1

if count > 0:
    with open(path, 'w', encoding='utf-8') as f:
        f.writelines(lines)
    print(f"Replaced {count} instances.")
else:
    print("Could not find line in file.")
