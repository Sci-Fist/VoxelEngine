with open(r"C:\Users\Butch\AppData\Local\UnrealBuildTool\Log.txt", "r", encoding="utf-8") as f:
    for line in f:
        if "): error" in line or "): warning" in line or "muss" in line or "unzulässig" in line:
            print(line.strip())
        elif "Compile [x64] Voxel" in line:
            print("--- " + line.strip() + " ---")
