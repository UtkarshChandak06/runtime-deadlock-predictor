"""Publish a saved, complete measurement set as static dashboard evidence."""
import argparse
import csv
import json
from pathlib import Path
import shutil
from run_benchmarks import ROOT, summarize

def main():
    parser=argparse.ArgumentParser();parser.add_argument('result_directory');args=parser.parse_args()
    source=(ROOT/args.result_directory).resolve()
    if not source.is_relative_to((ROOT/'benchmarks/results').resolve()): parser.error('select a directory within benchmarks/results')
    environment=json.loads((source/'environment.json').read_text())
    if environment['status']!='complete': raise SystemExit('Cannot publish an incomplete benchmark')
    with (source/'raw.csv').open() as file:
        rows=list(csv.DictReader(file))
    for row in rows:
        for key in ('threads','iterations','elapsed_ns','repeat'):row[key]=int(row[key])
        row['warmup']=row['warmup']=='True'
    if len(rows)!=environment['raw_rows']:raise SystemExit('raw row count mismatch')
    summary=summarize(rows)
    with (source/'summary.csv').open() as file: recorded=list(csv.DictReader(file))
    if len(recorded)!=len(summary):raise SystemExit('summary row count mismatch')
    for computed, stored in zip(summary,recorded):
        for key,value in computed.items():
            if str(value)!=stored[key]:raise SystemExit(f'summary does not match raw measurements: {key}')
    public=ROOT/'frontend/public';public.mkdir(exist_ok=True)
    evidence={'kind':'saved-measurements','source':str(source.relative_to(ROOT)),
        'environment':{key:environment[key] for key in ('date_utc','platform','cpu','logical_cpus','workload_flags','monitor_flags','configuration','raw_rows','limitations')},
        'summary':summary,'raw_csv':'benchmark-raw.csv','summary_csv':'benchmark-summary.csv',
        'method':f"Median of {environment['configuration']['repetitions']} measured repetitions per configuration; one warm-up round excluded. Same C binary, CLOCK_MONOTONIC. Both monitored modes retain analysis and JSON formatting."}
    (public/'benchmark-results.json').write_text(json.dumps(evidence,indent=2))
    shutil.copyfile(source/'raw.csv',public/'benchmark-raw.csv');shutil.copyfile(source/'summary.csv',public/'benchmark-summary.csv')
    print(f'Published verified saved results from {source.relative_to(ROOT)}')

if __name__=='__main__':main()
