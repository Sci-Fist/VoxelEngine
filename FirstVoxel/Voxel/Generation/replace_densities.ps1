$Path = "c:\Users\Butch\Documents\Unreal Projects\FirstVoxel\Source\FirstVoxel\Voxel\Generation\VoxelGeneratorTask.cpp"
$Content = Get-Content $Path -Raw
$Content = $Content -replace '\bDensities\b', 'DenseChunk->Densities'
Set-Content $Path $Content
Write-Host "Replaced Densities inside VoxelGeneratorTask.cpp successfully."
