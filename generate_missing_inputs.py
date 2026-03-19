'''
Unreal Engine Python Script
Run this script inside the Unreal Editor: Output Log -> Python input line.
This will recreate empty Input Action assets in the Content directory to fix "The package to load does not exist" warnings.
'''
import unreal

actions = [
    "IA_Jump", "IA_Move", "IA_Look", "IA_MouseLook",
    "IA_Fly", "IA_FlyDown", "IA_Map", "IA_Pause", "IA_ToggleCamera"
]

path = "/Game/Input/Actions/"
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

# Ensure target directory exists
# (AssetTools doesn't automatically create directory assets unless created with content)

for action_name in actions:
    asset_path = f"{path}{action_name}"
    
    # Check if asset already exists
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        print(f"Skipping {action_name} - already exists")
        continue
        
    print(f"Creating missing Input Action: {action_name} at {path}")
    asset_tools.create_asset(
        asset_name=action_name,
        package_path=path,
        asset_class=unreal.InputAction,
        factory=unreal.InputActionFactoryNew()
    )

unreal.EditorAssetLibrary.save_directory(path)
print("Input Action restoration complete!")
