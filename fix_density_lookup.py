import re

path = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Generation\VoxelGeneratorTask.cpp"

with open(path, 'r', encoding='utf-8') as f:
    target_content = f.read()

# Pattern for the Bedrock fully solid check
# It searches for `if (MaxWorldZ < Config.CaveTunnels.BedrockDepth)` or surrounding constructs
# and fixes the `if (DataMap)` loop inside it.

pattern = r'''(\s+if\s*\(DataMap\)\s*\{[\s\S]*?const\s+int32\s+GX\s*=[\s\S]*?GetDensity\([\s\S]*?\}\s*\}\s*)'''

match = re.search(pattern, target_content)
if match:
    # Look for the SECOND match, since the first one was already replaced!
    # Wait, the first one was replaced WITH `if (DenseHasEdit[Idx])`!
    # So there is ONLY ONE `if (DataMap)` left matching this complete pattern in that file block!
    
    print("Found lock contention loop matches!")
    
    replacement = """\n                if (DenseHasEdit[Idx])
                {
                    const float Override = DenseEditVals[Idx];
                    D = (Override < 0.f) ? FMath::Min(D, Override) : FMath::Max(D, Override);
                }\n"""
                
    fixed_content = re.sub(pattern, replacement, target_content, count=1)
    
    with open(path, 'w', encoding='utf-8') as f:
        f.write(fixed_content)
    print("Locked loops fully patched!")
else:
    print("No matches found for DataMap inner block.")
