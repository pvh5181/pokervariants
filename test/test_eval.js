const fs = require('fs');
const src = fs.readFileSync(__dirname + '/../public/app.js', 'utf8');
const a = src.indexOf('function cardParse(');
const b = src.indexOf('function renderBoard(');
if (a < 0 || b < 0 || b <= a) throw new Error('could not locate evaluator functions');
eval(src.slice(a, b));

let fails = 0;
function check(name, got, want) {
  if (JSON.stringify(got) !== JSON.stringify(want)) {
    console.error('FAIL', name, 'got', JSON.stringify(got), 'want', JSON.stringify(want));
    fails++;
  }
}

function mk(cards) { return cards.map(c => [ {T:10,J:11,Q:12,K:13,A:14}[c[0]] || parseInt(c[0]), c[1] ]); }

check('royal', eval5Rank(mk(['Ah','Kh','Qh','Jh','Th']), false).cat, 8);
check('quads', eval5Rank(mk(['As','Ad','Ah','Ac','2s']), false).cat, 7);
check('fh', eval5Rank(mk(['Ah','Ad','Ac','Kh','Kd']), false).cat, 6);
check('flush', eval5Rank(mk(['Ah','8h','5h','3h','2h']), false).cat, 5);
check('straight', eval5Rank(mk(['9s','8d','7c','6h','5s']), false).cat, 4);
check('wheel', eval5Rank(mk(['As','2d','3c','4h','5s']), false).cat, 4);
check('two pair', eval5Rank(mk(['As','Ad','Kc','Kh','2s']), false).cat, 2);
check('pair', eval5Rank(mk(['As','Ad','Kc','Qh','2s']), false).cat, 1);
check('high', eval5Rank(mk(['As','9d','7c','5h','2s']), false).cat, 0);
check('sd flush beats fh', eval5Rank(mk(['Ah','8h','5h','3h','2h']), true).cat, 6);
check('sd fh', eval5Rank(mk(['Ah','Ad','Ac','Kh','Kd']), true).cat, 5);
check('sd a6789 straight', straightHigh5([14,9,8,7,6], true), 9);

check('holdem straight', bestHandInfo(mk(['As','Kd']), mk(['Qs','Js','Ts','2c','3c']), {game:0}).cat, 4);
check('greek straight', bestHandInfo(mk(['As','Kd']), mk(['Qs','Js','Ts','2c','3c']), {game:0, greek:true}).cat, 4);
check('greek no pair only', bestHandInfo(mk(['As','Kd']), mk(['Qh','7h','3h','2c','9c']), {game:0, greek:true}).cat, 0);
check('omaha straight', bestHandInfo(mk(['As','Kd','2c','3c']), mk(['Qs','Js','Ts','9c','8c']), {game:1}).cat, 4);
check('omaha must use 2', bestHandInfo(mk(['As','Ad','2c','3c']), mk(['Ah','Kh','Qh','Jh','Th']), {game:1}).cat, 3);

const low = bestLowInfo(mk(['As','2d']), mk(['3c','4h','5s','6c','7c']));
check('low 5432A', low, [5,4,3,2,1]);
const lowNo = bestLowInfo(mk(['As','2d']), mk(['9c','Th','Jc','Qc','Kc']));
check('no low', lowNo, null);
check('rankChar', [rankChar(14), rankChar(10), rankChar(2)], ['A','T','2']);
check('catName sd flush', catName(6, true), 'Flush');
check('catName sd fh', catName(5, true), 'Full House');

const wc1 = winningCombo(mk(['As','Kd']), mk(['Qs','Js','Ts','2c','3c']), {game:0});
check('combo holdem bidx', [...wc1.bidx].sort(), [0,1,2]);
check('combo holdem hidx', [...wc1.hidx].sort(), [0,1]);
const wc2 = winningCombo(mk(['As','Ad','2c','3c']), mk(['Ah','Kh','Qh','Jh','Th']), {game:1});
check('combo omaha bidx', [...wc2.bidx].sort(), [0,1,2]);
const wc3 = winningCombo(mk(['As','Kd']), mk(['Qs','Js','Ts','2c','3c']), {game:0, greek:true});
check('combo greek bidx', [...wc3.bidx].sort(), [0,1,2]);
check('combo greek hidx', [...wc3.hidx].sort(), [0,1]);
const wc4 = winningCombo(mk(['As','2d']), mk(['Kh','7c','3h','9d','5c']), {game:0});
check('combo no-pair bidx', [...wc4.bidx].sort(), [0,1,3,4]);
const wc5 = winningCombo(mk(['Ac','Ah']), mk(['As','Ad','2c','2h','Ks']), {game:0});
check('quads bidx', [...wc5.bidx].sort(), [0,1,4]);
check('quads hidx', [...wc5.hidx].sort(), [0,1]);
const wc6 = winningCombo(mk(['2d','3d']), mk(['As','Ks','Qs','Js','Ts']), {game:0});
check('royal on board bidx', [...wc6.bidx].sort(), [0,1,2,3,4]);
check('royal on board hidx', wc6.hidx, []);

if (fails) { console.error(fails + ' FAILURES'); process.exit(1); }
console.log('ALL JS EVALUATOR CHECKS PASSED');
