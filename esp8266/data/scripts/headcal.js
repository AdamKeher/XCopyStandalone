// Head Calibration
// --------------------------------------------------------
//
// The browser is a third rendering of the session the device holds, never a copy
// of it. Every control sends a command and then waits to be told the new state,
// so a cylinder changed on the TFT and one changed here land in the same place.

var headCalRunning = false;
var headCalPaused = false;
var headCalHead = 2; // 0 lower, 1 upper, 2 both
var headCalAuto = false;
var headCalSound = false;

// Maps a glyph to the class that colours it. Same vocabulary as the TFT and the
// serial console, so a photograph of any of the three reads the same way.
var HEADCAL_CLASS = {
  '.': 'ok',
  'X': 'missing',
  '-': 'low',
  '+': 'high',
  'h': 'side',
  'c': 'chk'
};

// Session tally grid
// --------------------------------------------------------
//
// The same grid the Disk Copy and Test Disk tabs draw, built by the same function
// in diskcopy.js and wearing the same .track classes, so the two tabs read as one
// device. It used to be a table of its own with its own cell size, its own header
// row and its own colour names, which is why it looked like it belonged to some
// other program.
//
// It is the accumulated half of the display - the sector row above is one
// revolution, this is every pass the session has made over every cylinder the head
// has been moved to.
//
// Geometry comes from diskcopy.js so the three surfaces cannot drift apart. See the
// note there and in XCopyGeometry.h.

var headCalCurrent = -1;

// The four states share the disk copy key's vocabulary rather than inventing three
// more colours: green read clean, yellow needed a retry, red never read. Untested
// is the bare cell, the same colour an untouched track has during a copy.
var HEADCAL_TALLY_CLASS = ['', 'green', 'yellow', 'red'];

function buildHeadCalGrid() {
  buildCylinderGrid($('#headCalGrid'), {
    // No icon column: nothing is being copied from anywhere, so the grid starts at
    // the row labels.
    leadWidth: 0,
    sideLabel: function (side) { return 'Side ' + side + (side == 0 ? ' (Lower)' : ' (Upper)'); },
    cellId: function (cylinder, side) { return 'hcTrack' + ((cylinder * 2) + side); },
    cellTitle: function (cylinder, side) { return 'Cylinder ' + cylinder + ', side ' + side + ' - click to seek here'; }
  });
}

// Rewrites the class rather than adding and removing the four state names, but
// carries the head marker across: it says where the drive is, not how a cylinder
// has behaved, and clearing the tally must not move it.
function headCalPaint(cell, state) {
  var current = cell.hasClass('current') ? ' current' : '';
  cell.attr('class', 'track' + (state ? ' ' + state : '') + current);
}

function headCalClearTally() {
  $('#headCalGrid td.track').each(function () {
    headCalPaint($(this), '');
    $(this).html('');
  });
}

// Four states, the same ones the console panel paints: never visited, clean on
// every pass, clean on some, clean on none. A cylinder that reads perfectly four
// times in five looks flawless in any single pass, which is the whole reason the
// accumulated view exists alongside the realtime one.
function headCalTally(cylinder, side, passes, errors) {
  var cell = $('#hcTrack' + ((cylinder * 2) + side));
  if (cell.length == 0) return;

  var state = 0;
  if (passes > 0) state = (errors == 0) ? 1 : (errors >= passes ? 3 : 2);

  headCalPaint(cell, HEADCAL_TALLY_CLASS[state]);
  cell.html(passes > 0 ? (passes + '/' + errors) : '');
}

// Marks which cylinder the head is on, so it can be picked out of the grid
// without counting columns. Only the cells that gained or lost the mark move.
function headCalMark(cylinder) {
  if (headCalCurrent == cylinder) return;
  if (headCalCurrent >= 0) {
    $('#hcTrack' + (headCalCurrent * 2)).removeClass('current');
    $('#hcTrack' + ((headCalCurrent * 2) + 1)).removeClass('current');
  }
  headCalCurrent = cylinder;
  $('#hcTrack' + (cylinder * 2)).addClass('current');
  $('#hcTrack' + ((cylinder * 2) + 1)).addClass('current');
}

// Clicking a cell seeks there. The grid is already the map of the disk the operator
// is reading the fault off, so it is also the fastest way to say "go and look at
// that one" - quicker than nudging in tens and ones, and it needs no counting.
//
// Delegated from the tbody rather than bound per cell: there are 168 of them, and
// they are rebuilt whenever the grid is.
function headCalSeekFromCell(cell) {
  if (!headCalRunning) return;
  var id = cell.attr('id');
  if (!id) return;
  // Cells are ids, not data attributes, so the id is the logical track and the
  // cylinder is half of it. Both sides of a cylinder seek to the same place.
  var cylinder = Math.floor(parseInt(id.substring('hcTrack'.length), 10) / 2);
  if (!isNaN(cylinder)) wsSend('headCalCyl,' + cylinder);
}

function onLoad_HeadCal() {
  buildHeadCalGrid();
  headCalSetRunning(false);

  $('#headCalGrid').on('click', 'td.track', function () {
    headCalSeekFromCell($(this));
  });

  // Enter on the cylinder box jumps straight there, the one control that is
  // quicker to type than to step to.
  $('#headCalCyl').on('keydown', function (e) {
    if (e.key !== 'Enter') return;
    e.preventDefault();
    var value = parseInt($('#headCalCyl').val(), 10);
    if (!isNaN(value)) wsSend('headCalCyl,' + value);
  });
}

// The device drives the busy pin while a session runs, and disableInterface()
// greys out every button that is not exempt. These carry .live-action so they
// survive that, which means this owns whether they are usable.
function headCalSetRunning(running) {
  headCalRunning = running;

  $('.live-action').not('#headCalStart').prop('disabled', !running)
                   .toggleClass('disabled', !running);
  headCalPaintRunButton();

  // The grid only offers a pointer while there is a session to seek, so it does
  // not advertise a click that would go nowhere.
  $('#headCalGrid').toggleClass('seekable', running);

  if (!running) {
    headCalPaused = false;
    headCalPaintRunButton();
    $('#headCalStatus').html('Not running.');
    $('#headCalRpm').html('');
    headCalClearRow(0);
    headCalClearRow(1);
  } else {
    // The device clears its tally when a session opens, so the grid has to start
    // from the same place or the two disagree about what has been tested.
    headCalClearTally();
  }
}

function headCalClearRow(side) {
  $('#headCalGlyphs' + side).html('');
  $('#headCalCount' + side).html('-');
}

// Controls
// --------------------------------------------------------

/*
   Start, Pause and Resume are one button, because they are one question - is the
   drive reading right now - and a session that is running has no use for a Start
   button beside it. The device owns the answer: this only sends the command, and
   the button is relabelled when the config that comes back says it happened.
*/
function headCalToggleRun() {
  if (!headCalRunning) {
    var value = parseInt($('#headCalCyl').val(), 10);
    wsSend('headCalibration' + (isNaN(value) ? '' : ',' + value));
    return;
  }
  wsSend('headCalPause,' + (headCalPaused ? 0 : 1));
}

function headCalPaintRunButton() {
  var button = $('#headCalStart');
  var label, style;

  if (!headCalRunning) {
    label = '<i class="fa-solid fa-play"></i> Start';
    style = 'btn-success';
  } else if (headCalPaused) {
    label = '<i class="fa-solid fa-play"></i> Resume';
    style = 'btn-success';
  } else {
    label = '<i class="fa-solid fa-pause"></i> Pause';
    style = 'btn-warning';
  }

  // Never disabled now: there is always something for it to do.
  button.prop('disabled', false)
        .removeClass('disabled btn-success btn-warning')
        .addClass(style)
        .html(label);
}

// Not diskcopyCancel(): that pulses the device cancel pin, which on this PCB is
// wired to the joystick up line, so here it would adjust a control rather than
// leave the screen.
function headCalExit() { wsSend('headCalExit'); }

function headCalNudge(delta) { wsSend('headCalNudge,' + delta); }
function headCalReseek() { wsSend('headCalReseek'); }
function headCalCycleHead() { wsSend('headCalHead,' + ((headCalHead + 1) % 3)); }
function headCalToggleAuto() { wsSend('headCalAuto,' + (headCalAuto ? 0 : 1)); }
function headCalToggleSound() { wsSend('headCalSound,' + (headCalSound ? 0 : 1)); }

// Inbound
// --------------------------------------------------------

function headCalRpmText(tenths) {
  var value = parseInt(tenths, 10);
  if (isNaN(value) || value <= 0) return '-- RPM';
  return (Math.floor(value / 10) + '.' + (value % 10) + ' RPM');
}

function headCalConfig(cyl, step, head, auto, active, passes, snd, paused, rpm) {
  headCalHead = parseInt(head, 10);
  headCalAuto = (parseInt(auto, 10) !== 0);
  headCalSound = (parseInt(snd, 10) !== 0);
  var running = (parseInt(active, 10) !== 0);
  var wasPaused = headCalPaused;
  headCalPaused = (parseInt(paused, 10) !== 0);

  if (running !== headCalRunning) headCalSetRunning(running);
  else if (headCalPaused !== wasPaused) headCalPaintRunButton();
  if (!running) return;

  // Left alone while it has focus, so a number being typed is not overwritten
  // by the pass that lands mid keystroke.
  if (!$('#headCalCyl').is(':focus')) $('#headCalCyl').val(cyl);

  var headName = headCalHead === 0 ? 'Lower' : (headCalHead === 1 ? 'Upper' : 'Both');
  $('#headCalHeadBtn').html('Head: ' + headName);
  $('#headCalAutoBtn').html('Auto re-seek: ' + (headCalAuto ? 'On' : 'Off'));
  $('#headCalSoundBtn').html('<i class="fa-solid fa-volume-' +
                             (headCalSound ? 'high' : 'xmark') + '"></i> Sound: ' +
                             (headCalSound ? 'On' : 'Off'));
  $('#headCalStatus').html('Cylinder ' + cyl + ', step ' + step + ', pass ' + passes +
                           (headCalPaused ? ' - paused' : ''));
  // The speed the drive is actually turning at, which the TFT and the console have
  // shown all along and the browser had no way to see.
  $('#headCalRpm').html(headCalRpmText(rpm));

  // A side that is no longer selected keeps stale glyphs otherwise.
  if (headCalHead === 0) headCalClearRow(1);
  if (headCalHead === 1) headCalClearRow(0);
}

function headCalResult(cyl, side, valid, total, glyphs, passes, errors) {
  var target = $('#headCalGlyphs' + side);
  var count = $('#headCalCount' + side);

  cyl = parseInt(cyl, 10);
  side = parseInt(side, 10);
  total = parseInt(total, 10);
  valid = parseInt(valid, 10);

  headCalMark(cyl);

  // The accumulated counts arrive with every row, including the ones that carry
  // no realtime result, so a side that is not selected still keeps its history.
  if (passes !== undefined) {
    headCalTally(cyl, side, parseInt(passes, 10), parseInt(errors, 10));
  }

  if (!total || glyphs === '-') {
    target.html('');
    count.html('-');
    return;
  }

  var html = '';
  for (var i = 0; i < glyphs.length; i++) {
    var ch = glyphs.charAt(i);
    html += '<span class="hcSector ' + (HEADCAL_CLASS[ch] || 'missing') + '">' + ch + '</span>';
  }
  target.html(html);

  count.html('(' + valid + '/' + total + ' okay)');
  count.toggleClass('allok', valid === total);
}
