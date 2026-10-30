"""Bounded local projects, immutable revisions/builds and isolated compilation."""
import asyncio
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sqlite3
import time
import uuid
import atexit
from fastapi import HTTPException
from pydantic import BaseModel, ConfigDict, Field, model_validator
from .isolation import Isolation, COMPILER

MAX_FILES=8
MAX_SOURCE=131072
CONFIG='gcc-c11-O0-g-pthread-v1'


class SourceFile(BaseModel):
    model_config=ConfigDict(extra='forbid',strict=True)
    path: str = Field(min_length=1,max_length=96)
    content: str = Field(max_length=MAX_SOURCE)


class SourceInput(BaseModel):
    model_config=ConfigDict(extra='forbid',strict=True)
    name: str = Field(default='Untitled C project',min_length=1,max_length=80)
    files: list[SourceFile] = Field(min_length=1,max_length=MAX_FILES)

    @model_validator(mode='after')
    def paths(self):
        seen=set(); total=0
        for file in self.files:
            path=file.path
            if (not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_./-]*\.(c|h)',path) or
                any(x in ('','.','..') or x.startswith('.') for x in path.split('/')) or
                path.casefold() in seen or '\x00' in file.content):
                raise ValueError('invalid or duplicate source path; only relative .c/.h paths are allowed')
            seen.add(path.casefold());total+=len(file.content.encode('utf-8'))
        if total>MAX_SOURCE:raise ValueError('source exceeds 128 KiB total')
        if not any(f.path.endswith('.c') for f in self.files):raise ValueError('at least one .c file required')
        return self


class RunInput(BaseModel):
    model_config=ConfigDict(extra='forbid',strict=True)
    revision_id: str = Field(pattern=r'^[0-9a-f]{64}$')
    args: list[str] = Field(default_factory=list,max_length=16)
    stdin: str = Field(default='',max_length=8192)
    seconds: int = Field(default=5,ge=1,le=30)

    @model_validator(mode='after')
    def bounded(self):
        if any('\x00' in x or len(x.encode())>256 for x in self.args):raise ValueError('invalid argument')
        if '\x00' in self.stdin or len(self.stdin.encode())>8192:raise ValueError('stdin limit')
        return self


class Store:
    def __init__(self, path):
        self.root=Path(path).resolve();self.root.mkdir(parents=True,exist_ok=True,mode=0o700)
        self.lock_file=None
        self.lock()
        self.db=sqlite3.connect(self.root/'workspace.sqlite')
        self.db.execute('PRAGMA journal_mode=DELETE')
        self.db.execute('PRAGMA max_page_count=16384')  # 64 MiB database ceiling
        self.db.execute('CREATE TABLE IF NOT EXISTS records (kind TEXT,id TEXT,payload TEXT,PRIMARY KEY(kind,id))')
        self.db.commit()
        atexit.register(self.close)
    def lock(self):
        if self.lock_file:return
        import fcntl
        self.lock_file=(self.root/'backend.lock').open('a')
        try:fcntl.flock(self.lock_file,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except OSError:
            self.lock_file.close();self.lock_file=None
            raise RuntimeError('Another backend already owns this workspace')
    def close(self):
        if self.lock_file:self.lock_file.close();self.lock_file=None
        self.db.close()
        atexit.unregister(self.close)
    def put(self,kind,identifier,value):
        try:
            self.db.execute('INSERT OR REPLACE INTO records VALUES(?,?,?)',(kind,identifier,json.dumps(value)))
            self.db.commit()
        except sqlite3.OperationalError as error:
            self.db.rollback()
            raise HTTPException(507,'workspace_storage_unavailable_delete_retained_data') from error
    def get(self,kind,identifier):
        r=self.db.execute('SELECT payload FROM records WHERE kind=? AND id=?',(kind,identifier)).fetchone()
        if not r:raise HTTPException(404,'unknown_'+kind)
        return json.loads(r[0])
    def all(self,kind):
        return [json.loads(r[0]) for r in self.db.execute('SELECT payload FROM records WHERE kind=? ORDER BY rowid',(kind,))]
    def delete(self,kind,identifier):
        self.db.execute('DELETE FROM records WHERE kind=? AND id=?',(kind,identifier));self.db.commit()


@dataclass
class UserTarget:
    application_id: str
    metadata: dict
    deadlock: bool=False


class Workspace:
    def __init__(self,settings,manager):
        self.settings=settings;self.manager=manager
        self.store=Store(settings.workspace_path)
        self.runner=Isolation();self.jobs={};self.stops={};self.closing=False
        self.artifacts=self.store.root/'artifacts';self.artifacts.mkdir(exist_ok=True)
        # Scratch exists only for active compilation; remove abandoned generated
        # directories on restart, never follow a request-controlled filesystem path.
        self.scratch=self.store.root/'scratch'
        if self.scratch.exists():shutil.rmtree(self.scratch)
        self.scratch.mkdir()
        for build in self.store.all('build'):
            if build['state'] in ('building','queued'):
                build.update(state='interrupted',error='backend_restarted');self.store.put('build',build['id'],build)
        known={b['id'] for b in self.store.all('build')}
        for path in self.artifacts.iterdir():
            if path.name not in known:
                if path.is_dir():shutil.rmtree(path)
                else:path.unlink()

    async def initialize(self):
        self.store.lock()
        await self.runner.check()
        if self.runner.available:
            try:
                for build in self.store.all('build'):
                    unit=build.get('runner_unit','')
                    if re.fullmatch(r'deadlock-sentinel-[0-9a-f]{32}',unit):
                        if not await self.runner.cleanup(unit):raise RuntimeError('build scope cleanup unverified')
                for s in self.manager.sessions.values():
                    unit=(s.metadata or {}).get('runner_unit','')
                    if s.error=='backend_restarted' and re.fullmatch(r'deadlock-sentinel-[0-9a-f]{32}',unit):
                        s.cleanup_verified=await self.runner.cleanup(unit);self.manager.save(s)
                        if not s.cleanup_verified:raise RuntimeError('run scope cleanup unverified')
            except Exception as error:
                self.runner.available=False;self.runner.reason='Restart cleanup failed: '+str(error)

    def require_isolation(self):
        if self.closing:raise HTTPException(503,'backend_shutting_down')
        if not self.runner.available:raise HTTPException(503,self.runner.reason)

    def projects(self):return self.store.all('project')

    def save(self,request,identifier=None):
        projects=self.projects()
        if identifier is None:
            if len(projects)>=10:raise HTTPException(429,'project_limit_delete_a_project')
            identifier=uuid.uuid4().hex
        else:self.store.get('project',identifier)
        files=sorted([f.model_dump() for f in request.files],key=lambda f:f['path'])
        revision=hashlib.sha256(json.dumps(files,sort_keys=True,separators=(',',':')).encode()).hexdigest()
        key=identifier+':'+revision
        if len(self.store.all('revision'))>=50:
            try:self.store.get('revision',key)
            except HTTPException:raise HTTPException(429,'revision_limit_delete_a_project')
        self.store.put('revision',key,dict(id=revision,project_id=identifier,files=files))
        project=dict(id=identifier,name=request.name,revision_id=revision,updated_at=time.time())
        self.store.put('project',identifier,project)
        return {**project,'files':files}

    def project(self,identifier):
        p=self.store.get('project',identifier)
        return {**p,'files':self.store.get('revision',identifier+':'+p['revision_id'])['files']}

    def build(self,identifier):
        self.require_isolation()
        if len(self.jobs)>=2:raise HTTPException(429,'concurrent_build_limit')
        if len(self.store.all('build'))>=20:raise HTTPException(429,'build_limit_delete_a_project')
        used=sum(p.stat().st_size for p in self.artifacts.rglob('*') if p.is_file())
        if used+len(self.jobs)*12582912+12582912>67108864:raise HTTPException(429,'artifact_storage_limit')
        p=self.project(identifier);bid=uuid.uuid4().hex
        build=dict(id=bid,project_id=identifier,revision_id=p['revision_id'],state='building',
            compiler=COMPILER,configuration=CONFIG,stdout='',stderr='',exit_code=None,
            duration=None,error=None,created_at=time.time(),diagnostics=[],output_truncated=False,
            source_location='source location unavailable')
        self.store.put('build',bid,build)
        stop=asyncio.Event();self.stops[bid]=stop
        self.jobs[bid]=asyncio.create_task(self.compile(build,p['files'],stop))
        return build

    async def compile(self,build,files,stop):
        bid=build['id'];directory=self.scratch/bid
        destination=self.artifacts/bid
        payload=bytearray();outputs={'stdout':bytearray(),'stderr':bytearray()}
        def output(name,data):
            left=65536-len(outputs[name]);outputs[name].extend(data[:left])
            build['output_truncated'] |= len(data)>left
        try:
            directory.mkdir()
            for f in files:
                path=directory/f['path'];path.parent.mkdir(parents=True,exist_ok=True)
                path.write_text(f['content'],encoding='utf-8')
            # The shell script is fixed; source names are separate positional
            # arguments, never interpolated. Binary bytes use a dedicated FD.
            command=['/usr/bin/bash','-c',
                'fd="$1"; shift; "$@" -o /out/program || exit $?; cat /out/program >&"$fd"',
                'compile','{channel}',*COMPILER,*['/src/'+f['path'] for f in files if f['path'].endswith('.c')]]
            def unit_started(unit):
                build['runner_unit']=unit;self.store.put('build',bid,build)
            result=await self.runner.execute(['--ro-bind',str(directory),'/src'],command,20,stop,
                lambda pid:None,lambda data:payload.extend(data),output,channel_limit=8388608,unit_started=unit_started)
            build.update(exit_code=result.exit_code,duration=result.duration,cleanup_verified=result.cleanup_verified)
            build['state']='stopped' if result.stopped else 'timed_out' if result.timed_out else 'failed'
            build['error']=result.limit_hit
            if result.exit_code==0 and result.cleanup_verified and not result.limit_hit and not stop.is_set() and payload[:4]==b'\x7fELF':
                library=self.settings.root/'build/libdeadlock_monitor.so'
                if not library.is_file() or library.stat().st_size>4194304:raise RuntimeError('build monitor first: make monitor-build')
                destination.mkdir()
                (destination/'program').write_bytes(payload);os.chmod(destination/'program',0o500)
                shutil.copyfile(library,destination/'monitor.so');os.chmod(destination/'monitor.so',0o400)
                build.update(state='succeeded',binary_sha256=hashlib.sha256(payload).hexdigest(),
                    compiler_sha256=hashlib.sha256(Path('/usr/bin/gcc').read_bytes()).hexdigest(),
                    monitor_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
                    libc_sha256=self.libc_hash())
        except asyncio.CancelledError:
            build.update(state='interrupted',error='build_cancelled')
            raise
        except Exception as error:build.update(state='failed',error=str(error)[:500])
        finally:
            for name,value in outputs.items():build[name]=value.decode(errors='replace')
            build['diagnostics']=[dict(file=m[0],line=int(m[1]),column=int(m[2]),message=m[3])
                for m in re.findall(r'/src/([^:\n]+):(\d+):(\d+): ([^\n]+)',build['stderr'])][:100]
            try:
                self.store.put('build',bid,build)
            finally:
                shutil.rmtree(directory,ignore_errors=True)
                try:
                    if build['state']!='succeeded' and destination.exists():shutil.rmtree(destination)
                finally:
                    self.jobs.pop(bid,None);self.stops.pop(bid,None)

    @staticmethod
    def libc_hash():
        return hashlib.sha256(Path('/usr/lib/x86_64-linux-gnu/libc.so.6').read_bytes()).hexdigest()

    async def stop_build(self,bid):
        self.store.get('build',bid)
        if bid in self.jobs:
            self.stops[bid].set();await asyncio.shield(self.jobs[bid])
        return self.store.get('build',bid)

    def run(self,bid,request):
        self.require_isolation()
        b=self.store.get('build',bid);project=self.project(b['project_id'])
        if b['state']!='succeeded':raise HTTPException(409,'build_not_successful')
        if request.revision_id!=b['revision_id'] or project['revision_id']!=b['revision_id']:
            raise HTTPException(409,'stale_build_save_and_rebuild')
        directory=self.artifacts/bid
        for name,field in [('program','binary_sha256'),('monitor.so','monitor_sha256')]:
            if not (directory/name).is_file() or hashlib.sha256((directory/name).read_bytes()).hexdigest()!=b[field]:
                raise HTTPException(409,'artifact_hash_mismatch_rebuild')
        if self.libc_hash()!=b['libc_sha256']:raise HTTPException(409,'runtime_changed_rebuild')
        revision=self.store.get('revision',b['project_id']+':'+b['revision_id'])
        metadata=dict(project_id=b['project_id'],project_name=project['name'],build_id=bid,
            revision_id=b['revision_id'],files=revision['files'],compiler=b['compiler'],configuration=b['configuration'],
            binary_sha256=b['binary_sha256'],monitor_sha256=b['monitor_sha256'],libc_sha256=b['libc_sha256'],
            compiler_sha256=b.get('compiler_sha256'),
            args=request.args,stdin=request.stdin,seconds=request.seconds,isolation=self.runner.status(),
            source_location='source location unavailable')
        target=UserTarget('user_build',metadata)
        async def runner(settings,application,stop,started,events,output):
            def unit_started(unit):
                metadata['runner_unit']=unit
                # Persist before spawning so restart can terminate the exact owned scope.
                for s in self.manager.sessions.values():
                    if s.application is application:self.manager.save(s)
            return await self.runner.execute(['--ro-bind',str(directory/'program'),'/program',
                '--ro-bind',str(directory/'monitor.so'),'/monitor.so',
                '--ro-bind',str(Path(__file__).with_name('sandbox_target.py')),'/runner.py'],
                ['/usr/bin/python3','-I','-B','/runner.py','{channel}','{status}',*request.args],
                request.seconds,stop,started,events,output,stdin=request.stdin.encode(),unit_started=unit_started)
        return self.manager.create_user(target,runner)

    async def delete_project(self,identifier):
        self.store.get('project',identifier)
        builds=[b for b in self.store.all('build') if b['project_id']==identifier]
        if any(b['id'] in self.jobs for b in builds):raise HTTPException(409,'stop_build_before_deletion')
        runs=[s for s in self.manager.sessions.values() if s.metadata and s.metadata.get('project_id')==identifier]
        if any(s.state not in {'completed','failed','timed_out','stopped'} for s in runs):
            raise HTTPException(409,'stop_run_before_deletion')
        for s in runs:self.manager.delete(s.session_id)
        for b in builds:
            path=self.artifacts/b['id']
            if path.exists():shutil.rmtree(path)
            self.store.delete('build',b['id'])
        for r in self.store.all('revision'):
            if r['project_id']==identifier:self.store.delete('revision',identifier+':'+r['id'])
        self.store.delete('project',identifier)

    async def close(self):
        self.closing=True
        for stop in self.stops.values():stop.set()
        await asyncio.gather(*list(self.jobs.values()),return_exceptions=True)
