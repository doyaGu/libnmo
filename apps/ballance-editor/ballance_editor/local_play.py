"""A paused native engine owned by one editor draft and one local probe.

Stepping advances the same IVP world. Reset creates a fresh native world;
probe state never enters the authored route or full-level verification.
"""
import json
import math
import os
import re
from pathlib import Path
import select
import shutil
import subprocess
import time
import uuid

from ballance.core import project
from ballance.core.path import vector
from ballance.route.dynamics import copy_runtime
from ballance.verify import harness
from ballance.verify.resources import check as resource_check

FRAME_MS = 1000/60


class LocalPlay:
    def __init__(self, session):
        self.session = session
        self.process = None
        self.log = None
        self.buffer = b''
        self.token = None
        self.bindings = {}
        self.last = None
        self.bounds_revision = None

    def fall_limit(self):
        """Local recovery below all geometry, death zones and finite sweeps.

        This is a local-play boundary, not a replacement for native Ball Off
        events and not an authored death zone or gameplay-verification result.
        """
        if self.bounds_revision != self.session.revision():
            from ballance.view.scene import load_project
            folder=self.session.current
            levels=[p[1] for o in load_project(folder,hidden=True) if o.name!='SkyLayer'
                    for t in o.triangles for p in t.points]
            for name,key in (('death_zones.json','boxes'),('spatial_constraints.json','occupancy')):
                path=folder/name
                if path.exists():
                    levels.extend((entry['box'] if isinstance(entry,dict) else entry)[2]
                                  for entry in json.loads(path.read_text())[key])
            self.minimum_y=min(levels,default=0)-32
            self.bounds_revision=self.session.revision()
        return self.minimum_y

    def close(self):
        if self.process:
            if self.process.poll() is None:
                self.process.terminate()
                try:self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.kill();self.process.wait()
            self.process.stdin.close();self.process.stdout.close()
            self.process = None
        if self.log:self.log.close();self.log=None
        self.token = None
        self.last = None

    def _read(self):
        deadline=time.monotonic()+30
        while b'\n' not in self.buffer:
            remaining=deadline-time.monotonic()
            if remaining<=0 or not select.select([self.process.stdout],[],[],remaining)[0]:
                raise RuntimeError('Local engine timed out; reset the probe')
            chunk=os.read(self.process.stdout.fileno(),65536)
            if not chunk:raise RuntimeError('Local engine stopped; reset the probe and inspect local-play/runtime.log')
            self.buffer+=chunk
        line,self.buffer=self.buffer.split(b'\n',1)
        data=json.loads(line)
        if 'entities' not in data:raise RuntimeError('Local engine exited before completing the requested frames')
        for item in data['entities']:
            if len(item['matrix'])!=16 or not all(math.isfinite(v) for v in item['matrix']):
                raise RuntimeError('Native engine returned an invalid pose')
        return data

    def _step(self, frames, keys=0):
        self.process.stdin.write(f'step {frames} {keys}\n'.encode());self.process.stdin.flush()
        return self._read()

    def starts(self):
        """Use the compiled reset markers, including authored start transforms."""
        folder=self.session.current
        if not all((folder/name).exists() for name in ('manifest.json','route_sectors.json')):return []
        manifest=json.loads((folder/'manifest.json').read_text())
        markers={o['name']:o['transform']['position'] for scene in manifest['scenes']
                 for o in scene['objects'] if o['name'].startswith('PR_Resetpoint_')}
        sectors=json.loads((folder/'route_sectors.json').read_text())
        return [{'sector':i,'label':'地图起点' if i==1 else f'检查点 {i-1} · 第 {i} 小节',
                 'position':markers[f'PR_Resetpoint_{i:02d}'],'ball':row['ball']}
                for i,row in enumerate(sectors,1)]

    def start(self, identifier=None, position=None, ball=None, *, sector=None, checkpoint=False):
        s=self.session
        s._guard(s.revision())
        if not s.game:raise ValueError('Local play requires --game')
        if s.document_view()[1] or project.inspect(s.current)['status']!='passed':
            raise ValueError('Rebuild the draft before local play')
        if resource_check(s.current,s.game)['status']=='failed':raise ValueError('Native resources changed; rebuild first')
        if type(checkpoint) is not bool:raise ValueError('Invalid checkpoint mode')
        item=None
        if identifier:
            item=next((i for i in json.loads((s.current/'resources.json').read_text())['instances'] if i['id']==identifier),None)
            if not item or not item.get('mechanism'):raise ValueError('Unknown native mechanism')
        if sector is None:sector=item['sector'] if item else None
        starts=self.starts()
        if type(sector) is not int or not 1<=sector<=len(starts):raise ValueError('Choose a valid starting sector')
        if item and item['sector']!=sector:raise ValueError('Mechanism belongs to another starting sector')
        if checkpoint:
            position=starts[sector-1]['position'];ball=starts[sector-1]['ball']
        if ball not in ('Wood','Stone','Paper'):raise ValueError('Choose Wood, Stone or Paper')
        position=list(vector(position,'probe position'))
        if any(abs(v)>1e6 for v in position):raise ValueError('Probe position is outside the supported world')
        self.close()
        self.revision=s.revision();self.instance=identifier;self.position=position;self.ball=ball
        self.sector=sector;self.checkpoint=checkpoint
        self.fall_limit()
        folder=s.root/'local-play';folder.mkdir(exist_ok=True)
        # Replace only this probe's generated runtime overlay, leaving the editor build untouched.
        if (folder/'3D Entities').exists():shutil.rmtree(folder/'3D Entities')
        shutil.copyfile(project.pack(s.current,s.nmo),folder/'Level_01.NMO')
        copy_runtime(s.current,folder)
        table=s.nmo.data('data','dump','--name','Physicalize_GameBall',str(s.game/'3D Entities/Balls.nmo'))
        row=next(r for r in table['rows'] if r[0]==f'Ball_{ball}')
        physics={'Friction':row[1],'Elasticity':row[2],'Mass':row[3],'Collision Group':row[4],
                 'Linear Speed Dampening':row[5],'Rot Speed Dampening':row[6],
                 'Automatic Calculate Mass Center':False}
        base=folder/'probe.cmo'
        harness.write(base,s.nmo,sector,ball,probe_position=None if checkpoint else position,probe_physics=physics)
        objects=s.native_preview.scene(s.current)[0]
        names=sorted({o['body']+'*' for o in objects}|{'Ball_Wood','Ball_Stone','Ball_Paper'})
        self.log=(folder/'runtime.log').open('wb')
        command=[s.nmo.executable,'-f','json','behavior','run','--control','--physics','ivp',
                 '--delta',str(FRAME_MS),'--observe',','.join(names),'--write',str(folder),
                 '--path',str(s.game),str(base)]
        try:
            self.process=subprocess.Popen(command,cwd=s.game,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=self.log)
            self.buffer=b'';self._read()
            warmup=math.ceil((6000+harness.setup_ms(sector))/FRAME_MS)+2
            data=self._step(warmup)
            self.bindings={};used=set()
            # Bind the active sector, including CK's numbered copies. Afterwards IDs
            # stay fixed: movement never reassigns a body to another instance.
            for obj in objects:
                candidates=sorted((i for i in data['entities'] if re.fullmatch(re.escape(obj['body'])+r'\d*',i['name']) and i['id'] not in used and i['visible']),
                                  key=lambda i:math.dist(i['matrix'][12:15],obj['matrix'][12:15]))
                if candidates and math.dist(candidates[0]['matrix'][12:15],obj['matrix'][12:15])<5:
                    if len(candidates)>1 and math.dist(candidates[0]['matrix'][12:15],candidates[1]['matrix'][12:15])<1e-5:
                        raise ValueError('Coincident native bodies cannot be bound unambiguously; separate their installation points')
                    self.bindings[candidates[0]['id']]=obj['name'];used.add(candidates[0]['id'])
            self.origin_frame=data['frame'];self.token=uuid.uuid4().hex
            required={o['name'] for o in objects if o['instance']==identifier}
            if identifier and (not required or required-set(self.bindings.values())):raise RuntimeError('Selected mechanism did not bind to the native world')
            result=self._result(data)
            if result['status']=='alive' and (not result['ball'] or (not checkpoint and math.dist(result['ball']['matrix'][12:15],position)>1)):
                raise RuntimeError('Native ball placement did not complete')
            project.write_json(folder/'probe.json',{'revision':self.revision,'instance':identifier,'position':position,
                               'ball':ball,'sector':sector,'checkpoint':checkpoint,'frame_ms':FRAME_MS,'start_frame':self.origin_frame,
                               'local_fall_limit_y':self.minimum_y,
                               'level_sha256':project.digest(folder/'Level_01.NMO'),'nmo_sha256':project.digest(Path(s.nmo.executable)),
                               'scope':'Interactive local probe; not a complete level playthrough'})
            result['assets']=self.ball_assets()
            return result
        except Exception:
            self.close();raise

    def ball_assets(self):
        from ballance.core.obj import Triangle
        template=self.session.native_preview.template('3D Entities/Balls.nmo')
        namespace='local-ball/'
        objects=[]
        for body in template['objects']:
            if body['body'] not in ('Ball_Wood','Ball_Stone','Ball_Paper'):continue
            triangles=[Triangle(**dict(t,material=namespace+t['material'])) for t in body['triangles']]
            objects.append({'name':namespace+body['body'],'body':body['body'],'local_ball':True,
                            'matrix':body['matrix'],'sector':0,'color':[.7,.6,.4],
                            'batches':self.session.batches(triangles)})
        materials=[dict(m,name=namespace+m['name']) for m in template['materials']]
        return {'objects':objects,'materials':self.session.materials.catalog(self.session.current,materials)}

    def _result(self, data):
        poses={self.bindings[i['id']]:{'matrix':i['matrix'],'visible':i['visible']}
               for i in data['entities'] if i['id'] in self.bindings}
        balls=[i for i in data['entities'] if i['name'] in ('Ball_Wood','Ball_Stone','Ball_Paper') and i['visible']]
        below_map=len(balls)==1 and balls[0]['matrix'][13]<self.minimum_y
        reason='native_ball_off' if data['gameplay']['deaths'] else 'below_map' if below_map else None
        result={'session':self.token,'revision':self.revision,'instance':self.instance,
                'frame':data['frame']-self.origin_frame,'frame_ms':FRAME_MS,'poses':poses,
                'ball':balls[0] if len(balls)==1 else None,'spawn':self.position,
                'sector':self.sector,'checkpoint':self.checkpoint,'start_ball':self.ball,
                'status':'finished' if data['gameplay']['finished'] else 'dead' if reason else 'alive',
                'death_reason':reason,'local_fall_limit_y':self.minimum_y,
                'death_frame':max(0,data['gameplay']['death_frame']-self.origin_frame) if data['gameplay']['deaths'] else data['frame']-self.origin_frame if below_map else None}
        self.last=result
        return result

    def step(self, token, frames, keys=0):
        if not self.token or token!=self.token:raise ValueError('Local play session changed; place the ball again')
        if self.session.revision()!=self.revision:
            self.close();raise ValueError('The draft changed; reset local play')
        if type(frames) is not int or not 0<=frames<=120 or type(keys) is not int or not 0<=keys<=15:
            raise ValueError('Local step needs 0..120 frames and an arrow-key mask in 0..15')
        if self.last['status']!='alive':return self.last
        try:return self._result(self._step(frames,keys))
        except Exception:
            self.close();raise

    def retry(self, token):
        # Validate the original session and revision before discarding its world.
        self.step(token,0)
        return self.start(self.instance,self.position,self.ball,sector=self.sector,checkpoint=self.checkpoint)
