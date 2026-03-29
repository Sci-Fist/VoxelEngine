$vault = 'C:\Users\Butch\Documents\ObsidianVaults\YveMemory'  
$allFiles = Get-ChildItem -Path $vault -Filter '*.md' -Recurse | Where-Object { $_.FullName -notmatch '\.obsidian' }  
Write-Host 'Total .md files found:' $allFiles.Count  
