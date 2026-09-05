// Drive Toolkit
// --------------------------------------------------------
//
// Two layers over one session, the same way the device holds it: the sampled
// interface, live from the moment the session opens and needing no disk, and the
// head calibration read as an opt-in layer on top of it.
//
// The browser is a third rendering of that session, never a copy of it. Every
// control sends a command and then waits to be told the new state, so a line
// toggled here, one toggled from the serial console and a cylinder changed on the
// joystick all land in the same place and no surface holds state of its own.

// 0 closed, 1 open and watching, 2 reading, 3 paused. One number rather than a
// running flag and a paused flag, because it is one question - what is this
// session doing - and it is exactly what the run button renders.
var dtRun = 0;
var dtSticky = false;
var dtSound = false;
var dtAuto = false;
var dtDisk = false;
var dtTainted = false;
var dtHead = 2; // 0 lower, 1 upper, 2 both
// Last reading of each line, so a toggle can send the value it wants rather than
// "flip it" - two surfaces pressing at once cannot then land on opposite states.
var dtRaw = [];
// Last pass, per side, kept so the status sentence can be composed from the same
// facts the device composes it from.
var dtSeen = [-1, -1];
var dtValid = [0, 0];
var dtTotal = [0, 0];
// Last sample, kept so the status can be recomposed when a pass lands rather than
// only when the next sample does.
var dtRpm = 0;
var dtEdges = 0;
var dtCylinder = -1;

// The eleven lines, in the order XCopyDriveToolkit::Sig declares them. The device
// sends two strings of eleven digits - a level per line and a raw reading per
// line - and this is what says which digit is which.
var DT_SIGNALS = [
  { name: 'SEL',  pin: 12, dir: 'OUT' },
  { name: 'MOT',  pin: 16, dir: 'OUT' },
  { name: 'DIR',  pin: 18, dir: 'OUT' },
  { name: 'SIDE', pin: 32, dir: 'OUT' },
  { name: 'DENS', pin:  2, dir: 'OUT' },
  { name: 'STEP', pin: 20, dir: 'OUT' },
  { name: 'IDX',  pin:  8, dir: 'IN'  },
  { name: 'RDAT', pin: 30, dir: 'IN'  },
  { name: 'T0',   pin: 26, dir: 'IN'  },
  { name: 'WP',   pin: 28, dir: 'IN'  },
  { name: 'CHG',  pin: 34, dir: 'IN'  }
];

var DT_SEL = 0, DT_MOT = 1, DT_DIR = 2, DT_SIDE = 3, DT_DENS = 4, DT_STEP = 5;
var DT_IDX = 6, DT_RDAT = 7, DT_T0 = 8, DT_WP = 9, DT_CHG = 10;

// XCopyDriveToolkit::Level, in order. Four states rather than a boolean because
// "asserted" and "good" are both true and mean different things: one is us
// driving a line, the other is the drive answering.
var DT_LEVEL_CLASS = ['rest', 'asserted', 'good', 'fault'];

// Maps a sector glyph to the class that colours it. Same vocabulary as the TFT and
// the serial console, so a photograph of any of the three reads the same way.
var DT_SECTOR_CLASS = {
  '.': 'ok',
  'X': 'missing',
  '-': 'low',
  '+': 'high',
  'h': 'side',
  'c': 'chk'
};

// The dtState flags byte. Five booleans that used to be five fields - which is
// what keeps the message to nine, and websocket.js splits every message at twelve.
var DT_F_STICKY = 0x01, DT_F_TAINTED = 0x02, DT_F_SOUND = 0x04;
var DT_F_AUTO = 0x08, DT_F_DISK = 0x10;

/*
   Mirrors XCopyDriveToolkit::stateText().

   The device sends the raw reading rather than the words, so this is the same
   mapping written twice - deliberately, the way the sector glyph vocabulary above
   is mirrored. Change one and change the other, or a photograph of the browser and
   a photograph of the console will disagree about the same drive.
*/
function dtStateText(index, raw, motorOn) {
  switch (index) {
    case DT_SEL:  return raw ? 'ASSERTED' : 'RELEASED';
    case DT_MOT:  return raw ? 'ASSERTED' : 'RELEASED';
    case DT_DIR:  return raw ? 'INWARD' : 'OUTWARD';
    case DT_SIDE: return raw ? 'HEAD 1' : 'HEAD 0';
    case DT_DENS: return raw ? 'HIGH' : 'LOW';
    case DT_STEP: return 'IDLE';
    case DT_IDX:  return raw ? 'PULSING' : (motorOn ? 'NO PULSES' : 'IDLE');
    case DT_RDAT: return raw ? 'ACTIVE' : 'QUIET';
    case DT_T0:   return raw ? 'AT CYL 0' : 'OFF CYL 0';
    case DT_WP:   return raw ? 'PROTECTED' : 'WRITABLE';
    default:      return raw ? 'DISK IN' : 'NO DISK';
  }
}

function dtRpmText(tenths) {
  var value = parseInt(tenths, 10);
  if (isNaN(value) || value <= 0) return '-- RPM';
  return (Math.floor(value / 10) + '.' + (value % 10) + ' RPM');
}

/*
   Mirrors XCopyDriveToolkit::statusText(), branch for branch and in the same
   order, for the same reason dtStateText() does.

   The interface has the first word. A drive that is not spinning has nothing to
   say about head alignment, and reporting a failed pass over a released interface
   as a calibration result would invent the diagnostic the operator is about to act
   on. Then it says the next thing to try rather than only naming the symptom:
   stepping works with the spindle stopped, so "motor on and no index" almost
   always means the drive is not taking its motor command from where we give it.
*/
function dtStatusText(raw, rpmTenths, edges, cylinder) {
  var motorOn = raw[DT_MOT];
  var rpm = parseInt(rpmTenths, 10) || 0;

  if (!motorOn) {
    return {
      text: raw[DT_SEL] ? 'Selected, motor off' : 'Outputs released',
      level: 'rest'
    };
  }
  if (rpm <= 0 && edges === 0) {
    return { text: 'MOTOR ON, no index — is the spindle actually turning?', level: 'fault' };
  }
  if (rpm <= 0) return { text: edges + ' edges, no rate yet', level: 'warn' };
  if (dtRun === 1) return { text: 'Spinning at ' + dtRpmText(rpmTenths), level: 'good' };
  if (dtRun === 3) return { text: 'Paused — nothing is being read', level: 'hold' };
  if (!dtDisk) return { text: 'No disk in drive', level: 'fault' };
  if (dtTainted) return { text: 'Lines driven — pass not counted', level: 'driven' };

  var seen = -1;
  var allGood = true;
  var any = false;
  for (var side = 0; side < 2; side++) {
    var wanted = (side === 0) ? (dtHead !== 1) : (dtHead !== 0);
    if (dtSeen[side] >= 0) seen = dtSeen[side];
    if (!wanted) continue;
    any = true;
    if (!dtTotal[side] || dtValid[side] !== dtTotal[side]) allGood = false;
  }

  if (seen >= 0 && seen !== cylinder) {
    return { text: 'Cyl ' + cylinder + ' seen as ' + seen, level: 'warn' };
  }
  if (any && allGood) return { text: 'Aligned, all sectors okay', level: 'good' };
  return { text: 'Adjust for all sectors okay', level: 'rest' };
}

// Session tally grid
// --------------------------------------------------------
//
// The same grid the Disk Copy and Test Disk tabs draw, built by the same function
// in diskcopy.js and wearing the same .track classes, so the tabs read as one
// device. It used to be a table of its own with its own cell size, its own header
// row and its own colour names, which is why it looked like it belonged to some
// other program.
//
// It is the accumulated half of the display - the sector rows above are one
// revolution, this is every pass the session has made over every cylinder the head
// has been moved to.
//
// Geometry comes from diskcopy.js so the three surfaces cannot drift apart. See the
// note there and in XCopyGeometry.h.

var dtCurrent = -1;

// The four states share the disk copy key's vocabulary rather than inventing three
// more colours: green read clean, yellow needed a retry, red never read. Untested
// is the bare cell, the same colour an untouched track has during a copy.
var DT_TALLY_CLASS = ['', 'green', 'yellow', 'red'];

function dtBuildGrid() {
  buildCylinderGrid($('#dtGrid'), {
    // No icon column: nothing is being copied from anywhere, so the grid starts at
    // the row labels.
    leadWidth: 0,
    sideLabel: function (side) { return 'Side ' + side + (side == 0 ? ' (Lower)' : ' (Upper)'); },
    cellId: function (cylinder, side) { return 'dtTrack' + ((cylinder * 2) + side); },
    cellTitle: function (cylinder, side) { return 'Cylinder ' + cylinder + ', side ' + side + ' - click to seek here'; }
  });
}

// Rewrites the class rather than adding and removing the four state names, but
// carries the head marker across: it says where the drive is, not how a cylinder
// has behaved, and clearing the tally must not move it.
function dtPaintCell(cell, state) {
  var current = cell.hasClass('current') ? ' current' : '';
  cell.attr('class', 'track' + (state ? ' ' + state : '') + current);
}

// Clears the device's tally as well as the browser's. It used to clear only this
// one, so the console grid and the TFT kept counting from a history the browser
// had already thrown away.
function dtClearTally() {
  wsSend('dtTally');
  dtWipeTally();
}

function dtWipeTally() {
  $('#dtGrid td.track').each(function () {
    dtPaintCell($(this), '');
    $(this).html('');
  });
}

// Four states, the same ones the console panel paints: never visited, clean on
// every pass, clean on some, clean on none. A cylinder that reads perfectly four
// times in five looks flawless in any single pass, which is the whole reason the
// accumulated view exists alongside the realtime one.
function dtTally(cylinder, side, passes, errors) {
  var cell = $('#dtTrack' + ((cylinder * 2) + side));
  if (cell.length == 0) return;

  var state = 0;
  if (passes > 0) state = (errors == 0) ? 1 : (errors >= passes ? 3 : 2);

  dtPaintCell(cell, DT_TALLY_CLASS[state]);
  cell.html(passes > 0 ? (passes + '/' + errors) : '');
}

// Marks which cylinder the head is on, so it can be picked out of the grid
// without counting columns. Only the cells that gained or lost the mark move.
function dtMark(cylinder) {
  if (dtCurrent == cylinder) return;
  if (dtCurrent >= 0) {
    $('#dtTrack' + (dtCurrent * 2)).removeClass('current');
    $('#dtTrack' + ((dtCurrent * 2) + 1)).removeClass('current');
  }
  dtCurrent = cylinder;
  if (cylinder < 0) return;
  $('#dtTrack' + (cylinder * 2)).addClass('current');
  $('#dtTrack' + ((cylinder * 2) + 1)).addClass('current');
}

// Clicking a cell seeks there. The grid is already the map of the disk the operator
// is reading the fault off, so it is also the fastest way to say "go and look at
// that one" - quicker than nudging in tens and ones, and it needs no counting.
//
// Delegated from the tbody rather than bound per cell: there are 168 of them, and
// they are rebuilt whenever the grid is.
function dtSeekFromCell(cell) {
  if (dtRun === 0) return;
  var id = cell.attr('id');
  if (!id) return;
  // Cells are ids, not data attributes, so the id is the logical track and the
  // cylinder is half of it. Both sides of a cylinder seek to the same place.
  var cylinder = Math.floor(parseInt(id.substring('dtTrack'.length), 10) / 2);
  if (!isNaN(cylinder)) wsSend('dtCyl,' + cylinder);
}

function onLoad_DriveToolkit() {
  dtBuildTable();
  dtBuildGrid();
  dtSetRun(0);

  $('#dtGrid').on('click', 'td.track', function () {
    dtSeekFromCell($(this));
  });

  // Enter on the cylinder box seeks straight there, the one control quicker to
  // type than to step to.
  $('#dtCyl').on('keydown', function (e) {
    if (e.key !== 'Enter') return;
    e.preventDefault();
    var value = parseInt($('#dtCyl').val(), 10);
    if (!isNaN(value)) wsSend('dtCyl,' + value);
  });
}

function dtBuildTable() {
  var html = '';
  for (var i = 0; i < DT_SIGNALS.length; i++) {
    var s = DT_SIGNALS[i];
    // A rule between the outputs and the inputs, because "what am I driving" and
    // "what is the drive saying" are the two halves of every question asked here.
    var cls = (i === DT_IDX) ? ' class="dtInputsStart"' : '';
    html += '<tr' + cls + '>' +
            '<td class="dtName">' + s.name + '</td>' +
            '<td class="dtPin">' + s.pin + '</td>' +
            '<td class="dtDir">' + s.dir + '</td>' +
            '<td id="dtState' + i + '" class="dtState rest">-</td>' +
            '<td id="dtValue' + i + '" class="dtValue"></td>' +
            '</tr>';
  }
  $('#dtRows').html(html);
}

/*
   The device drives the busy pin for the whole session, and disableInterface()
   greys out every button that is not exempt - so all of these carry .live-action
   in the markup, which means this owns whether they are usable.

   Scoped to .dt-action rather than to .live-action. The calibration screen this
   absorbed used a global $('.live-action') selector, so opening a session there
   enabled and disabled this tab's buttons too; there is only one pane now, but the
   selector would still reach Disk Copy's cancel.
*/
function dtSetRun(run) {
  var was = dtRun;
  dtRun = run;
  var open = (run !== 0);

  $('.dt-action').prop('disabled', !open).toggleClass('disabled', !open);
  dtPaintRunButtons();

  // The grid only offers a pointer while there is a session to seek, so it does
  // not advertise a click that would go nowhere.
  $('#dtGrid').toggleClass('seekable', open);

  if (!open) {
    dtTainted = false;
    dtMark(-1);
    for (var i = 0; i < DT_SIGNALS.length; i++) {
      $('#dtState' + i).attr('class', 'dtState rest').html('-');
      $('#dtValue' + i).html('');
    }
    $('#dtStatus').attr('class', 'dtStatus rest').html('Not running.');
    $('#dtCylReadout').html('Cylinder: &mdash;');
    $('#dtEdges').html('');
    $('#dtPassLine').html('');
    dtClearRow(0);
    dtClearRow(1);
  } else if (was === 0) {
    // The device clears its tally when a session opens, so the grid has to start
    // from the same place or the two disagree about what has been tested.
    dtWipeTally();
  }

  if (run !== 2 && run !== 3) {
    dtClearRow(0);
    dtClearRow(1);
  }
}

function dtClearRow(side) {
  $('#dtGlyphs' + side).html('');
  $('#dtCount' + side).html('-').removeClass('allok driven');
}

/*
   Two buttons, two questions.

   Open/Close is whether the toolkit has the drive at all; Start/Pause/Resume is
   whether it is taking calibration passes. They were one button each on the two
   screens this merged, and they are genuinely different: the session is what makes
   the lines live, and the read is a layer over it that needs a disk.
*/
function dtPaintRunButtons() {
  var open = (dtRun !== 0);

  $('#dtStart').prop('disabled', false)
               .removeClass('disabled btn-success btn-danger')
               .addClass(open ? 'btn-danger' : 'btn-success')
               .html(open ? '<i class="fa-solid fa-stop"></i> Close'
                          : '<i class="fa-solid fa-play"></i> Open');

  var label, style;
  if (dtRun === 2) {
    label = '<i class="fa-solid fa-pause"></i> Pause read';
    style = 'btn-warning';
  } else if (dtRun === 3) {
    label = '<i class="fa-solid fa-play"></i> Resume read';
    style = 'btn-success';
  } else {
    label = '<i class="fa-solid fa-play"></i> Start read';
    style = 'btn-success';
  }
  $('#dtReadBtn').removeClass('btn-success btn-warning').addClass(style).html(label);
  $('#dtStopBtn').prop('disabled', dtRun < 2).toggleClass('disabled', dtRun < 2);
}

// Controls
// --------------------------------------------------------

// Open and Close are one button: a session that is open has no use for an Open
// beside it, and the device owns which of the two it currently is.
function dtToggleRun() {
  if (dtRun !== 0) {
    wsSend('dtExit');
    return;
  }
  var value = parseInt($('#dtCyl').val(), 10);
  wsSend('driveToolkit' + (isNaN(value) ? '' : ',' + value));
}

// Start, Pause and Resume are one button, because they are one question - is the
// drive reading right now. Starting from closed opens the session first.
function dtToggleRead() {
  if (dtRun === 0) {
    var value = parseInt($('#dtCyl').val(), 10);
    wsSend('driveToolkit,' + (isNaN(value) ? '' : value) + ',1');
    return;
  }
  if (dtRun === 1) { wsSend('dtRead,1'); return; }
  wsSend('dtPause,' + (dtRun === 3 ? 0 : 1));
}

function dtStopRead() { wsSend('dtRead,0'); }

function dtToggleSel()    { wsSend('dtSel,'    + (dtRaw[DT_SEL]  ? 0 : 1)); }
function dtToggleMot()    { wsSend('dtMot,'    + (dtRaw[DT_MOT]  ? 0 : 1)); }
function dtToggleDir()    { wsSend('dtDir,'    + (dtRaw[DT_DIR]  ? 0 : 1)); }
function dtToggleSide()   { wsSend('dtSide,'   + (dtRaw[DT_SIDE] ? 0 : 1)); }
function dtToggleDens()   { wsSend('dtDens,'   + (dtRaw[DT_DENS] ? 0 : 1)); }
function dtToggleSticky() { wsSend('dtSticky,' + (dtSticky ? 0 : 1)); }

function dtStep()  { wsSend('dtStep'); }
function dtRecal() { wsSend('dtRecal'); }
function dtClear() { wsSend('dtClear'); }
function dtSafe()  { wsSend('dtSafe'); }
function dtNudge(delta) { wsSend('dtNudge,' + delta); }
function dtReseek() { wsSend('dtSeek'); }
function dtCycleHead() { wsSend('dtHead,' + ((dtHead + 1) % 3)); }
function dtToggleAuto() { wsSend('dtAuto,' + (dtAuto ? 0 : 1)); }
function dtToggleSound() { wsSend('dtSound,' + (dtSound ? 0 : 1)); }

// Inbound
// --------------------------------------------------------

function dtState(run, cyl, rpmTenths, edges, steps, flags, levels, raws) {
  run = parseInt(run, 10) || 0;
  if (run !== dtRun) dtSetRun(run);
  if (run === 0) return;

  levels = String(levels);
  raws = String(raws);
  edges = parseInt(edges, 10) || 0;
  cyl = parseInt(cyl, 10);
  flags = parseInt(flags, 10) || 0;

  dtSticky = (flags & DT_F_STICKY) !== 0;
  dtTainted = (flags & DT_F_TAINTED) !== 0;
  dtSound = (flags & DT_F_SOUND) !== 0;
  dtAuto = (flags & DT_F_AUTO) !== 0;
  dtDisk = (flags & DT_F_DISK) !== 0;

  var raw = [];
  for (var i = 0; i < DT_SIGNALS.length; i++) {
    raw[i] = (raws.charAt(i) === '1');
  }
  dtRaw = raw;
  var motorOn = raw[DT_MOT];

  for (var j = 0; j < DT_SIGNALS.length; j++) {
    var level = DT_LEVEL_CLASS[parseInt(levels.charAt(j), 10)] || 'rest';
    $('#dtState' + j).attr('class', 'dtState ' + level).html(dtStateText(j, raw[j], motorOn));

    var value = '';
    if (j === DT_STEP) value = steps + ' pulses';
    else if (j === DT_IDX) value = (parseInt(rpmTenths, 10) > 0) ? dtRpmText(rpmTenths)
                                                                 : (edges + ' edges');
    else if (j === DT_SIDE) value = (cyl < 0) ? 'cyl ?' : ('cyl ' + cyl);
    $('#dtValue' + j).html(value);
  }

  // Button labels say what the line is now, not what pressing will do, so the row
  // of them reads as a picture of the drive rather than a list of verbs.
  $('#dtSelBtn').html('Select: ' + (raw[DT_SEL] ? 'On' : 'Off'))
                .toggleClass('btn-warning', raw[DT_SEL]).toggleClass('btn-secondary', !raw[DT_SEL]);
  $('#dtMotBtn').html('Motor: ' + (raw[DT_MOT] ? 'On' : 'Off'))
                .toggleClass('btn-warning', raw[DT_MOT]).toggleClass('btn-secondary', !raw[DT_MOT]);
  $('#dtDirBtn').html('Dir: ' + (raw[DT_DIR] ? 'In' : 'Out'));
  $('#dtSideBtn').html('SIDE line: ' + (raw[DT_SIDE] ? '1' : '0'));
  $('#dtDensBtn').html('Density: ' + (raw[DT_DENS] ? 'High' : 'Low'));
  $('#dtStickyBtn').html('Sticky: ' + (dtSticky ? 'On' : 'Off'))
                   .toggleClass('btn-warning', dtSticky).toggleClass('btn-secondary', !dtSticky);
  $('#dtAutoBtn').html('Auto re-seek: ' + (dtAuto ? 'On' : 'Off'));
  $('#dtSoundBtn').html('<i class="fa-solid fa-volume-' + (dtSound ? 'high' : 'xmark') +
                        '"></i> Sound: ' + (dtSound ? 'On' : 'Off'));

  // A question mark rather than a confident zero: nothing holds the head still
  // before a session opens, so the toolkit does not know where it is until a
  // recalibration or a completed pass puts it somewhere known.
  $('#dtCylReadout').html('Cylinder: ' + (cyl < 0 ? '?' : cyl));
  if (!$('#dtCyl').is(':focus') && cyl >= 0) $('#dtCyl').val(cyl);
  $('#dtEdges').html(edges + ' index edges');
  if (cyl >= 0) dtMark(cyl);

  dtRpm = rpmTenths;
  dtEdges = edges;
  dtCylinder = cyl;
  dtPaintStatus();
}

/*
   Repainted from dtState and again at the end of a pass.

   The sentence is composed from both layers - the interface first, then what the
   read saw - so it has to be redrawn whenever either moves. Painting it only on
   dtState left it a pass behind: the verdict on screen was the previous
   revolution's until the next sample landed, which is exactly the window an
   operator turning a screw is watching.
*/
function dtPaintStatus() {
  if (dtRun === 0 || !dtRaw.length) return;
  var status = dtStatusText(dtRaw, dtRpm, dtEdges, dtCylinder);
  $('#dtStatus').attr('class', 'dtStatus ' + status.level).html(status.text);
}

// The read layer's settings, which change far less often than the lines do. The
// cylinder, the speed and the session state travel in dtState, which carries them
// anyway - this is what was left of the calibration config once they moved.
function dtCal(cyl, step, head, passes, tainted) {
  dtHead = parseInt(head, 10);
  passes = parseInt(passes, 10) || 0;
  tainted = parseInt(tainted, 10) || 0;

  var headName = dtHead === 0 ? 'Lower' : (dtHead === 1 ? 'Upper' : 'Both');
  $('#dtHeadBtn').html('Read heads: ' + headName);
  $('#dtStepBtn').html('Step: ' + step);
  $('#dtPassLine').html(dtRun >= 2
    ? ('Pass ' + passes + (tainted > 0 ? ' — ' + tainted + ' not counted, lines were driven' : ''))
    : '');

  // A side that is no longer selected keeps stale glyphs otherwise.
  if (dtHead === 0) dtClearRow(1);
  if (dtHead === 1) dtClearRow(0);
}

function dtRead(cyl, side, valid, total, glyphs, passes, errors, tainted, seen) {
  var target = $('#dtGlyphs' + side);
  var count = $('#dtCount' + side);

  cyl = parseInt(cyl, 10);
  side = parseInt(side, 10);
  total = parseInt(total, 10);
  valid = parseInt(valid, 10);
  tainted = parseInt(tainted, 10) || 0;

  dtMark(cyl);
  dtSeen[side] = parseInt(seen, 10);
  dtValid[side] = valid;
  dtTotal[side] = total;

  // The accumulated counts arrive with every row, including the ones that carry
  // no realtime result, so a side that is not selected still keeps its history.
  if (passes !== undefined) {
    dtTally(cyl, side, parseInt(passes, 10), parseInt(errors, 10));
  }

  if (!total || glyphs === '-') {
    target.html('');
    count.html('-').removeClass('allok driven');
    return;
  }

  var html = '';
  for (var i = 0; i < glyphs.length; i++) {
    var ch = glyphs.charAt(i);
    html += '<span class="dtSector ' + (DT_SECTOR_CLASS[ch] || 'missing') + '">' + ch + '</span>';
  }
  target.html(html);

  // A driven pass is marked on the row rather than in the glyphs: those sectors
  // really were read, it is the pass that is not a measurement of alignment.
  count.html('(' + valid + '/' + total + (tainted ? ' driven)' : ' okay)'))
       .toggleClass('allok', valid === total && !tainted)
       .toggleClass('driven', tainted !== 0);

  // Side 1 is the last row of a pass, so the verdict is complete by here.
  if (side === 1 || dtHead === 0) dtPaintStatus();
}
