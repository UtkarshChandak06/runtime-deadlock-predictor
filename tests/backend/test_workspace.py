"""Real user-source compilation and Linux isolation integration, never mocked.

Run explicitly with DS_TEST_ISOLATION=1; ordinary backend runs still exercise
validation. Explicit isolation verification fails if the provider is missing.
"""
import asyncio
import os
import sys
from pathlib import Path
import tempfile
import unittest
from fastapi import HTTPException
from pydantic import ValidationError
from app.config import Settings
from app.session_manager import SessionManager
from app.workspace import SourceInput,RunInput

SAFE='''#include <pthread.h>
#include <stdio.h>
pthread_mutex_t a,b;
void *worker(void *unused) {
 (void)unused;
 pthread_mutex_lock(&a); pthread_mutex_lock(&b);
 puts("transfer observed");
 pthread_mutex_unlock(&b); pthread_mutex_unlock(&a); return 0;
}
int main(void) {
 pthread_t t[2]; pthread_mutex_init(&a,0); pthread_mutex_init(&b,0);
 for(int i=0;i<2;i++) pthread_create(&t[i],0,worker,0);
 for(int i=0;i<2;i++) pthread_join(t[i],0);
 pthread_mutex_destroy(&a); pthread_mutex_destroy(&b); return 0;
}
'''
REVERSE='''#include <pthread.h>
int main(void) {
 pthread_mutex_t a,b; pthread_mutex_init(&a,0); pthread_mutex_init(&b,0);
 pthread_mutex_lock(&a); pthread_mutex_lock(&b);
 pthread_mutex_unlock(&b); pthread_mutex_unlock(&a);
 pthread_mutex_lock(&b); pthread_mutex_lock(&a);
 pthread_mutex_unlock(&a); pthread_mutex_unlock(&b);
 pthread_mutex_destroy(&a); pthread_mutex_destroy(&b);
}
'''


class SourceValidation(unittest.TestCase):
    def test_paths_duplicates_sizes(self):
        for path in ['/tmp/main.c','../main.c','dir/../main.c','a//main.c','C:/main.c','-x.c','file.cpp','a\\main.c']:
            with self.subTest(path=path), self.assertRaises(ValidationError):
                SourceInput(files=[dict(path=path,content='int main(){}')])
        for files in [[dict(path='a.c',content=''),dict(path='A.c',content='')],
                      [dict(path='a.c',content='x'*131073)],
                      [dict(path=f'a{i}.c',content='') for i in range(9)]]:
            with self.assertRaises(ValidationError):SourceInput(files=files)


@unittest.skipUnless(os.getenv('DS_TEST_ISOLATION')=='1','set DS_TEST_ISOLATION=1 for real sandbox tests')
class UserCodeIntegration(unittest.IsolatedAsyncioTestCase):
    async def test_abrupt_backend_restart_cleans_live_scope(self):
        with tempfile.TemporaryDirectory(prefix='ds-restart-') as folder:
            ready=Path(folder)/'ready'
            script='''
import asyncio,sys
from pathlib import Path
from app.config import Settings
from app.session_manager import SessionManager
from app.workspace import SourceInput,RunInput
async def main():
 m=SessionManager(Settings(workspace_path=Path(sys.argv[1])))
 await m.workspace.initialize()
 p=m.workspace.save(SourceInput(files=[dict(path='main.c',content='int main(){for(;;){}}')]))
 b=m.workspace.build(p['id']);await m.workspace.jobs[b['id']]
 assert m.workspace.store.get('build',b['id'])['state']=='succeeded'
 s=m.workspace.run(b['id'],RunInput(revision_id=p['revision_id'],seconds=30))
 while not s.metadata.get('runner_unit') or s.state!='running':await asyncio.sleep(.05)
 await asyncio.sleep(.5)
 Path(sys.argv[2]).write_text(s.session_id)
 await asyncio.sleep(60)
asyncio.run(main())
'''
            child=await asyncio.create_subprocess_exec(sys.executable,'-c',script,folder,str(ready))
            try:
                for _ in range(400):
                    if ready.exists():break
                    if child.returncode is not None:self.fail('Restart worker exited before running')
                    await asyncio.sleep(.05)
                self.assertTrue(ready.exists(),'Restart worker never started')
            finally:
                if child.returncode is None:child.kill()
                await child.wait()
            restored=SessionManager(Settings(workspace_path=Path(folder)))
            try:
                await restored.workspace.initialize()
                self.assertTrue(restored.workspace.runner.available,restored.workspace.runner.reason)
                session=restored.get(ready.read_text())
                self.assertEqual(session.state,'stopped')
                self.assertEqual(session.error,'backend_restarted')
                self.assertFalse(session.monitoring_complete)
                self.assertTrue(session.cleanup_verified)
            finally:await restored.close()

    async def asyncSetUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='ds-workspace-')
        self.manager=SessionManager(Settings(workspace_path=Path(self.tmp.name)))
        self.w=self.manager.workspace
        await self.w.initialize()
        if not self.w.runner.available:
            reason=self.w.runner.reason
            await self.manager.close();self.tmp.cleanup()
            self.fail('Explicit isolated verification requires a working provider: '+reason)

    async def asyncTearDown(self):
        await self.manager.close()
        self.assertFalse(self.w.runner.units)
        self.w.store.db.close();self.tmp.cleanup()

    async def build(self,code=None,files=None):
        p=self.w.save(SourceInput(files=files or [dict(path='main.c',content=code)]))
        b=self.w.build(p['id']);await self.w.jobs[b['id']]
        b=self.w.store.get('build',b['id'])
        return p,b

    async def run_build(self,p,b,**kwargs):
        self.assertEqual(b['state'],'succeeded',b)
        s=self.w.run(b['id'],RunInput(revision_id=p['revision_id'],**kwargs))
        await asyncio.wait_for(s.task,40)
        self.assertTrue(s.cleanup_verified,s.view())
        return s

    async def test_safe_reverse_and_persistence(self):
        for source,count in [(SAFE,0),(REVERSE,1)]:
            p,b=await self.build(source);s=await self.run_build(p,b)
            self.assertEqual(s.state,'completed',s.view())
            self.assertEqual(s.projection.warning_count,count)
            self.assertEqual(len(s.projection.nodes),2)
            self.assertTrue(s.monitoring_complete,s.view())
            self.assertEqual(s.metadata['revision_id'],p['revision_id'])
        await self.manager.close()
        restored=SessionManager(self.manager.settings)
        self.assertEqual(restored.get(s.session_id).snapshot()['data']['events'],s.snapshot()['data']['events'])
        self.assertEqual(restored.get(s.session_id).metadata,s.metadata)
        await restored.close();restored.workspace.store.db.close()

    async def test_multiple_files_and_stale_build(self):
        p,b=await self.build(files=[dict(path='main.c',content='#include "helper.h"\nint main(){return answer()!=42;}'),
            dict(path='helper.h',content='int answer(void);'),dict(path='helper.c',content='int answer(void){return 42;}')])
        s=await self.run_build(p,b)
        self.assertEqual(s.state,'completed')
        self.assertEqual(s.view()['observation'],'no_relevant_mutex_activity')
        self.w.save(SourceInput(files=[dict(path='main.c',content='int main(){return 1;}')]),p['id'])
        with self.assertRaises(HTTPException) as ctx:self.w.run(b['id'],RunInput(revision_id=p['revision_id']))
        self.assertEqual(ctx.exception.status_code,409)

    async def test_compiler_failure(self):
        p,b=await self.build('int main(void) { broken syntax }')
        self.assertEqual(b['state'],'failed');self.assertTrue(b['diagnostics'])
        with self.assertRaises(HTTPException):self.w.run(b['id'],RunInput(revision_id=p['revision_id']))

    async def test_timeout_crash_output_and_stop(self):
        for code,state in [('int main(){for(;;){}}','timed_out'),
                           ('#include <signal.h>\nint main(){raise(SIGSEGV);}','failed'),
                           ('#include <stdio.h>\nint main(){for(;;)puts("lots of output");}','failed')]:
            p,b=await self.build(code);s=await self.run_build(p,b,seconds=1)
            self.assertEqual(s.state,state,s.view())
            self.assertFalse(s.controlled_deadlock_evidence)
            if 'SIGSEGV' in code:
                self.assertEqual(s.exit_code,-11)
                self.assertEqual(s.error,'crash_signal_11')
        p,b=await self.build('int main(){for(;;){}}')
        s=self.w.run(b['id'],RunInput(revision_id=p['revision_id'],seconds=10))
        await asyncio.sleep(.5);await self.manager.stop(s)
        self.assertEqual(s.state,'stopped');self.assertTrue(s.cleanup_verified)
        p,b=await self.build('int main(){return 139;}');s=await self.run_build(p,b)
        self.assertEqual(s.exit_code,139);self.assertEqual(s.error,'nonzero_exit')

    async def test_boundary(self):
        code='''#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
int main(void){
 if(getuid()==0)return 1;
 if(open("/etc/passwd",O_RDONLY)>=0)return 2;
 if(open("/usr/evil",O_CREAT|O_WRONLY,0600)>=0)return 3;
 if(open("/home/utkarsh/.ssh/id_rsa",O_RDONLY)>=0)return 4;
 int s=socket(AF_INET,SOCK_STREAM,0);
 struct sockaddr_in a={.sin_family=AF_INET,.sin_port=htons(80)};
 inet_pton(AF_INET,"1.1.1.1",&a.sin_addr);
 if(connect(s,(void*)&a,sizeof a)==0)return 5;
 puts("boundary checks passed");return 0;
}'''
        p,b=await self.build(code);s=await self.run_build(p,b)
        self.assertEqual(s.state,'completed',s.view())
        self.assertIn('boundary checks passed',s.view()['stdout'])

    async def test_deadlock_and_capacity_are_not_universal_verdicts(self):
        code='''#define _XOPEN_SOURCE 700
#include <pthread.h>
pthread_mutex_t a,b; pthread_barrier_t barrier;
void *worker(void *arg){pthread_mutex_t *first=arg?&a:&b,*second=arg?&b:&a;
pthread_mutex_lock(first);pthread_barrier_wait(&barrier);pthread_mutex_lock(second);return 0;}
int main(){pthread_t x,y;pthread_mutex_init(&a,0);pthread_mutex_init(&b,0);
pthread_barrier_init(&barrier,0,2);pthread_create(&x,0,worker,(void*)1);
pthread_create(&y,0,worker,0);pthread_join(x,0);pthread_join(y,0);}'''
        p,b=await self.build(code);s=await self.run_build(p,b,seconds=1)
        self.assertEqual(s.state,'timed_out');self.assertEqual(s.projection.warning_count,1)
        self.assertIsNone(s.controlled_deadlock_evidence)
        self.assertFalse(s.monitoring_complete)
        p,b=await self.build('''#include <pthread.h>
int main(){pthread_mutex_t locks[34];for(int i=0;i<34;i++)pthread_mutex_init(&locks[i],0);
pthread_mutex_lock(&locks[0]);for(int i=1;i<34;i++){pthread_mutex_lock(&locks[i]);pthread_mutex_unlock(&locks[i]);}
pthread_mutex_unlock(&locks[0]);for(int i=0;i<34;i++)pthread_mutex_destroy(&locks[i]);}''')
        s=await self.run_build(p,b);self.assertEqual(s.state,'completed');self.assertFalse(s.monitoring_complete)
        self.assertTrue(any(e['event'].get('reason')=='graph_node_capacity' for e in s.events))

    async def test_memory_process_scratch_and_file_limits(self):
        sources=[
            # Touch pages until the cgroup memory ceiling terminates the group.
            '#include <stdlib.h>\n#include <string.h>\nint main(){for(;;){char *p=malloc(8*1024*1024);if(!p)return 9;memset(p,1,8*1024*1024);}}',
            # The process ceiling must reject a fork well before 100 children.
            '''#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
int main(){int p[100],n=0;for(;n<100;n++){p[n]=fork();if(p[n]<0)break;if(!p[n]){sleep(15);_exit(0);}}
for(int i=0;i<n;i++){kill(p[i],SIGKILL);waitpid(p[i],0,0);}printf("children=%d\\n",n);return n<32?0:3;}''',
            # Exceed per-file size, with SIGXFSZ explicitly ignored to inspect errno.
            '''#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
int main(){signal(SIGXFSZ,SIG_IGN);int f=open("/tmp/big",O_CREAT|O_WRONLY,0600);char b[4096]={0};
long n=0;while(write(f,b,sizeof b)>0)n+=sizeof b;printf("file_bytes=%ld\\n",n);return n==8388608&&errno==EFBIG?0:1;}''',
            '''#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
int main(){char path[40],b[4096]={0};for(int i=0;i<40;i++){snprintf(path,sizeof path,"/tmp/f%d",i);
int f=open(path,O_CREAT|O_WRONLY,0600);for(int j=0;j<256;j++)if(write(f,b,4096)<0)return errno==ENOSPC?0:2;close(f);}return 3;}'''
        ]
        for i,code in enumerate(sources):
            p,b=await self.build(code);s=await self.run_build(p,b,seconds=5)
            self.assertEqual(s.state,'failed' if i==0 else 'completed',s.view())

    async def test_stop_build_concurrency_and_deletion(self):
        p=self.w.save(SourceInput(files=[dict(path='main.c',content=SAFE)]))
        first=self.w.build(p['id']);second=self.w.build(p['id'])
        with self.assertRaises(HTTPException) as ctx:self.w.build(p['id'])
        self.assertEqual(ctx.exception.status_code,429)
        await self.w.stop_build(first['id']);await self.w.stop_build(second['id'])
        self.assertEqual(self.w.store.get('build',first['id'])['state'],'stopped')
        await self.w.delete_project(p['id'])
        self.assertFalse(self.w.projects());self.assertFalse(self.w.store.all('build'))

    async def test_stdin_args_malformed_events_and_artifact_identity(self):
        code='''#include <stdio.h>
int main(int argc,char **argv){char b[40];if(!fgets(b,sizeof b,stdin))return 2;
printf("argc=%d arg=%s input=%s",argc,argv[1],b);return 0;}'''
        p,b=await self.build(code);s=await self.run_build(p,b,args=['literal; $(not a shell)'],stdin='hello\n')
        self.assertIn('literal; $(not a shell)',s.view()['stdout'])
        self.assertIn('input=hello',s.view()['stdout'])
        p,b=await self.build('''#include <stdlib.h>
#include <unistd.h>
int main(){int fd=atoi(getenv("RDP_EVENT_FD"));write(fd,"not json\\n",9);return 0;}''')
        s=await self.run_build(p,b);self.assertEqual(s.state,'completed')
        self.assertGreater(s.invalid_records,0);self.assertFalse(s.monitoring_complete)
        binary=self.w.artifacts/b['id']/'program';os.chmod(binary,0o600);binary.write_bytes(b'tampered')
        with self.assertRaises(HTTPException) as ctx:self.w.run(b['id'],RunInput(revision_id=p['revision_id']))
        self.assertEqual(ctx.exception.detail,'artifact_hash_mismatch_rebuild')
