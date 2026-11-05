/* Actual OBJ UVs and game material textures. Geometry is never retextured by category. */
const materialProgram = gl.createProgram();
gl.attachShader(
    materialProgram,
    shader(
        gl.VERTEX_SHADER,
        `attribute vec3 p;attribute vec3 n;attribute vec2 uv;uniform mat4 m;uniform mat4 objectMatrix;varying vec3 normal;varying vec2 texcoord;void main(){gl_Position=m*objectMatrix*vec4(p,1.);normal=mat3(objectMatrix)*n;texcoord=uv;}`
    )
);
gl.attachShader(
    materialProgram,
    shader(
        gl.FRAGMENT_SHADER,
        `precision mediump float;uniform sampler2D tex;uniform vec4 diffuse;uniform vec3 emissive;uniform vec3 specular;uniform float shininess;uniform vec3 eye;uniform float selectionTint;uniform float alphaRef;uniform float alphaFunc;uniform float textureMode;uniform float hasTexture;uniform float fade;uniform float environment;uniform mat3 camera;varying vec3 normal;varying vec2 texcoord;void main(){vec3 n=dot(normal,normal)>.00001?normalize(normal):vec3(0.,1.,0.);vec2 coords=texcoord;if(environment>.5){vec3 r=reflect(vec3(0.,0.,-1.),normalize(camera*n));coords=.5+r.xy/max(.001,2.*sqrt(r.x*r.x+r.y*r.y+(r.z+1.)*(r.z+1.)));}vec4 t=hasTexture>.5?texture2D(tex,fract(coords)):vec4(1.);float light=.48+.52*abs(dot(n,normalize(vec3(.3,.85,.4))));float highlight=pow(max(0.,dot(n,normalize(eye+normalize(vec3(.3,.85,.4))))),max(1.,shininess));vec3 base=t.rgb*diffuse.rgb;float alpha=t.a*diffuse.a;if(textureMode==1.||textureMode==7.){base=t.rgb;alpha=diffuse.a;}else if(textureMode==3.){base=mix(diffuse.rgb,t.rgb,t.a);alpha=diffuse.a;}else if(textureMode==8.){base=t.rgb+diffuse.rgb;alpha=diffuse.a;}vec3 color=base*light+t.rgb*emissive*.45+specular*highlight*.65;if(alphaFunc==1.||(alphaFunc==2.&&alpha>=alphaRef)||(alphaFunc==3.&&abs(alpha-alphaRef)>.002)||(alphaFunc==4.&&alpha>alphaRef)||(alphaFunc==5.&&alpha<=alphaRef)||(alphaFunc==6.&&abs(alpha-alphaRef)<.002)||(alphaFunc==7.&&alpha<alphaRef))discard;gl_FragColor=vec4(mix(color,vec3(.42,1.,.76),selectionTint)*fade,alpha);}`
    )
);
gl.linkProgram(materialProgram);
if (!gl.getProgramParameter(materialProgram, gl.LINK_STATUS)) {
    throw Error(gl.getProgramInfoLog(materialProgram));
}
const materialUniforms = Object.fromEntries(
    [
        'm',
        'objectMatrix',
        'diffuse',
        'emissive',
        'specular',
        'shininess',
        'eye',
        'selectionTint',
        'alphaRef',
        'alphaFunc',
        'textureMode',
        'hasTexture',
        'fade',
        'environment',
        'camera',
        'tex',
    ].map((k) => [k, gl.getUniformLocation(materialProgram, k)])
);
const materialAttributes = Object.fromEntries(
    ['p', 'n', 'uv'].map((k) => [k, gl.getAttribLocation(materialProgram, k)])
);
const identityMatrix = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
let renderBatches = [];
let textureCache = new Map();
const whiteTexture = gl.createTexture();
gl.bindTexture(gl.TEXTURE_2D, whiteTexture);
gl.texImage2D(
    gl.TEXTURE_2D,
    0,
    gl.RGBA,
    1,
    1,
    0,
    gl.RGBA,
    gl.UNSIGNED_BYTE,
    new Uint8Array([255, 255, 255, 255])
);
gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
function loadTexture(url) {
    if (textureCache.has(url)) {
        return;
    }
    const record = { texture: gl.createTexture(), ready: false };
    textureCache.set(url, record);
    const image = new Image();
    image.onload = () => {
        gl.bindTexture(gl.TEXTURE_2D, record.texture);
        gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, image);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        record.ready = true;
        draw();
    };
    image.onerror = () => {
        record.failed = true;
        notify('有贴图未能加载，请刷新场景。');
    };
    image.src = url;
}
function uploadScene() {
    for (const b of renderBatches) {
        for (const buffer of b.buffers) {
            gl.deleteBuffer(buffer);
        }
    }
    renderBatches = [];
    const active = before ? oldScene : scene;
    const sector = Number($('sector').value);
    const materials = Object.fromEntries(
        [
            ...(active.materials || []),
            ...(typeof localAssets === 'undefined' ? [] : localAssets?.materials || []),
        ].map((m) => [m.name, m])
    );
    for (const obj of [
        ...active.objects,
        ...(typeof localAssets === 'undefined' ? [] : localAssets?.objects || []),
    ]) {
        const other = sector && obj.sector && obj.sector !== sector;
        if ((other && !$('others').checked) || (obj.decoration && !$('decorations').checked)) {
            continue;
        }
        for (const part of obj.batches || []) {
            const material = materials[part.material] || {
                diffuse: [...obj.color, 1],
                emissive: [0, 0, 0],
            };
            if (material.texture_url) {
                loadTexture(material.texture_url);
            }
            const buffers = [part.vertices, part.normals, part.uvs].map((values) => {
                const b = gl.createBuffer();
                gl.bindBuffer(gl.ARRAY_BUFFER, b);
                gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(values), gl.STATIC_DRAW);
                return b;
            });
            renderBatches.push({
                object: obj,
                buffers,
                count: part.vertices.length / 3,
                material,
                fade: other ? 0.2 : 1,
            });
        }
    }
    renderBatches.sort(
        (a, b) => Boolean(a.material.blend?.source) - Boolean(b.material.blend?.source)
    );
}
function renderScene(w, h) {
    if (!renderBatches.length) {
        return;
    }
    gl.useProgram(materialProgram);
    const [r, u, d] = basis();
    const sx = w / (2 * scale);
    const sy = h / (2 * scale);
    const sz = 10000;
    const m = [];
    for (let i = 0; i < 3; i++) {
        m.push(r[i] / sx, u[i] / sy, -d[i] / sz, 0);
    }
    m.push(-dot(r, center) / sx, -dot(u, center) / sy, dot(d, center) / sz, 1);
    gl.uniformMatrix4fv(materialUniforms.m, false, new Float32Array(m));
    gl.uniformMatrix3fv(
        materialUniforms.camera,
        false,
        new Float32Array([r[0], u[0], d[0], r[1], u[1], d[1], r[2], u[2], d[2]])
    );
    gl.uniform3fv(materialUniforms.eye, new Float32Array(d));
    gl.activeTexture(gl.TEXTURE0);
    gl.uniform1i(materialUniforms.tex, 0);
    for (const b of renderBatches) {
        if (typeof localObjectVisible === 'function' && !localObjectVisible(b.object)) {
            continue;
        }
        const mat = b.material;
        const texture = textureCache.get(mat.texture_url);
        const textured = $('textured').checked;
        if (mat.blend?.source) {
            gl.enable(gl.BLEND);
            const factors = {
                zero: gl.ZERO,
                one: gl.ONE,
                src_color: gl.SRC_COLOR,
                inv_src_color: gl.ONE_MINUS_SRC_COLOR,
                src_alpha: gl.SRC_ALPHA,
                inv_src_alpha: gl.ONE_MINUS_SRC_ALPHA,
                dest_alpha: gl.DST_ALPHA,
                inv_dest_alpha: gl.ONE_MINUS_DST_ALPHA,
                dest_color: gl.DST_COLOR,
                inv_dest_color: gl.ONE_MINUS_DST_COLOR,
            };
            gl.blendFunc(
                factors[mat.blend.source] ?? gl.SRC_ALPHA,
                factors[mat.blend.destination] ?? gl.ONE_MINUS_SRC_ALPHA
            );
        } else {
            gl.disable(gl.BLEND);
        }
        gl.depthMask(mat.z_write !== false);
        gl.uniformMatrix4fv(
            materialUniforms.objectMatrix,
            false,
            new Float32Array(
                typeof nativeObjectMatrix === 'function'
                    ? nativeObjectMatrix(b.object)
                    : b.object.matrix || identityMatrix
            )
        );
        for (const [i, name, size] of [
            [0, 'p', 3],
            [1, 'n', 3],
            [2, 'uv', 2],
        ]) {
            gl.bindBuffer(gl.ARRAY_BUFFER, b.buffers[i]);
            gl.enableVertexAttribArray(materialAttributes[name]);
            gl.vertexAttribPointer(materialAttributes[name], size, gl.FLOAT, false, 0, 0);
        }
        gl.uniform4fv(
            materialUniforms.diffuse,
            new Float32Array(textured ? mat.diffuse || [1, 1, 1, 1] : [0.58, 0.64, 0.66, 1])
        );
        gl.uniform3fv(
            materialUniforms.emissive,
            new Float32Array(textured ? (mat.emissive || [0, 0, 0]).slice(0, 3) : [0, 0, 0])
        );
        gl.uniform3fv(
            materialUniforms.specular,
            new Float32Array(textured ? (mat.specular || [0, 0, 0]).slice(0, 3) : [0, 0, 0])
        );
        gl.uniform1f(materialUniforms.shininess, mat.specular_power || 1);
        gl.uniform1f(
            materialUniforms.selectionTint,
            typeof sceneSelectionStrength === 'function' ? sceneSelectionStrength(b.object) : 0
        );
        gl.uniform1f(materialUniforms.alphaRef, (mat.alpha?.ref || 0) / 255);
        gl.uniform1f(
            materialUniforms.alphaFunc,
            {
                never: 1,
                less: 2,
                equal: 3,
                less_equal: 4,
                greater: 5,
                not_equal: 6,
                greater_equal: 7,
                always: 8,
            }[mat.alpha?.func] || 8
        );
        gl.uniform1f(
            materialUniforms.textureMode,
            textured
                ? { decal: 1, modulate: 2, decal_alpha: 3, modulate_alpha: 4, copy: 7, add: 8 }[
                      mat.blend?.texture
                  ] || 4
                : 4
        );
        gl.uniform1f(materialUniforms.fade, b.fade);
        gl.uniform1f(materialUniforms.hasTexture, textured && texture?.ready ? 1 : 0);
        gl.uniform1f(
            materialUniforms.environment,
            /Rail_Environment/i.test(mat.texture || '') ? 1 : 0
        );
        gl.bindTexture(gl.TEXTURE_2D, texture?.ready ? texture.texture : whiteTexture);
        gl.drawArrays(gl.TRIANGLES, 0, b.count);
    }
    gl.depthMask(true);
    gl.disable(gl.BLEND);
}
$('textured').onchange = draw;
