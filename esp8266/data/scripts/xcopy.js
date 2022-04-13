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
  $('#uploadFile').change(fileUploadChange);
  $('#uploadSelect').click(function() { uploadFile.click(); });
  $("#uploadStart").click(fileUploadSelect);
}

function fileUploadChange() {
  if ($('#uploadFile')[0].files.length == 0) {
    $("#uploadSuccess").hide();
    $("#uploadError").hide();
    $("#uploadNoFileError").hide();
    $("#uploadDetails").hide();
    return;
  }

  $('#uploadProgress').width('0%').html('0%');
  var files = $('#uploadFile')[0].files[0];
  $('#uploadFilename').html(files.name);
  $('#uploadFileSize').html(files.size);
  $("#uploadSuccess").hide();
  $("#uploadError").hide();
  $("#uploadNoFileError").hide();
  $("#uploadDetails").show();
}

function fileUploadSelect() {
  $("#uploadSuccess").hide();
  $("#uploadError").hide();

  if ($('#uploadFile')[0].files.length == 0) {
    $("#uploadNoFileError").show();
    $("#uploadDetails").hide();
    return;
  }

  $("#uploadNoFileError").hide();
  $("#uploadDetails").show();
  $('#uploadProgress').width('0%');

  var fd = new FormData();
  var files = $('#uploadFile')[0].files[0];
  fd.append('file', files);

  $.ajax({
    xhr: function() {
      var xhr = new window.XMLHttpRequest();
      xhr.upload.addEventListener("progress", function(evt) {
          if (evt.lengthComputable) {
              var percentComplete = (evt.loaded / evt.total) * 100;
              $('#uploadProgress').width(percentComplete + '%');
          }
      }, false);
      return xhr;
    },
    url: '/upload?filesize=' + files.size,
    type: 'post',
    data: fd,
    contentType: false,
    processData: false,
    success: function(response){
        if(response != 0){
          $("#uploadSuccess").show();
          $("#uploadError").hide();
        }
        else{
          $("#uploadSuccess").hide();
          $("#uploadError").show();
        }
    },
    error: function(){
      $("#uploadSuccess").hide();
      $("#uploadError").show();
    }
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
    $('button').addClass('disabled');
    $('#diskcopy_cancel').prop('disabled', false);
    $('#diskcopy_cancel').removeClass('disabled');
  } else {
    $('#copyADFtoDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDisktoADF').removeClass('btn-light').addClass('btn-primary');
    $('#copyDisktoDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDisktoFlash').removeClass('btn-light').addClass('btn-primary');
    $('#copyFlashtoDisk').removeClass('btn-light').addClass('btn-primary');
    $('button').prop('disabled', false);
    $('button').removeClass('disabled');
    $('#diskcopy_cancel').prop('disabled', true);
    $('#diskcopy_cancel').addClass('disabled');
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