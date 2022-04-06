// Buttons
// --------------------------------------------------------

function resetButtons() {
    $('#copyADFtoDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDisktoADF').removeClass('btn-light').addClass('btn-primary');
    $('#copyDisktoDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDisktoFlash').removeClass('btn-light').addClass('btn-primary');
    $('#copyFlashtoDisk').removeClass('btn-light').addClass('btn-primary');
}

function setState(tempname) {
    console.log("State: '" + tempname + "'");

    if (tempname == 'copyADFtoDisk') {
        setMode('Copy ADF to Disk');
        $('#' + tempname).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(false, true, false);
        setIconsDest(true, false, false);
    }
    else if (tempname == 'copyDisktoADF') {
        setMode('Copy Disk to ADF');
        $('#' + tempname).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(false, true, false);
    }
    else if (tempname == 'copyDisktoDisk') {
        setMode('Copy Disk to Disk');
        $('#' + tempname).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(true, false, false);
    }
    else if (tempname == 'copyDisktoFlash') {
        setMode('Copy Disk to Flash');
        $('#' + tempname).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(false, false, true);
    }
    else if (tempname == 'copyFlashtoDisk') {
        setMode('Copy Flash to Disk');
        $('#' + tempname).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(false, false, true);
        setIconsDest(false, false, true);
    }
    else if (tempname == 'testDisk') {
        setMode('Test Disk AA');
        setIconsSrc(true, false, false);
        setIconsDest(false, false, false);
    }
    else if (tempname == 'formatDisk') {
        setMode('Format Disk');
        $('#' + tempname).removeClass('btn-primary').addClass('btn-light');
        setIconsSrc(true, false, false);
        setIconsDest(false, false, false);
    }
    else if (tempname == 'fluxDisk') {
        setMode('Disk Flux');
        setIconsSrc(true, false, false);
        setIconsDest(false, false, false);
    }        
}

function diskcopy(name) {
    resetButtons();
    setState(name);
    connection.send(name);
}

function getSdFiles(param) {
    connection.send("getSdFiles," + param);
}
  
function diskcopyCancel() {
    // TODO: handle cancellation / send back to xcopydevice
    disableInterface(false);
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