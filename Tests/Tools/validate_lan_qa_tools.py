"""Comprueba sintaxis, solicitudes cerradas y recibos sin iniciar juegos ni VM."""

import ast
import ctypes
from ctypes import wintypes
import csv
import hashlib
import io
import json
from pathlib import Path
import subprocess
import threading
import time
import uuid
import zipfile

from generate_lan_qa_vm import BUILD, LABEL, PROTOCOL, compile_runner, load_package, package_label, render, require, runner_destination
from run_lan_qa import EVIDENCE, check_identity, check_job, write_cancel, write_json


def validate_revisions(folder):
    summaries = []
    prefix = "qa-validation-" + uuid.uuid4().hex
    for revision in (1, 2):
        label = prefix + "-r" + str(revision)
        payload = {"NoTraceLanQa.exe": b"fixture de identidad; no ejecutable", "PROCEDENCIA.json": json.dumps({"Protocol": PROTOCOL, "Label": label}).encode("utf-8")}
        table = io.StringIO(newline="")
        writer = csv.DictWriter(table, fieldnames=("File", "SHA256", "Bytes"))
        writer.writeheader()
        for name, data in payload.items():
            writer.writerow({"File": name, "SHA256": hashlib.sha256(data).hexdigest().upper(), "Bytes": len(data)})
        manifest = table.getvalue().encode("utf-8")
        payload["SHA256SUMS.csv"] = manifest
        archive = BUILD / ("NO_TRACE_LAN-" + label + ".zip")
        package = BUILD / ("NO_TRACE_LAN-" + label)
        package.mkdir()
        with zipfile.ZipFile(archive, "x") as zipped:
            for name, data in payload.items():
                zipped.writestr(name, data)
                (package / name).write_bytes(data)
        summary = {"name": label, "metadata": json.loads(payload["PROCEDENCIA.json"]), "zip": str(archive), "directory": str(package), "zip_sha256": hashlib.sha256(archive.read_bytes()).hexdigest().upper(), "manifest_sha256": hashlib.sha256(manifest).hexdigest().upper(), "manifest_records": len(payload) - 1, "executables_sha256": {"NoTraceLanQa.exe": hashlib.sha256(payload["NoTraceLanQa.exe"]).hexdigest().upper()}}
        path = BUILD / (label + "-summary.json")
        path.write_text(json.dumps(summary), encoding="utf-8")
        actual, rows = load_package(path)
        require(actual == summary and len(rows) == 2, "No se acepto la revision del paquete verificado")
        summaries.append(summary)
    first, second = summaries
    require(first["executables_sha256"] == second["executables_sha256"] and runner_destination(render(first)) != runner_destination(render(second)), "Las revisiones comparten una cache incompatible del supervisor")
    require(render(first) == render(first) and runner_destination(render(first)) == runner_destination(render(first)), "La misma procedencia no conserva la cache")
    for invalid in ("../escape", "release/otra", "release\\otra", "release\"otra", "release_otra", "A" * 97):
        try:
            package_label({"name": invalid})
        except RuntimeError:
            pass
        else:
            raise RuntimeError("Se acepto una etiqueta de entrega insegura")
    job = {"schema": 1, "sessionId": "a" * 32, "jobId": "b" * 32, "runId": "c" * 32, "packageLabel": second["name"], "protocol": PROTOCOL, "role": "client", "scenario": "mission", "host": "192.168.56.1", "port": 55041, "timeoutSeconds": 360}
    check_job(job, job["sessionId"], second["name"])
    try:
        check_job(job, job["sessionId"], first["name"])
    except RuntimeError:
        pass
    else:
        raise RuntimeError("Un job de otra revision fue aceptado")
    result = {"status": "PASS", "packagesVerified": [item["name"] for item in summaries], "sameQaBinarySeparateSupervisorCache": True, "unsafeLabelsRejected": 6, "crossPackageJobRejected": True, "gameExecuted": False}
    (folder / "REVISIONS.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main():
    folder = BUILD / ("qa-tools-validation-" + uuid.uuid4().hex)
    folder.mkdir()
    revisions = validate_revisions(folder)
    summary = {"name": LABEL, "zip": "NO_TRACE_LAN-" + LABEL + ".zip", "zip_sha256": "A" * 64, "manifest_sha256": "B" * 64, "manifest_records": 109, "executables_sha256": {"NoTraceLanQa.exe": "C" * 64}}
    source = render(summary)
    job = {"schema": 1, "sessionId": "a" * 32, "jobId": "b" * 32, "runId": "c" * 32, "packageLabel": LABEL, "protocol": PROTOCOL, "role": "client", "scenario": "mission", "host": "192.168.56.1", "port": 55041, "timeoutSeconds": 360}
    check_job(job, job["sessionId"])
    invalid = [dict(job, command="cmd.exe"), dict(job, host="127.0.0.1 & calc"), dict(job, role="host"), dict(job, scenario="shell"), dict(job, runId="../escape"), dict(job, timeoutSeconds=99999), dict(job, schema=True), dict(job, scenario=["mission"]), dict(job, sessionId="d" * 32)]
    for item in invalid:
        try:
            check_job(item, job["sessionId"])
        except RuntimeError:
            pass
        else:
            raise RuntimeError("Una solicitud malformada fue aceptada en Python")
    try:
        check_identity({"schema": 1, "runId": "d" * 32, "role": "client", "scenario": "mission"}, job["runId"], "client", "mission")
    except RuntimeError:
        pass
    else:
        raise RuntimeError("Un reporte de otro run fue aceptado")
    runner = compile_runner(source, folder)
    result = subprocess.run([str(runner), "--self-test"], text=True, capture_output=True, timeout=30, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    (folder / "supervisor-stdout.log").write_text(result.stdout, encoding="utf-8")
    (folder / "supervisor-stderr.log").write_text(result.stderr, encoding="utf-8")
    require(result.returncode == 0, "Fallo validacion C#: " + result.stdout + result.stderr)
    result_cs = json.loads(result.stdout)
    require(result_cs["malformedJobsRejected"] == len(invalid) and result_cs["status"] == "PASS", "Validacion C# incompleta")
    locked_folder = EVIDENCE / "QA-TOOLS-VALIDATION" / uuid.uuid4().hex
    locked_folder.mkdir(parents=True)
    locked_path = locked_folder / "cancel.json"
    run_id = uuid.uuid4().hex
    write_json(locked_path, {"schema": 1, "runId": run_id, "reason": "Motivo original"})
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE)
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.CreateFileW(str(locked_path), 0x80000000, 1, None, 3, 0x80, None)
    require(handle != ctypes.c_void_p(-1).value, "No se pudo preparar bloqueo de lectura de prueba")
    before = locked_path.read_bytes()
    require(write_cancel(locked_folder, run_id, "No debe sustituirlo") == "Motivo original" and locked_path.read_bytes() == before, "Se sustituyo una cancelacion valida")
    released = threading.Timer(0.2, lambda: kernel.CloseHandle(handle))
    released.start()
    started = time.monotonic()
    write_json(locked_path, {"schema": 1, "runId": run_id, "reason": "Sustitucion tras liberar lectura"})
    duration = time.monotonic() - started
    released.join()
    require(duration >= 0.15 and json.loads(locked_path.read_text(encoding="utf-8"))["reason"] == "Sustitucion tras liberar lectura", "No se comprobo reintento de lectura bloqueada")
    files = list(Path(__file__).parent.glob("*lan_qa*.py"))
    for path in files:
        ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    outcome = {"status": "PASS", "packageRevisions": revisions, "pythonFilesParsed": [str(p) for p in files], "pythonMalformedJobsRejected": len(invalid), "crossRunReportRejected": True, "compiledSupervisor": result_cs, "lockedReaderRetry": {"status": "PASS", "seconds": duration, "originalCancellationPreserved": True, "evidence": str(locked_folder)}, "powerShellRequired": False, "evidence": str(folder), "gameExecuted": False, "vmChanged": False}
    (folder / "RESULTADO.json").write_text(json.dumps(outcome, indent=2) + "\n", encoding="utf-8")
    (BUILD / "LAN-qa-tools-static-results.json").write_text(json.dumps(outcome, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(outcome, indent=2))


if __name__ == "__main__":
    main()
