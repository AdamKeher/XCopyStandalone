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
            return '<td colspan=4>Target</td>';
        default:
            return '<td colspan=4></td>';
    }
}

function buildTrackGrid() {
    var body = $('#trackGrid');
    if (body.length == 0) return;

    // 3 icon columns and a spacer, then per side a label column, TRACK_COLS cells
    // and a trailing spacer.
    var columns = 4 + ((TRACK_COLS + 2) * 2);
    var html = '';
    var side, row, col, cylinder;

    html += '<tr><td colspan=4></td>';
    html += '<td colspan=' + (TRACK_COLS + 1) + '>Upper Side</td><td></td>';
    html += '<td colspan=' + (TRACK_COLS + 1) + '>Lower Side</td><td></td></tr>';

    html += '<tr><td colspan=4>Source</td>';
    for (side = 0; side < 2; side++) {
        html += '<td></td>';
        for (col = 0; col < TRACK_COLS; col++) html += '<td>' + trackColumnLabel(col) + '</td>';
        html += '<td></td>';
    }
    html += '</tr>';

    for (row = 0; row < TRACK_ROWS; row++) {
        html += '<tr>' + trackIconCells(row);
        for (side = 0; side < 2; side++) {
            // The row label is the first cylinder in the row - 0, 10, 20 ... 80 - not
            // the row index. Labelling the rows 0-6 was what left the grid reading as a
            // 12 x 7 block of nothing in particular.
            html += '<td>' + (row * TRACK_COLS) + '</td>';
            for (col = 0; col < TRACK_COLS; col++) {
                cylinder = (row * TRACK_COLS) + col;
                if (cylinder >= MAX_CYLINDERS) {
                    html += '<td></td>';
                    continue;
                }
                html += '<td class="track" id="track' + ((cylinder * 2) + side) + '"></td>';
            }
            html += '<td></td>';
        }
        html += '</tr>';
    }

    html += '<tr><td colspan="' + columns + '"></td></tr>';

    body.html(html);
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