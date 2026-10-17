Add-Type -AssemblyName System.Drawing
$spriteDirectory = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Characters'))
# Cada punto identifica el cañón revisado visualmente, no el extremo general de la silueta.
$standingPoints = @(
  '99,38 86,59 94,59 96,61 78,60 99,40 99,42 99,43 99,40 - - - 8,22 99,28 - 99,65',
  '99,17 99,13 87,57 98,61 99,28 99,32 99,32 99,32 99,40 - - - 96,8 99,28 - 99,60',
  '99,25 99,23 99,23 99,28 99,29 99,34 99,35 99,38 99,40 - - - 6,20 99,23 - 99,54',
  '99,51 3,60 7,55 99,60 5,20 99,38 99,35 99,38 99,40 - - - 97,8 99,30 - 99,58',
  '2,52 2,62 2,58 3,60 3,62 2,66 2,61 2,58 99,40 - - - 97,8 99,30 - 99,65',
  '2,37 2,41 2,31 2,34 2,35 2,43 2,44 2,41 99,40 - - - 8,22 99,27 - 99,60',
  '2,25 2,28 2,26 2,35 2,25 2,39 2,48 2,33 99,40 - - - 97,8 99,22 - 99,63',
  '99,39 90,60 94,60 99,62 99,31 99,34 99,41 99,42 99,40 - - - 8,22 99,28 - 99,65',
  '99,18 99,13 94,57 99,61 99,27 99,34 99,33 99,38 99,40 - - - 97,8 99,29 - 99,60',
  '99,29 99,23 99,23 99,28 99,29 99,34 99,34 99,34 99,40 - - - 8,22 99,24 - 99,54',
  '99,51 3,60 3,55 99,60 4,20 99,38 99,35 99,38 99,40 - - - 97,8 99,29 - 99,65',
  '2,50 2,62 2,57 3,61 3,64 2,60 2,57 2,57 99,40 - - - 97,8 99,27 - 99,65',
  '2,38 2,40 2,35 2,34 2,35 2,43 2,42 2,40 99,40 - - - 8,22 99,29 - 99,63'
)
$pronePoints = @(
  '58,1 50,1 55,1 58,1 50,1 55,1',
  '99,2 99,2 99,2 99,2 99,2 99,2',
  '99,41 99,35 99,41 99,41 99,35 99,41',
  '99,90 99,94 99,90 99,90 99,94 99,90',
  '49,99 64,99 49,99 49,99 64,99 49,99',
  '1,88 1,96 1,94 1,88 1,96 1,94',
  '1,45 1,45 1,41 1,45 1,45 1,41',
  '1,1 1,1 1,1 1,1 1,1 1,1'
)
foreach ($atlas in @(@{Name='SoldiersAtlas';Columns=16;Points=$standingPoints}, @{Name='ProneAtlas';Columns=6;Points=$pronePoints})) {
  $bitmap = [System.Drawing.Bitmap]::new((Join-Path $spriteDirectory ($atlas.Name+'.png')))
  try {
    $rectangles = Get-Content (Join-Path $spriteDirectory ($atlas.Name+'.frames'))
    $sockets = @()
    for ($row = 0; $row -lt $atlas.Points.Count; ++$row) {
      $points = $atlas.Points[$row].Split(' ')
      if ($points.Count -ne $atlas.Columns) { throw 'La fila de bocas no coincide con los fotogramas' }
      for ($column = 0; $column -lt $atlas.Columns; ++$column) {
        if ($points[$column] -eq '-') { $sockets += '-1 -1'; continue }
        $rect = $rectangles[$row*$atlas.Columns+$column].Split(' ') | ForEach-Object { [int]$_ }
        $point = $points[$column].Split(',') | ForEach-Object { [double]$_ }
        $desiredX = ($rect[2]-1)*$point[0]/100; $desiredY = ($rect[3]-1)*$point[1]/100
        $nearest = [double]::MaxValue; $socketX=-1; $socketY=-1
        for ($y=0; $y -lt $rect[3]; ++$y) { for ($x=0; $x -lt $rect[2]; ++$x) {
          if ($bitmap.GetPixel($rect[0]+$x,$rect[1]+$y).A -lt 220) { continue }
          $distance=($x-$desiredX)*($x-$desiredX)+($y-$desiredY)*($y-$desiredY)
          if ($distance -lt $nearest) { $nearest=$distance; $socketX=$x; $socketY=$y }
        } }
        if ($socketX -lt 0 -or $nearest -gt 100) { throw ('Boca no encontrada en '+$atlas.Name+' '+($row*$atlas.Columns+$column)) }
        $sockets += (($socketX.ToString()+'.5')+' '+($socketY.ToString()+'.5'))
      }
    }
    [System.IO.File]::WriteAllLines((Join-Path $spriteDirectory ($atlas.Name+'.muzzles')), [string[]]$sockets)
  } finally { $bitmap.Dispose() }
}
