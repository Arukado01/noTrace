"""Coordina los tres casos QA graficos sin inyectar entrada del sistema operativo."""

import argparse
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import uuid

from generate_lan_qa_vm import BUILD, EVIDENCE, LABEL, PROTOCOL, TRANSFER, assert_regular, digest, load_package, package_label, require, verify_folder


SCENARIOS = ("mission", "client-drop", "host-drop")
TIMEOUT = 360
PORT = 55041


class CancelledCase(RuntimeError):
    pass


def development_package(development_root):
    development_root = Path(development_root).resolve()
    require(development_root.is_relative_to(BUILD.resolve()) and development_root.is_dir(), "El preflight debe usar una compilacion dentro de build")
    executable = development_root / "NoTraceLanQa.exe"
    assert_regular(executable, BUILD)
    content = development_root / "Content"
    if not content.is_dir():
        content = BUILD.parent / "Content"
    require(content.resolve().is_relative_to(BUILD.parent.resolve()), "Content de preflight exterior al proyecto")
    destination = EVIDENCE / "QA-PREFLIGHT-SOURCES" / uuid.uuid4().hex
    destination.mkdir(parents=True, exist_ok=False)
    sources = [(executable, "NoTraceLanQa.exe")]
    for path in sorted(content.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(content)
        if path.name.lower() == "save.json" or any(part.lower() in {"screenshots", "sources", "docs", ".agents", "skills"} for part in relative.parts):
            continue
        assert_regular(path, content)
        sources.append((path, "Content/" + relative.as_posix()))
    rows = []
    for origin, relative in sources:
        before = digest(origin)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with origin.open("rb") as read, target.open("xb") as write:
            shutil.copyfileobj(read, write)
        require(before == digest(target) == digest(origin), "Fuente cambio durante preflight: " + str(origin))
        rows.append({"File": relative, "Bytes": str(target.stat().st_size), "SHA256": before})
    manifest = destination / "SHA256SUMS.csv"
    with manifest.open("x", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=("File", "SHA256", "Bytes"))
        writer.writeheader()
        writer.writerows(rows)
    summary = {"name": "DEVELOPMENT_PREFLIGHT_NOT_FOR_VM", "manifest_sha256": digest(manifest), "manifest_records": len(rows), "executables_sha256": {"NoTraceLanQa.exe": rows[0]["SHA256"]}, "developmentRoot": str(development_root), "contentRoot": str(content), "source": str(destination), "vmAllowed": False}
    write_json(destination / "PREFLIGHT.json", summary)
    verify_folder(destination, rows, summary)
    return summary, rows, destination


def utc():
    return datetime.now(timezone.utc).isoformat()


def write_json(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    with temporary.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    for attempt in range(25):
        try:
            temporary.replace(path)
            return
        except PermissionError:
            if attempt == 24:
                raise
            time.sleep(0.05)


def write_cancel(coord, run_id, reason):
    path = coord / "cancel.json"
    for candidate in (path, coord / "CANCEL.json"):
        if candidate.exists():
            try:
                existing = read_json(candidate)
                if existing.get("schema") == 1 and existing.get("runId") == run_id:
                    return str(existing.get("reason") or reason)
            except (OSError, ValueError, RuntimeError):
                pass
    write_json(path, {"schema": 1, "runId": run_id, "reason": reason, "createdUtc": utc()})
    return reason


def read_json(path):
    path = Path(path)
    assert_regular(path, EVIDENCE)
    require(path.stat().st_size <= 4 * 1024 * 1024, "JSON demasiado grande")
    return json.loads(path.read_text(encoding="utf-8-sig"))


def check_identity(value, run_id, role, scenario):
    require(value.get("schema") == 1 and value.get("runId") == run_id and value.get("role") == role and value.get("scenario") == scenario, "Identidad de reporte o marcador incorrecta")


def cancellation_reason(coord, run_id):
    for name in ("cancel.json", "CANCEL.json"):
        path = coord / name
        if path.exists():
            marker = read_json(path)
            if isinstance(marker, dict) and marker.get("schema") == 1 and marker.get("runId") == run_id:
                return str(marker.get("reason") or "Cancelacion solicitada durante el escenario")
    return None


def batch_status(results, expected):
    if any(item["status"] == "FAIL" for item in results):
        return "FAIL"
    if any(item["status"] == "CANCELLED" for item in results):
        return "CANCELLED"
    return "PASS" if len(results) == expected and all(item["status"] == "PASS" for item in results) else "FAIL"


def check_job(job, session_id, label=LABEL):
    allowed = {"schema", "sessionId", "jobId", "runId", "packageLabel", "protocol", "role", "scenario", "host", "port", "timeoutSeconds"}
    require(set(job) == allowed and all(type(job[key]) is int for key in ("schema", "protocol", "port", "timeoutSeconds")) and all(type(job[key]) is str for key in allowed - {"schema", "protocol", "port", "timeoutSeconds"}), "Tipos del job incorrectos")
    require(job["schema"] == 1 and job["sessionId"] == session_id and re.fullmatch("[a-f0-9]{32}", job["jobId"]) and re.fullmatch("[a-f0-9]{32}", job["runId"]), "Job no valido")
    require(job["packageLabel"] == label and job["protocol"] == PROTOCOL and job["role"] == "client" and job["scenario"] in SCENARIOS and job["host"] == "192.168.56.1" and job["port"] == PORT and job["timeoutSeconds"] == TIMEOUT, "Job fuera de la lista cerrada")


def find_session(summary, requested=None, require_idle=True):
    candidates = []
    for folder in EVIDENCE.glob("QA-RUNNER-*"):
        if folder.is_symlink() or not folder.is_dir():
            continue
        if requested is not None and folder.name != "QA-RUNNER-" + requested:
            continue
        try:
            ready = read_json(folder / "READY.json")
            heartbeat = read_json(folder / "HEARTBEAT.json")
            require(ready.get("schema") == 1 and re.fullmatch("[a-f0-9]{32}", ready["sessionId"]) and folder.name == "QA-RUNNER-" + ready["sessionId"], "Sesion no valida")
            require(ready["packageLabel"] == package_label(summary) and ready["protocol"] == PROTOCOL and ready["qaSha256"] == summary["executables_sha256"]["NoTraceLanQa.exe"] and ready["manifestVerified"] == summary["manifest_records"], "Paquete VM incompatible")
            require(heartbeat["sessionId"] == ready["sessionId"] and heartbeat["state"] in (("IDLE",) if require_idle else ("IDLE", "RUNNING")), "El ejecutor VM no esta disponible")
            age = (datetime.now(timezone.utc) - datetime.fromisoformat(heartbeat["utc"].replace("Z", "+00:00"))).total_seconds()
            require(-5 <= age <= 15, "Heartbeat VM no vigente")
            candidates.append((folder.stat().st_mtime, ready["sessionId"], folder))
        except (OSError, ValueError, KeyError, RuntimeError, TypeError):
            continue
    require(candidates, "No hay un bootstrap VM verificado y libre; el usuario debe iniciar INICIAR_EJECUTOR_QA_VM.cmd")
    _, session_id, folder = max(candidates)
    return session_id, folder


def wait_session(summary, requested=None, require_idle=True):
    deadline = time.monotonic() + 15
    while True:
        try:
            return find_session(summary, requested, require_idle)
        except RuntimeError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.5)


def copy_runtime(source, destination, rows, summary):
    destination.mkdir(parents=True, exist_ok=False)
    for row in rows:
        origin = source / row["File"]
        assert_regular(origin, source)
        target = destination / row["File"]
        target.parent.mkdir(parents=True, exist_ok=True)
        with origin.open("rb") as read, target.open("xb") as write:
            shutil.copyfileobj(read, write)
    shutil.copyfile(source / "SHA256SUMS.csv", destination / "SHA256SUMS.csv")
    verify_folder(destination, rows, summary)


class Child:
    def __init__(self, role, scenario, run_id, runtime, run_folder, coord, host):
        self.role = role
        self.scenario = scenario
        self.run_id = run_id
        self.runtime = runtime
        self.run_folder = run_folder
        self.coord = coord
        self.host = host
        self.attempts = []
        self.forced_drops = []
        self.process = None
        self.streams = []
        self.output = None
        self.report = None
        self.finished = False
        self.start()

    def start(self):
        require(len(self.attempts) < 2, "No se permiten mas de dos intentos del rol")
        attempt = len(self.attempts) + 1
        self.output = self.run_folder / ("attempt-" + str(attempt))
        self.output.mkdir(parents=True, exist_ok=False)
        arguments = [str(self.runtime / "NoTraceLanQa.exe"), "--role", self.role, "--scenario", self.scenario, "--host", self.host, "--port", str(PORT), "--run-id", self.run_id, "--coord", str(self.coord), "--output", str(self.output), "--timeout", str(TIMEOUT)]
        self.streams = [(self.output / name).open("xb") for name in ("stdout.log", "stderr.log")]
        flags = 0
        if hasattr(subprocess, "CREATE_NO_WINDOW"):
            flags = subprocess.CREATE_NO_WINDOW
        self.process = subprocess.Popen(arguments, cwd=self.runtime, stdin=subprocess.DEVNULL, stdout=self.streams[0], stderr=self.streams[1], creationflags=flags)
        self.attempts.append({"attempt": attempt, "pid": self.process.pid, "startedUtc": utc(), "exitCode": None, "forced": False, "output": str(self.output)})
        self.record()

    def record(self):
        write_json(self.run_folder / "PROCESS.json", {"schema": 1, "runId": self.run_id, "role": self.role, "scenario": self.scenario, "attempts": self.attempts, "forcedDrops": self.forced_drops})

    def close_attempt(self):
        self.attempts[-1]["exitCode"] = self.process.wait(timeout=5)
        self.attempts[-1]["finishedUtc"] = utc()
        for stream in self.streams:
            stream.close()
        self.streams = []
        self.record()

    def poll(self):
        if self.finished:
            return
        final_report = self.output / "qa-report.json"
        if self.process.poll() is None and final_report.is_file():
            report = read_json(final_report)
            check_identity(report, self.run_id, self.role, self.scenario)
            if report.get("status") == "CANCELLED":
                self.report = report
                raise CancelledCase(str(report.get("reason") or "Ventana QA cancelada: " + self.role))
            return
        drop_role = "client" if self.scenario == "client-drop" else "host" if self.scenario == "host-drop" else None
        ready = self.coord / (self.role + "-drop-ready.json")
        if self.process.poll() is None and self.role == drop_role and len(self.attempts) == 1 and ready.exists():
            marker = read_json(ready)
            check_identity(marker, self.run_id, self.role, self.scenario)
            self.process.kill()
            self.close_attempt()
            self.attempts[-1]["forced"] = True
            forced = {"schema": 1, "runId": self.run_id, "role": self.role, "scenario": self.scenario, "attempt": 1, "pid": self.process.pid, "exitCode": self.process.returncode, "createdUtc": utc(), "method": "Popen.kill del hijo exacto del coordinador"}
            self.forced_drops.append(forced)
            write_json(self.coord / (self.role + "-forced-drop.json"), forced)
            self.record()
            self.start()
            return
        if self.process.poll() is not None:
            self.close_attempt()
            path = self.output / "qa-report.json"
            require(path.is_file(), "El proceso termino sin reporte QA: " + self.role)
            report = read_json(path)
            check_identity(report, self.run_id, self.role, self.scenario)
            self.report = report
            if self.process.returncode == 3 and report.get("status") == "CANCELLED":
                raise CancelledCase(str(report.get("reason") or "Ventana QA cancelada: " + self.role))
            require(self.process.returncode == 0 and report.get("status") == "PASS", "El rol no termino PASS: " + self.role)
            if self.role == drop_role:
                require(len(self.forced_drops) == 1 and len(self.attempts) == 2, "Falta corte abrupto y segundo intento: " + self.role)
            self.finished = True

    def cancel(self):
        if self.process is not None and self.process.poll() is None:
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        if self.streams:
            self.close_attempt()
        if self.report is None and self.output is not None and (self.output / "qa-report.json").is_file():
            try:
                report = read_json(self.output / "qa-report.json")
                check_identity(report, self.run_id, self.role, self.scenario)
                self.report = report
            except (OSError, ValueError, RuntimeError, TypeError) as error:
                self.attempts[-1]["cleanupWarning"] = "No se pudo recuperar el reporte final: " + str(error)
                self.record()


def cancel_guest(job, reason):
    if job is not None:
        write_json(TRANSFER / "QA-CANCEL" / (job["jobId"] + ".json"), {"schema": 1, "sessionId": job["sessionId"], "jobId": job["jobId"], "reason": reason, "createdUtc": utc()})


def run_case(scenario, mode, summary, rows, source, session_id=None, session_folder=None):
    run_id = uuid.uuid4().hex
    root = EVIDENCE / "QA-RUNS" / run_id
    coord = root / "coord"
    coord.mkdir(parents=True, exist_ok=False)
    result = {"schema": 1, "runId": run_id, "scenario": scenario, "mode": mode, "packageLabel": summary["name"], "protocol": PROTOCOL, "qaSha256": summary["executables_sha256"]["NoTraceLanQa.exe"], "startedUtc": utc(), "status": "RUNNING", "roles": {}, "error": None}
    children = []
    job = None
    guest_result = None
    try:
        runtime = root / "host" / "runtime"
        copy_runtime(source, runtime, rows, summary)
        host = Child("host", scenario, run_id, runtime, root / "host", coord, "127.0.0.1" if mode == "both-local" else "192.168.56.1")
        children.append(host)
        if mode == "both-local":
            client_runtime = root / "client" / "runtime"
            copy_runtime(source, client_runtime, rows, summary)
            children.append(Child("client", scenario, run_id, client_runtime, root / "client", coord, "127.0.0.1"))
        else:
            job_id = uuid.uuid4().hex
            job = {"schema": 1, "sessionId": session_id, "jobId": job_id, "runId": run_id, "packageLabel": package_label(summary), "protocol": PROTOCOL, "role": "client", "scenario": scenario, "host": "192.168.56.1", "port": PORT, "timeoutSeconds": TIMEOUT}
            check_job(job, session_id, package_label(summary))
            write_json(TRANSFER / "QA-JOBS" / (job_id + ".json"), job)
            result["guestJob"] = job
        write_json(root / "ORQUESTACION.json", result)
        deadline = time.monotonic() + TIMEOUT + 15
        while time.monotonic() < deadline:
            reason = cancellation_reason(coord, run_id)
            if reason is not None:
                raise CancelledCase(reason)
            for child in children:
                child.poll()
            if job is not None:
                path = session_folder / "jobs" / job["jobId"] / "RESULTADO.json"
                rejected = session_folder / ("REJECTED-" + job["jobId"] + ".json")
                if rejected.exists():
                    raise RuntimeError("El ejecutor VM rechazo el job: " + json.dumps(read_json(rejected), ensure_ascii=False))
                if path.exists():
                    guest_result = read_json(path)
                    require(guest_result.get("schema") == 1 and guest_result.get("sessionId") == session_id and guest_result.get("jobId") == job["jobId"], "Resultado VM de otro job")
                    check_identity(guest_result, run_id, "client", scenario)
                    if guest_result.get("status") == "CANCELLED":
                        raise CancelledCase(str(guest_result.get("error") or (guest_result.get("report") or {}).get("reason") or "El ejecutor VM cancelo el escenario"))
                    require(guest_result.get("status") == "PASS", "La VM no termino PASS: " + str(guest_result.get("error")))
                    check_identity(guest_result["report"], run_id, "client", scenario)
                    require(guest_result["report"]["status"] == "PASS", "Reporte QA VM sin PASS")
                    if scenario == "client-drop":
                        require(len(guest_result.get("forcedDrops", [])) == 1 and len(guest_result.get("attempts", [])) == 2, "VM sin corte abrupto y segundo intento")
            if all(child.finished for child in children) and (job is None or guest_result is not None):
                result["status"] = "PASS"
                break
            time.sleep(0.25)
        else:
            raise RuntimeError("Tiempo agotado del escenario coordinado")
    except (Exception, KeyboardInterrupt) as error:
        result["status"] = "CANCELLED" if isinstance(error, (KeyboardInterrupt, CancelledCase)) else "FAIL"
        result["error"] = str(error) or "Interrupcion de teclado del coordinador"
        try:
            result["error"] = write_cancel(coord, run_id, result["error"])
        except (OSError, ValueError, RuntimeError) as cancellation_error:
            result.setdefault("warnings", []).append("No se pudo publicar cancelacion local: " + str(cancellation_error))
        try:
            cancel_guest(job, result["error"])
        except OSError as cancellation_error:
            result.setdefault("warnings", []).append("No se pudo publicar cancelacion VM: " + str(cancellation_error))
    finally:
        for child in children:
            child.cancel()
            result["roles"][child.role] = {"attempts": child.attempts, "forcedDrops": child.forced_drops, "report": child.report}
        if guest_result is not None:
            result["roles"]["client"] = guest_result
        result["finishedUtc"] = utc()
        write_json(root / "ORQUESTACION.json", result)
    return result, root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--summary")
    parser.add_argument("--development-root")
    parser.add_argument("--mode", choices=("both-local", "host-vm"), required=True)
    parser.add_argument("--session-id")
    parser.add_argument("--scenario", choices=SCENARIOS, action="append")
    parser.add_argument("--validate-only", action="store_true")
    parser.add_argument("--stop-runner", action="store_true")
    args = parser.parse_args()
    if args.session_id is not None:
        require(re.fullmatch("[a-f0-9]{32}", args.session_id), "Sesion no valida")
    require(bool(args.summary) != bool(args.development_root), "Selecciona resumen final o development-root")
    if args.development_root:
        require(args.mode == "both-local" and not args.session_id and not args.stop_runner, "development-root solo permite both-local sin VM")
        summary, rows, source = development_package(args.development_root)
    else:
        summary, rows = load_package(args.summary)
        source = BUILD / ("lan-host-vm-" + package_label(summary))
        verify_folder(source, rows, summary)
    session_id = session_folder = None
    if args.mode == "host-vm":
        session_id, session_folder = wait_session(summary, args.session_id, not args.stop_runner)
    if args.stop_runner:
        require(args.mode == "host-vm", "Parar ejecutor requiere modo host-vm")
        write_json(TRANSFER / "QA-CANCEL" / ("STOP-" + session_id + ".json"), {"schema": 1, "sessionId": session_id, "createdUtc": utc()})
        print(json.dumps({"status": "STOP_REQUESTED", "sessionId": session_id}))
        return
    if args.validate_only:
        print(json.dumps({"status": "VALIDATED_NOT_EXECUTED", "mode": args.mode, "sessionId": session_id, "manifestRecords": len(rows)}))
        return
    batch_id = uuid.uuid4().hex
    batch = {"schema": 1, "batchId": batch_id, "mode": args.mode, "packageLabel": summary["name"], "qaSha256": summary["executables_sha256"]["NoTraceLanQa.exe"], "startedUtc": utc(), "status": "RUNNING", "scenarios": []}
    for scenario in args.scenario or SCENARIOS:
        if args.mode == "host-vm":
            session_id, session_folder = wait_session(summary, session_id)
        result, folder = run_case(scenario, args.mode, summary, rows, source, session_id, session_folder)
        batch["scenarios"].append({"scenario": scenario, "runId": result["runId"], "status": result["status"], "evidence": str(folder), "error": result["error"]})
        print(json.dumps(batch["scenarios"][-1], ensure_ascii=False), flush=True)
        if result["status"] != "PASS":
            break
    batch["status"] = batch_status(batch["scenarios"], len(args.scenario or SCENARIOS))
    batch["finishedUtc"] = utc()
    target = EVIDENCE / ("QA-BATCH-" + batch_id + ".json")
    write_json(target, batch)
    print(json.dumps({"status": batch["status"], "evidence": str(target)}), flush=True)
    if batch["status"] != "PASS":
        raise SystemExit(3 if batch["status"] == "CANCELLED" else 1)


if __name__ == "__main__":
    main()
