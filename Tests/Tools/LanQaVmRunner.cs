using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Web.Script.Serialization;

internal static class LanQaVmRunner {
  private const string Label = "@@LABEL@@";
  private const int Protocol = @@PROTOCOL@@;
  private const string ZipName = "@@ZIP_NAME@@";
  private const string ZipSha = "@@ZIP_SHA@@";
  private const string ManifestSha = "@@MANIFEST_SHA@@";
  private const string QaSha = "@@QA_SHA@@";
  private const int ManifestCount = @@COUNT@@;
  private const string Transfer = @"\\VBOXSVR\NO_TRACE_TRANSFER";
  private const string Results = @"\\VBOXSVR\NO_TRACE_RESULTADOS";
  private static readonly string Package = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "NO_TRACE_LAN", Label);
  private static readonly string SessionId = Guid.NewGuid().ToString("N");
  private static readonly string SessionFolder = Path.Combine(Results, "QA-RUNNER-" + SessionId);
  private static readonly HashSet<string> Seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
  private static readonly JavaScriptSerializer Json = new JavaScriptSerializer { MaxJsonLength = 4 * 1024 * 1024, RecursionLimit = 24 };
  private static readonly object ProcessGate = new object();
  private static volatile bool Stopping;
  private static Process ActiveChild;
  private static List<Record> Records;

  private sealed class Record {
    internal string File;
    internal string Hash;
    internal long Bytes;
  }

  private sealed class Recorder : IDisposable {
    internal readonly Process Child;
    internal bool Started;
    private readonly StreamWriter output;
    private readonly StreamWriter error;
    internal Recorder(string exe, string folder, string arguments, string destination) {
      output = new StreamWriter(new FileStream(Path.Combine(destination, "stdout.log"), FileMode.CreateNew, FileAccess.Write, FileShare.ReadWrite), new UTF8Encoding(false));
      error = new StreamWriter(new FileStream(Path.Combine(destination, "stderr.log"), FileMode.CreateNew, FileAccess.Write, FileShare.ReadWrite), new UTF8Encoding(false));
      output.AutoFlush = true;
      error.AutoFlush = true;
      Child = new Process();
      Child.StartInfo = new ProcessStartInfo { FileName=exe, WorkingDirectory=folder, Arguments=arguments, UseShellExecute=false, CreateNoWindow=true, RedirectStandardOutput=true, RedirectStandardError=true, StandardOutputEncoding=Encoding.UTF8, StandardErrorEncoding=Encoding.UTF8 };
      Child.OutputDataReceived += delegate(object sender, DataReceivedEventArgs args) { if (args.Data != null) { lock(output) { output.WriteLine(args.Data); } } };
      Child.ErrorDataReceived += delegate(object sender, DataReceivedEventArgs args) { if (args.Data != null) { lock(error) { error.WriteLine(args.Data); } } };
    }
    internal void Start() {
      if (!Child.Start()) { throw new InvalidOperationException("No se pudo abrir el ejecutor QA."); }
      Started=true;
      Child.BeginOutputReadLine();
      Child.BeginErrorReadLine();
      lock(ProcessGate) { ActiveChild=Child; }
    }
    internal void Wait() { Child.WaitForExit(); Child.WaitForExit(); }
    public void Dispose() {
      lock(ProcessGate) { if (ActiveChild == Child) { ActiveChild=null; } }
      Child.Dispose(); output.Dispose(); error.Dispose();
    }
  }

  private static void Require(bool condition, string message) { if (!condition) { throw new InvalidOperationException(message); } }
  private static string Utc() { return DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture); }
  private static Dictionary<string, object> Map(params object[] values) {
    var result=new Dictionary<string, object>(StringComparer.Ordinal);
    for (int index=0; index<values.Length; index+=2) { result.Add((string)values[index], values[index+1]); }
    return result;
  }
  private static string Text(Dictionary<string, object> value, string name) {
    object item; Require(value.TryGetValue(name,out item) && item is string, "Campo de texto no escalar: " + name);
    return (string)item;
  }
  private static int Number(Dictionary<string, object> value, string name) {
    object item; Require(value.TryGetValue(name,out item) && item is int, "Campo numerico no entero: " + name);
    return (int)item;
  }
  private static bool Id(string value) { return Regex.IsMatch(value,"^[a-f0-9]{32}$"); }
  private static void PlainFile(string path) { Require(File.Exists(path) && (File.GetAttributes(path) & FileAttributes.ReparsePoint)==0, "Archivo no regular: " + path); }
  private static void PlainDirectory(string path) { Require(Directory.Exists(path) && (File.GetAttributes(path) & FileAttributes.ReparsePoint)==0, "Carpeta no regular: " + path); }
  private static string Hash(string path) {
    PlainFile(path);
    using(var source=File.OpenRead(path)) using(var algorithm=SHA256.Create()) { return BitConverter.ToString(algorithm.ComputeHash(source)).Replace("-",""); }
  }
  private static Dictionary<string, object> ReadJson(string path, int maximum) {
    PlainFile(path); Require(new FileInfo(path).Length<=maximum,"JSON demasiado grande.");
    var value=Json.DeserializeObject(File.ReadAllText(path,Encoding.UTF8)) as Dictionary<string, object>;
    Require(value!=null,"La raiz JSON debe ser un objeto."); return value;
  }
  private static void WriteJson(string path, object value) {
    Directory.CreateDirectory(Path.GetDirectoryName(path));
    var temporary=path+"."+Guid.NewGuid().ToString("N")+".tmp";
    using(var writer=new StreamWriter(new FileStream(temporary,FileMode.CreateNew,FileAccess.Write,FileShare.Read),new UTF8Encoding(false))) { writer.WriteLine(Json.Serialize(value)); }
    for(int attempt=0;attempt<25;attempt++) {
      try {
        if (File.Exists(path)) {
          PlainFile(path);
          try { File.Replace(temporary,path,null); }
          catch(IOException) { File.Copy(temporary,path,true); File.Delete(temporary); }
        } else { File.Move(temporary,path); }
        return;
      } catch(IOException) { if(attempt==24) { throw; } Thread.Sleep(50); }
      catch(UnauthorizedAccessException) { if(attempt==24) { throw; } Thread.Sleep(50); }
    }
  }
  private static bool SafeRelative(string path) {
    if (String.IsNullOrEmpty(path) || path.StartsWith("/",StringComparison.Ordinal) || path.Contains("\\") || path.Contains(":") || path.Contains("//")) { return false; }
    foreach(var part in path.Split('/')) {
      if (part=="." || part==".." || part.Length==0 || new[]{"docs","sources","screenshots",".agents","skills","save.json"}.Contains(part,StringComparer.OrdinalIgnoreCase)) { return false; }
    }
    return true;
  }
  private static string Inside(string root, string relative) {
    Require(SafeRelative(relative),"Ruta relativa no permitida.");
    var path=Path.GetFullPath(Path.Combine(root,relative.Replace('/',Path.DirectorySeparatorChar)));
    Require(path.StartsWith(Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar,StringComparison.OrdinalIgnoreCase),"Ruta exterior al destino.");
    return path;
  }
  private static List<string> CsvLine(string line) {
    var result=new List<string>(); var field=new StringBuilder(); bool quoted=false;
    for(int index=0; index<line.Length; index++) {
      char value=line[index];
      if(value=='"') { if(quoted && index+1<line.Length && line[index+1]=='"') { field.Append('"'); index++; } else { quoted=!quoted; } }
      else if(value==',' && !quoted) { result.Add(field.ToString()); field.Clear(); }
      else { field.Append(value); }
    }
    Require(!quoted,"CSV sin cierre de comillas."); result.Add(field.ToString()); return result;
  }
  private static List<Record> GetRecords(string root) {
    var manifest=Path.Combine(root,"SHA256SUMS.csv"); Require(Hash(manifest)==ManifestSha,"Manifiesto incompatible.");
    var lines=File.ReadAllLines(manifest,Encoding.UTF8); Require(lines.Length>1,"Manifiesto vacio.");
    var columns=CsvLine(lines[0].TrimStart('\uFEFF')); Require(columns.Count==3 && columns.Contains("File") && columns.Contains("SHA256") && columns.Contains("Bytes"),"Columnas de manifiesto incompatibles.");
    int fileColumn=columns.IndexOf("File"), hashColumn=columns.IndexOf("SHA256"), bytesColumn=columns.IndexOf("Bytes");
    var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase); var result=new List<Record>();
    foreach(var line in lines.Skip(1)) {
      if(line.Length==0) { continue; }
      var values=CsvLine(line); long bytes;
      Require(values.Count==3 && SafeRelative(values[fileColumn]) && seen.Add(values[fileColumn]) && Regex.IsMatch(values[hashColumn],"^[A-Fa-f0-9]{64}$") && Int64.TryParse(values[bytesColumn],NumberStyles.None,CultureInfo.InvariantCulture,out bytes),"Registro de manifiesto no valido.");
      result.Add(new Record { File=values[fileColumn], Hash=values[hashColumn].ToUpperInvariant(), Bytes=Int64.Parse(values[bytesColumn],CultureInfo.InvariantCulture) });
    }
    Require(result.Count==ManifestCount,"Recuento de manifiesto incorrecto."); return result;
  }
  private static List<Record> VerifyPackage(string root) {
    PlainDirectory(root); var records=GetRecords(root);
    foreach(var record in records) {
      var path=Inside(root,record.File); PlainFile(path);
      var parent=Path.GetDirectoryName(path);
      while(parent.Length>=root.Length) { PlainDirectory(parent); if(String.Equals(parent,root,StringComparison.OrdinalIgnoreCase)) { break; } parent=Path.GetDirectoryName(parent); }
      Require(new FileInfo(path).Length==record.Bytes && Hash(path)==record.Hash,"Archivo no coincide: "+record.File);
    }
    Require(Hash(Path.Combine(root,"NoTraceLanQa.exe"))==QaSha,"El ejecutor QA no corresponde.");
    Require(Number(ReadJson(Path.Combine(root,"PROCEDENCIA.json"),4194304),"Protocol")==Protocol,"Protocolo incompatible."); return records;
  }
  private static void PreparePackage() {
    PlainDirectory(Transfer); PlainDirectory(Results);
    var zip=Path.Combine(Transfer,ZipName); Require(Hash(zip)==ZipSha,"ZIP incompatible; no se extraera.");
    using(var archive=ZipFile.OpenRead(zip)) {
      var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
      foreach(var entry in archive.Entries) { Require(SafeRelative(entry.FullName) && seen.Add(entry.FullName) && ((entry.ExternalAttributes>>16)&0xF000)!=0xA000,"Ruta o enlace no permitido en ZIP."); }
      Require(archive.Entries.Count==ManifestCount+1,"ZIP con recuento incorrecto.");
      PrepareDirectory(Package,delegate(string staging) {
        foreach(var entry in archive.Entries) {
          var target=Inside(staging,entry.FullName); Directory.CreateDirectory(Path.GetDirectoryName(target));
          using(var read=entry.Open()) using(var write=new FileStream(target,FileMode.CreateNew,FileAccess.Write,FileShare.Read)) { read.CopyTo(write); }
        }
      },delegate(string folder) { VerifyPackage(folder); });
    }
    Records=VerifyPackage(Package);
  }
  private static void PrepareDirectory(string destination,Action<string> populate,Action<string> verify) {
    if(Directory.Exists(destination)) { verify(destination); return; }
    var staging=destination+".preparing-"+Guid.NewGuid().ToString("N");
    Directory.CreateDirectory(staging);
    try {
      populate(staging); verify(staging);
      try { Directory.Move(staging,destination); }
      catch(IOException) { if(!Directory.Exists(destination)) { throw; } verify(destination); }
    } catch(Exception error) { throw new InvalidOperationException("Copia incompleta conservada en "+staging+": "+error.Message,error); }
  }
  private static void CheckJob(Dictionary<string,object> job) {
    var allowed=new[]{"schema","sessionId","jobId","runId","packageLabel","protocol","role","scenario","host","port","timeoutSeconds"};
    Require(job.Count==allowed.Length && job.Keys.All(allowed.Contains),"Campo de job no permitido.");
    Require(Number(job,"schema")==1 && Text(job,"sessionId")==SessionId && Id(Text(job,"jobId")) && Id(Text(job,"runId")) && Text(job,"packageLabel")==Label && Number(job,"protocol")==Protocol && Text(job,"role")=="client" && new[]{"mission","client-drop","host-drop"}.Contains(Text(job,"scenario")) && Text(job,"host")=="192.168.56.1" && Number(job,"port")==55041 && Number(job,"timeoutSeconds")==360,"Job fuera de la lista cerrada.");
  }
  private static void CheckIdentity(Dictionary<string,object> value, Dictionary<string,object> job) {
    Require(Number(value,"schema")==1 && Text(value,"runId")==Text(job,"runId") && Text(value,"role")=="client" && Text(value,"scenario")==Text(job,"scenario"),"Identidad de reporte o marcador incorrecta.");
  }
  private static void Heartbeat(string state,string jobId) { WriteJson(Path.Combine(SessionFolder,"HEARTBEAT.json"),Map("schema",1,"sessionId",SessionId,"packageLabel",Label,"protocol",Protocol,"utc",Utc(),"state",state,"jobId",jobId)); }
  private static bool StopRequested() {
    if(Stopping) { return true; }
    var path=Path.Combine(Transfer,"QA-CANCEL","STOP-"+SessionId+".json");
    if(!File.Exists(path)) { return false; } var value=ReadJson(path,65536); return Number(value,"schema")==1 && Text(value,"sessionId")==SessionId;
  }
  private static string CancellationReason(Dictionary<string,object> job) {
    if(StopRequested()) { return "Parada del ejecutor VM"; }
    var path=Path.Combine(Transfer,"QA-CANCEL",Text(job,"jobId")+".json");
    if(!File.Exists(path)) { return null; } var value=ReadJson(path,65536);
    if(Number(value,"schema")!=1 || Text(value,"sessionId")!=SessionId || Text(value,"jobId")!=Text(job,"jobId")) { return null; }
    object reason; return value.TryGetValue("reason",out reason) && reason is string && ((string)reason).Length>0 ? (string)reason : "Cancelacion del ejecutor VM";
  }
  private static void CancelChild(Process child,string coord,string runId,string reason) {
    var cancel=Path.Combine(coord,"cancel.json"); bool exists=false;
    foreach(var name in new[]{"cancel.json","CANCEL.json"}) {
      var existing=Path.Combine(coord,name);
      if(File.Exists(existing)) {
        var value=ReadJson(existing,65536);
        if(Number(value,"schema")==1 && Text(value,"runId")==runId) { exists=true; break; }
      }
    }
    if(!exists) { WriteJson(cancel,Map("schema",1,"runId",runId,"reason",reason,"createdUtc",Utc())); }
    if(!child.HasExited && !child.WaitForExit(3000)) { child.Kill(); child.WaitForExit(); }
  }
  private static string Quote(string value) { Require(!value.Contains("\"") && !value.EndsWith("\\",StringComparison.Ordinal),"Ruta no permitida para argumento."); return "\""+value+"\""; }
  private static bool Exportable(string relative) {
    if(relative.Split(Path.DirectorySeparatorChar).Any(part=>new[]{"runtime","Content","Save.json"}.Contains(part,StringComparer.OrdinalIgnoreCase))) { return false; }
    var extension=Path.GetExtension(relative).ToLowerInvariant(); return extension==".json" || extension==".log" || extension==".png";
  }
  private static void ExportEvidence(string source,string destination) {
    Require(!Directory.Exists(destination),"El destino de evidencia ya existe."); Directory.CreateDirectory(destination);
    var files=new List<object>(); long total=0;
    foreach(var path in Directory.GetFiles(source,"*",SearchOption.AllDirectories)) {
      var relative=path.Substring(source.Length).TrimStart(Path.DirectorySeparatorChar);
      if(!Exportable(relative)) { continue; }
      PlainFile(path); var bytes=new FileInfo(path).Length; total+=bytes;
      Require(bytes<=33554432 && total<=268435456 && files.Count<256,"Evidencia supera limite de exportacion.");
      var parent=Path.GetDirectoryName(path); while(parent.Length>=source.Length) { PlainDirectory(parent); if(parent==source) { break; } parent=Path.GetDirectoryName(parent); }
      var target=Path.Combine(destination,relative); Directory.CreateDirectory(Path.GetDirectoryName(target)); File.Copy(path,target,false);
      files.Add(Map("file",relative.Replace('\\','/'),"bytes",bytes,"sha256",Hash(target)));
    }
    WriteJson(Path.Combine(destination,"EXPORTACION.json"),Map("schema",1,"sessionId",SessionId,"packageLabel",Label,"protocol",Protocol,"saveExported",false,"files",files));
  }
  private static void RunJob(Dictionary<string,object> job) {
    CheckJob(job); var jobId=Text(job,"jobId"); if(!Seen.Add(jobId)) { return; }
    var runId=Text(job,"runId"); var scenario=Text(job,"scenario");
    var runRoot=Path.Combine(Package,"QA-RUNS",jobId); Require(!Directory.Exists(runRoot),"El job ya tiene una carpeta local."); Directory.CreateDirectory(runRoot);
    var runtime=Path.Combine(runRoot,"runtime"); Directory.CreateDirectory(runtime);
    foreach(var record in Records) { var target=Inside(runtime,record.File); Directory.CreateDirectory(Path.GetDirectoryName(target)); File.Copy(Inside(Package,record.File),target,false); }
    File.Copy(Path.Combine(Package,"SHA256SUMS.csv"),Path.Combine(runtime,"SHA256SUMS.csv"),false); VerifyPackage(runtime);
    var coord=Path.Combine(Results,"QA-RUNS",runId,"coord"); PlainDirectory(coord);
    var remote=Path.Combine(SessionFolder,"jobs",jobId); Directory.CreateDirectory(remote);
    var attempts=new List<object>(); var forcedDrops=new List<object>();
    var receipt=Map("schema",1,"sessionId",SessionId,"jobId",jobId,"runId",runId,"scenario",scenario,"role","client","status","STARTED","startedUtc",Utc(),"packageLabel",Label,"protocol",Protocol,"qaSha256",QaSha,"attempts",attempts,"forcedDrops",forcedDrops,"report",null,"error",null);
    WriteJson(Path.Combine(remote,"STARTED.json"),receipt); var watch=Stopwatch.StartNew();
    try {
      for(int attempt=1;attempt<=2;attempt++) {
        var output=Path.Combine(runRoot,"attempt-"+attempt); Directory.CreateDirectory(output);
        var arguments="--role client --scenario "+scenario+" --host 192.168.56.1 --port 55041 --run-id "+runId+" --coord "+Quote(coord)+" --output "+Quote(output)+" --timeout 360";
        using(var recorder=new Recorder(Path.Combine(runtime,"NoTraceLanQa.exe"),runtime,arguments,output)) {
          bool forced=false, cancelled=false; var child=recorder.Child;
          try {
            recorder.Start(); var item=Map("attempt",attempt,"pid",child.Id,"startedUtc",Utc(),"exitCode",null,"forced",false); attempts.Add(item); WriteJson(Path.Combine(remote,"STARTED.json"),receipt);
            while(!child.HasExited) {
              Heartbeat("RUNNING",jobId);
              var cancellation=CancellationReason(job);
              if(cancellation!=null) { cancelled=true; receipt["error"]=cancellation; CancelChild(child,coord,runId,cancellation); break; }
              if(watch.Elapsed.TotalSeconds>365) { CancelChild(child,coord,runId,"Timeout del ejecutor VM"); throw new TimeoutException("Tiempo agotado del job VM."); }
              var finalReport=Path.Combine(output,"qa-report.json");
              if(File.Exists(finalReport)) {
                var ending=ReadJson(finalReport,4194304); CheckIdentity(ending,job);
                if(Text(ending,"status")=="CANCELLED") { cancelled=true; receipt["report"]=ending; receipt["error"]=Text(ending,"reason"); CancelChild(child,coord,runId,Text(ending,"reason")); break; }
              }
              var ready=Path.Combine(coord,"client-drop-ready.json");
              if(scenario=="client-drop" && attempt==1 && File.Exists(ready) && !File.Exists(finalReport)) {
                CheckIdentity(ReadJson(ready,4194304),job); child.Kill(); child.WaitForExit(); forced=true; item["forced"]=true;
                var drop=Map("schema",1,"runId",runId,"role","client","scenario",scenario,"attempt",1,"pid",child.Id,"exitCode",child.ExitCode,"createdUtc",Utc(),"method","Process.Kill del hijo exacto del ejecutor VM");
                WriteJson(Path.Combine(coord,"client-forced-drop.json"),drop); forcedDrops.Add(drop); break;
              }
              Thread.Sleep(500);
            }
            recorder.Wait(); item["exitCode"]=child.ExitCode; item["finishedUtc"]=Utc();
            if(cancelled) { receipt["status"]="CANCELLED"; break; }
            if(forced) { continue; }
            var report=ReadJson(Path.Combine(output,"qa-report.json"),4194304); CheckIdentity(report,job); receipt["report"]=report;
            if(child.ExitCode==3 && Text(report,"status")=="CANCELLED") { receipt["status"]="CANCELLED"; receipt["error"]=Text(report,"reason"); break; }
            Require(child.ExitCode==0 && Text(report,"status")=="PASS","El juego QA no completo el escenario con PASS.");
            Require(scenario!="client-drop" || forcedDrops.Count==1,"Falta la terminacion abrupta real del primer cliente."); receipt["status"]="PASS"; break;
          } finally { if(recorder.Started && !child.HasExited) { child.Kill(); child.WaitForExit(); } }
        }
      }
    } catch(Exception error) { receipt["status"]="FAIL"; receipt["error"]=error.Message; }
    finally {
      receipt["finishedUtc"]=Utc(); WriteJson(Path.Combine(runRoot,"RESULTADO.json"),receipt);
      try { ExportEvidence(runRoot,Path.Combine(remote,"evidence")); } catch(Exception error) { receipt["status"]="FAIL"; receipt["error"]="No se exporto evidencia: "+error.Message; }
      WriteJson(Path.Combine(remote,"RESULTADO.json"),receipt);
    }
  }
  private static int SelfTest() {
    var valid=Map("schema",1,"sessionId",SessionId,"jobId",new string('b',32),"runId",new string('c',32),"packageLabel",Label,"protocol",Protocol,"role","client","scenario","mission","host","192.168.56.1","port",55041,"timeoutSeconds",360); CheckJob(valid);
    var invalid=new List<Dictionary<string,object>>();
    foreach(var item in new[]{Map("command","cmd.exe"),Map("host","127.0.0.1 & calc"),Map("role","host"),Map("scenario","shell"),Map("runId","../escape"),Map("timeoutSeconds",99999),Map("schema",true),Map("scenario",new[]{"mission"}),Map("sessionId",new string('d',32))}) {
      var copy=new Dictionary<string,object>(valid); foreach(var pair in item) { copy[pair.Key]=pair.Value; } invalid.Add(copy);
    }
    int rejected=0; foreach(var item in invalid) { try { CheckJob(item); } catch(InvalidOperationException) { rejected++; } }
    Require(rejected==invalid.Count,"Se acepto una solicitud malformada.");
    Require(!SafeRelative("../escape") && !SafeRelative("C:/escape") && !SafeRelative("Content/Save.json") && !SafeRelative("Content/Screenshots/a.png") && SafeRelative("Content/Shaders/Additive.frag"),"Validacion de rutas incorrecta.");
    Require(!Exportable(@"attempt-1\runtime\Content\Save.json") && !Exportable(@"runtime\Content\Config.json") && !Exportable(@"attempt-1\Save.json") && Exportable(@"attempt-1\captures\sala.png") && Exportable(@"attempt-1\qa-report.json"),"Exportacion de evidencia no aislada.");
    bool identityRejected=false; try { CheckIdentity(Map("schema",1,"runId",new string('d',32),"role","client","scenario","mission"),valid); } catch(InvalidOperationException) { identityRejected=true; }
    Require(identityRejected,"Se acepto un marcador de otra ejecucion.");
    var copyRoot=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"copy-self-test-"+Guid.NewGuid().ToString("N")); Directory.CreateDirectory(copyRoot);
    var destination=Path.Combine(copyRoot,"package"); bool interrupted=false;
    Action<string> verify=delegate(string folder) { Require(File.ReadAllText(Path.Combine(folder,"payload.txt"))=="completo","Copia no verificada."); };
    try { PrepareDirectory(destination,delegate(string folder) { File.WriteAllText(Path.Combine(folder,"payload.txt"),"parcial"); throw new IOException("Corte de copia inyectado"); },verify); }
    catch(InvalidOperationException) { interrupted=true; }
    var partials=Directory.GetDirectories(copyRoot,"package.preparing-*");
    Require(interrupted && !Directory.Exists(destination) && partials.Length==1 && File.ReadAllText(Path.Combine(partials[0],"payload.txt"))=="parcial","Una copia interrumpida publico el destino final o perdio evidencia.");
    PrepareDirectory(destination,delegate(string folder) { File.WriteAllText(Path.Combine(folder,"payload.txt"),"completo"); },verify); verify(destination);
    Require(File.ReadAllText(Path.Combine(partials[0],"payload.txt"))=="parcial","Se sobrescribio la copia incompleta anterior.");
    File.WriteAllText(Path.Combine(destination,"payload.txt"),"modificado"); bool preserved=false;
    try { PrepareDirectory(destination,delegate(string folder) { throw new InvalidOperationException("No debe copiar sobre un destino existente"); },verify); }
    catch(InvalidOperationException) { preserved=File.ReadAllText(Path.Combine(destination,"payload.txt"))=="modificado"; }
    Require(preserved,"Se altero una copia local modificada.");
    Console.WriteLine(Json.Serialize(Map("status","PASS","malformedJobsRejected",rejected,"unsafePathsRejected",true,"saveExportRejected",true,"crossRunReportRejected",true,"interruptedCopyRecovered",true,"partialCopyPreserved",true,"modifiedDestinationPreserved",true,"copyEvidence",copyRoot,"gameExecuted",false,"vmChanged",false))); return 0;
  }
  private static int Main(string[] args) {
    try {
      if(args.Length==1 && args[0]=="--self-test") { return SelfTest(); }
      Require(args.Length==0,"El ejecutor VM no admite comandos ni argumentos.");
      Console.CancelKeyPress+=delegate(object sender,ConsoleCancelEventArgs value) { Stopping=true; value.Cancel=true; };
      AppDomain.CurrentDomain.ProcessExit+=delegate { lock(ProcessGate) { try { if(ActiveChild!=null && !ActiveChild.HasExited) { ActiveChild.Kill(); } } catch(InvalidOperationException) { } } };
      PreparePackage(); Directory.CreateDirectory(SessionFolder); var started=DateTime.UtcNow;
      WriteJson(Path.Combine(SessionFolder,"READY.json"),Map("schema",1,"sessionId",SessionId,"packageLabel",Label,"protocol",Protocol,"qaSha256",QaSha,"manifestVerified",ManifestCount,"startedUtc",started.ToString("o"),"expiresUtc",started.AddHours(4).ToString("o"),"computerName",Environment.MachineName,"manualBootstrap",true,"supervisor","C# .NET Framework","scope","NoTraceLanQa.exe: mission, client-drop, host-drop"));
      Console.WriteLine("Ejecutor QA listo. Sesion: "+SessionId); Console.WriteLine("Deja esta consola abierta. Caduca en cuatro horas; Ctrl+C cancela solo sus propias pruebas.");
      while((DateTime.UtcNow-started).TotalHours<4 && !StopRequested()) {
        Heartbeat("IDLE","");
        foreach(var path in Directory.GetFiles(Path.Combine(Transfer,"QA-JOBS"),"*.json")) {
          var jobId=Path.GetFileNameWithoutExtension(path); if(!Id(jobId) || Seen.Contains(jobId)) { continue; }
          try {
            var job=ReadJson(path,65536); if(Text(job,"sessionId")!=SessionId) { continue; } CheckJob(job); Require(Text(job,"jobId")==jobId,"El nombre del job no corresponde."); RunJob(job);
          } catch(Exception error) { Seen.Add(jobId); WriteJson(Path.Combine(SessionFolder,"REJECTED-"+jobId+".json"),Map("schema",1,"sessionId",SessionId,"jobId",jobId,"status","REJECTED","error",error.Message,"utc",Utc())); }
          if(StopRequested()) { break; }
        }
        Thread.Sleep(2000);
      }
      Heartbeat("STOPPED",""); return 0;
    } catch(Exception error) { Console.Error.WriteLine("FAIL del ejecutor QA: "+error.Message); return 1; }
  }
}
