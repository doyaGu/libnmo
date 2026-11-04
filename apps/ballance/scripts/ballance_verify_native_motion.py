#!/usr/bin/env python3
"""Probe seven native mechanisms in disposable copies of original levels."""
import argparse
import sys
import subprocess
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from ballance.core.nmo import Nmo, add_nmo_argument
from ballance.core.project import write_json
from ballance.verify.native_motion import PROBES, probe


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--resources',nargs='+',choices=PROBES,default=list(PROBES))
    parser.add_argument('--frames',type=int,default=1600)
    add_nmo_argument(parser)
    args=parser.parse_args();reports=[]
    if args.frames<1:parser.error('--frames must be positive')
    args.out=args.out.resolve();args.game=args.game.resolve()
    args.out.mkdir(parents=True,exist_ok=True)
    for name in args.resources:
        try:
            report=probe(name,args.game,args.out/name,Nmo(args.nmo),args.frames)
        except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as error:
            report={'resource':name,'status':'failed','error':str(error)}
            (args.out/name).mkdir(parents=True,exist_ok=True)
            write_json(args.out/name/'validation.json',report)
        reports.append(report)
        print(name,report['status'],report.get('displaced_bodies',report.get('error')),flush=True)
        write_json(args.out/'validation.json',{'status':'passed' if all(r['status']=='passed' for r in reports) else 'failed',
                    'complete':len(reports)==len(args.resources),'probes':reports})
    return int(any(r['status']!='passed' for r in reports))

if __name__=='__main__':sys.exit(main())
