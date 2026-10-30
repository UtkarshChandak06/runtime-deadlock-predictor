import { useEffect, useRef, useState } from "react";
import { request } from "../api/client";
import type { Session } from "../api/types";
import { Panel } from "../components/Common";

type File = { path: string; content: string };
type Project = { id: string; name: string; revision_id: string; files?: File[] };
type Build = { id: string; project_id: string; revision_id: string; state: string; stdout: string; stderr: string;
  error: string | null; duration: number | string | null; exit_code: number | null; output_truncated: boolean;
  diagnostics: {file:string;line:number;column:number;message:string}[] };
type Capabilities = { isolation: {available:boolean;reason:string;trust:string;limits:Record<string,unknown>};
  supported:string[];unsupported:string[];limits:Record<string,number>;interpretation:string };

export const samples: Record<string,File[]> = {
  "Consistent ordering": [{path:"main.c",content:`#include <pthread.h>
#include <stdio.h>
pthread_mutex_t first, second;
void *worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&first);
    pthread_mutex_lock(&second);
    puts("Both locks acquired");
    pthread_mutex_unlock(&second);
    pthread_mutex_unlock(&first);
    return NULL;
}
int main(void) {
    pthread_t a, b;
    pthread_mutex_init(&first, NULL);
    pthread_mutex_init(&second, NULL);
    pthread_create(&a, NULL, worker, NULL);
    pthread_create(&b, NULL, worker, NULL);
    pthread_join(a, NULL); pthread_join(b, NULL);
    pthread_mutex_destroy(&first); pthread_mutex_destroy(&second);
    return 0;
}
`}],
  "Conflicting historical ordering": [{path:"main.c",content:`#include <pthread.h>
int main(void) {
    pthread_mutex_t a, b;
    pthread_mutex_init(&a, 0); pthread_mutex_init(&b, 0);
    pthread_mutex_lock(&a); pthread_mutex_lock(&b);
    pthread_mutex_unlock(&b); pthread_mutex_unlock(&a);
    // Sequential inversion: a historical risk, not a simultaneous deadlock.
    pthread_mutex_lock(&b); pthread_mutex_lock(&a);
    pthread_mutex_unlock(&a); pthread_mutex_unlock(&b);
    pthread_mutex_destroy(&a); pthread_mutex_destroy(&b);
    return 0;
}
`}],
  "Multiple files": [{path:"main.c",content:'#include "message.h"\nint main(void) { message(); return 0; }\n'},
    {path:"message.h",content:'void message(void);\n'},
    {path:"message.c",content:'#include <stdio.h>\nvoid message(void) { puts("Hello from another source file"); }\n'}],
};

export function highlight(code:string) {
  const tokens=code.split(/(\/\*[\s\S]*?\*\/|\/\/[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|^\s*#.*$|\b(?:int|void|return|if|else|for|while|struct|typedef|static|const|char|long|unsigned|sizeof|NULL)\b|\b\d+\b)/gm);
  return tokens.map((t,i)=><span key={i} className={/^\/[/\*]/.test(t)?"token-comment":/^["']/.test(t)?"token-string":/^\s*#/.test(t)?"token-preproc":/^(int|void|return|if|else|for|while|struct|typedef|static|const|char|long|unsigned|sizeof|NULL)$/.test(t)?"token-keyword":/^\d+$/.test(t)?"token-number":""}>{t}</span>);
}

export function YourCode({onRun}:{onRun:(s:Session)=>void}) {
  const [files,setFiles]=useState<File[]>(samples["Consistent ordering"].map(f=>({...f})));
  const [active,setActive]=useState(0),[name,setName]=useState("My pthread program"),[project,setProject]=useState<Project|null>(null);
  const [projects,setProjects]=useState<Project[]>([]),[builds,setBuilds]=useState<Build[]>([]),[build,setBuild]=useState<Build|null>(null);
  const [cap,setCap]=useState<Capabilities|null>(null),[error,setError]=useState(""),[busy,setBusy]=useState(false);
  const [builtFiles,setBuiltFiles]=useState(""),[args,setArgs]=useState("[]"),[stdin,setStdin]=useState(""),[seconds,setSeconds]=useState(5);
  const [filename,setFilename]=useState("helper.h"),[run,setRun]=useState<Session|null>(null);
  const painted=useRef<HTMLPreElement>(null);
  const signature=JSON.stringify(files);
  const stale=!!build && (signature!==builtFiles || project?.revision_id!==build.revision_id);
  const building=build?.state==="building";
  const running=!!run && ["starting","running"].includes(run.state);
  async function refresh() {
    try {const [c,p,b]=await Promise.all([request<Capabilities>("/capabilities"),request<Project[]>("/projects"),request<Build[]>("/builds")]);
      setCap(c);setProjects(p);setBuilds(b);
    } catch(e){setError((e as Error).message);}
  }
  useEffect(()=>{void refresh();},[]);
  useEffect(()=>{
    if(!building || !build)return;
    const timer=setInterval(()=>{void request<Build>(`/builds/${build.id}`).then(b=>{setBuild(b);if(b.state!=="building")void refresh();}).catch(e=>setError(e.message));},500);
    return ()=>clearInterval(timer);
  },[build?.id,building]);
  useEffect(()=>{
    if(!running || !run)return;
    const timer=setInterval(()=>{void request<Session>(`/sessions/${run.session_id}`).then(setRun).catch(e=>setError(e.message));},500);
    return ()=>clearInterval(timer);
  },[run?.session_id,running]);
  async function action(fn:()=>Promise<void>) {setBusy(true);setError("");try{await fn();}catch(e){setError((e as Error).message);}finally{setBusy(false);}}
  async function save() {
    const saved=await request<Project>(project?`/projects/${project.id}/revisions`:"/projects",{method:"POST",body:JSON.stringify({name,files})});
    setProject(saved);return saved;
  }
  async function compile() {const saved=await save();const next=await request<Build>(`/projects/${saved.id}/builds`,{method:"POST"});setBuild(next);setBuiltFiles(signature);await refresh();}
  async function execute() {
    const list:unknown=JSON.parse(args);
    if(!Array.isArray(list)||list.some(x=>typeof x!=="string"))throw Error('Arguments must be a JSON array of strings, such as ["one", "two"].');
    if(!build||!project||stale)throw Error("Save and rebuild the edited source first.");
    const s=await request<Session>(`/builds/${build.id}/runs`,{method:"POST",body:JSON.stringify({revision_id:project.revision_id,args:list,stdin,seconds})});
    setRun(s);onRun(s);
  }
  async function load(id:string) {
    if(!id){setProject(null);setBuild(null);setName("My pthread program");return;}
    const p=await request<Project>(`/projects/${id}`);setProject(p);setName(p.name);setFiles(p.files!);setActive(0);
    const previous=builds.filter(b=>b.project_id===id&&b.revision_id===p.revision_id).at(-1);
    setBuild(previous||null);setBuiltFiles(JSON.stringify(p.files));
  }
  return <div className="stack your-code">
    <Panel title="Your Code" subtitle="Paste or upload C source, build it in isolation, then run with the real monitor.">
      {error&&<p role="alert" className="error-banner">{error}</p>}
      {!cap?.isolation.available&&<p className="warn">Custom Build and Run are disabled. {cap?.isolation.reason||"Checking isolation provider…"}</p>}
      <div className="workspace-controls">
        <label>Project name<input value={name} maxLength={80} onChange={e=>setName(e.target.value)}/></label>
        <label>Saved project<select value={project?.id||""} disabled={busy||building||running} onChange={e=>void action(()=>load(e.target.value))}>
          <option value="">New project</option>{projects.map(p=><option key={p.id} value={p.id}>{p.name}</option>)}</select></label>
        <label>Load editable example<select defaultValue="" disabled={busy||building||running} onChange={e=>{if(e.target.value){setFiles(samples[e.target.value].map(f=>({...f})));setActive(0);e.target.value="";}}}>
          <option value="">Choose source</option>{Object.keys(samples).map(k=><option key={k}>{k}</option>)}</select></label>
      </div>
      <p>Up to 8 .c/.h files, 128 KiB total. File paths are relative. No archives, Makefiles, shell commands or custom compiler flags.</p>
      <div className="actions">
        <button onClick={()=>void refresh()}>Refresh workspace</button>
        <button disabled={busy||building||running} onClick={()=>{setProject(null);setBuild(null);setFiles([]);setActive(0);}}>New empty workspace</button>
        <label className="upload-label">Upload C/H files<input aria-label="Upload C/H files" type="file" multiple accept=".c,.h" onChange={e=>{
          const picked=Array.from(e.target.files||[]);e.target.value="";
          void action(async()=>{if(picked.length+files.length>8||picked.reduce((n,f)=>n+f.size,0)>131072)throw Error("File count or size limit exceeded.");
            const added=await Promise.all(picked.map(async f=>({path:f.name,content:await f.text()})));
            if(added.some(f=>files.some(old=>old.path.toLowerCase()===f.path.toLowerCase())))throw Error("Duplicate filename. Rename or remove the existing file first.");
            setFiles(old=>[...old,...added]);setActive(files.length);});
        }}/></label>
        <input aria-label="New filename" value={filename} maxLength={96} onChange={e=>setFilename(e.target.value)}/>
        <button disabled={files.length>=8} onClick={()=>{setFiles([...files,{path:filename,content:""}]);setActive(files.length);}}>Add file</button>
      </div>
      <div role="tablist" aria-label="Source files" className="file-tabs">{files.map((f,i)=><button role="tab" aria-selected={i===active} key={i} onClick={()=>setActive(i)}>{f.path}</button>)}</div>
      <div className="actions"><label>Filename<input aria-label="Filename" value={files[active]?.path||""} onChange={e=>setFiles(files.map((f,i)=>i===active?{...f,path:e.target.value}:f))}/></label>
        <button disabled={files.length<2} onClick={()=>{setFiles(files.filter((_,i)=>i!==active));setActive(0);}}>Remove file</button></div>
      <div className="source-editor">
        <pre ref={painted} aria-hidden="true">{highlight(files[active]?.content||"")}{"\n"}</pre>
        <textarea aria-label="C source editor" disabled={!files.length} maxLength={131072} spellCheck={false} value={files[active]?.content||""}
          onScroll={e=>{if(painted.current){painted.current.scrollTop=e.currentTarget.scrollTop;painted.current.scrollLeft=e.currentTarget.scrollLeft;}}}
          onChange={e=>setFiles(files.map((f,i)=>i===active?{...f,content:e.target.value}:f))}/>
      </div>
      <div className="actions">
        <button disabled={busy||building} onClick={()=>void action(async()=>{await save();await refresh();})}>Save revision</button>
        <button className="primary" disabled={busy||building||!cap?.isolation.available} onClick={()=>void action(compile)}>Build</button>
        <button disabled={busy||!building} onClick={()=>void action(async()=>{setBuild(await request<Build>(`/builds/${build!.id}/stop`,{method:"POST"}));})}>Stop build</button>
        <button disabled={!project||busy||building||running} onClick={()=>void action(async()=>{await request(`/projects/${project!.id}`,{method:"DELETE"});setProject(null);setBuild(null);await refresh();})}>Delete project and retained runs</button>
      </div>
      <p aria-live="polite">{build?`Build ${build.state}${stale?" — STALE: source edited; rebuild required":""}`:"No build yet"}</p>
      {build&&<><p className="mono">Build ID {build.id}<br/>Source revision {build.revision_id}</p>
        <p>Compiler exit: {build.exit_code??"pending"} · Duration: {build.duration==null?"pending":Number(build.duration).toFixed(3)} s {build.output_truncated&&"· Output truncated"}</p>
        {build.error&&<p className="warn">{build.error}</p>}
        <pre className="console">{build.stderr||build.stdout||"No compiler diagnostics."}</pre>
        {build.diagnostics.map((d,i)=><p key={i}>{d.file}:{d.line}:{d.column}: {d.message}</p>)}</>}
    </Panel>
    <Panel title="Run settings" subtitle="The selected immutable build runs inside the same Linux runtime as its monitor.">
      <label>Arguments as a JSON string array<input aria-label="Arguments" value={args} onChange={e=>setArgs(e.target.value)}/></label>
      <label>Standard input (up to 8 KiB)<textarea aria-label="Standard input" maxLength={8192} value={stdin} onChange={e=>setStdin(e.target.value)}/></label>
      <label>Runtime limit in seconds<input type="number" min={1} max={30} value={seconds} onChange={e=>setSeconds(Number(e.target.value))}/></label>
      <div className="actions"><button className="primary" disabled={busy||running||building||stale||build?.state!=="succeeded"||!cap?.isolation.available} onClick={()=>void action(execute)}>Run with Monitoring</button>
        <button disabled={!running||busy} onClick={()=>void action(async()=>{setRun(await request<Session>(`/sessions/${run!.session_id}/stop`,{method:"POST"}));})}>Stop run</button>
        {run&&<button onClick={()=>onRun(run)}>Inspect last run</button>}</div>
      {run&&<p>Last run: {run.state} · {run.warning_count} observed warnings. Inspect the session for output, coverage, graph and replay.</p>}
    </Panel>
    <Panel title="Supported scope and isolation" subtitle="Warnings describe observed historical dependencies, not a proof of safety or a general deadlock verdict.">
      <p>{cap?.interpretation}</p><p>Supported: {cap?.supported.join(", ")}</p><p>Unsupported: {cap?.unsupported.join(", ")}</p>
      <p>{cap?.isolation.trust}</p><p>Source location unavailable. No guessed file, function or line attribution.</p>
      <details><summary>Resource and retention limits</summary><pre>{JSON.stringify({workspace:cap?.limits,isolation:cap?.isolation.limits},null,2)}</pre></details>
    </Panel>
  </div>;
}
