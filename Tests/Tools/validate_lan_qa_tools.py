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
import sys
import threading
import time
import uuid
import zipfile
from unittest.mock import patch

import generate_lan_qa_vm as generator
import run_lan_qa as coordinator
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


def validate_interrupted_copy(folder, revisions):
    summary, rows = load_package(BUILD / (revisions["packagesVerified"][0] + "-summary.json"))
    host = BUILD / ("lan-host-vm-" + summary["name"])

    def interrupted_extract(archive, destination):
        archive.extract(archive.infolist()[0], destination)
        raise OSError("Corte de extraccion inyectado despues del primer archivo")

    with patch.object(zipfile.ZipFile, "extractall", interrupted_extract):
        try:
            generator.prepare_host_package(summary, rows)
        except RuntimeError:
            pass
        else:
            raise RuntimeError("No se detecto la extraccion interrumpida")
    partials = list(BUILD.glob(host.name + ".preparing-*"))
    require(not host.exists() and len(partials) == 1, "Una extraccion incompleta publico el destino final")
    partial_file = partials[0] / "NoTraceLanQa.exe"
    original_partial = partial_file.read_bytes()
    require(generator.prepare_host_package(summary, rows) == host and partial_file.read_bytes() == original_partial, "No se recupero la copia o se perdio su evidencia anterior")
    modified = host / "NoTraceLanQa.exe"
    modified.write_bytes(b"copia local modificada")
    try:
        generator.prepare_host_package(summary, rows)
    except RuntimeError:
        pass
    else:
        raise RuntimeError("Se acepto un destino local modificado")
    require(modified.read_bytes() == b"copia local modificada", "Se sobrescribio la copia local modificada")
    share = folder / "transfer-fixture"
    share.mkdir()

    def interrupted_copy(read, write):
        write.write(read.read(12))
        raise OSError("Corte de transferencia inyectado despues de doce bytes")

    with patch.object(generator, "TRANSFER", share):
        target = share / Path(summary["zip"]).name
        with patch.object(generator.shutil, "copyfileobj", interrupted_copy):
            try:
                generator.publish_archive(summary)
            except OSError:
                pass
            else:
                raise RuntimeError("No se detecto la transferencia interrumpida")
        partial_archives = list(share.glob(target.name + ".preparing-*"))
        require(not target.exists() and len(partial_archives) == 1 and partial_archives[0].stat().st_size == 12, "Se publico o perdio el ZIP incompleto")
        generator.publish_archive(summary)
        require(partial_archives[0].stat().st_size == 12 and generator.digest(target) == summary["zip_sha256"], "La segunda transferencia no recupero el ZIP verificado")
        target.write_bytes(b"ZIP local modificado")
        try:
            generator.publish_archive(summary)
        except RuntimeError:
            pass
        else:
            raise RuntimeError("Se acepto un ZIP compartido modificado")
        require(target.read_bytes() == b"ZIP local modificado", "Se sobrescribio un ZIP compartido modificado")
    return {"status": "PASS", "hostExtractionRecovered": True, "zipTransferRecovered": True, "partialCopiesPreserved": True, "modifiedCopiesPreserved": True, "partialHost": str(partials[0]), "partialZip": str(partial_archives[0]), "gameExecuted": False}


def validate_cancellation(revisions):
    summary, rows = load_package(BUILD / (revisions["packagesVerified"][1] + "-summary.json"))
    source = Path(summary["directory"])
    results = []
    for mode in ("report", "marker", "failure"):
        status, exit_code = ("FAIL", 1) if mode == "failure" else ("CANCELLED", 3)
        reason = "Fallo ordinario de control" if mode == "failure" else "Cancelacion original del proceso de control"

        class ControlChild(coordinator.Child):
            def start(self):
                require(not self.attempts, "Se reinicio automaticamente un hijo de control cancelado")
                self.output = self.run_folder / "attempt-1"
                self.output.mkdir(parents=True, exist_ok=False)
                report = {"schema": 1, "runId": self.run_id, "role": self.role, "scenario": self.scenario, "protocol": PROTOCOL, "status": status, "reason": reason, "controlFixture": True, "gameExecuted": False}
                script = "import json,pathlib,sys,time;time.sleep(0.1);pathlib.Path(sys.argv[1]).write_text(sys.argv[2],encoding='utf-8');sys.exit(int(sys.argv[3]))"
                self.streams = [(self.output / name).open("xb") for name in ("stdout.log", "stderr.log")]
                self.process = subprocess.Popen([sys.executable, "-B", "-c", script, str(self.output / "qa-report.json"), json.dumps(report), str(exit_code)], stdin=subprocess.DEVNULL, stdout=self.streams[0], stderr=self.streams[1], creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
                self.attempts.append({"attempt": 1, "pid": self.process.pid, "startedUtc": coordinator.utc(), "exitCode": None, "forced": False, "output": str(self.output)})
                self.record()
                if mode == "marker" and self.role == "client":
                    write_cancel(self.coord, self.run_id, reason)

        with patch.object(coordinator, "Child", ControlChild):
            result, evidence = coordinator.run_case("client-drop", "both-local", summary, rows, source)
        require(result["status"] == status and (reason in result["error"] if mode != "failure" else "no termino PASS" in result["error"]), "Se perdio la clasificacion o el motivo del hijo de control")
        require(len(result["roles"]) == 2 and all(len(role["attempts"]) == 1 and not role["forcedDrops"] for role in result["roles"].values()), "Una cancelacion provoco un reinicio o corte automatico")
        require(all(role["report"] and role["report"]["status"] == status for role in result["roles"].values()), "No se recuperaron los reportes finales de ambos hijos")
        require(coordinator.batch_status([result], 3) == status, "El lote no conserva la clasificacion del caso")
        results.append({"source": mode, "status": status, "evidence": str(evidence), "singleAttemptPerRole": True, "automaticForcedDrops": 0})
    require(coordinator.batch_status([{"status": "CANCELLED"}, {"status": "FAIL"}], 3) == "FAIL", "Una cancelacion oculto un fallo ordinario")
    race = validate_ending_child()
    return {"status": "PASS", "realChildProcessCases": results, "endingChildWithDropMarker": race, "gameExecuted": False, "vmChanged": False}


def validate_ending_child():
    folder = EVIDENCE / "QA-TOOLS-VALIDATION" / uuid.uuid4().hex
    coord = folder / "coord"
    output = folder / "attempt-1"
    coord.mkdir(parents=True)
    output.mkdir()
    child = coordinator.Child.__new__(coordinator.Child)
    child.role, child.scenario, child.run_id = "client", "client-drop", uuid.uuid4().hex
    child.runtime, child.run_folder, child.coord, child.output, child.host = folder, folder, coord, output, "127.0.0.1"
    child.attempts, child.forced_drops, child.report, child.finished = [], [], None, False
    child.streams = [(output / name).open("xb") for name in ("stdout.log", "stderr.log")]
    reason = "Escape durante la espera del corte de control"
    report = {"schema": 1, "runId": child.run_id, "role": child.role, "scenario": child.scenario, "status": "CANCELLED", "reason": reason, "controlFixture": True, "gameExecuted": False}
    script = "import pathlib,sys,time;pathlib.Path(sys.argv[1]).write_text(sys.argv[2],encoding='utf-8');time.sleep(0.6);sys.exit(3)"
    child.process = subprocess.Popen([sys.executable, "-B", "-c", script, str(output / "qa-report.json"), json.dumps(report)], stdin=subprocess.DEVNULL, stdout=child.streams[0], stderr=child.streams[1], creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    child.attempts.append({"attempt": 1, "pid": child.process.pid, "startedUtc": coordinator.utc(), "exitCode": None, "forced": False, "output": str(output)})
    write_json(coord / "client-drop-ready.json", {"schema": 1, "runId": child.run_id, "role": child.role, "scenario": child.scenario})
    deadline = time.monotonic() + 2
    while not (output / "qa-report.json").exists() and time.monotonic() < deadline:
        time.sleep(0.01)
    require(child.process.poll() is None and (output / "qa-report.json").is_file(), "El hijo de control no entro en su ventana de cierre")
    cancelled = False
    try:
        child.poll()
    except coordinator.CancelledCase as error:
        cancelled = str(error) == reason
    finally:
        child.cancel()
    require(cancelled and len(child.attempts) == 1 and not child.forced_drops and child.process.returncode == 3, "El marcador de corte reinicio un hijo que ya habia publicado CANCELLED")
    return {"status": "PASS", "evidence": str(folder), "exitCode": child.process.returncode, "singleAttempt": True, "forcedDrops": 0, "originalReasonPreserved": True}


def main():
    folder = BUILD / ("qa-tools-validation-" + uuid.uuid4().hex)
    folder.mkdir()
    revisions = validate_revisions(folder)
    interrupted_copy = validate_interrupted_copy(folder, revisions)
    cancellation = validate_cancellation(revisions)
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
    require(all(result_cs.get(key) is True for key in ("interruptedCopyRecovered", "partialCopyPreserved", "modifiedDestinationPreserved")), "No se verifico la recuperacion de copia del supervisor")
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
    outcome = {"status": "PASS", "packageRevisions": revisions, "interruptedCopy": interrupted_copy, "cancellation": cancellation, "pythonFilesParsed": [str(p) for p in files], "pythonMalformedJobsRejected": len(invalid), "crossRunReportRejected": True, "compiledSupervisor": result_cs, "lockedReaderRetry": {"status": "PASS", "seconds": duration, "originalCancellationPreserved": True, "evidence": str(locked_folder)}, "powerShellRequired": False, "evidence": str(folder), "gameExecuted": False, "vmChanged": False}
    (folder / "RESULTADO.json").write_text(json.dumps(outcome, indent=2) + "\n", encoding="utf-8")
    (BUILD / "LAN-qa-tools-static-results.json").write_text(json.dumps(outcome, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(outcome, indent=2))


if __name__ == "__main__":
    main()
