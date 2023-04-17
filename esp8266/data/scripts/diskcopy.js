// UI
// --------------------------------------------------------
 
function resetButtons() {
    $('#copyADFToDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDiskToADF').removeClass('btn-light').addClass('btn-primary');
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
        connection.send(state);
    }
}

// TODO: handle cancellation / send back to xcopydevice
function diskcopyCancel() {
    connection.send("espCommand,cancelPin");
    // disableInterface(false);
}

function writeADFFile(path) {
    $('#staticBackdrop').modal('hide');
    connection.send("writeADFFile," + path);
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