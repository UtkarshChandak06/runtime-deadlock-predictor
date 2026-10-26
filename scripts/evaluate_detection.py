"""Expected-versus-observed runtime matrix; reuses the bounded Phase 4 harness."""
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
import test_runtime as runtime

def main():
    output=ROOT/'evidence/detection'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    output.mkdir(parents=True)
    runtime.ARTIFACTS=output
    cases=[
        ('consistent_order','banking_safe',(),0,False,0),
        ('sequential_inversion','banking_unsafe',(),1,False,3),
        ('three_lock_cycle','test_runtime_mutex',('cycle3',),1,False,4),
        ('eight_lock_cycle','test_runtime_mutex',('cycle8',),1,False,9),
        ('controlled_deadlock','banking_deadlock',('--controlled-fixture',),1,False,3),
        ('recursive_valid','test_runtime_mutex',('recursive',),0,False,0),
        ('unknown_static_normal','test_runtime_mutex',('static',),0,False,0),
        ('unknown_static_recursive','test_runtime_mutex',('static-recursive',),1,True,2),
        ('duplicate_closing_edge','test_runtime_mutex',('duplicate',),1,False,3),
        ('failed_errorcheck_operations','test_runtime_mutex',('errorcheck',),1,False,2),
        ('graph_capacity','test_runtime_mutex',('nodes',),0,True,0),
        ('identity_capacity','test_runtime_mutex',('registry',),0,True,0),
        ('unsupported_trylock','test_runtime_mutex',('trylock',),0,True,0),
        ('unsupported_robust','test_runtime_mutex',('robust',),0,True,0),
    ]
    rows=[]
    for name,binary,args,expected_warning,incomplete,path_length in cases:
        stdout,events=runtime.run(binary,args,deadlock=name=='controlled_deadlock',artifact=name)
        warnings=runtime.select(events,'potential_lock_order_cycle')
        observed_incomplete=any(not event['monitoring_complete'] for event in events)
        assert len(warnings)==expected_warning,(name,warnings)
        assert observed_incomplete==incomplete,(name,observed_incomplete)
        if path_length: assert len(warnings[0]['cycle'])==path_length
        if binary.startswith('banking_') and name!='controlled_deadlock': assert 'VALIDATION PASS' in stdout
        rows.append(dict(scenario=name,expected_warnings=expected_warning,observed_warnings=len(warnings),
            expected_c_incomplete=incomplete,observed_c_incomplete=observed_incomplete,
            observed_events=len(events),cycle_length=len(warnings[0]['cycle']) if warnings else 0,
            outcome='bounded_actual_deadlock_verified' if name=='controlled_deadlock' else 'completed',
            cleanup_verified=True,result='PASS'))
    with (output/'matrix.csv').open('w',newline='') as file:
        writer=csv.DictWriter(file,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    (output/'evaluation.json').write_text(json.dumps({'cases':rows,'all_passed':True,
        'helgrind_available':bool(shutil.which('valgrind')),'helgrind_comparison':'not run; valgrind not installed' if not shutil.which('valgrind') else 'available; separate comparison required',
        'classification':'warnings are historical potential risks; controlled deadlock adds stdout/futex proof; no prediction accuracy metric'},indent=2))
    print(f'PASS detection matrix: {len(rows)} real runtime cases; {output.relative_to(ROOT)}')

if __name__=='__main__': main()
