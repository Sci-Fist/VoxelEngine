import numpy as np

def cross(a, b):
    return np.cross(a, b)

def check_axis(axis_name, edge_dir, v0_coord, v1_coord, v2_coord, v3_coord, bSolidToAir):
    v0 = np.array(v0_coord)
    v1 = np.array(v1_coord)
    v2 = np.array(v2_coord)
    v3 = np.array(v3_coord)

    # Current Winding Swap in EmitQuad:
    # if (bSolidToAir): v0, v1, v2 and v0, v2, v3
    # else:           v0, v2, v1 and v0, v3, v2
    
    if bSolidToAir:
        n1 = cross(v1 - v0, v2 - v0)
        n2 = cross(v2 - v0, v3 - v0)
    else:
        n1 = cross(v2 - v0, v1 - v0)
        n2 = cross(v3 - v0, v2 - v0)

    print(f"--- {axis_name} ---")
    print(f"Edge Trigger: {edge_dir}")
    print(f"bSolidToAir: {bSolidToAir}")
    print(f"Normal 1: {n1}")
    print(f"Normal 2: {n2}")
    avg = (n1 + n2) / 2
    print(f"Avg Normal: {avg}")
    # We WANT the avg normal to point along edge_dir for SolidToAir, and -edge_dir for AirToSolid!
    # Let's check:
    target = np.array(edge_dir) if bSolidToAir else -np.array(edge_dir)
    print(f"Target Norm: {target}")
    is_correct = np.dot(avg, target) > 0
    print(f"Correct: {is_correct}\n")


# 1. X-axis edges
# Edge is (X,Y,Z) to (X+1,Y,Z). Direction is +X: [1, 0, 0]
# sharing cells: (X,Y,Z), (X,Y,Z-1), (X,Y-1,Z-1), (X,Y-1,Z)
print("=== X-AXIS ===")
check_axis("X-Axis", [1, 0, 0], [0,0,0], [0,0,-1], [0,-1,-1], [0,-1,0], True)
check_axis("X-Axis", [1, 0, 0], [0,0,0], [0,0,-1], [0,-1,-1], [0,-1,0], False)

# 2. Y-axis edges
# Edge is (X,Y,Z) to (X,Y+1,Z). Direction is +Y: [0, 1, 0]
# sharing cells: (X,Y,Z), (X,Y,Z-1), (X-1,Y,Z-1), (X-1,Y,Z)
print("=== Y-AXIS ===")
check_axis("Y-Axis", [0, 1, 0], [0,0,0], [0,0,-1], [-1,0,-1], [-1,0,0], True)
check_axis("Y-Axis", [0, 1, 0], [0,0,0], [0,0,-1], [-1,0,-1], [-1,0,0], False)

# 3. Z-axis edges
# Edge is (X,Y,Z) to (X,Y,Z+1). Direction is +Z: [0, 0, 1]
# CURRENT Z-axis calls: (X,Y,Z), (X, Y-1, Z), (X-1, Y-1, Z), (X-1, Y, Z) (Which is what I added in step 321)
print("=== Z-AXIS (NEW) ===")
check_axis("Z-Axis", [0, 0, 1], [0,0,0], [0,-1,0], [-1,-1,0], [-1,0,0], True)
check_axis("Z-Axis", [0, 0, 1], [0,0,0], [0,-1,0], [-1,-1,0], [-1,0,0], False)

print("=== Z-AXIS (OLD) ===")
check_axis("Z-Axis (OLD)", [0, 0, 1], [0,0,0], [-1,0,0], [-1,-1,0], [0,-1,0], True)
check_axis("Z-Axis (OLD)", [0, 0, 1], [0,0,0], [-1,0,0], [-1,-1,0], [0,-1,0], False)
