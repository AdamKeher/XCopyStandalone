// Flux
// --------------------------------------------------------

function componentToHex(c) {
    var hex = c.toString(16);
    return hex.length == 1 ? "0" + hex : hex;
  }
  
  function LerpRGB(a, b, t) {
    t = 1.0;
  
    ar = (a & 0xff0000) >> 16;
    ag = (a & 0x00ff00) >> 8;
    ab = (a & 0x0000ff);
  
    br = (b & 0xff0000) >> 16;
    bg = (b & 0x00ff00) >> 8;
    bb = (b & 0x0000ff);
  
    cr = ar + (br - ar) * t;
    cg = ag + (bg - ag) * t;
    cb = ab + (bb - ab) * t;
  
    result = "#" + componentToHex(cr) + componentToHex(cg) + componentToHex(cb)
    return result;
  }
  
  function clearFlux() {
    canvas = document.getElementById('fluxCanvas');
    ctx = canvas.getContext("2d");
    ctx.clearRect(0, 0, canvas.width, canvas.height);
  }
  
  function drawFlux(trackNum, fluxString) {
    fluxData = fluxString.split("|", 255);
    element = document.getElementById('fluxCanvas');
    ctx = element.getContext("2d");
  
    for (i = 1; i < fluxData.length; i++) {
      if (fluxData[i] > 0) {
        if (fluxData[i] < 5)
          ctx.fillStyle = LerpRGB(0x000000, 0xffff00, fluxData[i]);
        else if (fluxData[i] < 50)
          ctx.fillStyle = LerpRGB(0xffff00, 0xffa500, fluxData[i]);
        else
          ctx.fillStyle = LerpRGB(0xffa500, 0xff0000, fluxData[i]);
  
        ctx.fillRect(trackNum * 2, i, 2, 1);
      }
    }
  }