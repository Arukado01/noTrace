param([string]$Output = (Join-Path $PSScriptRoot '../../../../build/sprite-socket-review'))
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$spriteDirectory = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Characters'))
foreach ($atlas in @(@{Name='SoldiersAtlas';Rows=13;Columns=16;Grid=4}, @{Name='ProneAtlas';Rows=8;Columns=6;Grid=3})) {
  $bitmap = [System.Drawing.Bitmap]::new((Join-Path $spriteDirectory ($atlas.Name+'.png')))
  try {
    $rectangles = Get-Content (Join-Path $spriteDirectory ($atlas.Name+'.frames'))
    $sockets = Get-Content (Join-Path $spriteDirectory ($atlas.Name+'.muzzles'))
    for ($row=0; $row -lt $atlas.Rows; ++$row) {
      $sheet = [System.Drawing.Bitmap]::new($atlas.Grid*240,[int][Math]::Ceiling($atlas.Columns/$atlas.Grid)*280)
      $graphics = [System.Drawing.Graphics]::FromImage($sheet)
      $font = [System.Drawing.Font]::new('Consolas',13)
      try {
        $graphics.Clear([System.Drawing.Color]::FromArgb(30,40,50)); $graphics.InterpolationMode=[System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        for ($column=0; $column -lt $atlas.Columns; ++$column) {
          $index=$row*$atlas.Columns+$column
          $rect=$rectangles[$index].Split(' ') | ForEach-Object { [int]$_ }
          $socket=$sockets[$index].Split(' ') | ForEach-Object { [double]::Parse($_,[System.Globalization.CultureInfo]::InvariantCulture) }
          $left=($column%$atlas.Grid)*240+10; $top=[Math]::Floor($column/$atlas.Grid)*280+35
          $scale=[Math]::Min(210.0/$rect[2],230.0/$rect[3]); $width=[int]($rect[2]*$scale); $height=[int]($rect[3]*$scale)
          $graphics.DrawImage($bitmap,[System.Drawing.Rectangle]::new($left,$top,$width,$height),$rect[0],$rect[1],$rect[2],$rect[3],[System.Drawing.GraphicsUnit]::Pixel)
          $graphics.DrawString("$index col $column",$font,[System.Drawing.Brushes]::White,$left,$top-31)
          if ($socket[0] -ge 0) {
            $x=[single]($left+$socket[0]*$width/$rect[2]); $y=[single]($top+$socket[1]*$height/$rect[3])
            $graphics.DrawEllipse([System.Drawing.Pens]::Yellow,$x-5,$y-5,10,10)
          }
        }
        $sheet.Save((Join-Path $Output ($atlas.Name+'-'+$row+'.png')))
      } finally { $font.Dispose(); $graphics.Dispose(); $sheet.Dispose() }
    }
  } finally { $bitmap.Dispose() }
}
