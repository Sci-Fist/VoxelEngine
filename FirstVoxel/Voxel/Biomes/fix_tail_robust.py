filepath = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Biomes\VoxelBiomeGenerators.cpp"

with open(filepath, 'rb') as f:
    content = f.read()

# Find where the previous patch injected
start = content.find(b"Cache.WX_base = X + Off.X;")
if start == -1:
    print("Start not found. Already fixed?")
    import sys
    sys.exit(0)

# Find the start of the next function to bound our search
index_next_func = content.find(b"float FVoxelBiomeGenerators::GetSkylandDensityFromCache")
if index_next_func == -1:
    print("Next function not found")
    import sys
    sys.exit(1)

# Find the closing brace of GetSkylandColumnCache
index_brace = content.rfind(b"}", start, index_next_func)
if index_brace == -1:
    print("Closing brace not found")
    import sys
    sys.exit(1)

# Replacement
replacement = b"Cache.WX_base = X + Off.X;\n    Cache.WY_base = Y + Off.Y;\n    return Cache;\n"

is_crlf = b'\r\n' in content
if is_crlf:
    replacement = replacement.replace(b'\n', b'\r\n')

new_content = content[:start] + replacement + content[index_brace:]

with open(filepath, 'wb') as f:
    f.write(new_content)

print("Robust cleanup successful.")
