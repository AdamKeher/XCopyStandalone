var term

// Startup
// --------------------------------------------------------

function onLoad() {
  var hash = location.hash;
  if (hash != "") { setTab(hash); }

  disableGlobes();
  term = new Terminal({
    rows: 45,
    cols: 140, //any value
  });
  term.open(document.getElementById('terminal'));
  term.write('\x1B[1;3;32mXCopy Standalone\x1B[0m Logging Console\r\n');
  $('#diskcopy_cancel').prop('disabled', true);

  $("#uploadButton").click(function() {
      var fd = new FormData();
      var files = $('#file')[0].files[0];
      fd.append('file', files);
      $.ajax({
          url: '/upload?filesize=' + files.size,
          type: 'post',
          data: fd,
          contentType: false,
          processData: false,
          success: function(response){
              if(response != 0){
                  alert('file uploaded');
              }
              else{
                  alert('file not uploaded');
              }
          },
      });
  });
}

function setTab(tabName) {
  tabName += "-tab";
  $(tabName).tab('show')
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

function showsSelectDialog() {
  if (sdFiles.length == 0) getSdFiles(sdPath);
}