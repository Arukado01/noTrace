const path = require('node:path');
const sharp = require('sharp');

const root = path.resolve(__dirname, '..');
const source = path.join(__dirname, 'MenuCoopPixel.generated.png');
const output = path.join(root, 'Backgrounds/MenuCoopPixel.png');

async function main() {
  await sharp(source)
    .resize(320, 180, { fit: 'cover', position: 'centre', kernel: sharp.kernel.nearest })
    .png({ palette: true, colours: 16, dither: 0 })
    .toFile(output);
  console.log(await sharp(output).metadata());
}

main().catch(error => { console.error(error); process.exitCode = 1; });
