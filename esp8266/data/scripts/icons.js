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