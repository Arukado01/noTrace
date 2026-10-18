import argparse
import collections
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import urllib.parse
import urllib.request
import zipfile

parser = argparse.ArgumentParser(description="Obtiene únicamente las grabaciones CC0 declaradas, conservando evidencia y hashes.")
parser.add_argument("--root", type=Path, default=Path("build/qa-cierre-eventos-20261017/audio-candidates"))
parser.add_argument("--fetch", action="store_true", help="Autoriza descargas públicas CC0 si faltan las fuentes locales.")
args = parser.parse_args()
root = args.root.resolve()
root.mkdir(parents=True, exist_ok=True)
cache = root / "downloads"
cache.mkdir(exist_ok=True)
manifest = json.loads((Path(__file__).resolve().parent / "Sources.json").read_text(encoding="utf-8"))
headers = {"User-Agent": "Mozilla/5.0 (compatible; no-trace-audio-assets/1.0)"}
groups = collections.defaultdict(list)
for source in manifest["sources"]:
    path = (root / source["file"]).resolve()
    if source["license"] != "CC0-1.0" or not path.is_relative_to(root): raise ValueError("Fuente no autorizada")
    if path.exists() and hashlib.file_digest(path.open("rb"), "sha256").hexdigest() == source["sourceFileSha256"]:
        continue
    groups[source["downloadSha256"]].append(source)
for digest, sources in groups.items():
    if not args.fetch: raise ValueError("Falta una fuente verificada; vuelve a ejecutar con --fetch: " + sources[0]["file"])
    for source_url in {source["sourceUrl"] for source in sources}:
        page = urllib.request.urlopen(urllib.request.Request(source_url, headers=headers), timeout=45).read()
        if b"creativecommons.org/publicdomain/zero/1.0" not in page: raise ValueError("La fuente ya no acredita CC0: " + source_url)
        evidence = root / "source-pages" / (hashlib.sha256(source_url.encode()).hexdigest() + ".html")
        evidence.parent.mkdir(exist_ok=True)
        evidence.write_bytes(page)
    url = sources[0]["downloadUrl"]
    if urllib.parse.urlparse(url).scheme != "https": raise ValueError("La descarga debe usar HTTPS")
    suffix = Path(urllib.parse.urlparse(url).path).suffix.lower()
    download = cache / (digest + suffix)
    if not download.exists():
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=45) as response:
            expected = int(response.headers.get("Content-Length", "0"))
            if expected > 200 * 1024 * 1024: raise ValueError("Archivo demasiado grande")
            total = 0
            with download.open("wb") as output:
                while block := response.read(1024 * 1024):
                    total += len(block)
                    if total > 200 * 1024 * 1024: raise ValueError("Descarga supera el límite")
                    output.write(block)
    if hashlib.file_digest(download.open("rb"), "sha256").hexdigest() != digest: raise ValueError("La descarga cambió de contenido: " + url)
    for source in sources:
        target = root / source["file"]
        target.parent.mkdir(parents=True, exist_ok=True)
        member = source.get("archiveMember")
        if member:
            name = PurePosixPath(member.replace("\\", "/"))
            if name.is_absolute() or ".." in name.parts or ":" in member: raise ValueError("Ruta de archivo no segura")
            if suffix == ".zip":
                with zipfile.ZipFile(download) as packed: data = packed.read(member)
            elif suffix == ".7z":
                data = subprocess.run(["tar", "-xOf", str(download), "--", member], check=True, capture_output=True).stdout
            else: raise ValueError("Formato de paquete no admitido")
        else:
            data = download.read_bytes()
        if hashlib.sha256(data).hexdigest() != source["sourceFileSha256"]: raise ValueError("La toma extraída no coincide: " + source["file"])
        target.write_bytes(data)
    print(json.dumps({"download": url, "sources": len(sources)}), flush=True)
print(json.dumps({"verified": len(manifest["sources"]), "missing": 0}))
