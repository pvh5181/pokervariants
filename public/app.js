'use strict';
const $ = s => document.querySelector(s);
const SUITS = { s: '♠', h: '♥', d: '♦', c: '♣' };

const S = {
  ws: null, id: null, key: null, token: null, me: null, st: null, hist: [],
  myName: null, myStack: null, discSel: new Set(), tab: null, reconnect: false,
  watchMode: false, pendingJoin: null, sitSeat: 0, betMenu: false, betAmt: null, ovMode: null, bbMode: false, sound: true, alertDeadline: 0, closed: false
};

let sfxAt = 0;
function playSfx(s, n) {
  if (!S.sound) return;
  const file = s === 'deal' ? 'deal-card' : s;
  const vol = s === 'alert' ? 0.45 : 0.6;
  const count = Math.max(1, n | 0);
  const base = Math.max(performance.now(), sfxAt);
  for (let i = 0; i < count; i++) {
    const when = base + i * 130;
    setTimeout(() => {
      const a = new Audio('/sounds/' + file + '.mp3');
      a.volume = vol;
      a.play().catch(() => {});
    }, Math.max(0, when - performance.now()));
    sfxAt = when + 130;
  }
}

const SEAT_POS = [];
for (let i = 0; i < 10; i++) {
  const ang = (90 - i * 36) * Math.PI / 180;
  SEAT_POS.push({ x: 50 + 46 * Math.cos(ang), y: 50 - 42 * Math.sin(ang) });
}

function showView(v) {
  ['landing', 'join', 'closed', 'table'].forEach(x => { $('#view-' + x).hidden = x !== v; });
}

function fmt(cents) {
  const v = Number(cents) / 100;
  return v.toLocaleString(undefined, { minimumFractionDigits: 0, maximumFractionDigits: 2 });
}

function toCents(str) {
  const v = parseFloat(str);
  if (isNaN(v)) return 0;
  return Math.round(v * 100);
}

function amt(cents) {
  if (!S.bbMode) return fmt(cents);
  const st = S.st;
  const bb = (st && st.hand && st.hand.bb) || (st && st.settings && st.settings.bb) || 100;
  const v = Number(cents) / bb;
  const r = Math.abs(v - Math.round(v)) < 0.005 ? String(Math.round(v)) : v.toFixed(2).replace(/0+$/, '').replace(/\.$/, '');
  return r + 'bb';
}

function toast(m) {
  const t = $('#toast');
  t.textContent = m;
  t.hidden = false;
  clearTimeout(t._tm);
  t._tm = setTimeout(() => { t.hidden = true; }, 3500);
}

function cardHTML(s, cls) {
  if (!s) return '<div class="card back' + (cls ? ' ' + cls : '') + '"></div>';
  const r = s[0], su = s[1];
  return '<div class="card' + (su === 'h' || su === 'd' ? ' red' : '') + (cls ? ' ' + cls : '') + '">' +
    '<span class="rk">' + (r === 'T' ? '10' : r) + '</span><span class="rs">' + SUITS[su] + '</span></div>';
}

function miniCardHTML(s) { return cardHTML(s, 'mini'); }

function boot() {
  const m = location.hash.match(/^#\/t\/([A-Za-z0-9]+)(?:\/([A-Za-z0-9]+))?$/);
  if (!m) { S.id = null; S.closed = false; showView('landing'); return; }
  S.closed = false;
  S.id = m[1];
  S.key = sessionStorage.getItem('pv_hostkey_' + m[1]) || null;
  S.token = sessionStorage.getItem('pv_token_' + m[1]) || null;
  if (m[2]) location.hash = '#/t/' + S.id;
  const saved = sessionStorage.getItem('pv_join_' + m[1]);
  const enter = () => {
    if (S.closed || S.id !== m[1]) return;
    if ((S.pendingJoin && S.pendingJoin.id === m[1]) || S.key || S.token) {
      if (saved) {
        try {
          const j2 = JSON.parse(saved);
          if (!S.myName) S.myName = j2.name;
          S.myStack = j2.stack || S.myStack;
        } catch (e) {}
      }
      if (!S.myName) S.myName = 'host';
      S.watchMode = false;
      S.pendingJoin = null;
      showView('table');
      connect();
    } else {
      $('#j-table').textContent = S.id;
      showView('join');
    }
  };
  fetch('/api/table/' + encodeURIComponent(S.id)).then(r => r.ok ? r.json() : null).then(info => {
    if (S.id !== m[1]) return;
    if (info && info.closed) showClosed(info);
    else enter();
  }).catch(enter);
}

function showClosed(info) {
  S.closed = true;
  S.st = null;
  if (S.ws) {
    S.ws.onclose = null;
    try { S.ws.close(); } catch (e) {}
    S.ws = null;
  }
  $('#closed-id').textContent = info.id || S.id || '';
  const rows = info.ledger || [];
  $('#closed-ledger').innerHTML = '<table class="led"><tr><th>Player</th><th>Bought in</th><th>Stack</th><th>Won/Lost</th></tr>' +
    (rows.length ? rows.map(r =>
      '<tr><td>' + esc(r.name) + (r.seated ? '' : ' (left)') + '</td>' +
      '<td>' + fmt(r.bought) + '</td><td>' + fmt(r.stack) + '</td>' +
      '<td class="' + (r.wl >= 0 ? 'pos' : 'neg') + '">' + (r.wl >= 0 ? '+' : '') + fmt(r.wl) + '</td></tr>'
    ).join('') : '<tr><td colspan="4" class="muted">no ledger entries</td></tr>') +
    '</table>';
  showView('closed');
}

function connect() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  S.closed = false;
  S.ws = new WebSocket(proto + '://' + location.host + '/ws/' + S.id + (S.key ? '?k=' + S.key : ''));
  S.ws.onopen = () => {
    const tok = sessionStorage.getItem('pv_token_' + S.id) || undefined;
    if (S.watchMode) S.ws.send(JSON.stringify({ t: 'watch', name: S.myName || 'guest', tok }));
    else S.ws.send(JSON.stringify({ t: 'join', name: S.myName, stack: parseInt(S.myStack) || 100000, tok }));
  };
  S.ws.onmessage = e => { try { onMsg(JSON.parse(e.data)); } catch (err) {} };
  S.ws.onclose = () => {
    if (S.st) {
      S.reconnect = true;
      renderOverlay();
    }
  };
  S.ws.onerror = () => {};
}

function onMsg(m) {
  switch (m.t) {
    case 'welcome':
      S.me = { id: m.id, host: m.host, table: m.table };
      if (m.token) {
        S.token = m.token;
        sessionStorage.setItem('pv_token_' + m.table, m.token);
      }
      $('#btn-host').hidden = !m.host;
      break;
    case 'state':
      if (m.closed) { showClosed(m); break; }
      S.st = m;
      if (S.me && m.me) S.me.host = !!m.me.host;
      if (m.me && m.me.hostKey) {
        S.key = m.me.hostKey;
        sessionStorage.setItem('pv_hostkey_' + m.id, m.me.hostKey);
      }
      $('#btn-host').hidden = !(S.me && S.me.host);
      render();
      break;
    case 'chat':
      appendChat(m);
      break;
    case 'hand': {
      const i = S.hist.findIndex(h => h.id === m.id);
      if (i >= 0) S.hist[i] = m;
      else S.hist.unshift(m);
      if (S.hist.length > 500) S.hist.length = 500;
      if ($('#tab-history').hidden === false) renderHistory();
      break;
    }
    case 'history':
      S.hist = m.hands || [];
      renderHistory();
      break;
    case 'err':
      toast(m.m);
      break;
    case 'sfx':
      playSfx(m.s, m.n || 1);
      break;
  }
}

function send(o) { if (S.ws && S.ws.readyState === 1) S.ws.send(JSON.stringify(o)); }

function cardParse(s) {
  const r = s[0];
  const rank = 'TJQKA'.includes(r) ? { T: 10, J: 11, Q: 12, K: 13, A: 14 }[r] : parseInt(r);
  return [rank, s[1]];
}

function combos(arr, k) {
  const out = [];
  const n = arr.length;
  if (k > n) return out;
  const idx = [];
  for (let i = 0; i < k; i++) idx.push(i);
  while (true) {
    out.push(idx.map(i => arr[i]));
    let i = k - 1;
    while (i >= 0 && idx[i] === n - k + i) i--;
    if (i < 0) break;
    idx[i]++;
    for (let j = i + 1; j < k; j++) idx[j] = idx[j - 1] + 1;
  }
  return out;
}

function straightHigh5(uniq, sd) {
  for (let i = uniq.length - 1; i >= 0; i--) {
    const hi = uniq[i];
    let ok = true;
    for (let k = 0; k < 5; k++) if (!uniq.includes(hi - k)) { ok = false; break; }
    if (ok) return hi;
  }
  if (sd) {
    if ([14, 6, 7, 8, 9].every(r => uniq.includes(r))) return 9;
  } else if ([14, 2, 3, 4, 5].every(r => uniq.includes(r))) return 5;
  return 0;
}

function eval5Rank(cards, sd) {
  const ranks = cards.map(c => c[0]).sort((a, b) => b - a);
  const cnt = {};
  cards.forEach(c => { cnt[c[0]] = (cnt[c[0]] || 0) + 1; });
  const flush = cards.every(c => c[1] === cards[0][1]);
  const uniq = [...new Set(ranks)].sort((a, b) => b - a);
  const sh = straightHigh5(uniq, sd);
  const quads = Object.keys(cnt).filter(r => cnt[r] === 4).map(Number);
  const trips = Object.keys(cnt).filter(r => cnt[r] === 3).map(Number).sort((a, b) => b - a);
  const pairs = Object.keys(cnt).filter(r => cnt[r] === 2).map(Number).sort((a, b) => b - a);
  const singles = Object.keys(cnt).filter(r => cnt[r] === 1).map(Number).sort((a, b) => b - a);
  let cat = 0, tb = [];
  if (flush && sh) { cat = 8; tb = [sh]; }
  else if (quads.length) { cat = 7; tb = [quads[0], singles[0]]; }
  else if (trips.length === 2 || (trips.length === 1 && pairs.length >= 1)) {
    cat = sd ? 5 : 6;
    tb = trips.length === 2 ? [trips[0], trips[1]] : [trips[0], pairs[0]];
  }
  else if (flush) { cat = sd ? 6 : 5; tb = ranks; }
  else if (sh) { cat = 4; tb = [sh]; }
  else if (trips.length) { cat = 3; tb = [trips[0], ...singles.slice(0, 2)]; }
  else if (pairs.length >= 2) { cat = 2; tb = [pairs[0], pairs[1], singles[0]]; }
  else if (pairs.length === 1) { cat = 1; tb = [pairs[0], ...singles.slice(0, 3)]; }
  else { cat = 0; tb = ranks.slice(0, 5); }
  return { key: [cat, ...tb], cat };
}

function catName(cat, sd) {
  switch (cat) {
    case 8: return 'Straight Flush';
    case 7: return 'Four of a Kind';
    case 6: return sd ? 'Flush' : 'Full House';
    case 5: return sd ? 'Full House' : 'Flush';
    case 4: return 'Straight';
    case 3: return 'Three of a Kind';
    case 2: return 'Two Pair';
    case 1: return 'Pair';
    default: return 'High Card';
  }
}

function rankChar(r) {
  return r === 14 ? 'A' : (r === 10 ? 'T' : (r === 11 ? 'J' : (r === 12 ? 'Q' : (r === 13 ? 'K' : String(r)))));
}

function cmpKey(a, b) {
  for (let i = 0; i < Math.min(a.length, b.length); i++) {
    if (a[i] !== b[i]) return a[i] - b[i];
  }
  return a.length - b.length;
}

function bestHandInfo(hole, board, set) {
  const sd = !!set.shortdeck;
  const holeC = hole.map(cardParse);
  const boardC = board.map(cardParse);
  const list = [];
  if (set.game === 0) {
    if (set.greek) {
      combos(boardC, 3).forEach(b3 => list.push([...holeC, ...b3]));
    } else {
      combos([...holeC, ...boardC], 5).forEach(c5 => list.push(c5));
    }
  } else {
    combos(holeC, 2).forEach(h2 => combos(boardC, 3).forEach(b3 => list.push([...h2, ...b3])));
  }
  let best = null;
  list.forEach(c => {
    if (c.length !== 5) return;
    const r = eval5Rank(c, sd);
    if (!best || cmpKey(r.key, best.key) > 0) best = r;
  });
  return best;
}

function bestLowInfo(hole, board) {
  const holeC = hole.map(cardParse);
  const boardC = board.map(cardParse);
  let best = null;
  combos(holeC, 2).forEach(h2 => combos(boardC, 3).forEach(b3 => {
    const rs = [...h2, ...b3].map(c => c[0] === 14 ? 1 : c[0]);
    if (rs.some(r => r > 8)) return;
    if (new Set(rs).size < 5) return;
    const key = rs.slice().sort((a, b) => b - a);
    if (!best || cmpKey(key, best) < 0) best = key;
  }));
  return best;
}

function updateHandRank(st) {
  const box = $('#handrank');
  const me = st.me;
  if (!me || !me.cards || !me.cards.length) { box.hidden = true; return; }
  const mep = st.players.find(p => p.id === me.id);
  if (!mep || mep.state === 'folded') { box.hidden = true; return; }
  const boards = [st.hand.board || []];
  if (st.hand.board2 && st.hand.board2.length) boards.push(st.hand.board2);
  const set = st.settings;
  const parts = [];
  boards.forEach((board, bi) => {
    if (!board.length || me.cards.length + board.length < 5) return;
    const info = bestHandInfo(me.cards, board, set);
    if (!info) return;
    let name = catName(info.cat, set.shortdeck);
    if (info.cat === 1) name += ' (' + rankChar(info.key[1]) + ')';
    if (info.cat === 2) name += ' (' + rankChar(info.key[1]) + ', ' + rankChar(info.key[2]) + ')';
    let lowHtml = '';
    if (set.hilo) {
      const low = bestLowInfo(me.cards, board);
      if (low) lowHtml = ' <span class="hr-low">Low: ' + low.map(rankChar).join('-') + '</span>';
    }
    parts.push('<div><span class="hr-name">' + (boards.length > 1 ? 'Board ' + (bi + 1) + ': ' : '') + name + '</span>' + lowHtml + '</div>');
  });
  if (!parts.length) { box.hidden = true; return; }
  box.innerHTML = parts.join('');
  box.hidden = false;
}

function render() {
  if (!S.st) return;
  const st = S.st, set = st.settings;
  $('#t-title').textContent = 'Table ' + st.id + ' — ' + set.v + ' — ' + amt(set.sb) + '/' + amt(set.bb) +
    (set.runTwice ? ' · RIT' : '') + (set.paused ? ' · PAUSED' : '');
  $('#t-status').textContent = st.seatedCount + '/' + st.maxPlayers + ' seated';
  $('#btn-stand').hidden = !(st.me && st.me.seated);
  $('#btn-sitout').hidden = !(st.me && st.me.seated);
  $('#btn-sitout').textContent = (st.me && st.me.sitout) ? 'Sit in' : 'Sit out';
  $('#btn-chips').hidden = !(st.me && st.me.seated && !st.me.host);
  $('#btn-bb').textContent = S.bbMode ? 'stack' : 'BB';
  const tb = $('#timebankbox');
  tb.hidden = !(st.me && st.me.seated);
  if (st.me && st.me.seated) {
    if (tb.dataset.built !== '1') {
      tb.innerHTML = '<div class="tb-line" id="tb-bank"></div>' +
        '<div class="tb-line" id="tb-clock" data-mine></div>' +
        '<div class="timebar" id="my-timebar"><div class="timefill"></div></div>';
      tb.dataset.built = '1';
    }
    $('#tb-bank').textContent = 'timebank: ' + st.me.bank + 's';
    const clk = $('#tb-clock');
    const bar = $('#my-timebar');
    if (clk) clk.style.display = st.me.deadline ? '' : 'none';
    if (bar) bar.style.display = st.me.deadline ? '' : 'none';
  }
  $('#btn-pause').hidden = !(st.me && st.me.host);
  $('#btn-host').classList.toggle('hasreq', !!(st.pending && st.pending.length));
  $('#btn-pause').innerHTML = st.settings.paused
    ? '<span class="playtri"></span>'
    : '<span class="pausebars"><i></i><i></i></span>';
  $('#btn-pause').title = st.settings.paused ? 'Resume game' : 'Pause game';
  const isHost = !!(S.me && S.me.host);
  $('#tabbtn-host').hidden = !isHost;
  $('#tabbtn-info').hidden = isHost;
  if (S.tab === 'host' && !isHost) S.tab = 'info';
  if (!$('#sidepanel').hidden && $('#tab-' + (S.tab || 'info')).hidden) openPanel(S.tab || 'info');
  renderSeats(st);
  renderBoard(st);
  renderAction(st);
  renderOverlay(st);
  renderRun2(st);
  updateHandRank(st);
  renderChatLog();
  if (!$('#sidepanel').hidden) {
    const ae = document.activeElement;
    const typing = ae && ae.closest('#tab-host') && (ae.tagName === 'INPUT' || ae.tagName === 'SELECT' || ae.tagName === 'TEXTAREA');
    if (!typing) renderSide(st);
  }
}

function renderSeats(st) {
  const box = $('#seats');
  const marks = showdownMarks(st);
  for (let i = 0; i < 10; i++) {
    let el = box.querySelector('[data-seat="' + i + '"]');
    if (!el) {
      el = document.createElement('div');
      el.className = 'seat';
      el.dataset.seat = i;
      el.style.left = SEAT_POS[i].x + '%';
      el.style.top = SEAT_POS[i].y + '%';
      box.appendChild(el);
    }
    const p = st.players.find(x => x.seat === i);
    const isMe = p && S.me && p.id === S.me.id;
    const turn = st.turns.find(t => t.p === p && p.id);
    const winner = (p && st.hand.phase === 'over' && st.last && st.last.players)
      ? st.last.players.find(x => x.id === p.id && x.won > 0) : null;
    let html = '';
    if (p) {
      const badges = [];
      if (st.hand.dealer === i) badges.push('<span class="badge dealer">D</span>');
      if (st.hand.sbSeat === i) badges.push('<span class="badge blind">SB</span>');
      if (st.hand.bbSeat === i) badges.push('<span class="badge blind">BB</span>');
      if (p.host) badges.push('<span class="badge host">H</span>');
      if (st.settings.nitGame && p.wonhand > 0) badges.push('<span class="badge nit' + (st.nit === p.id ? ' cur' : '') + '" title="nit game">N</span>');
      let state = '';
      if (p.sitout && p.state === 'waiting') state = '<div class="muted">sitting out</div>';
      else if (p.state === 'folded') state = '<div class="muted">folded</div>';
      else if (p.state === 'allin') state = '<div style="color:#ff9d5e;font-size:11px">ALL-IN</div>';
      else if (p.state === 'in' && turn && st.hand.toAct === p.id) {
        state = '<div class="turnclock" data-clock="' + p.id + '"></div>' +
          '<div class="timebar mini" data-tbar="' + p.id + '"><div class="timefill"></div></div>';
      } else if (p.state === 'in') state = '<div class="muted">in hand</div>';
      let hole = '';
      const hcls = n => n === 5 ? 'hole hole-5' : (n >= 6 ? 'hole hole-grid3' : (n >= 4 ? 'hole hole-grid' : 'hole'));
      if (p.hole) {
        const hc = hcls(p.hole.length);
        if (marks && !marks.winners.has(p.id)) {
          hole = '<div class="' + hc + '">' + p.hole.map(c => cardHTML(c, 'small fade')).join('') + '</div>';
        } else if (marks && marks.holes[p.id]) {
          const hset = marks.holes[p.id];
          hole = '<div class="' + hc + '">' + p.hole.map((c, ci) => cardHTML(c, 'small ' + (hset.has(ci) ? 'used' : 'fade'))).join('') + '</div>';
        } else {
          hole = '<div class="' + hc + '">' + p.hole.map(c => cardHTML(c, 'small')).join('') + '</div>';
        }
      } else if (p.state !== 'waiting') {
        const n = p.n || (st.settings.game === 0 ? 2 : st.settings.cards);
        hole = '<div class="' + hcls(n) + '">' + new Array(n).fill(0).map(() => cardHTML(null, 'small')).join('') + '</div>';
      }
      const winnings = winner ? '<div class="winnings">+' + amt(winner.won) + '</div>' : '';
      const eq = (p.eq !== undefined && p.eq !== null) ? '<div class="eqbadge">' + p.eq.toFixed(1) + '% win</div>' : '';
      html = '<div class="seat-box">' +
        '<div class="name">' + esc(p.name) + (isMe ? ' (you)' : '') + '</div>' +
        '<div class="stack">' + amt(p.stack) + '</div>' +
        (badges.length ? '<div class="badges">' + badges.join('') + '</div>' : '') +
        hole + state + eq + winnings + '</div>';
    } else {
      html = '<div class="seat-box"><div class="emptytxt">empty</div><div class="emptytxt">seat ' + (i + 1) + '</div></div>';
    }
    if (el.innerHTML !== html) el.innerHTML = html;
    el.className = 'seat' + (p && p.state === 'folded' ? ' folded' : '') +
      (p && st.hand.toAct === p.id && (st.hand.phase === 'betting' || st.hand.phase === 'discard') ? ' active' : '') +
      (p && isMe ? ' me' : '') +
      (p && winner ? ' winner' : '') +
      (!p ? ' free' : '');
    el.onclick = p ? null : () => onSeatClick(i);
  }
  const chipSeen = new Set();
  st.players.forEach(p => {
    if (p.seat < 0 || p.bet <= 0) return;
    chipSeen.add(p.id);
    let el = box.querySelector('[data-betchip="' + p.id + '"]');
    if (!el) {
      el = document.createElement('div');
      el.className = 'betchip';
      el.dataset.betchip = p.id;
      box.appendChild(el);
    }
    const sp = SEAT_POS[p.seat];
    const dx = sp.x - 50, dy = sp.y - 50;
    const len = Math.max(0.0001, Math.hypot(dx, dy));
    el.style.left = (sp.x - (dx / len) * 24) + '%';
    el.style.top = (sp.y - (dy / len) * 24) + '%';
    const chips = p.bet / 100;
    let txt;
    if (S.bbMode) {
      const bbv = (st.hand && st.hand.bb) || st.settings.bb || 100;
      const v = p.bet / bbv;
      txt = v.toFixed(2).replace(/\.?0+$/, '') + 'bb';
    } else {
      txt = chips.toFixed(2).replace(/\.?0+$/, '');
    }
    if (el.textContent !== txt) el.textContent = txt;
  });
  box.querySelectorAll('[data-betchip]').forEach(el => {
    if (!chipSeen.has(parseInt(el.dataset.betchip))) el.remove();
  });
  let puck = box.querySelector('.dealerpuck');
  if (!puck) {
    puck = document.createElement('div');
    puck.className = 'dealerpuck';
    box.appendChild(puck);
  }
  if (st.hand.dealer >= 0) {
    const sp = SEAT_POS[st.hand.dealer];
    const dx = sp.x - 50, dy = sp.y - 50;
    const a = 0.15;
    const rx = dx * Math.cos(a) - dy * Math.sin(a);
    const ry = dx * Math.sin(a) + dy * Math.cos(a);
    puck.style.left = (50 + rx * 0.5) + '%';
    puck.style.top = (50 + ry * 0.5) + '%';
    puck.hidden = false;
  } else {
    puck.hidden = true;
  }
  updateClocks();
}

function onSeatClick(seat) {
  const me = S.st && S.st.me;
  if (!me) return;
  if (me.seated) {
    const mine = S.st.players.find(p => p.id === me.id);
    if (mine && mine.state !== 'waiting') { toast('You cannot change seats during a hand'); return; }
    send({ t: 'move', seat });
    return;
  }
  if (me.pending) { toast('Your seat request is already waiting for approval'); return; }
  S.sitSeat = seat;
  $('#sit-seat').textContent = seat + 1;
  $('#sit-amt').value = (S.myStack || 100000) / 100;
  $('#sit-modal').hidden = false;
}

function winningCombo(hole, board, set) {
  const sd = !!set.shortdeck;
  const holeC = hole.map(cardParse);
  const boardC = board.map(cardParse);
  let best = null;
  const cands = [];
  if (set.game === 0) {
    if (set.greek) {
      combos(boardC, 3).forEach(b3 => cands.push({
        cards: [...holeC, ...b3],
        hidx: [0, 1],
        bidx: b3.map(c => boardC.indexOf(c))
      }));
    } else {
      combos([...holeC, ...boardC], 5).forEach(c5 => cands.push({
        cards: c5,
        hidx: holeC.map((hc, i) => c5.includes(hc) ? i : -1).filter(i => i >= 0),
        bidx: boardC.map((bc, i) => c5.includes(bc) ? i : -1).filter(i => i >= 0)
      }));
    }
  } else {
    combos(holeC, 2).forEach(h2 => combos(boardC, 3).forEach(b3 => cands.push({
      cards: [...h2, ...b3],
      hidx: h2.map(c => holeC.indexOf(c)),
      bidx: b3.map(c => boardC.indexOf(c))
    })));
  }
  cands.forEach(cd => {
    const r = eval5Rank(cd.cards, sd);
    if (!best || cmpKey(r.key, best.key) > 0) best = { key: r.key, cat: r.cat, bidx: cd.bidx, hidx: cd.hidx };
  });
  return best;
}

function showdownMarks(st) {
  const h = st.hand;
  const revealing = h.phase === 'over' || h.phase === 'showdown';
  if (!revealing) return null;
  const shown = st.players.filter(p => (p.state === 'in' || p.state === 'allin') && p.hole && p.hole.length);
  if (!shown.length || shown.some(p => !p.hole)) return null;
  const marks = { boards: [], holes: {}, winners: new Set() };
  const boards = [h.board];
  if (h.board2 && h.board2.length) boards.push(h.board2);
  boards.forEach(board => {
    if (!board || !board.length) { marks.boards.push(new Set()); return; }
    const evals = shown.map(p => ({ p, w: winningCombo(p.hole, board, st.settings) }));
    let maxKey = null;
    evals.forEach(e => { if (!maxKey || cmpKey(e.w.key, maxKey) > 0) maxKey = e.w.key; });
    const winners = evals.filter(e => cmpKey(e.w.key, maxKey) === 0);
    const used = new Set();
    winners.forEach(e => {
      marks.winners.add(e.p.id);
      e.w.bidx.forEach(i => used.add(i));
      if (!marks.holes[e.p.id]) marks.holes[e.p.id] = new Set();
      e.w.hidx.forEach(i => marks.holes[e.p.id].add(i));
    });
    marks.boards.push(used);
  });
  return marks;
}

function renderBoard(st) {
  const h = st.hand;
  const bc = $('#board-cards'), bc2 = $('#board-cards2');
  if (h.phase === 'idle' && !h.board.length) {
    $('#board-note').textContent = h.id ? '' : 'waiting for players...';
  } else if (h.phase === 'over') {
    $('#board-note').textContent = 'hand over';
  } else {
    $('#board-note').textContent = '';
  }
  const marks = showdownMarks(st);
  const usedA = marks && marks.boards[0] ? marks.boards[0] : new Set();
  const usedB = marks && marks.boards[1] ? marks.boards[1] : new Set();
  const cls = (i, used) => marks ? (used.has(i) ? 'used' : 'fade') : '';
  bc.innerHTML = h.board.map((c, i) => cardHTML(c, cls(i, usedA))).join('');
  bc2.innerHTML = h.board2 ? h.board2.map((c, i) => cardHTML(c, cls(i, usedB))).join('') : '';
  const rb = $('#board-rabbit');
  if (rb) {
    rb.innerHTML = (h.rabbit && h.rabbit.length)
      ? '<span class="rabbitlbl">would have come:</span> ' + h.rabbit.map(c => cardHTML(c, 'small')).join('')
      : '';
  }
  $('#pot').textContent = h.pot > 0 ? 'Pot: ' + amt(h.pot) : (h.phase === 'idle' ? '' : 'Pot: 0');
}

function renderAction(st) {
  const bar = $('#actionbar');
  const me = st.me;
  if (me && me.straddleChoice) {
    bar.hidden = false;
    bar.innerHTML = '<span class="lbl">Post a straddle (2 BB) before the deal?</span>' +
      '<button class="primary" id="st-yes">Straddle</button>' +
      '<button id="st-no">No</button>';
    $('#st-yes').onclick = () => send({ t: 'straddle', v: true });
    $('#st-no').onclick = () => send({ t: 'straddle', v: false });
    return;
  }
  if (me && me.showChoice) {
    bar.hidden = false;
    bar.innerHTML = '<span class="lbl">Everyone folded to you — show your hand?</span>' +
      '<button class="primary" id="b-show">Show cards</button>' +
      '<button id="b-muck">Muck</button>' +
      (st.hand && st.hand.canRabbit ? '<button id="b-rabbit">Rabbit hunt</button>' : '');
    $('#b-show').onclick = () => send({ t: 'show' });
    $('#b-muck').onclick = () => send({ t: 'muck' });
    const rb = $('#b-rabbit');
    if (rb) rb.onclick = () => send({ t: 'rabbit' });
    return;
  }
  if (!me || !me.myTurn || (st.hand.phase !== 'betting' && st.hand.phase !== 'discard')) {
    S.betMenu = false;
    bar.hidden = true;
    return;
  }
  bar.hidden = false;
  if (st.hand.phase === 'discard') {
    S.betMenu = false;
    bar.innerHTML = '<span class="lbl">Select ' + me.discard + ' card' + (me.discard > 1 ? 's' : '') + ' to discard</span>' +
      '<button class="primary" id="btn-discard" disabled>Discard</button>';
    $('#btn-discard').onclick = sendDiscard;
    updateDiscardBtns(st);
    return;
  }
  const canCheck = me.canCheck && me.toCall === 0;
  const canBet = me.stack > 0 && me.maxRaise >= me.minRaise;
  const defaultBet = Math.min(me.maxRaise, Math.max(me.minRaise, me.toCall * 2 + st.hand.bb));
  if (S.betMenu && canBet) {
    const min = me.minRaise, max = me.maxRaise;
    const bb = st.hand.bb || 1;
    const q = [
      ['Min', min, true],
      ['½ Pot', me.halfPot, me.halfPot >= min && me.halfPot < max],
      ['¾ Pot', me.threePot, me.threePot >= min && me.threePot < max],
      ['Pot', me.fullPot, me.fullPot >= min && me.fullPot < max],
      ['All-in', max, true]
    ];
    const cur = S.betAmt && S.betAmt >= min && S.betAmt <= max ? S.betAmt : defaultBet;
    bar.innerHTML = '<div class="betmenu">' +
      '<div class="qrow">' +
      q.map(x => '<button class="qbet" data-q="' + x[1] + '"' + (x[2] ? '' : ' disabled') + '>' + x[0] + '</button>').join('') +
      '<button class="primary" id="bm-bet">Bet ' + amt(cur) + '</button>' +
      '<button id="bm-back">← Back</button>' +
      '</div>' +
      '<div class="srow">' +
      '<button class="stepbtn" id="bm-minus">−</button>' +
      '<input type="number" id="bm-amt" min="' + (min / 100) + '" max="' + (max / 100) + '" step="0.01" value="' + (cur / 100) + '">' +
      '<button class="stepbtn" id="bm-plus">+</button>' +
      '<input type="range" id="bm-slider" min="' + min + '" max="' + max + '" step="1" value="' + cur + '">' +
      '</div>' +
      '<div class="bbl" id="bm-bb"></div>' +
      '</div>';
    const bbTxt = n => {
      const v = n / bb;
      return '= ' + (Math.abs(v - Math.round(v)) < 0.001 ? Math.round(v) : v.toFixed(2).replace(/0$/, '')) + ' BB';
    };
    const setAmt = v => {
      let n = Math.round(v);
      if (isNaN(n)) n = min;
      if (n > max) n = max;
      if (n < min) n = min;
      S.betAmt = n;
      $('#bm-amt').value = n / 100;
      const sl = $('#bm-slider');
      if (sl) sl.value = n;
      $('#bm-bb').textContent = bbTxt(n);
      $('#bm-bet').textContent = 'Bet ' + amt(n);
    };
    $('#bm-slider').oninput = e => setAmt(parseInt(e.target.value));
    $('#bm-amt').oninput = e => setAmt(toCents(e.target.value));
    $('#bm-minus').onclick = () => setAmt((S.betAmt || cur) - bb);
    $('#bm-plus').onclick = () => setAmt((S.betAmt || cur) + bb);
    bar.querySelectorAll('.qbet').forEach(b => b.onclick = () => { if (!b.disabled) setAmt(parseInt(b.dataset.q)); });
    $('#bm-back').onclick = () => { S.betMenu = false; renderAction(st); };
    $('#bm-bet').onclick = () => { S.betMenu = false; send({ t: 'act', a: 'bet', amt: S.betAmt }); };
    setAmt(cur);
    return;
  }
  let html = '';
  html += '<button class="danger" id="b-fold">Fold</button>';
  if (canCheck) html += '<button class="primary" id="b-check">Check</button>';
  else html += '<button class="primary" id="b-call">Call ' + amt(me.toCall) + '</button>';
  html += '<button class="primary" id="b-bet"' + (canBet ? '' : ' disabled') + '>' + (me.toCall > 0 ? 'Raise' : 'Bet') + '</button>';
  bar.innerHTML = html;
  const bind = (id, fn) => { const el = $(id); if (el) el.onclick = fn; };
  bind('#b-fold', () => {
    if (canCheck) $('#fold-modal').hidden = false;
    else send({ t: 'act', a: 'fold' });
  });
  bind('#b-check', () => send({ t: 'act', a: 'check' }));
  bind('#b-call', () => send({ t: 'act', a: 'call' }));
  bind('#b-bet', () => {
    if (!canBet) return;
    S.betMenu = true;
    S.betAmt = defaultBet;
    renderAction(st);
  });
}

function clockTxt(me) {
  if (!me.deadline) return '';
  const left = Math.max(0, Math.round((me.deadline - Date.now()) / 1000));
  return (me.phase === 'bank' ? 'TIME BANK ' : '') + left + 's';
}

function renderOverlay(st) {
  const ov = $('#overlay');
  const banner = $('#spectator-banner');
  banner.hidden = true;
  const me = st.me;
  const setBox = html => {
    if (S.ovMode !== html) {
      S.ovMode = html;
      ov.querySelector('.ovbox').innerHTML = html;
    }
  };
  if (S.reconnect) {
    setBox('<h2>Disconnected</h2><p>The connection to the table was lost.</p>' +
      '<button class="primary" onclick="location.reload()">Reconnect</button>');
    ov.hidden = false;
    return;
  }
  if (!me) { setBox('<h2>Connecting...</h2>'); ov.hidden = false; return; }
  if (me.seated) {
    const mep = st.players.find(p => p.id === me.id) || {};
    const live = st.hand.phase === 'betting' || st.hand.phase === 'discard' || st.hand.phase === 'run2' || st.hand.phase === 'showdown';
    const inHandNow = live && (mep.state === 'in' || mep.state === 'allin');
    const inOverPause = st.hand.phase === 'over' && st.hand.nextDeal && Date.now() < st.hand.nextDeal;
    const prompt = me.stack <= 0 && !inHandNow && !inOverPause && !me.host;
    if (prompt) {
      const pendRebuy = (st.pending || []).some(r => r.type === 1 && r.name === me.name);
      if (pendRebuy) {
        setBox('<h2>You are out of chips</h2><p>Your rebuy request is waiting for the host to approve.</p>');
        ov.hidden = false;
      } else {
        if (S.ovMode !== 'rebuy') {
          S.ovMode = 'rebuy';
          ov.querySelector('.ovbox').innerHTML = '<h2>You are out of chips</h2>' +
            '<p>Request a rebuy and the host will add chips to your stack.</p>' +
            '<div class="row2"><input id="rb-amt" type="number" min="0.01" step="0.01" value="' + st.settings.bb + '"><button class="primary" id="rb-go">Request rebuy</button></div>';
          const go = $('#rb-go');
          if (go) go.onclick = () => { send({ t: 'rebuy', amt: toCents($('#rb-amt').value) }); $('#overlay').hidden = true; };
        }
        ov.hidden = false;
      }
    } else {
      S.ovMode = null;
      ov.hidden = true;
    }
    return;
  }
  S.ovMode = null;
  ov.hidden = true;
  banner.hidden = false;
  if (me.pending) {
    banner.textContent = 'Seat request sent — waiting for the host to approve. You can watch and chat meanwhile.';
  } else {
    banner.textContent = 'You are watching. Click an empty seat at the table to request a seat.';
  }
}

function renderRun2(st) {
  const r2 = st.hand.run2;
  const me = st.me;
  const modal = $('#run2-modal');
  if (r2 && me && me.run2Asked && !me.run2Answered) {
    modal.hidden = false;
    $('#r2-yes').onclick = () => { send({ t: 'run2', ans: true }); modal.hidden = true; };
    $('#r2-no').onclick = () => { send({ t: 'run2', ans: false }); modal.hidden = true; };
  } else {
    modal.hidden = true;
  }
}

function updateClocks() {
  const st = S.st;
  if (!st) return;
  document.querySelectorAll('[data-clock]').forEach(el => {
    const p = st.players.find(x => x.id === parseInt(el.dataset.clock));
    const turn = st.turns.find(t => t.p === p && p.id);
    if (turn) el.textContent = (turn.phase === 'bank' ? 'TB ' : '') + Math.max(0, Math.round((turn.deadline - Date.now()) / 1000)) + 's';
  });
  document.querySelectorAll('[data-tbar]').forEach(el => {
    const p = st.players.find(x => x.id === parseInt(el.dataset.tbar));
    const turn = st.turns.find(t => t.p === p && p.id);
    if (turn) fillBar(el, turn.deadline, turn.window, turn.phase);
  });
  const mine = document.querySelector('#my-timebar');
  const myclock = document.querySelector('[data-mine]');
  if (mine && st.me && st.me.deadline) {
    fillBar(mine, st.me.deadline, st.me.window, st.me.phase);
    if (myclock) myclock.textContent = clockTxt(st.me);
  }
  if (st.me && st.me.deadline) {
    const left = (st.me.deadline - Date.now()) / 1000;
    if (left <= 5 && S.alertDeadline !== st.me.deadline) {
      S.alertDeadline = st.me.deadline;
      playSfx('alert');
    }
  } else {
    S.alertDeadline = 0;
  }
  const r2 = st.hand.run2;
  if (r2 && !$('#run2-modal').hidden) {
    $('#r2-countdown').textContent = Math.max(0, Math.round((r2.deadline - Date.now()) / 1000)) + 's to decide';
  }
}

function fillBar(el, deadline, windowMs, phase) {
  const win = windowMs || 1;
  const pct = Math.max(0, Math.min(100, (deadline - Date.now()) / win * 100));
  el.classList.toggle('bank', phase === 'bank');
  const fill = el.querySelector('.timefill');
  if (fill) fill.style.width = pct + '%';
}

function updateDiscardBtns(st) {
  const me = st.me;
  const n = me.discard;
  S.discSel.clear();
  const cards = document.querySelectorAll('#seats .me .card.selectable');
  cards.forEach(c => c.classList.remove('selected'));
  if (me.cards) {
    const hole = document.querySelectorAll('#seats .me .card');
    hole.forEach((c, i) => {
      c.classList.add('selectable');
      c.onclick = () => {
        const idx = i;
        if (S.discSel.has(idx)) S.discSel.delete(idx);
        else if (S.discSel.size < n) S.discSel.add(idx);
        hole.forEach((cc, j) => cc.classList.toggle('selected', S.discSel.has(j)));
        $('#btn-discard').disabled = S.discSel.size !== n;
      };
    });
  }
}

function sendDiscard() {
  send({ t: 'discard', idx: [...S.discSel] });
  S.discSel.clear();
}

function esc(s) {
  return String(s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function appendChat(m) {
  if (!S.st) return;
  S.st.chat.push({ name: m.name, m: m.m });
  if (S.st.chat.length > 50) S.st.chat.shift();
  renderChatLog();
}

function renderChatLog() {
  const log = $('#chat-log');
  if (!S.st) return;
  const msgs = S.st.chat || [];
  const key = msgs.length + ':' + (msgs.length ? msgs[msgs.length - 1].m : '');
  if (log.dataset.key === key) return;
  log.dataset.key = key;
  log.innerHTML = msgs.map(c =>
    c.name === 'system'
      ? '<div><span class="csys">' + esc(c.m) + '</span></div>'
      : '<div><span class="cname">' + esc(c.name) + ':</span> ' + esc(c.m) + '</div>'
  ).join('');
  log.scrollTop = log.scrollHeight;
}

function openPanel(tab) {
  $('#sidepanel').hidden = false;
  S.tab = tab;
  $('#sidepanel').classList.toggle('host-wide', tab === 'host' && !!(S.me && S.me.host));
  document.querySelectorAll('.tabbtn').forEach(b => b.classList.toggle('on', b.dataset.tab === tab));
  ['host', 'info', 'ledger', 'history'].forEach(t => { $('#tab-' + t).hidden = t !== tab; });
  renderSide(S.st);
}

function closePanel() { $('#sidepanel').hidden = true; }

function renderSide(st) {
  if (!st || $('#sidepanel').hidden) return;
  const isHost = !!(S.me && S.me.host);
  if (S.tab === 'host' && !isHost) S.tab = 'info';
  $('#sidepanel').classList.toggle('host-wide', S.tab === 'host' && isHost);
  if (S.tab === 'info') renderInfoTab(st);
  if (S.tab === 'host' && isHost) renderHostTab(st);
  if (S.tab === 'ledger') renderLedger(st);
  if (S.tab === 'history') renderHistory();
}

function renderInfoTab(st) {
  const set = st.settings;
  const host = st.players.find(p => p.id === st.host);
  const box = $('#tab-info');
  box.innerHTML =
    '<h3>Game info</h3>' +
    '<div class="handrow"><div class="htitle">' + esc(set.v) + '</div>' +
    '<div>Blinds ' + amt(set.sb) + '/' + amt(set.bb) + ' · Decision clock ' + set.sec + 's · Run it twice: ' + (set.runTwice ? 'allowed' : 'off') + '</div>' +
    '<div>Reveal all hands when no more action is possible: ' + (set.revealAllIn ? 'on' : 'off') + '</div>' +
    '<div>Host: ' + esc(host ? host.name : '—') + ' · ' + st.seatedCount + '/' + st.maxPlayers + ' seated</div>' +
    '<div>' + (set.paused ? 'The game is paused' : 'The game is live') + '</div></div>' +
    '<h3>Variants available</h3>' +
    '<div class="handrow">Hold\'em: No Limit / Pot Limit · Greek · Pineapple · Irish · Shortdeck · 2-6 cards<br>' +
    'Omaha: No Limit / Pot Limit · Pineapple · Irish · Shortdeck · Hi/Lo · 3-6 cards<br>' +
    'Other: No river · Extra river</div>' +
    '<h3>How to play</h3>' +
    '<div class="handrow">Request a seat with your buy-in and the host will approve it. ' +
    'When your decision clock runs out your time bank takes over; an empty time bank folds your hand. ' +
    'Rebuy when you run out of chips.</div>';
}

function segGroup(parent, name, options, current, onChange) {
  const div = document.createElement('div');
  div.className = 'seg';
  options.forEach(o => {
    const b = document.createElement('button');
    b.textContent = o.label;
    b.classList.toggle('on', o.val === current);
    b.onclick = () => { div.querySelectorAll('button').forEach(x => x.classList.remove('on')); b.classList.add('on'); onChange(o.val); };
    div.appendChild(b);
  });
  parent.appendChild(div);
}

function renderHostTab(st) {
  const live = st.settings;
  const set = st.pendingSettings || st.settings;
  const box = $('#tab-host');
  const settings = { ...set };
  let html = '';

  html += '<h3>Blinds</h3><div class="optrow">SB <input class="numfield" id="f-sb" value="' + (set.sb / 100) + '"> BB <input class="numfield" id="f-bb" value="' + (set.bb / 100) + '"></div>';
  html += '<h3>Timing</h3><div class="optrow">Decision time <select class="numfield" id="f-sec" style="width:110px">' +
    [5, 10, 15, 20, 30, 45, 60, 90].map(s => '<option' + (s === set.sec ? ' selected' : '') + '>' + s + 's</option>').join('') +
    '</select></div>';
  html += '<h3>Game</h3>';
  html += '<div class="optrow" id="seg-game"></div>';
  html += '<div class="optrow" id="seg-bet"></div>';
  html += '<div id="seg-cards-wrap" class="optrow"></div>';

  html += '<div id="sec-holdem" ' + (set.game !== 0 ? 'hidden' : '') + '>';
  html += '<h3>Hold\'em</h3>';
  html += '<div class="optrow"><input type="checkbox" id="f-greek" ' + (set.greek ? 'checked' : '') + '> Greek (must use both hole cards)</div>';
  html += '</div>';

  html += '<div id="sec-omaha" ' + (set.game !== 1 ? 'hidden' : '') + '>';
  html += '<h3>Omaha</h3>';
  html += '<div class="optrow"><input type="checkbox" id="f-hilo" ' + (set.hilo ? 'checked' : '') + '> Hi/Lo (8-or-better)</div>';
  html += '</div>';

  html += '<h3>Discard</h3><div id="sec-discard">';
  html += '<div class="optrow"><input type="checkbox" id="f-pineapple" ' + (set.pineapple ? 'checked' : '') + '> Pineapple (discard after preflop)</div>';
  html += '<div class="optrow"><input type="checkbox" id="f-irish" ' + (set.irish ? 'checked' : '') + '> Irish (discard after flop)</div>';
  html += '</div>';

  html += '<h3>Deck</h3>';
  html += '<div class="optrow"><input type="checkbox" id="f-shortdeck" ' + (set.shortdeck ? 'checked' : '') + '> Shortdeck (2-5 removed, flush beats full house)</div>';
  html += '<h3>Board structure</h3>';
  html += '<div class="optrow"><input type="checkbox" id="f-noriver" ' + (set.noRiver ? 'checked' : '') + '> No river (showdown on turn)</div>';
  html += '<div class="optrow"><input type="checkbox" id="f-extrariver" ' + (set.extraRiver ? 'checked' : '') + '> Extra river (6th board card + betting)</div>';
  html += '<h3>Options</h3>';
  html += '<div class="optrow"><input type="checkbox" id="f-runtwice" ' + (set.runTwice ? 'checked' : '') + '> Allow running it twice</div>';
  html += '<div class="optrow"><input type="checkbox" id="f-revealallin" ' + (set.revealAllIn ? 'checked' : '') + '> Reveal all hands when no more action is possible</div>';
  html += '<h3>Side games</h3>';
  html += '<div class="optrow" id="row-game27"' + (set.game !== 0 ? ' hidden' : '') + '><input type="checkbox" id="f-game27" ' + (set.game27 ? 'checked' : '') + '> 2-7 game — value <input class="numfield" id="f-value27" value="' + (set.value27 / 100) + '"></div>';
  html += '<div class="optrow"><input type="checkbox" id="f-nit" ' + (set.nitGame ? 'checked' : '') + '> Nit game — value <input class="numfield" id="f-nitvalue" value="' + (set.nitValue / 100) + '"></div>';
  html += '<div class="optrow"><input type="checkbox" id="f-straddle" ' + (set.straddle ? 'checked' : '') + '> UTG straddle (2 BB)</div>';
  html += '<div class="optrow">Max players for variant: <b>' + st.maxPlayers + '</b></div>';

  html += '<div class="row2"><button class="primary" id="f-apply">Apply settings</button>' +
    '<button id="f-pause">' + (live.paused ? 'Resume game' : 'Pause game') + '</button></div>';
  if (st.pendingSettings) html += '<div class="hint">Pending changes apply from the next hand.</div>';

  html += '<h3>Seat requests</h3><div id="reqlist">' +
    (st.pending.length ? st.pending.map(r =>
      '<div class="reqrow"><span class="who">' + esc(r.name) + ' — ' + amt(r.stack) + (r.type === 1 ? ' (chips request)' : '') + '</span>' +
      '<button class="primary small" data-approve="' + r.id + '">Approve</button>' +
      '<button class="danger small" data-reject="' + r.id + '">Reject</button></div>').join('')
      : '<div class="muted">no pending requests</div>') + '</div>';

  html += '<h3>Players</h3><div class="hint">Use Add for chip adjustments, Set for an exact stack, and “Make host” to transfer host permissions.</div><div id="plist">' +
    st.players.filter(p => p.seated).map(p =>
      '<div class="reqrow"><span class="who">' + esc(p.name) + ' — ' + amt(p.stack) + '</span>' +
      '<input class="numfield" id="ch-' + p.id + '" placeholder="+ chips" style="width:80px">' +
      '<button class="small" data-chips="' + p.id + '">Add</button>' +
      '<button class="small" data-setstack="' + p.id + '">Set</button>' +
      (p.host || !p.connected ? '' : '<button class="small" data-host="' + p.id + '">Make host</button>') +
      (p.host ? '' : '<button class="danger small" data-remove="' + p.id + '">Kick</button>') +
      '</div>').join('') + '</div>';

  box.innerHTML = html;

  function renderCards(s) {
    const wrap = $('#seg-cards-wrap');
    const opts = s.game === 0 ? [2, 3, 4, 5, 6] : [3, 4, 5, 6];
    if (!opts.includes(s.cards)) s.cards = opts[0];
    wrap.innerHTML = '';
    const sel = document.createElement('div');
    sel.className = 'seg';
    wrap.appendChild(sel);
    segGroup(sel, 'cards', opts.map(c => ({ label: c + ' cards', val: c })), s.cards, v => {
      s.cards = v;
      syncHostForm(s);
    });
  }

  function syncHostForm(s) {
    if (s.game === 1 && s.cards < 3) s.cards = 3;
    if (s.cards < 3) { s.pineapple = false; s.irish = false; }
    if (s.game === 0) s.hilo = false;
    if (s.cards !== 2) s.greek = false;
    $('#f-greek').checked = s.greek;
    $('#f-greek').disabled = s.cards !== 2;
    $('#f-pineapple').checked = s.pineapple;
    $('#f-irish').checked = s.irish;
    $('#f-shortdeck').checked = s.shortdeck;
    $('#f-hilo').checked = s.hilo;
    $('#f-noriver').checked = s.noRiver;
    $('#f-extrariver').checked = s.extraRiver;
    $('#f-runtwice').checked = s.runTwice;
    $('#f-revealallin').checked = s.revealAllIn;
    $('#f-straddle').checked = s.straddle;
    $('#sec-discard').hidden = s.cards < 3;
    $('#sec-holdem').hidden = s.game !== 0;
    $('#sec-omaha').hidden = s.game !== 1;
    $('#row-game27').hidden = s.game !== 0;
  }

  segGroup($('#seg-game'), 'game', [{ label: 'Hold\'em', val: 0 }, { label: 'Omaha', val: 1 }], settings.game, v => {
    settings.game = v;
    renderCards(settings);
    syncHostForm(settings);
  });
  segGroup($('#seg-bet'), 'bet', [{ label: 'No Limit', val: 0 }, { label: 'Pot Limit', val: 1 }], settings.bet, v => { settings.bet = v; });
  renderCards(settings);
  syncHostForm(settings);

  $('#f-pineapple').onchange = () => {
    settings.pineapple = $('#f-pineapple').checked;
    if (settings.pineapple) settings.irish = false;
    $('#f-irish').checked = settings.irish;
  };
  $('#f-irish').onchange = () => {
    settings.irish = $('#f-irish').checked;
    if (settings.irish) settings.pineapple = false;
    $('#f-pineapple').checked = settings.pineapple;
  };
  $('#f-noriver').onchange = () => {
    settings.noRiver = $('#f-noriver').checked;
    if (settings.noRiver) settings.extraRiver = false;
    $('#f-extrariver').checked = settings.extraRiver;
  };
  $('#f-extrariver').onchange = () => {
    settings.extraRiver = $('#f-extrariver').checked;
    if (settings.extraRiver) settings.noRiver = false;
    $('#f-noriver').checked = settings.noRiver;
  };

  $('#f-apply').onclick = () => {
    send({
      t: 'set',
      sb: toCents($('#f-sb').value) || 100,
      bb: toCents($('#f-bb').value) || 200,
      sec: parseInt($('#f-sec').value) || 20,
      game: settings.game,
      bet: settings.bet,
      cards: settings.cards,
      greek: $('#f-greek').checked,
      pineapple: $('#f-pineapple').checked,
      irish: $('#f-irish').checked,
      shortdeck: $('#f-shortdeck').checked,
      hilo: $('#f-hilo').checked,
      noRiver: $('#f-noriver').checked,
      extraRiver: $('#f-extrariver').checked,
      runTwice: $('#f-runtwice').checked,
      revealAllIn: $('#f-revealallin').checked,
      game27: $('#f-game27').checked,
      value27: toCents($('#f-value27').value) || 100,
      nitGame: $('#f-nit').checked,
      nitValue: toCents($('#f-nitvalue').value) || 100,
      straddle: $('#f-straddle').checked,
      paused: live.paused
    });
  };
  $('#f-pause').onclick = () => {
    send({
      t: 'set', sb: set.sb, bb: set.bb, sec: set.sec, game: set.game, bet: set.bet, cards: set.cards,
      greek: set.greek, pineapple: set.pineapple, irish: set.irish, shortdeck: set.shortdeck,
      hilo: set.hilo, noRiver: set.noRiver, extraRiver: set.extraRiver, runTwice: set.runTwice,
      revealAllIn: set.revealAllIn, game27: set.game27, value27: set.value27, nitGame: set.nitGame, nitValue: set.nitValue, straddle: set.straddle,
      paused: !live.paused
    });
  };

  box.querySelectorAll('[data-approve]').forEach(b => b.onclick = () => send({ t: 'approve', id: parseInt(b.dataset.approve) }));
  box.querySelectorAll('[data-reject]').forEach(b => b.onclick = () => send({ t: 'reject', id: parseInt(b.dataset.reject) }));
  box.querySelectorAll('[data-chips]').forEach(b => b.onclick = () => {
    const amt = toCents($('#ch-' + b.dataset.chips).value);
    if (!amt) return;
    send({ t: 'chips', id: parseInt(b.dataset.chips), amt });
  });
  box.querySelectorAll('[data-setstack]').forEach(b => b.onclick = () => {
    const raw = $('#ch-' + b.dataset.setstack).value.trim();
    if (raw === '') return;
    const value = toCents(raw);
    if (value < 0) { toast('stack cannot be negative'); return; }
    send({ t: 'setstack', id: parseInt(b.dataset.setstack), amt: value });
  });
  box.querySelectorAll('[data-host]').forEach(b => b.onclick = () => send({ t: 'transfer', id: parseInt(b.dataset.host) }));
  box.querySelectorAll('[data-remove]').forEach(b => b.onclick = () => send({ t: 'remove', id: parseInt(b.dataset.remove) }));

  $('#row-game27').hidden = settings.game !== 0;
}

function renderLedger(st) {
  const box = $('#tab-ledger');
  const rows = st.ledger || [];
  box.innerHTML = '<h3>Table ledger</h3>' +
    '<table class="led"><tr><th>Player</th><th>Bought in</th><th>Stack</th><th>Won/Lost</th><th></th></tr>' +
    rows.map(r =>
      '<tr><td>' + esc(r.name) + (r.seated ? '' : ' (left)') + '</td>' +
      '<td>' + amt(r.bought) + '</td><td>' + amt(r.stack) + '</td>' +
      '<td class="' + (r.wl >= 0 ? 'pos' : 'neg') + '">' + (r.wl >= 0 ? '+' : '') + amt(r.wl) + '</td>' +
      '<td>' + (r.connected ? '' : 'offline') + '</td></tr>').join('') + '</table>';
}

function renderHistory() {
  const box = $('#tab-history');
  const streets = ['Preflop', 'Flop', 'Turn', 'River', 'Extra river'];
  box.innerHTML = '<h3>Hand history (last 500)</h3>' + (S.hist.length ? S.hist.map(h => {
    const ps = (h.players || []).map(p =>
      esc(p.name) + ' ' + (p.net >= 0 ? '+' : '') + amt(p.net) + (p.hole ? ' ' + p.hole.map(miniCardHTML).join(' ') : '')).join(' · ');
    let acts = '', lastS = -1;
    (h.actions || []).forEach(a => {
      if (a.s !== lastS) { if (acts) acts += '<br>'; acts += '<b>' + (streets[a.s] || ('Street ' + a.s)) + ':</b> '; lastS = a.s; }
      else acts += ' · ';
      acts += esc(a.name) + ' ' + esc(a.a) + (a.amt > 0 ? ' ' + amt(a.amt) : '');
    });
    return '<div class="handrow"><div class="htitle">#' + h.id + ' ' + esc(h.variant) + ' ' + amt(h.sb) + '/' + amt(h.bb) + (h.run2 ? ' (run twice)' : '') + '</div>' +
      '<div class="hcards">' + (h.board || []).map(miniCardHTML).join('') + (h.board2 ? '<span class="muted"> / </span>' + h.board2.map(miniCardHTML).join('') : '') + '</div>' +
      '<div>' + ps + '</div>' +
      (acts ? '<div class="hacts">' + acts + '</div>' : '') + '</div>';
  }).join('') : '<div class="muted">no hands yet</div>');
}

function bindUI() {
  $('#btn-create').onclick = () => {
    const name = $('#c-name').value.trim();
    const stack = toCents($('#c-stack').value);
    const sb = toCents($('#c-sb').value) || 100;
    const bb = toCents($('#c-bb').value) || 200;
    if (!name) { toast('enter a name'); return; }
    if (stack <= 0) { toast('enter a starting stack'); return; }
    fetch('/api/new').then(r => r.json()).then(j => {
      S.myName = name; S.myStack = stack;
      sessionStorage.setItem('pv_join_' + j.id, JSON.stringify({ name, stack, sb, bb }));
      sessionStorage.setItem('pv_hostkey_' + j.id, j.hostKey);
      S.pendingJoin = { id: j.id };
      location.hash = '#/t/' + j.id;
    });
  };

  $('#btn-openlink').onclick = () => {
    const link = $('#j-link').value.trim();
    const m = link.match(/\/#\/t\/([A-Za-z0-9]+)/) || link.match(/^([A-Za-z0-9]{5})$/);
    if (m) { location.hash = '#/t/' + m[1]; boot(); }
    else toast('invalid table link');
  };

  $('#btn-join').onclick = () => {
    const name = $('#jn-name').value.trim();
    const stack = toCents($('#jn-stack').value);
    if (!name) { toast('enter a name'); return; }
    if (stack <= 0) { toast('enter a buy-in stack'); return; }
    S.myName = name; S.myStack = stack;
    S.watchMode = false;
    sessionStorage.setItem('pv_join_' + S.id, JSON.stringify({ name, stack }));
    showView('table');
    connect();
  };

  $('#btn-watch').onclick = () => {
    const name = $('#jn-name').value.trim() || 'guest';
    S.myName = name; S.myStack = toCents($('#jn-stack').value) || 100000;
    S.watchMode = true;
    showView('table');
    connect();
  };

  $('#btn-copy').onclick = () => {
    const link = location.origin + '/#/t/' + S.id;
    (navigator.clipboard ? navigator.clipboard.writeText(link) : Promise.reject()).then(
      () => toast('link copied'), () => toast('link: ' + link));
  };

  $('#btn-panels').onclick = () => {
    if ($('#sidepanel').hidden) {
      S.tab = S.tab || (S.me && S.me.host ? 'ledger' : 'info');
      openPanel(S.tab);
    } else closePanel();
  };
  $('#btn-host').onclick = () => {
    if (!$('#sidepanel').hidden && S.tab === 'host') closePanel();
    else openPanel('host');
  };
  $('#btn-sound').onclick = () => {
    S.sound = !S.sound;
    $('#btn-sound').textContent = S.sound ? '🔉' : '🔇';
  };
  $('#btn-pause').onclick = () => {
    if (S.st) send({ t: 'set', paused: !S.st.settings.paused });
  };
  document.querySelectorAll('.tabbtn').forEach(b => b.onclick = () => openPanel(b.dataset.tab));

  $('#chat-input').addEventListener('keydown', e => {
    if (e.key === 'Enter') {
      const v = e.target.value.trim();
      if (v) send({ t: 'chat', m: v });
      e.target.value = '';
    }
  });

  $('#btn-chat-ledger').onclick = () => openPanel('ledger');
  $('#btn-chat-history').onclick = () => openPanel('history');

  $('#sit-go').onclick = () => {
    const amt = toCents($('#sit-amt').value) || 100000;
    send({ t: 'join', name: S.myName || 'guest', stack: amt, seat: S.sitSeat });
    S.myStack = amt;
    $('#sit-modal').hidden = true;
  };
  $('#sit-cancel').onclick = () => { $('#sit-modal').hidden = true; };

  $('#btn-stand').onclick = () => send({ t: 'stand' });

  $('#btn-bb').onclick = () => { S.bbMode = !S.bbMode; render(); };
  $('#btn-sitout').onclick = () => {
    if (!S.st || !S.st.me) return;
    send({ t: 'sitout', v: !S.st.me.sitout });
  };
  $('#btn-chips').onclick = () => {    $('#chips-amt').value = (S.myStack || 100000) / 100;
    $('#chips-modal').hidden = false;
  };
  $('#chips-go').onclick = () => {
    send({ t: 'rebuy', amt: toCents($('#chips-amt').value) });
    $('#chips-modal').hidden = true;
  };
  $('#chips-cancel').onclick = () => { $('#chips-modal').hidden = true; };

  $('#fold-check').onclick = () => { $('#fold-modal').hidden = true; send({ t: 'act', a: 'check' }); };
  $('#fold-go').onclick = () => { $('#fold-modal').hidden = true; send({ t: 'act', a: 'fold' }); };
}

window.addEventListener('hashchange', () => { boot(); });

setInterval(() => {
  updateClocks();
  if (S.st) renderOverlay(S.st);
}, 200);

bindUI();
boot();
