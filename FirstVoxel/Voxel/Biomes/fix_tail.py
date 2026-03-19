filepath = r"c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Biomes\VoxelBiomeGenerators.cpp"

with open(filepath, 'rb') as f:
    content = f.read()

# Find exact start of the leftover block
start = content.find(b"if (MaxW <= 0.f) return Cache;")
if start == -1:
    print("Start target not found.")
    import sys
    sys.exit(1)

# Find the return Cache statement following it
end = content.find(b"return Cache;", start)
if end == -1:
    print("End target not found.")
    import sys
    sys.exit(1)

end += len(b"return Cache;") # include the statement

# Check if there's any carriage returns to preserve formatting
is_crlf = b'\r\n' in content

# Replacement text
replacement = b"    Cache.WX_base = X + Off.X;\n    Cache.WY_base = Y + Off.Y;\n    return Cache;"
if is_crlf:
    replacement = replacement.replace(b'\n', b'\r\n')

# Execute replacement
new_content = content[:start] + replacement + content[end:]

with open(filepath, 'wb') as f:
    f.write(new_content)

print("Tail fixed successfully.")
