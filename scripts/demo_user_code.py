"""Submit two editable C programs through the real local workspace HTTP API."""
import json
from pathlib import Path
import time
from urllib.request import Request, urlopen

BASE='http://127.0.0.1:8000/api/v1'
ROOT=Path(__file__).resolve().parents[1]

def api(path,body=None):
    req=Request(BASE+path,data=None if body is None else json.dumps(body).encode(),
                headers={'Content-Type':'application/json'})
    with urlopen(req,timeout=10) as response:return json.load(response)

def wait(path,active):
    end=time.monotonic()+40
    while time.monotonic()<end:
        result=api(path)
        if result['state'] not in active:return result
        time.sleep(.2)
    raise RuntimeError('Job did not reach a terminal state')

caps=api('/capabilities')
if not caps['isolation']['available']:raise SystemExit(caps['isolation']['reason'])
records=[]
for name,expected in [('consistent',0),('conflicting',1)]:
    source=(ROOT/'examples/user_sources'/f'{name}.c').read_text()
    project=api('/projects',{'name':'Submitted '+name,'files':[{'path':'main.c','content':source}]})
    build=api('/projects/'+project['id']+'/builds',{})
    build=wait('/builds/'+build['id'],{'building','queued'})
    if build['state']!='succeeded':raise RuntimeError(build)
    run=api('/builds/'+build['id']+'/runs',{'revision_id':project['revision_id'],'seconds':5})
    run=wait('/sessions/'+run['session_id'],{'created','starting','running'})
    # Check actual evidence, never substitute expected counts into the response.
    if run['state']!='completed' or run['warning_count']!=expected or not run['monitoring_complete']:
        raise RuntimeError(run)
    record={'project':project,'build':build,'session':run,
            'events':api('/sessions/'+run['session_id']+'/events?limit=1000'),
            'graph':api('/sessions/'+run['session_id']+'/graph')}
    records.append(record)
    print(name,run['state'],'warnings=',run['warning_count'],'events=',run['event_count'])
    print('http://127.0.0.1:5173/#view=graph&session='+run['session_id'])
output=ROOT/'build/user-code-demo.json';output.parent.mkdir(exist_ok=True)
output.write_text(json.dumps(records,indent=2),encoding='utf-8')
print('Actual execution evidence:',output)
