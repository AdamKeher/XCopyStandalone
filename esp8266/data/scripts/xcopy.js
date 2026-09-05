var term
let termBuffer = [];

// Startup
// --------------------------------------------------------

function onLoad() {
  var hash = location.hash;
  // Head Cal became a layer of the Drive Toolkit rather than a tab of its own, so
  // a bookmark on the old name lands where the feature went. setTab() swallows a
  // name it cannot find, which would silently leave Disk Copy showing instead.
  if (hash == "#headcal") hash = "#drivetoolkit";
  if (hash != "") { setTab(hash); }

  // Before disableGlobes(), which reaches for the icons the grid builds, and well
  // before wsConnect(), which can start painting tracks the moment it opens.
  buildTrackGrid();

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
  onLoad_DriveToolkit();

  // Last: the message dispatch writes to term and to the tab DOM, so the socket
  // must not be able to deliver anything before both exist.
  wsConnect();
}

function setTab(tabName) {
  if (!tabName.startsWith('#')) tabName = '#' + tabName;
  try {
    $(tabName + "-tab").tab('show')
  } catch (error) { }
}

// UI
// --------------------------------------------------------

/*
   Buttons carrying .live-action are exempt from both arms below.

   The busy sweep exists so a second operation cannot be started underneath a
   running one, and that is still what we want - but the drive toolkit is
   interactive for as long as it is busy, and greying its own controls out would
   leave it with nothing but the tab. drivetoolkit.js owns those instead, scoped
   to .dt-action so it reaches only its own pane.
*/
function disableInterface(isBusy) {
  if (isBusy) {
    $('button:not(.connection-action):not(.live-action)').prop('disabled', true);
    $('button:not(.connection-action):not(.live-action)').addClass('disabled');
    $('#diskcopy_cancel').prop('disabled', false);
    $('#diskcopy_cancel').removeClass('disabled');
    $('#diskmon_cancel').prop('disabled', false);
    $('#diskmon_cancel').removeClass('disabled');
    $('#diskinfo_cancel').prop('disabled', false);
    $('#diskinfo_cancel').removeClass('disabled');
    $('#uploadCancel').prop('disabled', false);
    $('#uploadCancel').removeClass('disabled');
  } else {
    $('#copyADFToDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDiskToADF').removeClass('btn-light').addClass('btn-primary');
    $('#copyDiskToDisk').removeClass('btn-light').addClass('btn-primary');
    $('#copyDiskToFlash').removeClass('btn-light').addClass('btn-primary');
    $('#copyFlashToDisk').removeClass('btn-light').addClass('btn-primary');
    $('button:not(.connection-action):not(.live-action)').prop('disabled', false);
    $('button:not(.connection-action):not(.live-action)').removeClass('disabled');
    $('#diskcopy_cancel').prop('disabled', true);
    $('#diskcopy_cancel').addClass('disabled');
    $('#diskmon_cancel').prop('disabled', true);
    $('#diskmon_cancel').addClass('disabled');
    disableGlobes();
  }
}
