// Dev 2.5.0.48 schematic rebuild probe.
//
// The PowerShell wrapper prepends a DEV25_PROBE_CONFIG object before loading
// this file. The agent emits machine-readable lines prefixed with
// "__DEV25_EVENT__"; the wrapper extracts those lines into events.jsonl.

'use strict';

const CFG = (typeof DEV25_PROBE_CONFIG !== 'undefined') ? DEV25_PROBE_CONFIG : {};
const ADDR = CFG.addresses || {};
const IMAGE_BASE = ptr(CFG.idaBase || '0x400000');
const MODULE_NAME = CFG.moduleName || 'Dev.exe';
const MODULE = Process.getModuleByName(MODULE_NAME);
const BASE = MODULE.base;

const state = {
  currentView: ptr(0),
  targetScript: ptr(0),
  targetName: '',
  targetSubCount: -1,
  targetReason: '',
  triggered: false,
  behaviors: {},
  uiByObject: {},
  linkRecordCount: 0,
  createUiLinksCalls: 0
};

function rva(name) {
  if (!ADDR[name]) {
    throw new Error('Missing address in profile: ' + name);
  }
  return BASE.add(ptr(ADDR[name]).sub(IMAGE_BASE));
}

function ptrString(p) {
  return p ? p.toString() : '0x0';
}

function safeAnsi(p) {
  if (!p || p.isNull()) return null;
  try {
    return p.readAnsiString();
  } catch (e) {
    return '<unreadable:' + e.message + '>';
  }
}

function safeU32(p) {
  try {
    return p.readU32();
  } catch (e) {
    return null;
  }
}

function safeCStringText(p) {
  const direct = safeAnsi(p);
  if (direct !== null && direct.indexOf('<unreadable:') !== 0) return direct;
  try {
    return safeAnsi(p.readPointer()) || direct;
  } catch (e) {
    return direct;
  }
}

function emit(type, data) {
  const event = {
    type: type,
    time: Date.now(),
    data: data || {}
  };
  console.log('__DEV25_EVENT__' + JSON.stringify(event));
}

function log(message) {
  console.log('[dev25] ' + message);
}

function nativeThiscall(name, ret, args) {
  return new NativeFunction(rva(name), ret, args, 'thiscall');
}

function nativeDefault(name, ret, args) {
  return new NativeFunction(rva(name), ret, args);
}

const getName = nativeThiscall('CKObject_GetName', 'pointer', ['pointer']);
const getClassID = nativeThiscall('CKObject_GetClassID', 'uint', ['pointer']);
const ckIsChildClassOfObject = nativeDefault('CKIsChildClassOfObject', 'int', ['pointer', 'uint']);
const getType = nativeThiscall('CKBehavior_GetType', 'int', ['pointer']);
const getSubBehaviorCount = nativeThiscall('CKBehavior_GetSubBehaviorCount', 'int', ['pointer']);
const getInputCount = nativeThiscall('CKBehavior_GetInputCount', 'int', ['pointer']);
const getOutputCount = nativeThiscall('CKBehavior_GetOutputCount', 'int', ['pointer']);
const getSubBehaviorLinkCount = nativeThiscall('CKBehavior_GetSubBehaviorLinkCount', 'int', ['pointer']);
const getUIElementForCKObject = nativeDefault('GetUIElementForCKObject', 'pointer', ['pointer']);
const setUIElementForCKObject = nativeDefault('SetUIElementForCKObject', 'pointer', ['pointer', 'int']);
const reconstructScriptUIFromRuntimeGraph =
  nativeThiscall('CUIKSchematicView_ReconstructScriptUIFromRuntimeGraph', 'int', ['pointer', 'pointer', 'int']);

function describeBehavior(p) {
  const out = {
    pointer: ptrString(p),
    name: null,
    classId: null,
    type: null,
    subBehaviorCount: null,
    inputCount: null,
    outputCount: null,
    subBehaviorLinkCount: null
  };
  if (!p || p.isNull()) return out;
  try { out.classId = getClassID(p); } catch (e) {}
  try { out.name = safeAnsi(getName(p)); } catch (e) {}
  try { out.isBehavior = ckIsChildClassOfObject(p, 8) !== 0; } catch (e) { out.isBehavior = false; }
  if (!out.isBehavior) return out;
  try { out.type = getType(p); } catch (e) {}
  try { out.subBehaviorCount = getSubBehaviorCount(p); } catch (e) {}
  try { out.inputCount = getInputCount(p); } catch (e) {}
  try { out.outputCount = getOutputCount(p); } catch (e) {}
  try { out.subBehaviorLinkCount = getSubBehaviorLinkCount(p); } catch (e) {}
  return out;
}

function rememberBehavior(p, ui) {
  const b = describeBehavior(p);
  if (!b.isBehavior || b.type === null || b.name === null) return;
  b.ui = ptrString(ui || getUIElementForCKObject(p));
  state.behaviors[ptrString(p)] = b;
  considerTarget(p, b);
  emit('behavior', b);
}

function considerTarget(p, b) {
  if (b.type !== 1) return;

  const targetName = CFG.targetName || '';
  if (targetName && b.name && b.name.indexOf(targetName) !== -1) {
    state.targetScript = p;
    state.targetName = b.name;
    state.targetSubCount = b.subBehaviorCount || 0;
    state.targetReason = 'targetName';
    emit('target-selected', {
      pointer: ptrString(p),
      name: state.targetName,
      subBehaviorCount: state.targetSubCount,
      reason: state.targetReason
    });
    return;
  }

  if (!targetName && (state.targetScript.isNull() || (b.subBehaviorCount || 0) > state.targetSubCount)) {
    state.targetScript = p;
    state.targetName = b.name || '';
    state.targetSubCount = b.subBehaviorCount || 0;
    state.targetReason = 'largest-script';
    emit('target-selected', {
      pointer: ptrString(p),
      name: state.targetName,
      subBehaviorCount: state.targetSubCount,
      reason: state.targetReason
    });
  }
}

function hookThiscall(name, onEnter, onLeave) {
  const addr = rva(name);
  log('hook ' + name + ' @ ' + addr);
  Interceptor.attach(addr, {
    onEnter(args) {
      this.thisPtr = this.context.ecx;
      if (onEnter) onEnter.call(this, args);
    },
    onLeave(retval) {
      if (onLeave) onLeave.call(this, retval);
    }
  });
}

function readTempArrayInfo(p, stride) {
  try {
    const begin = p.readPointer();
    const end = p.add(4).readPointer();
    const capacity = p.add(8).readPointer();
    const bytes = end.sub(begin).toInt32();
    return {
      pointer: ptrString(p),
      begin: ptrString(begin),
      end: ptrString(end),
      capacity: ptrString(capacity),
      count: stride ? Math.floor(bytes / stride) : bytes
    };
  } catch (e) {
    return { pointer: ptrString(p), error: e.message };
  }
}

function readLinkRecord(record) {
  const fields = [];
  for (let i = 0; i < 19; i++) {
    fields.push(record.add(i * 4).readS32());
  }
  return {
    linkType: fields[0],
    sourceObjectId: fields[3],
    destObjectId: fields[4],
    sourceEndpointType: fields[5],
    destEndpointType: fields[6],
    sourceRuntimeId: fields[9],
    destRuntimeId: fields[10],
    sourceIndex: fields[11],
    destIndex: fields[12],
    delay: fields[13],
    behaviorLink: ptrString(ptr(fields[14] >>> 0)),
    raw: fields
  };
}

function emitLinkRecords(saveLinks, phase) {
  const info = readTempArrayInfo(saveLinks, 76);
  emit('link-record-array', Object.assign({ phase: phase }, info));
  if (info.error || !info.count || info.count > (CFG.maxLinkRecords || 1000)) return;
  const begin = ptr(info.begin);
  for (let i = 0; i < info.count; i++) {
    emit('link-record', {
      phase: phase,
      index: i,
      record: readLinkRecord(begin.add(i * 76))
    });
  }
}

function triggerRebuild(reason) {
  if (state.triggered) return;
  state.triggered = true;

  if (state.currentView.isNull() || state.targetScript.isNull()) {
    emit('trigger-skipped', {
      reason: 'missing-view-or-target',
      view: ptrString(state.currentView),
      targetScript: ptrString(state.targetScript),
      targetName: state.targetName
    });
    return;
  }

  if (!CFG.allowMutation) {
    emit('trigger-blocked', {
      reason: 'AllowMutation not set',
      view: ptrString(state.currentView),
      targetScript: ptrString(state.targetScript),
      targetName: state.targetName
    });
    return;
  }

  const before = getUIElementForCKObject(state.targetScript);
  setUIElementForCKObject(state.targetScript, 0);
  const afterRemove = getUIElementForCKObject(state.targetScript);
  const ret = reconstructScriptUIFromRuntimeGraph(state.currentView, state.targetScript, CFG.rowIndex || -1);
  const afterRebuild = getUIElementForCKObject(state.targetScript);

  emit('trigger-rebuild', {
    reason: reason,
    view: ptrString(state.currentView),
    targetScript: ptrString(state.targetScript),
    targetName: state.targetName,
    uiBefore: ptrString(before),
    uiAfterRemove: ptrString(afterRemove),
    rebuildReturn: ret,
    uiAfterRebuild: ptrString(afterRebuild)
  });
}

function requestTrigger(reason) {
  if ((CFG.triggerCallStyle || 'Deferred') === 'Deferred') {
    setImmediate(function () {
      triggerRebuild(reason);
    });
  } else {
    triggerRebuild(reason);
  }
}

function installHooks() {
  const mode = CFG.mode || 'trace';

  hookThiscall('CUIKLanguageManager_Load', function (args) {
    this.langId = args[0].toInt32();
  }, function (retval) {
    const ids = CFG.languageIds || [562, 563, 565, 566, 609, 797];
    if (ids.indexOf(this.langId) !== -1 || mode === 'language') {
      emit('language-load', {
        id: this.langId,
        pointer: ptrString(retval),
        text: safeAnsi(retval)
      });
    }
  });

  Interceptor.attach(rva('StatusBarOutput'), {
    onEnter(args) {
      this.text = safeCStringText(args[0]) || '';
      this.flag = args[1].toInt32();
      emit('status', { flag: this.flag, text: this.text, textPointer: ptrString(args[0]) });
    },
    onLeave() {
      if ((mode === 'trigger-rebuild' || mode === 'link-records') &&
          (CFG.triggerPoint || 'after-file-loaded') === 'after-file-loaded' &&
          this.text.indexOf('File Loaded Successfully') !== -1) {
        requestTrigger('after-file-loaded');
      }
    }
  });

  hookThiscall('CUIKSchematicView_LoadBehaviorArrayIntoSchematic', function (args) {
    state.currentView = this.thisPtr;
    emit('schematic-load-enter', {
      view: ptrString(this.thisPtr),
      objectArray: ptrString(args[0]),
      arg3: args[1].toInt32()
    });
  }, function (retval) {
    emit('schematic-load-leave', {
      view: ptrString(this.thisPtr),
      retval: ptrString(retval),
      behaviorCount: Object.keys(state.behaviors).length,
      targetScript: ptrString(state.targetScript),
      targetName: state.targetName
    });
    if ((mode === 'trigger-rebuild' || mode === 'link-records') &&
        CFG.triggerPoint === 'after-schematic-load') {
      requestTrigger('after-schematic-load');
    }
  });

  hookThiscall('CUIKSchematicView_RebuildMissingBehaviorWindowsFromList', function (args) {
    emit('missing-behavior-list-enter', {
      view: ptrString(this.thisPtr),
      vector: readTempArrayInfo(args[0], 4)
    });
  }, function (retval) {
    emit('missing-behavior-list-leave', { view: ptrString(this.thisPtr), retval: retval.toInt32() });
  });

  hookThiscall('CUIKSchematicView_CreateBehaviorWindowForBehavior', function (args) {
    const b = describeBehavior(args[0]);
    emit('create-behavior-window', {
      view: ptrString(this.thisPtr),
      behavior: b,
      createMode: args[1].toInt32(),
      rowIndex: args[2].toInt32()
    });
  });

  Interceptor.attach(rva('GetUIElementForCKObject'), {
    onEnter(args) {
      this.obj = args[0];
    },
    onLeave(retval) {
      if (!retval.isNull() && (mode === 'trace' || mode === 'inventory')) {
        emit('ui-map-get', { object: ptrString(this.obj), ui: ptrString(retval) });
      }
    }
  });

  Interceptor.attach(rva('SetUIElementForCKObject'), {
    onEnter(args) {
      const obj = args[0];
      const ui = args[1];
      state.uiByObject[ptrString(obj)] = ptrString(ui);
      if (!obj.isNull() && !ui.isNull()) {
        rememberBehavior(obj, ui);
      }
      emit('ui-map-set', {
        object: ptrString(obj),
        ui: ptrString(ui),
        behavior: (!obj.isNull() && !ui.isNull()) ? describeBehavior(obj) : null
      });
    }
  });

  hookThiscall('CUIKSchematicView_ReconstructScriptUIFromRuntimeGraph', function (args) {
    emit('rebuild-enter', {
      view: ptrString(this.thisPtr),
      script: describeBehavior(args[0]),
      rowIndex: args[1].toInt32()
    });
  }, function (retval) {
    emit('rebuild-leave', { view: ptrString(this.thisPtr), retval: retval.toInt32() });
  });

  hookThiscall('CUIKSchematicView_CollectRuntimeGraphLinkRecords', function (args) {
    emit('collect-link-records-enter', {
      view: ptrString(this.thisPtr),
      visitedBehaviors: readTempArrayInfo(args[0], 4),
      saveLinks: readTempArrayInfo(args[1], 76),
      flags: args[2].toInt32()
    });
    this.saveLinks = args[1];
  }, function (retval) {
    emit('collect-link-records-leave', {
      view: ptrString(this.thisPtr),
      verticalSpan: retval.toString(),
      saveLinks: readTempArrayInfo(this.saveLinks, 76)
    });
    if (CFG.dumpLinkRecords || CFG.mode === 'link-records') {
      emitLinkRecords(this.saveLinks, 'after-collect');
    }
  });

  hookThiscall('CUIKSchematicView_AppendRuntimeGraphLinkRecord', function (args) {
    this.saveLinks = args[0];
    this.beforeCount = readTempArrayInfo(args[0], 76).count || 0;
    emit('append-link-record-enter', {
      view: ptrString(this.thisPtr),
      saveLinks: readTempArrayInfo(args[0], 76),
      sourceIO: ptrString(args[1]),
      destIO: ptrString(args[2]),
      sourceBehavior: describeBehavior(args[3]),
      destBehavior: describeBehavior(args[4]),
      behaviorLink: ptrString(args[5])
    });
  }, function () {
    const info = readTempArrayInfo(this.saveLinks, 76);
    state.linkRecordCount = info.count || state.linkRecordCount;
    emit('append-link-record-leave', {
      view: ptrString(this.thisPtr),
      saveLinks: info,
      appended: (info.count || 0) > this.beforeCount
    });
  });

  hookThiscall('CUIKSchematicView_CreateUILinksFromRebuildRecords', function (args) {
    state.createUiLinksCalls++;
    emit('create-ui-links-enter', {
      view: ptrString(this.thisPtr),
      saveLinks: readTempArrayInfo(args[0], 76)
    });
    if (CFG.dumpLinkRecords || CFG.mode === 'link-records') {
      emitLinkRecords(args[0], 'before-create-ui-links');
    }
  }, function () {
    emit('create-ui-links-leave', {
      view: ptrString(this.thisPtr),
      callCount: state.createUiLinksCalls
    });
  });
}

installHooks();
emit('ready', {
  mode: CFG.mode || 'trace',
  module: MODULE_NAME,
  modulePath: MODULE.path,
  base: ptrString(BASE),
  profileSha256: CFG.sha256 || null,
  sample: CFG.sample || null,
  targetName: CFG.targetName || null,
  allowMutation: !!CFG.allowMutation
});
