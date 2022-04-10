// Buttons
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
        showsSelectDialog();
    } else {
        connection.send(state);
    }
}

function getSdFiles(path) {
    sdPath = path;
    connection.send("getSdFiles," + sdPath);
}
  
function diskcopyCancel() {
    // TODO: handle cancellation / send back to xcopydevice
    disableInterface(false);
}


function writeADFFile(path) {
    $('#staticBackdrop').modal('hide');
    connection.send("writeADFFile," + path);
}
  
// function copyADFtoDisk() {
//     connection.send("copyADFtoDisk");
//   }
  
//   function copyDisktoADF() {
//     connection.send("copyDisktoADF");
//   }
  
//   function copyDisktoDisk() {
//     connection.send("copyDisktoDisk");
//   }
  
//   function copyDisktoFlash() {
//     connection.send("copyDisktoFlash");
//   }
  
//   function copyFlashtoDisk() {
//     connection.send("copyFlashtoDisk");
//   }
  
//   function testDisk() {
//     connection.send("testDisk");
//   }
  
//   function formatDisk() {
//     connection.send("formatDisk");
//   }
  
//   function diskFlux() {
//     connection.send("diskFlux");
//   }