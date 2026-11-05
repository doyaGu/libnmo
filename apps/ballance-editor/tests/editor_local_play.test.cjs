const test=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const picking=require('../ballance_editor/scene_picking.js');
const preview=fs.readFileSync(__dirname+'/../ballance_editor/native_preview.js','utf8');
const code=fs.readFileSync(__dirname+'/../ballance_editor/local_play.js','utf8');
const I=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
const c={ScenePicking:picking,dot:(a,b)=>a.reduce((s,x,i)=>s+x*b[i],0),state:{revision:'current'},before:false};
vm.createContext(c);vm.runInContext(preview.slice(0,preview.indexOf('function stopNative')),c);
c.nativeObjectMatrix=o=>o.matrix;
vm.runInContext(code.slice(0,code.indexOf("$('localOpen').onclick")),c);
vm.runInContext(code.slice(code.indexOf('function limitObject'),code.indexOf('const overlayBeforeLocal')),c);
for(const axis of [[0,1,0],[0,0,1],[1,0,0]])test('Measured hinge angle ignores installation frame and template scale: '+axis,()=>{
 const instance=c.jointMatrix({pivot:[0,0,0],axis:[0,1,0]},90);instance[12]=50;instance[13]=12;instance[14]=-230;
 const template=I.slice();template[0]=2;template[5]=.5;template[12]=3;template[14]=-2;
 const obj={joint:{axis,pivot:[2,3,4]},instance_matrix:instance,template_matrix:template};
 for(const angle of [-120,-45,0,60,170]){
  const matrix=c.matrixProduct(instance,c.matrixProduct(c.jointMatrix(obj.joint,angle),template));
  assert.ok(Math.abs(c.measuredJointAngle(obj,matrix)-angle)<1e-8);
  const p=Array.from(c.limitPoint(obj,angle)),normal=picking.point(instance,axis,0);
  c.sceneRay=()=>({origin:p.map((v,i)=>v+100*normal[i]),direction:normal.map(v=>-v)});
  assert.ok(Math.abs(c.limitPointerAngle(obj,12,15)-angle)<1e-8);
 }
});
test('An edge-on limit plane refuses an unstable drag',()=>{
 const obj={joint:{axis:[0,1,0],pivot:[0,0,0]},instance_matrix:I};
 c.sceneRay=()=>({origin:[0,10,100],direction:[0,0,-1]});assert.equal(c.limitPointerAngle(obj,0,0),null);
});
test('The gap of a sloped double rail places the ball on its centreline, with layer occlusion',()=>{
 const rail={category:'geometry',sector:2,ports:{entry:{section:{kind:'rail',gauge:3.8,radius:.35}}},path:[[0,10,0],[0,0,20]]};
 const ray={origin:[0,100,10],direction:[0,-1,0]};
 assert.deepEqual(Array.from(c.localRailSpawn([rail],ray,null,2,false).position),[0,7.1,10]);
 assert.equal(c.localRailSpawn([rail],ray,null,1,false),null);
 assert.equal(c.localRailSpawn([rail],{origin:[4,100,10],direction:[0,-1,0]},null,2,false),null);
 assert.equal(c.localRailSpawn([rail],ray,{object:{play_surface:'floor'},distance:80},2,false),null);
 assert.ok(c.localRailSpawn([rail],ray,{object:{play_surface:'floor'},distance:110},2,false));
 assert.equal(c.localRailSpawn([rail],ray,{object:{play_surface:'rail'},distance:80},2,false),null);
});
test('Live poses and the test ball disappear when the document revision changes',()=>{
 const pose=I.slice();pose[12]=9;
 c.scene={objects:[]};
 vm.runInContext("localSnapshot={revision:'current',ball:{name:'Ball_Paper',matrix:[1,0,0,0,0,1,0,0,0,0,1,0,9,0,0,1]},poses:{arm:{matrix:[1,0,0,0,0,1,0,0,0,0,1,0,9,0,0,1],visible:true}}}",c);
 const arm={name:'arm',native:true,matrix:I};assert.deepEqual(Array.from(c.nativeObjectMatrix(arm)),pose);
 assert.equal(c.localObjectVisible({local_ball:true,body:'Ball_Paper'}),true);
 assert.equal(c.localObjectVisible({local_ball:true,body:'Ball_Wood'}),false);
 c.state.revision='changed';assert.deepEqual(c.nativeObjectMatrix(arm),I);assert.equal(c.localObjectVisible({local_ball:true,body:'Ball_Paper'}),false);
});
test('Pause remains clickable during a native step; death permits retry but not more steps',()=>{
 const fields={};c.$=id=>fields[id]??=( {} );c.busy=false;c.state.revision='current';
 vm.runInContext("localSnapshot={revision:'current',status:'alive'};localPlaying=true;localPending=true",c);
 c.localSetEnabled();assert.equal(fields.localRun.disabled,false);assert.equal(fields.localStep.disabled,true);
 vm.runInContext("localSnapshot.status='dead';localPlaying=false;localPending=false",c);
 c.localSetEnabled();assert.equal(fields.localRun.disabled,true);assert.equal(fields.localStep.disabled,true);assert.equal(fields.localRetry.disabled,false);
});
