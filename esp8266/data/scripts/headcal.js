// Head Calibration
// --------------------------------------------------------
//
// The browser is a third rendering of the session the device holds, never a copy
// of it. Every control sends a command and then waits to be told the new state,
// so a cylinder changed on the TFT and one changed here land in the same place.

var headCalRunning = false;
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
// Same shape as the disk copy grid and the console panel: MAX_CYLINDERS laid out
// TRACK_COLS to a row, both sides side by side. It is the accumulated half of the
// display - the sector row above is one revolution, this is every pass the session
// has made over every cylinder the head has been moved to.
//
// Geometry comes from diskcopy.js so the three surfaces cannot drift apart. See
// the note there and in XCopyGeometry.h.

var headCalCurrent = -1;

function buildHeadCalGrid() {
  var body = $('#headCalGrid');
  if (body.length == 0) return;

  var html = '';
  var side, row, col, cylinder;

  html += '<tr><th></th>';
  for (side = 0; side < 2; side++) {
    html += '<th colspan="' + TRACK_COLS + '">Side ' + side +
            (side == 0 ? ' (Lower)' : ' (Upper)') + '</th><th></th>';
  }
  html += '</tr>';

  html += '<tr><th></th>';
  for (side = 0; side < 2; side++) {
    for (col = 0; col < TRACK_COLS; col++) html += '<th>' + trackColumnLabel(col) + '</th>';
    html += '<th></th>';
  }
  html += '</tr>';

  for (row = 0; row < TRACK_ROWS; row++) {
    html += '<tr><th>' + (row * TRACK_COLS) + '</th>';
    for (side = 0; side < 2; side++) {
      for (col = 0; col < TRACK_COLS; col++) {
        cylinder = (row * TRACK_COLS) + col;
        // Cylinders past MAX_CYLINDERS are left empty rather than drawn as cells
        // that will never fill in - the last row is deliberately short.
        if (cylinder >= MAX_CYLINDERS) { html += '<td></td>'; continue; }
        html += '<td class="hcTrack untested" id="hcTrack' + ((cylinder * 2) + side) +
                '" title="Cylinder ' + cylinder + ', side ' + side + '"></td>';
      }
      html += '<td></td>';
    }
    html += '</tr>';
  }

  body.html(html);
}

function headCalClearTally() {
  $('#headCalGrid td.hcTrack').removeClass('clean intermittent failing')
                              .addClass('untested').html('');
}

// Four states, the same ones the console panel paints: never visited, clean on
// every pass, clean on some, clean on none. A cylinder that reads perfectly four
// times in five looks flawless in any single pass, which is the whole reason the
// accumulated view exists alongside the realtime one.
function headCalTally(cylinder, side, passes, errors) {
  var cell = $('#hcTrack' + ((cylinder * 2) + side));
  if (cell.length == 0) return;

  var state = 'untested';
  if (passes > 0) {
    if (errors == 0) state = 'clean';
    else if (errors >= passes) state = 'failing';
    else state = 'intermittent';
  }

  cell.removeClass('untested clean intermittent failing').addClass(state);
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

function onLoad_HeadCal() {
  buildHeadCalGrid();
  headCalSetRunning(false);

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

  $('#headCalStart').prop('disabled', running).toggleClass('disabled', running);
  $('.live-action').not('#headCalStart').prop('disabled', !running)
                   .toggleClass('disabled', !running);

  if (!running) {
    $('#headCalStatus').html('Not running.');
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

function headCalStart() {
  var value = parseInt($('#headCalCyl').val(), 10);
  wsSend('headCalibration' + (isNaN(value) ? '' : ',' + value));
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

function headCalConfig(cyl, step, head, auto, active, passes, snd) {
  headCalHead = parseInt(head, 10);
  headCalAuto = (parseInt(auto, 10) !== 0);
  headCalSound = (parseInt(snd, 10) !== 0);
  var running = (parseInt(active, 10) !== 0);

  if (running !== headCalRunning) headCalSetRunning(running);
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
  $('#headCalStatus').html('Cylinder ' + cyl + ', step ' + step + ', pass ' + passes);

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
