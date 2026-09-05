// Track grid
// --------------------------------------------------------
//
// Built here rather than written out in index.html, where it used to be 160 hand
// numbered <td> elements interleaved with the source and destination icon column.
// SCP capture reaches cylinder 83, so the grid had to grow - and growing it by hand
// meant renumbering every cell and re-deriving every rowspan, which is exactly the
// kind of edit that silently mislabels a track.
//
// TRACK_COLS x TRACK_ROWS mirrors the serial console map (XCopyTrackMap) and the TFT
// block grid (XCopyGraphics::drawTrack). Change one and change all three, or the
// device stops agreeing with itself about what it is doing.

var MAX_CYLINDERS = 84;
var TRACK_COLS = 10;
// Rounded up: 84 is not a multiple of 10, so the last row is short and holds only
// cylinders 80-83. That is deliberate - see XCopyGeometry.h. Cells past MAX_CYLINDERS
// are left empty rather than being drawn as tracks that will never fill in.
var TRACK_ROWS = Math.ceil(MAX_CYLINDERS / TRACK_COLS);
var TRACK_COUNT = MAX_CYLINDERS * 2;

// Column header labels. Ten columns, so these are simply 0-9 and a cylinder number is
// read off as row label plus column - 40 plus 7 is cylinder 47. The A-F headings a
// twelve column grid needed are what made this unreadable. Same scheme as the console
// map; the fallback past 9 is kept only so a wider grid degrades rather than breaks.
function trackColumnLabel(col) {
    return col < 10 ? String(col) : String.fromCharCode(65 + col - 10);
}

// The four icon groups used to be pinned to grid rows by rowspan, over eight rows
// with two spacers. The mapping lives here as data rather than being spelled out in
// markup: source icons, source globes, destination icons, destination globes, and the
// Target label under them. Rows past the Target label get an empty cell - with nine
// rows the default arm would otherwise repeat "Target" three times down the side.
function trackIconCells(row) {
    switch (row) {
        case 0:
            return '<td rowspan=2><img id="src_floppy" src="./images/floppysrc.png"></td>' +
                   '<td rowspan=2><img id="src_sdcard" src="./images/sdcard.png"></td>' +
                   '<td rowspan=2><img id="src_flash" src="./images/flash.png"></td>' +
                   '<td></td>';
        case 1:
            return '<td></td>';
        case 2:
            return '<td><img id="src_floppy_globe" src="./images/selected.png"></td>' +
                   '<td><img id="src_sdcard_globe" src="./images/selected.png"></td>' +
                   '<td><img id="src_flash_globe" src="./images/selected.png"></td>' +
                   '<td></td>';
        case 3:
            return '<td rowspan=2><img id="dst_floppy" src="./images/floppydest.png"></td>' +
                   '<td rowspan=2><img id="dst_sdcard" src="./images/sdcard.png"></td>' +
                   '<td rowspan=2><img id="dst_flash" src="./images/flash.png"></td>' +
                   '<td></td>';
        case 4:
            return '<td></td>';
        case 5:
            return '<td><img id="dst_floppy_globe" src="./images/selected.png"></td>' +
                   '<td><img id="dst_sdcard_globe" src="./images/selected.png"></td>' +
                   '<td><img id="dst_flash_globe" src="./images/selected.png"></td>' +
                   '<td></td>';
        case 6:
            return '<td colspan=4 class="gridSide">Target</td>';
        default:
            return '<td colspan=4></td>';
    }
}

// Built once here and used by both grids on the page. The disk copy / test disk
// map and the head calibration session tally are the same map of the same disk, so
// they are drawn by the same function rather than by two that have to be kept
// looking alike. Head cal used to build its own table, with its own cell size and
// its own header row, and it read as a different device.
//
//   leadWidth   how many table columns the left hand block occupies, 0 for none
//   leadHeader  what sits in that block on the column label row
//   leadCells   function(row) -> the cells of that block, for grid rows
//   sideLabel   function(side) -> the heading over that side
//   cellId      function(cylinder, side) -> the id of one track cell
//   cellTitle   function(cylinder, side) -> its tooltip, optional
//   stacked     true to draw side 1 under side 0 rather than beside it
//
// Stacked is for the Drive Toolkit, where the grid shares its row with the signal
// table and there is not the width for two sides abreast at the cell size the
// Disk Copy tab draws them. The cells stay that size - it is the same map of the
// same disk - and the two sides go one over the other instead.
function buildCylinderGrid(body, options) {
    if (body.length == 0) return;

    var leadWidth = options.leadWidth || 0;
    var stacked = !!options.stacked;
    // The lead block, then per side a label column, TRACK_COLS cells and a
    // trailing spacer.
    var columns = leadWidth + ((TRACK_COLS + 2) * (stacked ? 1 : 2));
    var html = '';

    function lead(content) {
        return leadWidth > 0 ? '<td colspan=' + leadWidth + ' class="gridSide">' + content + '</td>' : '';
    }

    function label(content) {
        return '<td class="gridLabel">' + content + '</td>';
    }

    // The side title and the column numbers, over the sides first to last.
    function headerRows(first, last) {
        var side, col;
        var h = '<tr>' + lead('');
        for (side = first; side <= last; side++) {
            h += '<td colspan=' + (TRACK_COLS + 1) + ' class="gridSide">' + options.sideLabel(side) + '</td><td></td>';
        }
        h += '</tr>';

        h += '<tr>' + lead(options.leadHeader || '');
        for (side = first; side <= last; side++) {
            h += '<td></td>';
            for (col = 0; col < TRACK_COLS; col++) h += label(trackColumnLabel(col));
            h += '<td></td>';
        }
        return h + '</tr>';
    }

    function gridRows(first, last) {
        var side, row, col, cylinder, id, title;
        var h = '';
        for (row = 0; row < TRACK_ROWS; row++) {
            h += '<tr>' + (options.leadCells ? options.leadCells(row) : lead(''));
            for (side = first; side <= last; side++) {
                // The row label is the first cylinder in the row - 0, 10, 20 ... 80 -
                // not the row index. Labelling the rows 0-6 was what left the grid
                // reading as a 12 x 7 block of nothing in particular.
                h += label(row * TRACK_COLS);
                for (col = 0; col < TRACK_COLS; col++) {
                    cylinder = (row * TRACK_COLS) + col;
                    if (cylinder >= MAX_CYLINDERS) {
                        h += '<td></td>';
                        continue;
                    }
                    id = options.cellId(cylinder, side);
                    title = options.cellTitle ? ' title="' + options.cellTitle(cylinder, side) + '"' : '';
                    h += '<td class="track" id="' + id + '"' + title + '></td>';
                }
                h += '<td></td>';
            }
            h += '</tr>';
        }
        return h;
    }

    var spacer = '<tr><td colspan="' + columns + '"></td></tr>';

    if (stacked) {
        html += headerRows(0, 0) + gridRows(0, 0) + spacer;
        html += headerRows(1, 1) + gridRows(1, 1);
    } else {
        html += headerRows(0, 1) + gridRows(0, 1);
    }
    html += spacer;

    body.html(html);
}

// One name for a side, on every grid. Side 0 is the lower head.
function cylinderSideLabel(side) {
    return 'Side ' + side + (side == 0 ? ' (Lower)' : ' (Upper)');
}

function buildTrackGrid() {
    buildCylinderGrid($('#trackGrid'), {
        leadWidth: 4,
        leadHeader: 'Source',
        leadCells: trackIconCells,
        sideLabel: cylinderSideLabel,
        cellId: function (cylinder, side) { return 'track' + ((cylinder * 2) + side); }
    });
}

// UI
// --------------------------------------------------------
 
function resetButtons() {
    $('#copyADFToDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDiskToADF').removeClass('btn-light').addClass('btn-primary');
    $('#copyDiskToSCP').removeClass('btn-light').addClass('btn-primary');
    $('#copyDiskToDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDiskToFlash').removeClass('btn-light').addClass('btn-primary');
    $('#copyFlashToDisk').removeClass('btn-light').addClass('btn-primary');
}

function setState(state) {
    console.log("State: '" + state + "'");
    $('#diskname').html('[Unknown]');
    $('#disknameUI').hide();

    if (state == 'copyADFToDisk') {
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(false, true, false);
        setIconsDest(true, false, false);
    }
    else if (state == 'copyDiskToADF') {
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(false, true, false);
    }
    else if (state == 'copyDiskToSCP') {
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(false, true, false);
    }
    else if (state == 'copyDiskToDisk') {
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(true, false, false);
    }
    else if (state == 'copyDiskToFlash') {
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(false, false, true);
    }
    else if (state == 'copyFlashToDisk') {
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(false, false, true);
        setIconsDest(true, false, false);
    }
    else if (state == 'testDisk') {
        setIconsSrc(true, false, false);
        setIconsDest(false, false, false);
    }
    else if (state == 'formatDisk') {
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(false, false, false);
    }
    else if (state == 'fluxDisk') {
        setIconsSrc(true, false, false);
        setIconsDest(false, false, false);
    }        
}

function diskcopy(state) {
    resetButtons();
    setState(state);
    if (state == 'copyADFToDisk') {
        if (sdFiles.length == 0) getSdFiles(sdPath);
    } else {
        wsSend(state);
    }
}

// TODO: handle cancellation / send back to xcopydevice
function diskcopyCancel() {
    wsSend("espCommand,cancelPin");
    // disableInterface(false);
}

/*
   Restarts the Teensy, not the radio. The ESP stays up and so does this page, so
   the connection banner never appears and the only sign of it is the device going
   quiet for a second and the status line coming back from the boot.

   Asked first. It sits one item below Format Disk in the same menu, and it
   abandons whatever the drive is in the middle of.
*/
function rebootDevice() {
    if (!confirm('Reboot the XCopy device now? Anything in progress will be abandoned.')) return;
    setStatus('Rebooting ...');
    wsSend('rebootDevice');
}

function writeADFFile(path) {
    $('#staticBackdrop').modal('hide');
    wsSend("writeADFFile," + path);
}

// Status / Diskname
// --------------------------------------------------------

function setStatus(status) {
    $('#status').html(status);
}
  
function setDiskname(diskname) {
    if (diskname == '') diskname = '[Unknown]';
    $('#diskname').html(diskname);
    $('#disknameUI').show();
}

// Tracks
// --------------------------------------------------------

function setTrack(trackNum, classname, text = "") {
    $('#track' + trackNum).attr("class", "track " + classname);
    $('#track' + trackNum).html(text)
}
  
function resetTracks(classname = "", start = 0) {
    for (i = start; i < TRACK_COUNT; i++) {
        $('#track' + i).attr("class", classname == "" ? "track" : "track " + classname);
        $('#track' + i).html("");
    }
}

// Icons
// --------------------------------------------------------

function setIcons(group, floppy, sdcard, flash) {
    document.getElementById(group + '_floppy').className = floppy ? "" : "gray";
    document.getElementById(group + '_sdcard').className = sdcard ? "" : "gray";
    document.getElementById(group + '_flash').className = flash ? "" : "gray";
    document.getElementById(group + '_floppy_globe').className = floppy ? "" : "gray";
    document.getElementById(group + '_sdcard_globe').className = sdcard ? "" : "gray";
    document.getElementById(group + '_flash_globe').className = flash ? "" : "gray";
}
  
function setIconsSrc(floppy, sdcard, flash) {
setIcons("src", floppy, sdcard, flash);
}

function setIconsDest(floppy, sdcard, flash) {
setIcons("dst", floppy, sdcard, flash);
}

function disableGlobes() {
setIconsSrc(false, false, false);
setIconsDest(false, false, false);
}