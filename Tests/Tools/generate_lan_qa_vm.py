"""Verifica y publica el paquete QA y su ejecutor limitado para la VM."""

import argparse
import csv
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import time
import uuid
import zipfile


REPO = Path(__file__).resolve().parents[2]
BUILD = REPO / "build"
LABEL = "d7ca796-qa-lan-20261017"
PROTOCOL = 6
TRANSFER = BUILD / "vm-lan-transfer-7c19ef1"
EVIDENCE = BUILD / "vm-lan-evidence-7c19ef1"
TEMPLATE = Path(__file__).with_name("LanQaVmRunner.cs")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest().upper()


def assert_regular(path, root):
    path = Path(path)
    require(path.resolve().is_relative_to(Path(root).resolve()), "Ruta exterior al destino permitido")
    require(path.is_file() and not path.is_symlink(), "Archivo inexistente o no regular: " + str(path))
    for parent in path.parents:
        require(not parent.is_symlink(), "Carpeta enlazada no permitida")
        if parent.resolve() == Path(root).resolve():
            break


def package_label(summary):
    label = summary.get("name")
    require(type(label) is str and re.fullmatch("[a-z0-9][a-z0-9-]{0,95}", label), "Etiqueta de entrega no valida")
    return label


def load_package(summary_path):
    summary_path = Path(summary_path).resolve()
    require(summary_path.parent == BUILD.resolve(), "El resumen debe estar directamente en build")
    summary = json.loads(summary_path.read_text(encoding="utf-8-sig"))
    label = package_label(summary)
    require(type(summary["metadata"]["Protocol"]) is int and summary["metadata"]["Protocol"] == PROTOCOL, "Protocolo incompatible")
    archive = Path(summary["zip"]).resolve()
    folder = Path(summary["directory"]).resolve()
    require(archive == BUILD / ("NO_TRACE_LAN-" + label + ".zip"), "ZIP inesperado")
    require(folder == BUILD / ("NO_TRACE_LAN-" + label), "Carpeta inesperada")
    assert_regular(archive, BUILD)
    require(digest(archive) == summary["zip_sha256"], "SHA256 del ZIP incorrecto")
    with zipfile.ZipFile(archive) as zipped:
        require(zipped.testzip() is None, "CRC incorrecto")
        payload = zipped.read("SHA256SUMS.csv")
        require(hashlib.sha256(payload).hexdigest().upper() == summary["manifest_sha256"], "SHA256 del manifiesto incorrecto")
        rows = list(csv.DictReader(io.StringIO(payload.decode("utf-8-sig"))))
        require(len(rows) == summary["manifest_records"] and rows, "Recuento incorrecto")
        seen = set()
        for row in rows:
            require(set(row) == {"File", "Bytes", "SHA256"}, "Columnas del manifiesto incorrectas")
            name = row["File"]
            parts = PurePosixPath(name)
            require(name == parts.as_posix() and not parts.is_absolute() and "\\" not in name and ":" not in name and all(p not in {".", "..", ""} for p in parts.parts), "Ruta insegura")
            require(not any(p.lower() in {"docs", "sources", "screenshots", ".agents", "skills"} for p in parts.parts) and parts.name.lower() != "save.json", "Contenido no permitido")
            require(name.lower() not in seen and re.fullmatch("[A-Fa-f0-9]{64}", row["SHA256"]) and row["Bytes"].isdigit(), "Registro incorrecto")
            seen.add(name.lower())
            data = zipped.read(name)
            require(len(data) == int(row["Bytes"]) and hashlib.sha256(data).hexdigest().upper() == row["SHA256"].upper(), "Archivo ZIP incorrecto: " + name)
        require(len(zipped.infolist()) == len(rows) + 1 and set(zipped.namelist()) == {r["File"] for r in rows} | {"SHA256SUMS.csv"}, "Entradas extra o repetidas")
        require(all(not e.is_dir() and (e.external_attr >> 16) & 0o170000 != 0o120000 for e in zipped.infolist()), "Enlace o directorio dentro del ZIP")
        metadata = json.loads(zipped.read("PROCEDENCIA.json").decode("utf-8-sig"))
        require(metadata == summary["metadata"], "Procedencia distinta del resumen")
    qa_sha = summary["executables_sha256"]["NoTraceLanQa.exe"]
    require(re.fullmatch("[A-F0-9]{64}", qa_sha), "SHA256 QA incorrecto")
    require(next((r["SHA256"].upper() for r in rows if r["File"] == "NoTraceLanQa.exe"), None) == qa_sha, "El manifiesto no contiene el QA esperado")
    verify_folder(folder, rows, summary)
    return summary, rows


def verify_folder(folder, rows, summary):
    require(digest(folder / "SHA256SUMS.csv") == summary["manifest_sha256"], "Manifiesto local distinto")
    for row in rows:
        path = folder / row["File"]
        assert_regular(path, folder)
        require(path.stat().st_size == int(row["Bytes"]) and digest(path) == row["SHA256"].upper(), "Archivo local distinto: " + row["File"])


def render(summary):
    source = TEMPLATE.read_text(encoding="utf-8")
    replacements = {
        "@@LABEL@@": package_label(summary),
        "@@PROTOCOL@@": str(PROTOCOL),
        "@@ZIP_NAME@@": Path(summary["zip"]).name,
        "@@ZIP_SHA@@": summary["zip_sha256"],
        "@@MANIFEST_SHA@@": summary["manifest_sha256"],
        "@@QA_SHA@@": summary["executables_sha256"]["NoTraceLanQa.exe"],
        "@@COUNT@@": str(summary["manifest_records"]),
    }
    for token, value in replacements.items():
        require('"' not in value and "\n" not in value and "\\" not in value, "Token no escalar")
        source = source.replace(token, value)
    require(not re.search(r"@@[A-Z_]+@@", source), "Tokens pendientes")
    source.encode("ascii")
    return source


def runner_destination(source):
    return BUILD / ("qa-vm-runner-" + hashlib.sha256(source.encode("ascii")).hexdigest()[:24])


def compile_runner(source, destination):
    destination = Path(destination)
    require(destination.resolve().is_relative_to(BUILD.resolve()), "El supervisor compilado debe permanecer en build")
    destination.mkdir(parents=True, exist_ok=True)
    source_path = destination / "LanQaVmRunner.generated.cs"
    executable = destination / "NoTraceLanQaVmRunner.exe"
    if executable.exists():
        require(source_path.is_file() and source_path.read_text(encoding="ascii") == source, "El supervisor existente tiene otra procedencia")
        return executable
    source_path.write_text(source, encoding="ascii")
    framework = Path("C:/Windows/Microsoft.NET/Framework64/v4.0.30319")
    compiler = framework / "csc.exe"
    require(compiler.is_file(), "No se encontro el compilador .NET Framework de Windows")
    arguments = [str(compiler), "/nologo", "/target:exe", "/platform:x64", "/optimize+", "/warn:4", "/out:" + str(executable), "/reference:" + str(framework / "System.Web.Extensions.dll"), "/reference:" + str(framework / "System.IO.Compression.dll"), "/reference:" + str(framework / "System.IO.Compression.FileSystem.dll"), str(source_path)]
    result = subprocess.run(arguments, capture_output=True, text=True, timeout=60, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    (destination / "compile-stdout.log").write_text(result.stdout, encoding="utf-8")
    (destination / "compile-stderr.log").write_text(result.stderr, encoding="utf-8")
    require(result.returncode == 0 and executable.is_file(), "No compilo el supervisor C#: " + result.stdout + result.stderr)
    return executable


def publish_staging(staging, target, verify):
    for attempt in range(25):
        try:
            staging.rename(target)
            return
        except FileExistsError:
            verify(target)
            return
        except PermissionError:
            if target.exists():
                verify(target)
                return
            if attempt == 24:
                raise
            time.sleep(0.05)


def prepare_host_package(summary, rows):
    host = BUILD / ("lan-host-vm-" + package_label(summary))
    if not host.exists():
        staging = BUILD / (host.name + ".preparing-" + uuid.uuid4().hex)
        staging.mkdir()
        try:
            with zipfile.ZipFile(summary["zip"]) as zipped:
                zipped.extractall(staging)
            verify_folder(staging, rows, summary)
            publish_staging(staging, host, lambda folder: verify_folder(folder, rows, summary))
        except Exception as error:
            raise RuntimeError("Copia incompleta conservada en " + str(staging) + ": " + str(error)) from error
    verify_folder(host, rows, summary)
    return host


def publish_archive(summary):
    target = TRANSFER / Path(summary["zip"]).name
    if not target.exists():
        temporary = target.with_name(target.name + ".preparing-" + uuid.uuid4().hex)
        with Path(summary["zip"]).open("rb") as read, temporary.open("xb") as write:
            shutil.copyfileobj(read, write)
        require(digest(temporary) == summary["zip_sha256"], "El ZIP compartido no termino de copiarse: " + str(temporary))
        publish_staging(temporary, target, lambda path: require(digest(path) == summary["zip_sha256"], "El ZIP publicado por otro proceso difiere"))
    require(digest(target) == summary["zip_sha256"], "El ZIP compartido difiere")
    return target


def publish(summary, rows, source):
    require(TRANSFER.is_dir() and EVIDENCE.is_dir(), "No existen las carpetas compartidas")
    host = prepare_host_package(summary, rows)
    target = publish_archive(summary)
    runner = compile_runner(source, runner_destination(source))
    transfer_runner = TRANSFER / "NoTraceLanQaVmRunner.exe"
    if transfer_runner.exists() and digest(transfer_runner) != digest(runner):
        backup = BUILD / ("vm-qa-runner-backup-" + digest(transfer_runner)[:16] + ".exe")
        if not backup.exists():
            shutil.copyfile(transfer_runner, backup)
    shutil.copyfile(runner, transfer_runner)
    require(digest(transfer_runner) == digest(runner), "El supervisor cambio al copiarlo al share")
    wrapper = ('@echo off\nsetlocal\n"%~dp0NoTraceLanQaVmRunner.exe"\n'
               'set "NT_QA_EXIT=%errorlevel%"\necho.\necho Ejecutor finalizado. Conserva sus resultados.\npause\nexit /b %NT_QA_EXIT%\n')
    helper = TRANSFER / "INICIAR_EJECUTOR_QA_VM.cmd"
    if helper.exists() and helper.read_bytes() != wrapper.replace("\n", "\r\n").encode("ascii"):
        backup = BUILD / ("vm-qa-helper-backup-" + digest(helper)[:16] + ".cmd")
        if not backup.exists():
            shutil.copyfile(helper, backup)
    helper.write_bytes(wrapper.replace("\n", "\r\n").encode("ascii"))
    (TRANSFER / "QA-JOBS").mkdir(exist_ok=True)
    (TRANSFER / "QA-CANCEL").mkdir(exist_ok=True)
    return {"Status": "PUBLICADO_SIN_EJECUTAR", "Label": package_label(summary), "Protocol": PROTOCOL, "HostPackage": str(host), "TransferZip": str(target), "Bootstrap": str(helper), "BootstrapSHA256": digest(helper), "Supervisor": str(transfer_runner), "SupervisorSHA256": digest(transfer_runner), "SupervisorSourceSHA256": hashlib.sha256(source.encode("ascii")).hexdigest().upper(), "PowerShellRequired": False, "ManifestRecords": len(rows), "QaSHA256": summary["executables_sha256"]["NoTraceLanQa.exe"], "VmExecuted": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary", required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    summary, rows = load_package(args.summary)
    source = render(summary)
    if args.apply:
        result = publish(summary, rows, source)
    else:
        runner = compile_runner(source, runner_destination(source))
        result = {"Status": "VALIDADO_SIN_APLICAR", "ManifestRecords": len(rows), "SupervisorCompiled": str(runner), "PowerShellRequired": False}
    if args.apply:
        (BUILD / "LAN-qa-runner-publish-results.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
