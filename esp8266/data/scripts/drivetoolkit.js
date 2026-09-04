// Drive Toolkit
// --------------------------------------------------------
//
// The browser is the driven interface here - the device's own screen is display
// only, because a joystick field list would cost layout code in the most
// expensive flash on the board. Every control still sends a command and then
// waits to be told the new state, so a line toggled here and one toggled from the
// serial console land in the same place and neither surface holds state of its
// own.

var dtRunning = false;
var dtSticky = false;
// Last reading of each line, so a toggle can send the value it wants rather than
// "flip it" - two surfaces pressing at once cannot then land on opposite states.
var dtRaw = [];

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

/*
   Mirrors XCopyDriveToolkit::stateText().

   The device sends the raw reading rather than the words, so this is the same
   mapping written twice - deliberately, the way headcal.js mirrors the sector
   glyph vocabulary. Change one and change the other, or a photograph of the
   browser and a photograph of the console will disagree about the same drive.
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

// Mirrors XCopyDriveToolkit::statusText(), for the same reason dtStateText() does.
// It says the next thing to try rather than only naming the symptom: stepping
// works with the spindle stopped, so "motor on and no index" almost always means
// the drive is not taking its motor command from where we are giving it.
function dtStatusText(raw, motorOn, rpmTenths, edges) {
  if (!raw[DT_CHG]) return { text: 'No disk in the drive', level: 'warn' };
  if (!motorOn) {
    return {
      text: raw[DT_SEL] ? 'Selected, motor off' : 'Outputs released',
      level: 'rest'
    };
  }
  if (parseInt(rpmTenths, 10) > 0) {
    return { text: 'Spinning at ' + dtRpmText(rpmTenths), level: 'good' };
  }
  if (edges > 0) return { text: edges + ' edges, no rate yet', level: 'warn' };
  return {
    text: 'MOTOR ON, no index — is the spindle actually turning?',
    level: 'fault'
  };
}

function onLoad_DriveToolkit() {
  dtBuildTable();
  dtSetRunning(false);

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

// The device drives the busy pin for the whole session, and disableInterface()
// greys out every button that is not exempt - so all of these carry .live-action
// in the markup, which means this owns whether they are usable. Without that the
// entire driven interface would grey itself out the moment a session opened.
function dtSetRunning(running) {
  dtRunning = running;

  $('.dt-action').prop('disabled', !running).toggleClass('disabled', !running);
  $('#dtStart').prop('disabled', false)
               .removeClass('disabled btn-success btn-danger')
               .addClass(running ? 'btn-danger' : 'btn-success')
               .html(running ? '<i class="fa-solid fa-stop"></i> Exit'
                             : '<i class="fa-solid fa-play"></i> Start');

  if (!running) {
    for (var i = 0; i < DT_SIGNALS.length; i++) {
      $('#dtState' + i).attr('class', 'dtState rest').html('-');
      $('#dtValue' + i).html('');
    }
    $('#dtStatus').attr('class', 'dtStatus rest').html('Not running.');
    $('#dtCylReadout').html('Cylinder: &mdash;');
    $('#dtEdges').html('');
  }
}

// Controls
// --------------------------------------------------------

// Start and Exit are one button: a session that is running has no use for a Start
// beside it, and the device owns which of the two it currently is.
function dtToggleRun() {
  wsSend(dtRunning ? 'dtExit' : 'driveToolkit');
}

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

// Inbound
// --------------------------------------------------------

function dtState(active, cyl, rpmTenths, edges, steps, sticky, levels, raws) {
  var running = (parseInt(active, 10) !== 0);
  if (running !== dtRunning) dtSetRunning(running);
  if (!running) return;

  levels = String(levels);
  raws = String(raws);
  edges = parseInt(edges, 10) || 0;
  cyl = parseInt(cyl, 10);
  dtSticky = (parseInt(sticky, 10) !== 0);

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
    $('#dtValue' + j).html(value);
  }

  // Button labels say what the line is now, not what pressing will do, so the row
  // of them reads as a picture of the drive rather than a list of verbs.
  $('#dtSelBtn').html('Select: ' + (raw[DT_SEL] ? 'On' : 'Off'))
                .toggleClass('btn-warning', raw[DT_SEL]).toggleClass('btn-secondary', !raw[DT_SEL]);
  $('#dtMotBtn').html('Motor: ' + (raw[DT_MOT] ? 'On' : 'Off'))
                .toggleClass('btn-warning', raw[DT_MOT]).toggleClass('btn-secondary', !raw[DT_MOT]);
  $('#dtDirBtn').html('Dir: ' + (raw[DT_DIR] ? 'In' : 'Out'));
  $('#dtSideBtn').html('Side: ' + (raw[DT_SIDE] ? '1' : '0'));
  $('#dtDensBtn').html('Density: ' + (raw[DT_DENS] ? 'High' : 'Low'));
  $('#dtStickyBtn').html('Sticky: ' + (dtSticky ? 'On' : 'Off'))
                   .toggleClass('btn-warning', dtSticky).toggleClass('btn-secondary', !dtSticky);

  // A question mark rather than a confident zero: nothing holds the head still
  // before a session opens, so the toolkit does not know where it is until a
  // recalibration puts it somewhere known.
  $('#dtCylReadout').html('Cylinder: ' + (cyl < 0 ? '?' : cyl));
  if (!$('#dtCyl').is(':focus') && cyl >= 0) $('#dtCyl').val(cyl);
  $('#dtEdges').html(edges + ' index edges');

  var status = dtStatusText(raw, motorOn, rpmTenths, edges);
  $('#dtStatus').attr('class', 'dtStatus ' + status.level).html(status.text);
}
