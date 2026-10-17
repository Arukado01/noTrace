param([Parameter(Mandatory=$true)][string]$Soldiers, [Parameter(Mandatory=$true)][string]$Prone)

Add-Type -AssemblyName System.Drawing
$drawingReferences = @([System.Drawing.Bitmap].Assembly.Location, [System.Drawing.Color].Assembly.Location) + @(Get-ChildItem -LiteralPath $PSHOME -Filter 'System.Private.Windows.*.dll' | ForEach-Object FullName)
Add-Type -ReferencedAssemblies $drawingReferences -TypeDefinition @'
using System;
using System.Drawing;
public static class NoTraceFrameBounds {
  public static int[][][] Read(string path, int rows, int columns) {
    using (var bitmap = new Bitmap(path)) {
      var rowBands = new int[rows,2];
      int row = 0, start = -1;
      for (int y=0; y<=bitmap.Height; ++y) {
        int count = 0;
        if (y<bitmap.Height) for (int x=0; x<bitmap.Width; ++x) if (bitmap.GetPixel(x,y).A>100) ++count;
        if (count>5 && start<0) start=y;
        if (count<=5 && start>=0) {
          if (row>=rows) throw new Exception("Demasiadas filas en el atlas");
          rowBands[row,0]=start; rowBands[row++,1]=y-1; start=-1;
        }
      }
      if (row!=rows) throw new Exception("Faltan filas en el atlas");
      var result = new int[rows][][];
      for (int r=0; r<rows; ++r) {
        result[r]=new int[columns][];
        int column=0; start=-1;
        for (int x=0; x<=bitmap.Width; ++x) {
          int count=0;
          if (x<bitmap.Width) for (int y=rowBands[r,0]; y<=rowBands[r,1]; ++y) if (bitmap.GetPixel(x,y).A>100) ++count;
          if (count>0 && start<0) start=x;
          if (count==0 && start>=0) {
            if (column>=columns) throw new Exception("Demasiados fotogramas en una fila");
            int top=bitmap.Height, bottom=-1;
            for (int px=start; px<x; ++px) for (int y=rowBands[r,0]; y<=rowBands[r,1]; ++y) if (bitmap.GetPixel(px,y).A>100) { top=Math.Min(top,y); bottom=Math.Max(bottom,y); }
            result[r][column++]=new int[]{start,top,x-start,bottom-top+1}; start=-1;
          }
        }
        if (column!=columns) throw new Exception("Faltan fotogramas en una fila");
      }
      return result;
    }
  }
}
'@

$characterDirectory = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../Characters'))
New-Item -ItemType Directory -Force -Path $characterDirectory | Out-Null
$sources = @(@{Path=$Soldiers; Name='SoldiersAtlas'; Rows=13; Columns=16}, @{Path=$Prone; Name='ProneAtlas'; Rows=8; Columns=6})
$manifest = @()
foreach ($atlas in $sources) {
  $frames = [NoTraceFrameBounds]::Read($atlas.Path, $atlas.Rows, $atlas.Columns)
  $lines = @()
  foreach ($row in $frames) { foreach ($frame in $row) { $lines += ($frame -join ' ') } }
  Copy-Item -LiteralPath $atlas.Path -Destination (Join-Path $characterDirectory ($atlas.Name+'.png'))
  [System.IO.File]::WriteAllLines((Join-Path $characterDirectory ($atlas.Name+'.frames')), [string[]]$lines)
  $manifest += @{file=$atlas.Name+'.png'; rows=$atlas.Rows; columns=$atlas.Columns; frames=$lines.Count; alphaThreshold=100}
}
& (Join-Path $PSScriptRoot 'anotar_bocas.ps1')
@{author='Carlos Cortina'; organization='CIO'; generator='OpenAI image_gen'; atlases=$manifest;
  directionPolicy='Ocho direcciones; espejo de diagonal inferior en atlas de pie; tumbado tiene ocho filas diseñadas.';
  animationPolicy='Fotogramas distintos de contacto y paso; arrastre alterna brazo y rodilla; no se divide una ilustración estática.';
  prompts=@('Atlas original 2D de soldados CIO azul y guardias oliva, direcciones y poses independientes, andar de cuatro fotogramas, agachado y arrastre de dos, idle, caída, cobertura, asomado y acción; transparente, píxeles definidos, sin vóxeles ni suelo.',
    'Atlas complementario tumbado y arrastre de 6 columnas y 8 filas: agente idle/izquierda/derecha y guardia idle/izquierda/derecha; cada fila orienta el cuerpo completo N,NE,E,SE,S,SW,W,NW; pixel art 2D original sin sombras ni cubos.')
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'soldados.json') -Encoding utf8
