"""Paired, bounded real workloads. Keeps raw measurements; never drops slow rows."""
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import signal
import statistics
import subprocess
import tempfile
import time

ROOT=Path(__file__).resolve().parents[1]
CASES=[('independent',n) for n in (1,2,4,8)]+[('shared',n) for n in (2,4,8)]+[('nested',2)]
MODES=('baseline','monitored_file','monitored_null')

def command(args):
    return subprocess.check_output(args,cwd=ROOT,text=True,timeout=15).strip()

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def summarize(rows):
    summary=[]
    for workload,threads in CASES:
        data=[r for r in rows if r['workload']==workload and r['threads']==threads and not r['warmup']]
        baseline=statistics.median(r['elapsed_ns'] for r in data if r['mode']=='baseline')
        for mode in MODES[1:]:
            values=[r['elapsed_ns'] for r in data if r['mode']==mode]
            measured=statistics.median(values)
            summary.append(dict(workload=workload,threads=threads,iterations_per_thread=data[0]['iterations'],repetitions=len(values),
                baseline_median_ns=baseline,monitored_median_ns=measured,absolute_overhead_ns=measured-baseline,
                overhead_percent=(measured-baseline)/baseline*100,mode=mode,
                monitored_min_ns=min(values),monitored_max_ns=max(values),
                baseline_min_ns=min(r['elapsed_ns'] for r in data if r['mode']=='baseline'),
                baseline_max_ns=max(r['elapsed_ns'] for r in data if r['mode']=='baseline')))
    return summary

def validate_stream(stream, expected, locks, threads):
    stream.seek(0); sequence=0; operations={'lock_request':0,'lock_acquired':0,'lock_released':0}; first=last=None
    for line in stream:
        event=json.loads(line)
        if event['sequence']!=sequence+1 or not event['monitoring_complete']:
            raise AssertionError('incomplete benchmark monitoring')
        sequence=event['sequence']; kind=event['session_event']; first=first or kind; last=kind
        if kind=='potential_lock_order_cycle': raise AssertionError('safe benchmark warned')
        if kind in operations: operations[kind]+=1
    if first!='session_start' or last!='session_end' or any(value!=expected for value in operations.values()):
        raise AssertionError(f'event lifecycle/operation count mismatch: {operations}')
    if sequence!=3*expected+2*locks+2*threads+2: raise AssertionError('unexpected record count')
    return sequence

def run(workload,threads,iterations,mode,timeout):
    env={'PATH':'/usr/bin:/bin','LANG':'C.UTF-8'}
    # A relative preload is loader-safe in a workspace with spaces; cwd is fixed.
    if mode!='baseline': env['LD_PRELOAD']='./build/libdeadlock_monitor.so'; env['RDP_ENABLED']='1'
    with tempfile.TemporaryFile(dir='/tmp') as events, tempfile.TemporaryFile() as out, tempfile.TemporaryFile() as err:
        sink=events if mode!='monitored_null' else open('/dev/null','wb')
        try:
            env['RDP_EVENT_FD']=str(sink.fileno())
            before=time.monotonic_ns()
            child=subprocess.Popen(['./build/benchmark',workload,str(threads),str(iterations)],cwd=ROOT,
                env=env,pass_fds=(sink.fileno(),),stdout=out,stderr=err,start_new_session=True)
            try: child.wait(timeout=timeout)
            finally:
                try: os.killpg(child.pid,signal.SIGKILL)
                except ProcessLookupError: pass
                child.wait()
            wall=time.monotonic_ns()-before
            if child.returncode: raise RuntimeError(f'benchmark exit {child.returncode}')
            try: os.killpg(child.pid,0)
            except ProcessLookupError: pass
            else: raise AssertionError('benchmark process group remains')
            out.seek(0); result=json.load(out); err.seek(0)
            diagnostic=err.read(8193)
            if diagnostic: raise AssertionError(f'benchmark stderr: {diagnostic[:200]!r}')
            if result['operations']!=threads*iterations or not result['correct'] or result['elapsed_ns']<=0:
                raise AssertionError('workload result mismatch')
            size=events.seek(0,2)
            locks=2 if workload=='nested' else 1 if workload=='shared' else threads
            event_count=validate_stream(events,result['lock_acquisitions'],locks,threads) if mode=='monitored_file' else None
            return {**result,'mode':mode,'wall_ns':wall,'event_bytes':size if mode!='monitored_null' else None,
                'event_count':event_count,'event_validation':'validated_full_stream' if mode=='monitored_file' else 'discarded_sink_not_observed' if mode=='monitored_null' else 'no_preload',
                'cleanup_verified':True}
        finally:
            if sink is not events: sink.close()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--iterations',type=int,default=20000)
    parser.add_argument('--repetitions',type=int,default=5)
    parser.add_argument('--timeout',type=float,default=30)
    args=parser.parse_args()
    if not 1<=args.iterations<=1000000 or not 3<=args.repetitions<=30 or not 1<=args.timeout<=120:
        parser.error('iterations 1..1000000, repetitions 3..30, timeout 1..120 required')
    destination=ROOT/'benchmarks/results'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    destination.mkdir(parents=True)
    environment={'date_utc':datetime.now(timezone.utc).isoformat(),'platform':platform.platform(),'python':platform.python_version(),
        'cpu':next((line.split(':',1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name')),'unknown'),
        'logical_cpus':os.cpu_count(),'affinity_cpus':sorted(os.sched_getaffinity(0)), 'compiler':command(['gcc','--version']),
        'workload_flags':'-O2 -std=c11 -Wall -Wextra -Wpedantic -pthread',
        'monitor_flags':'Makefile default -std=c11 -Wall -Wextra -Wpedantic -g -fPIC -pthread; no optimization flag',
        'filesystem_note':'checkout under /mnt/c; temporary event sink on Linux /tmp; no fsync durability guarantee',
        'binary_sha256':digest(ROOT/'build/benchmark'),'monitor_sha256':digest(ROOT/'build/libdeadlock_monitor.so'),
        'source_sha256':digest(ROOT/'benchmarks/benchmark.c'),'configuration':vars(args),'warmup_rounds':1,
        'seed':7,'clock':'C CLOCK_MONOTONIC; barrier release through joins; initialization excluded',
        'wall_clock':'parent monotonic process start-to-reap, excluding post-run JSON validation',
        'ordering':'seeded shuffled cases per round; modes rotate by round and case index',
        'sink_policy':'full JSON formatting/analysis in both monitored modes; file validated after timing; /dev/null records unavailable',
        'limitations':['No CPU pinning or frequency control','WSL shared host; short baselines are sensitive to scheduling','No FastAPI/browser timing included','No reduced-logging or analysis-only mode'],
        'status':'running'}
    (destination/'environment.json').write_text(json.dumps(environment,indent=2))
    rows=[]; rng=random.Random(7)
    try:
        with (destination/'raw.csv').open('w',newline='') as output:
            writer=None
            for repeat in range(args.repetitions+1):
                cases=CASES.copy();rng.shuffle(cases)
                for index,(workload,threads) in enumerate(cases):
                    offset=(repeat+index)%len(MODES)
                    for mode in MODES[offset:]+MODES[:offset]:
                        row=run(workload,threads,args.iterations,mode,args.timeout)
                        row.update(repeat=repeat,warmup=repeat==0,order=len(rows)+1)
                        if writer is None: writer=csv.DictWriter(output,fieldnames=list(row));writer.writeheader()
                        writer.writerow(row);output.flush();rows.append(row)
                    print(f'round={repeat} {workload}/{threads}: validated all modes',flush=True)
        result=summarize(rows)
        with (destination/'summary.csv').open('w',newline='') as output:
            writer=csv.DictWriter(output,fieldnames=list(result[0]));writer.writeheader();writer.writerows(result)
        environment['status']='complete';environment['raw_rows']=len(rows);environment['summary_rows']=len(result)
    except BaseException as error:
        environment['status']='failed';environment['failure']=type(error).__name__+': '+str(error)
        raise
    finally:
        (destination/'environment.json').write_text(json.dumps(environment,indent=2))
        print('RESULT_DIRECTORY='+str(destination.relative_to(ROOT)),flush=True)

if __name__=='__main__': main()
