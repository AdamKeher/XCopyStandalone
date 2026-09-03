// Head Calibration
// --------------------------------------------------------
//
// The browser is a third rendering of the session the device holds, never a copy
// of it. Every control sends a command and then waits to be told the new state,
// so a cylinder changed on the TFT and one changed here land in the same place.

var headCalRunning = false;
var headCalHead = 2; // 0 lower, 1 upper, 2 both
var headCalAuto = false;

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

function onLoad_HeadCal() {
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

// Inbound
// --------------------------------------------------------

function headCalConfig(cyl, step, head, auto, active, passes) {
  headCalHead = parseInt(head, 10);
  headCalAuto = (parseInt(auto, 10) !== 0);
  var running = (parseInt(active, 10) !== 0);

  if (running !== headCalRunning) headCalSetRunning(running);
  if (!running) return;

  // Left alone while it has focus, so a number being typed is not overwritten
  // by the pass that lands mid keystroke.
  if (!$('#headCalCyl').is(':focus')) $('#headCalCyl').val(cyl);

  var headName = headCalHead === 0 ? 'Lower' : (headCalHead === 1 ? 'Upper' : 'Both');
  $('#headCalHeadBtn').html('Head: ' + headName);
  $('#headCalAutoBtn').html('Auto re-seek: ' + (headCalAuto ? 'On' : 'Off'));
  $('#headCalStatus').html('Cylinder ' + cyl + ', step ' + step + ', pass ' + passes);

  // A side that is no longer selected keeps stale glyphs otherwise.
  if (headCalHead === 0) headCalClearRow(1);
  if (headCalHead === 1) headCalClearRow(0);
}

function headCalResult(cyl, side, valid, total, glyphs) {
  var target = $('#headCalGlyphs' + side);
  var count = $('#headCalCount' + side);

  total = parseInt(total, 10);
  valid = parseInt(valid, 10);

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
