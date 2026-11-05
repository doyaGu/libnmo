/* Orthographic picking uses the displayed pose and the nearest visible surface. */
(function(root){
 const dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
 const sub=(a,b)=>a.map((v,i)=>v-b[i]);
 const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
 function point(m,p,w=1){return [0,1,2].map(i=>m[i]*p[0]+m[4+i]*p[1]+m[8+i]*p[2]+m[12+i]*w)}
 function inverse(m){const a=m.slice(0,3),b=m.slice(4,7),c=m.slice(8,11),rows=[cross(b,c),cross(c,a),cross(a,b)],det=dot(a,rows[0]);if(Math.abs(det)<1e-12)return null;
  const r=[rows[0][0]/det,rows[1][0]/det,rows[2][0]/det,0,rows[0][1]/det,rows[1][1]/det,rows[2][1]/det,0,rows[0][2]/det,rows[1][2]/det,rows[2][2]/det,0,0,0,0,1];const p=point(r,m.slice(12,15),0);for(let i=0;i<3;i++)r[12+i]=-p[i];return r;}
 function boxHit(origin,direction,box){let lo=0,hi=Infinity;for(let i=0;i<3;i++){if(Math.abs(direction[i])<1e-12){if(origin[i]<box[i]||origin[i]>box[i+3])return false;continue}const a=(box[i]-origin[i])/direction[i],b=(box[i+3]-origin[i])/direction[i];lo=Math.max(lo,Math.min(a,b));hi=Math.min(hi,Math.max(a,b));if(lo>hi)return false}return true;}
 function triangleHit(origin,direction,a,b,c){const u=sub(b,a),v=sub(c,a),p=cross(direction,v),det=dot(u,p);if(Math.abs(det)<1e-10)return null;const t=sub(origin,a),x=dot(t,p)/det;if(x<0||x>1)return null;const q=cross(t,u),y=dot(direction,q)/det;if(y<0||x+y>1)return null;const distance=dot(v,q)/det;return distance>=0?distance:null;}
 function bounds(batches){const b=[Infinity,Infinity,Infinity,-Infinity,-Infinity,-Infinity];for(const batch of batches)for(let i=0;i<batch.vertices.length;i++){const axis=i%3,v=batch.vertices[i];b[axis]=Math.min(b[axis],v);b[axis+3]=Math.max(b[axis+3],v)}return b;}
 function hit(objects,origin,direction,matrixFor){let best=null;for(const object of objects){const matrix=matrixFor(object),inv=inverse(matrix);if(!inv)continue;const o=point(inv,origin),d=point(inv,direction,0);object.pickBounds??=bounds(object.batches);if(!boxHit(o,d,object.pickBounds))continue;
   for(const batch of object.batches){const v=batch.vertices;for(let i=0;i<v.length;i+=9){const t=triangleHit(o,d,v.slice(i,i+3),v.slice(i+3,i+6),v.slice(i+6,i+9));if(t!==null&&(!best||t<best.distance)){const a=point(matrix,v.slice(i,i+3)),b=point(matrix,v.slice(i+3,i+6)),c=point(matrix,v.slice(i+6,i+9)),normal=cross(sub(b,a),sub(c,a)),length=Math.hypot(...normal);best={object,distance:t,position:origin.map((x,k)=>x+t*direction[k]),normal:normal.map(n=>n/length)}};}}
  }return best;}
 const api={point,inverse,boxHit,triangleHit,bounds,hit};if(typeof module!=='undefined')module.exports=api;else root.ScenePicking=api;
})(typeof globalThis==='undefined'?this:globalThis);
