// UI
// --------------------------------------------------------
 
function resetButtons() {
    $('#copyADFtoDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDisktoADF').removeClass('btn-light').addClass('btn-primary');
    $('#copyDisktoDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDisktoFlash').removeClass('btn-light').addClass('btn-primary');
    $('#copyFlashtoDisk').removeClass('btn-light').addClass('btn-primary');
}

function setState(state) {
    console.log("State: '" + state + "'");
    $('#diskname').html('[Unknown]');
    $('#disknameUI').hide();

    if (state == 'copyADFtoDisk') {
        setMode('Copy ADF to Disk');
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(false, true, false);
        setIconsDest(true, false, false);
    }
    else if (state == 'copyDisktoADF') {
        setMode('Copy Disk to ADF');
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(false, true, false);
    }
    else if (state == 'copyDisktoDisk') {
        setMode('Copy Disk to Disk');
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(true, false, false);
    }
    else if (state == 'copyDisktoFlash') {
        setMode('Copy Disk to Flash');
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(false, false, true);
    }
    else if (state == 'copyFlashtoDisk') {
        setMode('Copy Flash to Disk');
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(false, false, true);
        setIconsDest(true, false, false);
    }
    else if (state == 'testDisk') {
        setMode('Test Disk AA');
        setIconsSrc(true, false, false);
        setIconsDest(false, false, false);
    }
    else if (state == 'formatDisk') {
        setMode('Format Disk');
        $('#' + state).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(false, false, false);
    }
    else if (state == 'fluxDisk') {
        setMode('Disk Flux');
        setIconsSrc(true, false, false);
        setIconsDest(false, false, false);
    }        
}

function diskcopy(state) {
    resetButtons();
    setState(state);
    if (state == 'copyADFtoDisk') {
        if (sdFiles.length == 0) getSdFiles(sdPath);
    } else {
        connection.send(state);
    }
}

// TODO: handle cancellation / send back to xcopydevice
function diskcopyCancel() {
    disableInterface(false);
}

function writeADFFile(path) {
    $('#staticBackdrop').modal('hide');
    connection.send("writeADFFile," + path);
}

// Mode / Status / Diskname
// --------------------------------------------------------

function setMode(mode) {
    $('#mode').html(mode);
}

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
    for (i = start; i < 160; i++) {
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