"""Check extracted/native sources before trusting resource-dependent validation."""

import json
from pathlib import Path

from ..game.resources import check_export, digest


def dependency_paths(folder, game):
    path=Path(folder)/'resources.json'
    if game is None or not path.is_file():return []
    record=json.loads(path.read_text(encoding='utf-8'))
    return [Path(game)/p for p in sorted({p for item in record['resources'].values()
             if item['native'] for p in item['native']['sources']})]


def check(folder, game=None):
    path=Path(folder)/'resources.json'
    if not path.is_file():
        return {'status':'unavailable','reason':'No unified resource record; rebuild to bind resource descriptions.'}
    record=json.loads(path.read_text(encoding='utf-8'))
    if record.get('schema_version')!=1:
        raise ValueError('unsupported unified resource record')
    findings=[]
    for name in record.get('unbound_native_objects', []):
        findings.append({'kind':'unbound native object','object':name})
    for name,item in record['resources'].items():
        if item['native'] is None:
            if record['native_assets']:
                findings.append({'resource':name,'kind':'missing native provenance'})
            continue
        try:
            check_export(folder,name,item['native'])
        except (OSError,ValueError) as error:
            findings.append({'resource':name,'kind':'extracted assets','reason':str(error)})
        if game is not None:
            for filename,sha in item['native']['sources'].items():
                source=Path(game)/filename
                if not source.is_file() or digest(source)!=sha:
                    findings.append({'resource':name,'kind':'native source changed or missing','file':filename})
    for instance in record['instances']:
        for component in instance['components']:
            if component['status']=='missing':
                findings.append({'resource':instance['resource'],'instance':instance['id'],
                                 'kind':'missing component','part':component['part']})
    return {'status':'failed' if findings else 'review' if not record['native_assets'] or game is None else 'passed',
            'count':len(record['resources']),'instances':len(record['instances']),'findings':findings,
            'native_sources_checked':game is not None,'scope':record['scope']}
