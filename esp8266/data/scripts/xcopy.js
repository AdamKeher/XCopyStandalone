var term
let termBuffer = [];

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
  term.onKey(e => {
    const { key, domEvent } = e;
    const { keyCode, altKey, altGraphKey, ctrlKey, metaKey } = domEvent;

    if (keyCode == 0x08) 
      sendKey('\033[^H');
    else
      sendKey(key);
  })
  
  $('#diskcopy_cancel').prop('disabled', true);
  $('#diskmon_cancel').prop('disabled', true);
  $('#uploadFile').change(fileUploadChange);
  $('#uploadSelect').click(function() { uploadFile.click(); });
  $("#uploadStart").click(fileUploadSelect);

  onLoad_DiskMon();
  onLoad_DiskInfo();
}

function setTab(tabName) {
  if (!tabName.startsWith('#')) tabName = '#' + tabName;
  try {
    $(tabName + "-tab").tab('show')
  } catch (error) { }
}

// UI
// --------------------------------------------------------

function disableInterface(isBusy) {
  if (isBusy) {
    $('button').prop('disabled', true);
    $('button').addClass('disabled');
    $('#diskcopy_cancel').prop('disabled', false);
    $('#diskcopy_cancel').removeClass('disabled');
    $('#diskmon_cancel').prop('disabled', false);
    $('#diskmon_cancel').removeClass('disabled');
    $('#uploadCancel').prop('disabled', false);
    $('#uploadCancel').removeClass('disabled');
    $('#websocketReconnectButton').prop('disabled', false);
    $('#websocketReconnectButton').removeClass('disabled');
  } else {
    $('#copyADFToDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDiskToADF').removeClass('btn-light').addClass('btn-primary');
    $('#copyDiskToDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDiskToFlash').removeClass('btn-light').addClass('btn-primary');
    $('#copyFlashToDisk').removeClass('btn-light').addClass('btn-primary');
    $('button').prop('disabled', false);
    $('button').removeClass('disabled');
    $('#diskcopy_cancel').prop('disabled', true);
    $('#diskcopy_cancel').addClass('disabled');
    $('#diskmon_cancel').prop('disabled', true);
    $('#diskmon_cancel').addClass('disabled');
    disableGlobes();
  }
}
