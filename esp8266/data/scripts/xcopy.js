var term

// Startup
// --------------------------------------------------------

function onLoad() {
  disableGlobes();
  term = new Terminal({
    rows: 45,
    cols: 140, //any value
  });
  term.open(document.getElementById('terminal'));
  term.write('\x1B[1;3;32mXCopy Standalone\x1B[0m Logging Console\r\n');
  $('#diskcopy_cancel').prop('disabled', true);
}

// UI
// --------------------------------------------------------

function disableInterface(isBusy) {
  if (isBusy) {
    $('button').prop('disabled', true);
    $('#diskcopy_cancel').prop('disabled', false);
  } else {
    $('#copyADFtoDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDisktoADF').removeClass('btn-light').addClass('btn-primary');
    $('#copyDisktoDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDisktoFlash').removeClass('btn-light').addClass('btn-primary');
    $('#copyFlashtoDisk').removeClass('btn-light').addClass('btn-primary');
    $('button').prop('disabled', false);
    $('#diskcopy_cancel').prop('disabled', true);
    disableGlobes();
  }
}

function setHardwareStatus(status) {  
  element = document.getElementById('hardwareStatus');
  if (status == 0) {
    $('#hardwareStatus').removeClass('alert-danger').addClass('alert-success').html('Device Idle');
    disableInterface(false);
  }
  else if (status == 1) {
    $('#hardwareStatus').removeClass('alert-success').addClass('alert-danger').html('Device Busy');
    disableInterface(true);
  }
}

function setDiskname(diskname) {
  $('#diskname').html(diskname);
}

function setStatus(status) {
  $('#status').html(status);
}

function setMode(mode) {
  $('#mode').html(mode);
}
