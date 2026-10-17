const path = require('node:path');
const sharp = require('sharp');
const root = path.resolve(__dirname, '..');
const assets = ['Branding/WindowIcon', 'Interface/Cursor', 'Interface/CursorActive'];

async function main() {
  for (const asset of assets) {
    await sharp(path.join(root, asset + '.svg')).png().toFile(path.join(root, asset + '.png'));
    console.log(asset + '.png', await sharp(path.join(root, asset + '.png')).metadata());
  }
  const logoPath = path.join(root, 'Branding/Logo.png');
  const { data, info } = await sharp(logoPath).raw().toBuffer({ resolveWithObject: true });
  let minX = info.width, minY = info.height, maxX = -1, maxY = -1;
  for (let y = 0; y < info.height; y++) {
    for (let x = 0; x < info.width; x++) {
      if (data[(y * info.width + x) * info.channels + 3] > 0) {
        minX = Math.min(minX, x); minY = Math.min(minY, y);
        maxX = Math.max(maxX, x); maxY = Math.max(maxY, y);
      }
    }
  }
  console.log('Logo: límites alfa inclusivos', { minX, minY, maxX, maxY, width: maxX - minX + 1, height: maxY - minY + 1 });
}

main().catch(error => { console.error(error); process.exitCode = 1; });
