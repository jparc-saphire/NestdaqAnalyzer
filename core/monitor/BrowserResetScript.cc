#include "monitor/ControlServer.hh"

namespace nda::monitor {

// ES module loaded by the JSROOT page (item field _autoload of the top folder). It makes a Reset that only changes
// what this browser tab shows: when the button is pressed it reads every cumulative histogram once and keeps the
// bin contents; from then on every object the page receives has those contents subtracted. The monitor program
// and other browsers are not affected.
const char* BrowserResetScript() {
  return R"JS(import { HierarchyPainter } from '../jsrootsys/modules/gui/HierarchyPainter.mjs';
import { httpRequest } from '../jsrootsys/modules/core.mjs';

const GENERATION = 'Control/generation';
const SUMS = ['fEntries', 'fTsumw', 'fTsumw2', 'fTsumwx', 'fTsumwx2', 'fTsumwy', 'fTsumwy2', 'fTsumwxy'];
const state = { baselines: new Map(), generation: null, since: null, note: null };
const proto = HierarchyPainter.prototype;
const origGet = proto.getOnlineItem;
const origExecute = proto.executeCommand;
let genCache = null, genTime = 0;

function isHist(obj) {
  return obj && /^TH[123]/.test(obj._typename || '') && obj.fArray && obj.fArray.length > 0;
}

function showNote() {
  let el = document.getElementById('nda-browser-reset-note');
  if (!el) {
    el = document.createElement('div');
    el.id = 'nda-browser-reset-note';
    el.style.cssText = 'position:fixed;right:8px;bottom:8px;z-index:10;padding:2px 8px;border-radius:4px;' +
                       'background:#fe9;color:#000;font:12px sans-serif;display:none';
    document.body.appendChild(el);
  }
  el.style.display = state.note ? 'block' : 'none';
  el.textContent = state.note || '';
}

function clearBaselines(note) {
  state.baselines = new Map();
  state.generation = null;
  state.since = null;
  state.note = note || null;
  showNote();
}

// Identifier of the monitor's histograms: it changes at the Reset button and when the program starts again.
function currentGeneration(hp) {
  const now = Date.now();
  if (!genCache || now - genTime > 300) {
    genTime = now;
    genCache = httpRequest((hp.h?._online ?? '') + GENERATION + '/root.json?compact=3', 'object')
                 .then(o => o?.fTitle ?? null, () => null);
  }
  return genCache;
}

function titleWithTag(title, tag) {
  const i = (title || '').indexOf(';');
  return i < 0 ? `${title || ''} ${tag}` : `${title.slice(0, i)} ${tag}${title.slice(i)}`;
}

function subtract(obj, base) {
  const a = obj.fArray;
  if (a.length !== base.arr.length)
    return obj;
  const out = new a.constructor(a.length);
  for (let i = 0; i < a.length; ++i) {
    out[i] = a[i] - base.arr[i];
    if (out[i] < -1e-6)
      return null;  // the histogram holds less than at the browser reset
  }
  obj.fArray = out;
  if (obj.fSumw2?.length && obj.fSumw2.length === base.sumw2?.length)
    obj.fSumw2 = obj.fSumw2.map((v, i) => Math.max(0, v - base.sumw2[i]));
  for (const k of SUMS) {
    if (typeof obj[k] === 'number' && typeof base.sums[k] === 'number')
      obj[k] = k === 'fEntries' ? Math.max(0, obj[k] - base.sums[k]) : obj[k] - base.sums[k];
  }
  obj.fTitle = titleWithTag(obj.fTitle, `(since ${state.since})`);
  return obj;
}

proto.getOnlineItem = async function(item, itemname, option) {
  const obj = await origGet.call(this, item, itemname, option);
  if (!state.baselines.size || !item || item._nda_instant || !isHist(obj))
    return obj;
  const base = state.baselines.get(this.itemFullName(item));
  if (!base)
    return obj;
  if (await currentGeneration(this) !== state.generation) {
    clearBaselines('Browser reset cleared: the analyzer was reset or restarted');
    return obj;
  }
  return subtract(obj, base) || obj;
};

async function resetHere(hp) {
  const generation = await currentGeneration(hp);
  const items = [];
  hp.forEachItem(it => {
    if (/^ROOT\.TH[123]/.test(it._kind || '') && !it._nda_instant)
      items.push(it);
  });
  const baselines = new Map();
  let next = 0;
  const worker = async () => {
    while (next < items.length) {
      const it = items[next++];
      const obj = await origGet.call(hp, it, hp.itemFullName(it));
      if (!isHist(obj))
        continue;
      const sums = {};
      for (const k of SUMS) sums[k] = obj[k];
      baselines.set(hp.itemFullName(it), { arr: Float64Array.from(obj.fArray), sumw2: obj.fSumw2 ? Array.from(obj.fSumw2) : null, sums });
    }
  };
  await Promise.all([worker(), worker(), worker(), worker(), worker(), worker()]);
  genCache = null;
  if (await currentGeneration(hp) !== generation)
    throw new Error('the analyzer was reset during the browser reset');
  state.baselines = baselines;
  state.generation = generation;
  state.since = new Date().toLocaleTimeString();
  state.note = `Browser reset at ${state.since}: ${baselines.size} histograms (other browsers are not affected)`;
  showNote();
}

proto.executeCommand = async function(itemname, elem, ...rest) {
  const isReset = itemname.endsWith('/ResetBrowser') || itemname === 'ResetBrowser';
  const isClear = itemname.endsWith('/ClearBrowserReset') || itemname === 'ClearBrowserReset';
  if (!isReset && !isClear)
    return origExecute.call(this, itemname, elem, ...rest);
  const flash = color => {
    if (!elem) return;
    elem.style.backgroundColor = color;
    if (color) setTimeout(() => { elem.style.backgroundColor = ''; }, 2000);
  };
  flash('yellow');
  try {
    if (isReset)
      await resetHere(this);
    else
      clearBaselines(null);
    flash('green');
    return 'true';
  } catch (err) {
    clearBaselines(`Browser reset failed: ${err.message}`);
    flash('red');
    return 'false';
  }
};
)JS";
}

}  // namespace nda::monitor
